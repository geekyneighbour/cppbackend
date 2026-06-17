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

#include "json_loader.h"
#include "request_handler.h"
#include "http_server.h"
#include "logging.h"
#include "ticker.h" 

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
    po::options_description desc{"All options"};
    
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

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return std::nullopt;
    }

    if (!vm.count("config-file")) {
        throw std::runtime_error("Config file path is required");
    }
    if (!vm.count("www-root")) {
        throw std::runtime_error("WWW root path is required");
    }

    if (vm.count("tick-period")) {
        args.tick_period = vm["tick-period"].as<uint64_t>();
    }

    return args;
}

void SetupLogger() {
    logging::add_common_attributes();
    
    logging::add_console_log(
        std::cout,
        boost::log::keywords::auto_flush = true,
        boost::log::keywords::format = (
            expr::stream
                << "{\"timestamp\":\"" << expr::format_date_time<boost::posix_time::ptime>(timestamp, "%Y-%m-%d %H:%M:%S.%f")
                << "\",\"data\":" << expr::smessage << "}"
        )
    );
}

int main(int argc, const char* argv[]) {
    try {
        SetupLogger();

        auto args = ParseCommandLine(argc, argv);
        if (!args) {
            return EXIT_SUCCESS;
        }

        // 1. Загружаем игру из файла конфигурации
        model::Game game = json_loader::LoadGame(args->config_file);

        // 2. Инициализируем хранилище токенов
        model::PlayerTokens tokens;

        const unsigned num_threads = std::thread::hardware_concurrency();
        net::io_context ioc(num_threads);

        // Сигналы остановки
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code& ec, int signal_number) {
            if (!ec) {
                json::object stop_data;
                stop_data["code"] = 0;
                BOOST_LOG_TRIVIAL(info)
                    << logging::add_value(additional_data, stop_data)
                    << "server exited";
                ioc.stop();
            }
        });

        auto handler = std::make_shared<http_handler::RequestHandler>(
            game, tokens, args->www_root, args->randomize_spawn_points
        );

        std::shared_ptr<Ticker> ticker;

        if (args->tick_period) {
            // Режим автоматических тиков времени
            handler->SetTickMode(false); 

            auto strand = net::make_strand(ioc);
            ticker = std::make_shared<Ticker>(
                strand,
                std::chrono::milliseconds(*args->tick_period),
                [&game](std::chrono::milliseconds delta) {
                    double delta_seconds = static_cast<double>(delta.count()) / 1000.0;
                    game.UpdateAllSessions(delta_seconds);
                }
            );
            ticker->Start();
        } else {
            // Режим мануальных тиков через POST /api/v1/game/tick
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
        json::object crash_data;
        crash_data["code"] = EXIT_FAILURE;
        crash_data["exception"] = e.what();

        BOOST_LOG_TRIVIAL(fatal)
            << logging::add_value(additional_data, crash_data)
            << "server exited";
            
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}