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
        ("state-file", po::value<std::string>(), "path to state file for save/load")
        ("save-state-period", po::value<uint64_t>(), "period for automatic state saving in milliseconds");

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
    
    if (vm.count("state-file"s)) {
        args.state_file = vm["state-file"s].as<std::string>();
    }
    
    if (vm.count("save-state-period"s)) {
        args.save_state_period = vm["save-state-period"s].as<uint64_t>();
        if (!args.state_file) {
            throw std::runtime_error("--save-state-period requires --state-file"s);
        }
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

// Глобальное состояние для сохранения при завершении
struct GlobalState {
    model::Game* game = nullptr;
    http_handler::RequestHandler* handler = nullptr;
    std::optional<std::string> state_file;
    std::chrono::milliseconds save_state_period{0};
    std::chrono::steady_clock::time_point last_save_time;
};

GlobalState g_state;

void SaveState() {
    if (!g_state.state_file || !g_state.game || !g_state.handler) {
        return;
    }
    
    try {
        if (g_state.game->GetSessions().empty() && g_state.handler->GetTokens().empty()) {
            if (fs::exists(fs::path(*g_state.state_file))) {
                fs::remove(fs::path(*g_state.state_file));
            }
            return;
        }
        
        if (state_saver::SaveState(*g_state.game, g_state.handler->GetTokens(), 
                                   fs::path(*g_state.state_file))) {
            g_state.last_save_time = std::chrono::steady_clock::now();
            BOOST_LOG_TRIVIAL(info) << "State saved to " << *g_state.state_file;
        } else {
            BOOST_LOG_TRIVIAL(error) << "Failed to save state to " << *g_state.state_file;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error saving state: " << e.what();
    }
}

int main(int argc, char* argv[]) {
    InitLogging();

    try {
        auto args = ParseCommandLine(argc, argv);
        if (!args) {
            return EXIT_SUCCESS;
        }

        model::Game game;
        http_handler::RequestHandler::TokensMap tokens;
        

        if (args->state_file && fs::exists(fs::path(*args->state_file))) {
            BOOST_LOG_TRIVIAL(info) << "Loading state from " << *args->state_file;
            if (!state_saver::LoadState(game, tokens, fs::path(*args->state_file))) {
                BOOST_LOG_TRIVIAL(fatal) << "Failed to load state from " << *args->state_file;
                return EXIT_FAILURE;
            }
            BOOST_LOG_TRIVIAL(info) << "State loaded successfully";
        } else {

            game = json_loader::LoadGame(args->config_file);
            if (args->state_file) {
                BOOST_LOG_TRIVIAL(info) << "Starting with fresh state, will save to " << *args->state_file;
            }
        }
        
        const unsigned num_threads = std::thread::hardware_concurrency();
        net::io_context ioc(num_threads);

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const sys::error_code& ec, int) {
            if (!ec) {
                BOOST_LOG_TRIVIAL(info) << "Shutdown signal received, saving state...";
                SaveState();
                ioc.stop();
            }
        });

        auto strand = net::make_strand(ioc);

        auto handler = std::make_shared<http_handler::RequestHandler>(
            args->www_root, strand, game);
        

        for (const auto& [token, player] : tokens) {
            handler->AddToken(token, player);
        }
        

        handler->SetSaveCallback([&game, &handler, &args]() {
            if (args->state_file) {
                state_saver::SaveState(game, handler->GetTokens(), 
                                      fs::path(*args->state_file));
            }
        });


        g_state.game = &game;
        g_state.handler = handler.get();
        g_state.state_file = args->state_file;
        if (args->save_state_period) {
            g_state.save_state_period = std::chrono::milliseconds(*args->save_state_period);
            g_state.last_save_time = std::chrono::steady_clock::now();
        }

        // save_ticker должен работать независимо от tick_period
        if (args->save_state_period) {
            auto save_ticker = std::make_shared<Ticker>(
                strand,
                std::chrono::milliseconds(*args->save_state_period),
                [&game, &handler, &args](std::chrono::milliseconds /*delta*/) {
                    state_saver::SaveState(game, handler->GetTokens(), 
                                          fs::path(*args->state_file));
                }
            );
            save_ticker->Start();
        }

        if (args->tick_period) {
            auto ticker = std::make_shared<Ticker>(
                strand, 
                std::chrono::milliseconds(*args->tick_period),
                [&game, &handler, &args](std::chrono::milliseconds delta) {
                    game.UpdateAllSessions(delta.count() / 1000.0);
                    
                    // Сохраняем состояние после каждого тика
                    if (args->state_file) {
                        state_saver::SaveState(game, handler->GetTokens(), 
                                              fs::path(*args->state_file));
                    }
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


        if (args->state_file) {
            SaveState();
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