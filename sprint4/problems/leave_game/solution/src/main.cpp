#include "sdk.h"

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include <iostream>
#include <thread>
#include <csignal>
#include <optional>
#include <vector>
#include <filesystem>
#include <cstdlib>

#include <pqxx/pqxx>

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
namespace fs = std::filesystem;

using namespace std::literals;

BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)

// FIX: объявляем additional_data
BOOST_LOG_ATTRIBUTE_KEYWORD(additional_data, "Data", boost::json::value)

struct Args {
    std::optional<uint64_t> tick_period;
    std::string config_file;
    std::string www_root;
    bool randomize_spawn_points = false;
    std::optional<std::string> state_file;
    std::optional<uint64_t> save_state_period;
};

std::optional<Args> ParseCommandLine(int argc, const char* const argv[]) {
    po::options_description desc{"Allowed options"};
    Args args;

    desc.add_options()
        ("help,h", "help")
        ("tick-period,t", po::value<uint64_t>())
        ("config-file,c", po::value<std::string>(&args.config_file))
        ("www-root,w", po::value<std::string>(&args.www_root))
        ("randomize-spawn-points", po::bool_switch(&args.randomize_spawn_points))
        ("state-file", po::value<std::string>())
        ("save-state-period", po::value<uint64_t>());

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return std::nullopt;
    }

    if (!vm.count("config-file") || !vm.count("www-root")) {
        throw std::runtime_error("Missing required arguments");
    }

    if (vm.count("tick-period"))
        args.tick_period = vm["tick-period"].as<uint64_t>();

    if (vm.count("state-file"))
        args.state_file = vm["state-file"].as<std::string>();

    if (vm.count("save-state-period"))
        args.save_state_period = vm["save-state-period"].as<uint64_t>();

    return args;
}

void InitDatabase() {
    const char* db_url = std::getenv("GAME_DB_URL");
    if (!db_url)
        throw std::runtime_error("GAME_DB_URL is not set");

    pqxx::connection conn(db_url);
    pqxx::work tx(conn);

    tx.exec(R"(
        CREATE TABLE IF NOT EXISTS retired_players (
            id SERIAL PRIMARY KEY,
            name TEXT NOT NULL,
            score INTEGER NOT NULL,
            play_time DOUBLE PRECISION NOT NULL
        );
    )");

    tx.exec(R"(
        CREATE INDEX IF NOT EXISTS idx_retired_score
        ON retired_players(score DESC);
    )");

    tx.exec(R"(
        CREATE INDEX IF NOT EXISTS idx_retired_playtime
        ON retired_players(play_time ASC);
    )");

    tx.commit();
}

int main(int argc, char* argv[]) {
    try {
        auto args = ParseCommandLine(argc, argv);
        if (!args) return EXIT_SUCCESS;

        logging::add_common_attributes();

        logging::add_console_log(
            std::clog,
            logging::keywords::format =
                (expr::stream
                    << "{\"timestamp\":\""
                    << expr::format_date_time<boost::posix_time::ptime>(
                        "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
                    << "\",\"data\":"
                    << additional_data
                    << ",\"message\":\"" << expr::smessage << "\"}")
        );

        model::Game game = json_loader::LoadGame(args->config_file);

        unsigned threads_count = std::max(1u, std::thread::hardware_concurrency());
        net::io_context ioc(threads_count);

        auto strand = net::make_strand(ioc);

        auto handler = std::make_shared<http_handler::RequestHandler>(
            args->www_root, strand, game);

        if (args->state_file) {
            state_saver::LoadState(game, handler->GetTokensMutable(), fs::path(*args->state_file));
        }

        InitDatabase();

        auto SaveState = [&]() {
            if (args->state_file) {
                state_saver::SaveState(game, handler->GetTokensMap(), fs::path(*args->state_file));
            }
        };

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](auto, int) {
            SaveState();
            ioc.stop();
        });

        auto accumulated = std::make_shared<std::chrono::milliseconds>(0);
        std::optional<std::chrono::milliseconds> save_period;

        if (args->save_state_period)
            save_period = std::chrono::milliseconds(*args->save_state_period);

        auto update_game_state = [&](std::chrono::milliseconds delta) {
            game.UpdateAllSessions(delta.count() / 1000.0);

            if (save_period) {
                *accumulated += delta;
                if (*accumulated >= *save_period) {
                    SaveState();
                    *accumulated = std::chrono::milliseconds(0);
                }
            }
        };

        if (args->tick_period) {
            auto ticker = std::make_shared<Ticker>(
                strand,
                std::chrono::milliseconds(*args->tick_period),
                update_game_state
            );
            ticker->Start();
            handler->SetTickMode(true);
        }

        handler->SetSaveCallback(update_game_state);

        net::ip::tcp::endpoint endpoint{net::ip::make_address("0.0.0.0"), 8080};

        {
            json::object start_data;
            start_data["address"] = "0.0.0.0";
            start_data["port"] = 8080;

            if (args->state_file)
                start_data["state_file"] = *args->state_file;

            BOOST_LOG_TRIVIAL(info)
                << logging::add_value(additional_data, start_data)
                << "server started";
        }

        http_server::ServeHttp(
            ioc,
            endpoint,
            [&](auto&& req, auto&& send, auto&& ep) {
                (*handler)(std::move(req), std::forward<decltype(send)>(send), ep);
            });

        std::vector<std::thread> threads;
        threads.reserve(threads_count - 1);

        for (unsigned i = 0; i < threads_count - 1; ++i)
            threads.emplace_back([&] { ioc.run(); });

        ioc.run();

        for (auto& t : threads)
            t.join();

    } catch (const std::exception& e) {
        json::object error;
        error["code"] = EXIT_FAILURE;
        error["exception"] = e.what();

        BOOST_LOG_TRIVIAL(fatal)
            << logging::add_value(additional_data, error)
            << "server exited with exception";

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}