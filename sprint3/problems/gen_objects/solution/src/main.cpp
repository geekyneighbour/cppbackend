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
#include "extra_data.h"

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
    po::options_description desc{"Allowed options"};
    
    Args args;
    desc.add_options()
        ("help,h", "produce help message")
        ("tick-period,t", po::value<uint64_t>(&args.tick_period.emplace()), "set tick period in milliseconds")
        ("config-file,c", po::value<std::string>(&args.config_file), "set config file path")
        ("www-root,w", po::value<std::string>(&args.www_root), "set static files root");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return std::nullopt;
    }

    if (!vm.count("config-file")) {
        throw std::runtime_error("Config file path is required");
    }
    if (!vm.count("www-root")) {
        throw std::runtime_error("WWW root path is required");
    }

    return args;
}

int main(int argc, const char* argv[]) {
    std::optional<Args> args;
    try {
        args = ParseCommandLine(argc, argv);
        if (!args) {
            return EXIT_SUCCESS;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing command line: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    try {
        const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
        net::io_context ioc(num_threads);

        infra::ExtraData extra_data;
        model::Game game = json_loader::LoadGame(args->config_file, extra_data);

        auto api_strand = net::make_strand(ioc);
        auto handler = std::make_shared<http_handler::RequestHandler>(game, extra_data, args->www_root, api_strand);

        std::chrono::milliseconds tick_period;
    if (args->tick_period) {
        tick_period = std::chrono::milliseconds(*args->tick_period);
    } else {
        tick_period = std::chrono::milliseconds(10);
        handler->SetTickMode(true);
    }

        auto ticker = std::make_shared<Ticker>(
            api_strand,
            tick_period,
            [&game](std::chrono::milliseconds delta) {
                game.UpdateAllSessions(delta.count() / 1000.0);
            }
        );
        ticker->Start();

        {
            json::object start_data;
            start_data["address"] = "0.0.0.0";
            start_data["port"] = 8080;
            start_data["tick_period"] = tick_period.count();

            BOOST_LOG_TRIVIAL(info)
                << logging::add_value(additional_data, start_data)
                << "server started";
        }

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;
        net::ip::tcp::endpoint endpoint{address, port};

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
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}