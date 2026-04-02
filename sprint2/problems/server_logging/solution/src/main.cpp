#include "sdk.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json.hpp>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include <iostream>
#include <thread>
#include <csignal>

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
BOOST_LOG_ATTRIBUTE_KEYWORD(additional_data, "AdditionalData", json::value)

namespace {


void InitLogging() {
    logging::add_common_attributes();

    logging::add_console_log(std::cout,
        logging::keywords::auto_flush = true,  
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

    if (argc != 3) {
        std::cerr << "Usage: game_server <game-config-json> <static-files-directory>"sv << std::endl;
        return EXIT_FAILURE;
    }

    try {

        model::Game game = json_loader::LoadGame(argv[1]);

        const unsigned num_threads = std::thread::hardware_concurrency();
        net::io_context ioc(num_threads);


        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) {
            json::object data;
            data["code"] = 0;

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, data)
                << "server exited";

            ioc.stop();
        });


        http_handler::RequestHandler handler{game, argv[2]};
        http_handler::LoggingRequestHandler<http_handler::RequestHandler> logging_handler{handler};

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr unsigned short port = 8080;


        {
            json::object start_data;
            start_data["address"] = address.to_string();
            start_data["port"] = port;

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, start_data)
                << "server started";
        }


        http_server::ServeHttp(ioc, {address, port}, logging_handler);


        RunWorkers(std::max(1u, num_threads), [&ioc] {
            ioc.run();
        });

    } catch (const std::exception& e) {
        json::object data;
        data["code"] = EXIT_FAILURE;
        data["exception"] = e.what();

        BOOST_LOG_TRIVIAL(error)
            << boost::log::add_value(additional_data, data)
            << "server exited with error";

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}