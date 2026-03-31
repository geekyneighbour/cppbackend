#include "sdk.h"
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <thread>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/json.hpp>


#include "json_loader.h"
#include "request_handler.h"
#include "http_server.h"
#include "logging.h"

using namespace std::literals;
namespace net = boost::asio;
namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace json = boost::json;

BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(message, "Message", std::string)

namespace {

    void InitLogging() {
        logging::add_common_attributes();

        logging::add_console_log(std::cout,
            logging::keywords::format =
                (
                    expr::stream
                        << "{"
                        << "\"timestamp\":\""
                        << expr::format_date_time(timestamp, "%Y-%m-%dT%H:%M:%S.%f")
                        << "\"," 
                        << "\"data\":"
                        << expr::if_(expr::has_attr(additional_data))
                               [expr::stream << additional_data]
                               .else_
                               [expr::stream << "{}"]
                        << ",\"message\":\""
                        << message
                        << "\""
                        << "}"
                ));
    }

    template <typename Fn>
    void RunWorkers(unsigned n, const Fn& fn) {
        n = std::max(1u, n);
        std::vector<std::jthread> workers;
        workers.reserve(n - 1);
        while (--n) {
            workers.emplace_back(fn);
        }
        fn();
    }

}  // namespace

int main(int argc, const char* argv[]) {
    InitLogging();

    if (argc != 2) {
        std::cerr << "Usage: game_server <game-config-json>"sv << std::endl;
        return EXIT_FAILURE;
    }

    try {
        model::Game game = json_loader::LoadGame(argv[1]);

        const unsigned num_threads = std::thread::hardware_concurrency();
        net::io_context ioc(num_threads);

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) {
            json::object data_obj;
            data_obj["code"] = 0;
            json::value data = std::move(data_obj);

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, data)
                << "server exited";
            ioc.stop();
        });

        http_handler::RequestHandler handler{game};
        http_handler::LoggingRequestHandler<http_handler::RequestHandler> logging_handler{handler};

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr unsigned short port = 8080;


        {
            json::object start_data;
    start_data["event"] = "server_started";
    start_data["address"] = address.to_string();
    start_data["port"] = port;


    std::string message = "Server started";


    std::cout << message << std::endl;


    BOOST_LOG_TRIVIAL(info)
        << boost::log::add_value(additional_data, start_data)
        << message;
        }


        RunWorkers(std::max(1u, num_threads), [&ioc, &logging_handler, &address, port] {
            http_server::ServeHttp(ioc, {address, port}, logging_handler);
            ioc.run();
        });


        json::object exit_data_obj;
        exit_data_obj["code"] = 0;
        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, exit_data_obj)
            << "server exited";

    } catch (const std::exception& e) {
        json::object data_obj;
        data_obj["code"] = EXIT_FAILURE;
        data_obj["exception"] = e.what();
        BOOST_LOG_TRIVIAL(error)
            << boost::log::add_value(additional_data, data_obj)
            << "server exited with error";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}