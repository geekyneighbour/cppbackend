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
#include <filesystem>

#include "json_loader.h"
#include "request_handler.h"
#include "http_server.h"
#include "logging.h"
#include "ticker.h"
#include "state_saver.h"

namespace net = boost::asio;
namespace json = boost::json;
namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace po = boost::program_options;
namespace sys = boost::system;
namespace fs = std::filesystem;

using namespace std::literals;

BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)

struct Args {
    std::optional<uint64_t> tick_period;
    std::string config_file;
    std::string www_root;
    bool randomize_spawn_points = false;
    std::optional<std::string> state_file;
    std::optional<uint64_t> save_state_period;
};

std::optional<Args> ParseCommandLine(int argc, const char* const argv[]) {
    po::options_description desc{"Allowed options"s};
    Args args;

    desc.add_options()
        ("help,h", "produce help message")
        ("tick-period,t", po::value<uint64_t>(), "set tick period in milliseconds")
        ("config-file,c", po::value<std::string>(&args.config_file), "set config file path")
        ("www-root,w", po::value<std::string>(&args.www_root), "set static files root")
        ("randomize-spawn-points", po::bool_switch(&args.randomize_spawn_points), "spawn dogs at random positions")
        ("state-file", po::value<std::string>(), "set state file path")
        ("save-state-period", po::value<uint64_t>(), "set save state period in milliseconds");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return std::nullopt;
    }

    if (!vm.count("config-file")) {
        throw std::runtime_error("Config file path is required"s);
    }

    if (!vm.count("www-root")) {
        throw std::runtime_error("Static files root path is required"s);
    }

    if (vm.count("tick-period")) {
        args.tick_period = vm["tick-period"].as<uint64_t>();
    }

    if (vm.count("state-file")) {
        args.state_file = vm["state-file"].as<std::string>();
    }

    if (vm.count("save-state-period")) {
        args.save_state_period = vm["save-state-period"].as<uint64_t>();
    }

    return args;
}

int main(int argc, char* argv[]) {
    try {
        auto args = ParseCommandLine(argc, argv);
        if (!args) {
            return EXIT_SUCCESS;
        }

        logging::add_common_attributes();
        logging::add_console_log(
            std::clog,
            logging::keywords::format = (
                expr::stream
                    << "{\"timestamp\":\"" << expr::format_date_time<boost::posix_time::ptime>("timestamp", "%Y-%m-%d %H:%M:%S.%f")
                    << "\",\"data\":" << additional_data
                    << ",\"message\":\"" << expr::smessage << "\"}"
            )
        );

        model::Game game = json_loader::LoadGame(args->config_file);


        const unsigned num_threads = std::max<unsigned>(1, std::thread::hardware_concurrency());
        net::io_context ioc(num_threads);

        auto strand = net::make_strand(ioc);

        auto handler = std::make_shared<http_handler::RequestHandler>(args->www_root, strand, game);

        
        if (args->state_file) {
            state_saver::LoadState(game, handler->GetTokens(), fs::path(*args->state_file));
        }

        auto SaveState = [&game, &handler, &args]() {
            if (args->state_file) {
                state_saver::SaveState(game, handler->GetTokens(), fs::path(*args->state_file));
            }
        };

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc, SaveState](const boost::system::error_code& ec, int signal_number) {
            if (!ec) {
                ioc.stop();
                SaveState();
            }
        });

        auto accumulated_time = std::make_shared<std::chrono::milliseconds>(0);
        std::optional<std::chrono::milliseconds> save_period;
        if (args->save_state_period) {
            save_period = std::chrono::milliseconds(*args->save_state_period);
        }

        auto update_game_state = [&game, SaveState, accumulated_time, save_period](std::chrono::milliseconds delta) {
            game.UpdateAllSessions(delta.count() / 1000.0);

            if (save_period) {
                *accumulated_time += delta;
                if (*accumulated_time >= *save_period) {
                    SaveState();
                    *accumulated_time = std::chrono::milliseconds(0);
                }
            }
        };

        if (args->tick_period) {
            auto ticker = std::make_shared<Ticker>(
                strand,
                std::chrono::milliseconds(*args->tick_period),
                [update_game_state](std::chrono::milliseconds delta) {
                    update_game_state(delta);
                }
            );
            ticker->Start();
            handler->SetTickMode(true);
        }

        handler->SetSaveCallback([SaveState]() {
            SaveState();
        });

        const auto address = net::ip::make_address("0.0.0.0");
        const unsigned short port = 8080;
        net::ip::tcp::endpoint endpoint{address, port};

        {
            json::object start_data;
            start_data["address"] = address.to_string();
            start_data["port"] = port;
            if (args->state_file) {
                start_data["state_file"] = *args->state_file;
            }

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

        SaveState();

    } catch (const std::exception& e) {
        json::object error_data;
        error_data["code"] = EXIT_FAILURE;
        error_data["exception"] = e.what();

        BOOST_LOG_TRIVIAL(fatal)
            << logging::add_value(additional_data, error_data)
            << "server exited with exception";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}