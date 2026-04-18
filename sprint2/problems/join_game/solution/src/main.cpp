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

namespace net = boost::asio;
namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace json = boost::json;

BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(message, "Message", std::string)

void InitLogging() {
    logging::add_common_attributes();

    logging::add_console_log(
        std::cout,
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
        )
    );
}

int main(int argc, const char* argv[]) {
    InitLogging();

    if (argc != 3) {
        std::cerr << "Usage: game_server <config> <static_dir>\n";
        return 1;
    }

    try {
        model::Game game = json_loader::LoadGame(argv[1]);

        const unsigned threads = std::thread::hardware_concurrency();
        net::io_context ioc(threads);

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) {
            json::object data;
            data["code"] = 0;

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, data)
                << "server exited";

            ioc.stop();
        });

        auto strand = net::make_strand(ioc);

        auto handler = std::make_shared<http_handler::RequestHandler>(
            argv[2], strand, game);

        http_handler::LoggingRequestHandler logging_handler{*handler};

        const auto address = net::ip::make_address("0.0.0.0");
        constexpr unsigned short port = 8080;

        std::cout << "Server started" << std::endl;

        http_server::ServeHttp(ioc, {address, port},
            [&logging_handler](auto&& req, auto&& send, auto&& endpoint) {
                logging_handler(
                    std::move(req),
                    std::forward<decltype(send)>(send),
                    endpoint
                );
            });

        ioc.run();
    }
    catch (const std::exception& e) {
        json::object data;
        data["code"] = 1;
        data["exception"] = e.what();

        BOOST_LOG_TRIVIAL(error)
            << boost::log::add_value(additional_data, data)
            << "server exited with error";

        return 1;
    }

    return 0;
}