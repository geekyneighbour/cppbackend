// main.cpp - обновленная версия
#include "sdk.h"

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include <iostream>
#include <thread>
#include <csignal>
#include <optional>
#include <vector>
#include <unordered_map>

#include "json_loader.h"
#include "request_handler.h"
#include "http_server.h"
#include "logging.h"
#include "ticker.h"
#include "loot_generator.h"

namespace net = boost::asio;
namespace json = boost::json;
namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace po = boost::program_options;

using namespace std::literals;

BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)

struct Args {
    std::optional<uint64_t> tick_period;
    std::string config_file;
    std::string www_root;
    bool randomize_spawn_points = false;
};

std::optional<Args> ParseCommandLine(int argc, const char* const argv[]) {
    po::options_description desc{"Allowed options"s};

    Args args;
    desc.add_options()
        ("help,h", "produce help message")
        ("tick-period,t", po::value<uint64_t>(), "set tick period in milliseconds")
        ("config-file,c", po::value<std::string>(&args.config_file), "set config file path")
        ("www-root,w", po::value<std::string>(&args.www_root), "set static files root")
        ("randomize-spawn-points", po::bool_switch(&args.randomize_spawn_points), "spawn dogs at random positions");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help"s)) {
        std::cout << desc << std::endl;
        return std::nullopt;
    }

    if (!vm.count("config-file"s)) {
        throw std::runtime_error("Config file path is not specified"s);
    }
    if (!vm.count("www-root"s)) {
        throw std::runtime_error("Static files root is not specified"s);
    }
    
    if (vm.count("tick-period"s)) {
        args.tick_period = vm["tick-period"s].as<uint64_t>();
    }

    return args;
}

static void InitLogging() {
    logging::add_common_attributes();
    logging::add_console_log(
        std::cout,
        logging::keywords::auto_flush = true,
        logging::keywords::format = (
            expr::stream
                << "{\"timestamp\":\"" << expr::format_date_time(timestamp, "%Y-%m-%d %H:%M:%S.%f")
                << "\",\"message\":\"" << expr::smessage
                << "\",\"additional_data\":" << additional_data << "}"
        )
    );
}

int main(int argc, char* argv[]) {
    InitLogging();

    try {
        auto args = ParseCommandLine(argc, argv);
        if (!args) {
            return EXIT_SUCCESS;
        }

        // Загружаем конфигурацию игры
        model::Game game = json_loader::LoadGame(args->config_file);
        
        // Загружаем конфигурацию генератора трофеев
        auto loot_config = json_loader::LoadLootGeneratorConfig(args->config_file);
        
        // Загружаем типы трофеев для карт
        auto loot_types = json_loader::LoadLootTypes(args->config_file);
        
        // Создаем генератор трофеев
        auto loot_generator = std::make_shared<loot_gen::LootGenerator>(
            loot_config.period,
            loot_config.probability
        );
        
        // Сохраняем типы трофеев в хендлере для передачи фронтенду
        // (будет передано через RequestHandler)
        
        const unsigned num_threads = std::thread::hardware_concurrency();
        net::io_context ioc(num_threads);

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

        auto strand = net::make_strand(ioc);

        auto handler = std::make_shared<http_handler::RequestHandler>(
            args->www_root, strand, game, loot_types, loot_generator);

        if (args->tick_period) {
            auto ticker = std::make_shared<Ticker>(
                strand, 
                std::chrono::milliseconds(*args->tick_period),
                [handler](std::chrono::milliseconds delta) {
                    handler->OnTick(delta);
                }
            );
            ticker->Start();

            handler->SetTickMode(true); 
        }

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;
        net::ip::tcp::endpoint endpoint{address, port};

        {
            json::object start_data;
            start_data["address"] = address.to_string();
            start_data["port"] = port;

            BOOST_LOG_TRIVIAL(info)
                << logging::add_value(additional_data, start_data)
                << "server started";
        }

        http_server::ServeHttp(ioc, endpoint,
            [handler](auto&& req, auto&& send, auto&& endpoint) {
                (*handler)(
                    std::move(req),
                    std::forward<decltype(send)>(send),
                    endpoint
                );
            });

        std::vector<std::thread> threads;
        threads.reserve(num_threads - 1);
        for (unsigned i = 0; i < num_threads - 1; ++i) {
            threads.emplace_back([&ioc] { ioc.run(); });
        }
        ioc.run();

        for (auto& t : threads) {
            t.join();
        }

    } catch (const std::exception& e) {
        json::object error_data;
        error_data["exception"] = e.what();

        BOOST_LOG_TRIVIAL(fatal)
            << logging::add_value(additional_data, error_data)
            << "server exited with exception";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}