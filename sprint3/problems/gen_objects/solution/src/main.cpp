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

struct Args {
    std::optional<uint64_t> tick_period;
    std::string config_file;
    std::string www_root;
    bool randomize_spawn_points = false;
};

std::optional<Args> ParseCommandLine(int argc, const char* const argv[]) {
    po::options_description desc{"Options"};

    Args args;
    desc.add_options()
        ("config-file,c", po::value<std::string>(&args.config_file))
        ("www-root,w", po::value<std::string>(&args.www_root))
        ("tick-period,t", po::value<uint64_t>())
        ("randomize-spawn-points", po::bool_switch(&args.randomize_spawn_points));

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("tick-period")) {
        args.tick_period = vm["tick-period"].as<uint64_t>();
    }

    if (!vm.count("config-file") || !vm.count("www-root")) {
        throw std::runtime_error("Missing config or www-root");
    }

    return args;
}

int main(int argc, char* argv[]) {
    try {
        auto args = ParseCommandLine(argc, argv);
        if (!args) return 0;

        model::Game game = json_loader::LoadGame(args->config_file);

        net::io_context ioc;

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto){ ioc.stop(); });

        auto strand = net::make_strand(ioc);

        auto handler = std::make_shared<http_handler::RequestHandler>(
            args->www_root, strand, game
        );

        if (args->tick_period) {
            auto ticker = std::make_shared<Ticker>(
                strand,
                std::chrono::milliseconds(*args->tick_period),
                [&game](std::chrono::milliseconds delta) {
                    game.UpdateAllSessions(delta.count() / 1000.0);
                }
            );
            ticker->Start();

            handler->SetTickMode(true);
        }

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr net::ip::port_type port = 8080;

        http_server::ServeHttp(ioc, {address, port},
            [handler](auto&& req, auto&& send, auto&& endpoint) {
                (*handler)(
                    std::move(req),
                    std::forward<decltype(send)>(send),
                    endpoint
                );
            });

        ioc.run();

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}