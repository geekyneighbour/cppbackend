#include "sdk.h"

#include <boost/asio.hpp>
#include <boost/json.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include <iostream>
#include <thread>
#include <csignal>

#include "json_loader.h"
#include "request_handler.h"
#include "http_server.h"
#include "logging.h"

namespace net = boost::asio;
namespace json = boost::json;
namespace logging = boost::log;
namespace expr = boost::log::expressions;

BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(message, "Message", std::string)

static void InitLogging() {
    logging::add_common_attributes();

    logging::add_console_log(
        std::cout,
        logging::keywords::auto_flush = true,
        logging::keywords::format =
            (
                expr::stream
                    << "{\"timestamp\":\""
                    << expr::format_date_time(timestamp, "%Y-%m-%dT%H:%M:%S.%f")
                    << "\",\"data\":"
                    << expr::if_(expr::has_attr(additional_data))
                           [expr::stream << additional_data]
                           .else_
                           [expr::stream << "{}"]
                    << ",\"message\":\"" << message << "\"}"
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

        net::io_context ioc(std::thread::hardware_concurrency());

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](auto, auto) { ioc.stop(); });

        auto strand = net::make_strand(ioc);

        auto handler = std::make_shared<http_handler::RequestHandler>(
            argv[2], strand, game);

        auto address = net::ip::make_address("0.0.0.0");
net::ip::tcp::endpoint endpoint{address, 8080};

http_server::ServeHttp(ioc, endpoint,
    [handler](auto&& req, auto&& send, auto&& endpoint) {
        (*handler)(
            std::move(req),
            std::forward<decltype(send)>(send),
            endpoint
        );
    });
        std::cout << "Server started\n";

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
}