#pragma once

#include "http_server.h"
#include "model.h"
#include "logging.h"

#include <boost/json.hpp>
#include <boost/asio.hpp>

#include <filesystem>
#include <string>
#include <random>
#include <chrono>
#include <memory>

namespace http_handler {

namespace http = boost::beast::http;
namespace json = boost::json;
namespace fs = std::filesystem;
namespace net = boost::asio;

/* =========================
 * Request handler
 * ========================= */
class RequestHandler : public std::enable_shared_from_this<RequestHandler> {
public:
    using Strand = net::strand<net::io_context::executor_type>;

    RequestHandler(fs::path root, Strand strand, model::Game& game)
        : root_(std::move(root))
        , api_strand_(strand)
        , game_(game) {}

    template <typename Body, typename Alloc, typename Send, typename Endpoint>
void operator()(
    http::request<Body, http::basic_fields<Alloc>>&& req,
    Send&& send,
    Endpoint endpoint)
    {
        LogRequest(req, endpoint);

        const auto target = std::string(req.target());
        const bool is_api = target.starts_with("/api/");

        const auto version = req.version();
        const auto keep_alive = req.keep_alive();

        auto self = shared_from_this();
        auto start = std::chrono::steady_clock::now();

        auto send_with_log =
            [this, endpoint, start, send = std::forward<Send>(send)]
            (auto&& response) mutable {

                auto end = std::chrono::steady_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

                LogResponse(response, endpoint, duration);

                send(std::forward<decltype(response)>(response));
            };

        if (is_api) {
            net::dispatch(api_strand_,
                [self,
                 req = std::move(req),
                 send_with_log = std::move(send_with_log),
                 version,
                 keep_alive]() mutable {

                    try {
                        send_with_log(self->HandleApi(req));
                    } catch (...) {
                        send_with_log(self->ReportServerError(version, keep_alive));
                    }
                });

            return;
        }

        send_with_log(HandleFile(req));
    }

    http::response<http::string_body>
    ReportServerError(unsigned version, bool keep_alive) const {
        json::object obj{
            {"code", "internalError"},
            {"message", "Internal server error"}
        };

        http::response<http::string_body> res{
            http::status::internal_server_error, version};

        res.set(http::field::content_type, "application/json");
        res.keep_alive(keep_alive);
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

private:
    model::Game& game_;
    fs::path root_;
    Strand api_strand_;
    model::PlayerTokens tokens_;

    template <typename Req>
    http::response<http::string_body> HandleFile(const Req& req) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.body() = "static file stub";
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    http::response<http::string_body> HandleApi(const Req& req) {
        std::string path = std::string(req.target());

        if (path == "/api/v1/maps") {
            json::array arr;

            for (const auto& m : game_.GetMaps()) {
                json::object o;
                o["id"] = *m->GetId();
                o["name"] = m->GetName();
                arr.push_back(o);
            }

            return JsonResponse(req, arr);
        }

        return Error(req, "badRequest");
    }

    template <typename Req, typename Json>
    http::response<http::string_body>
    JsonResponse(const Req& req, const Json& obj) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    http::response<http::string_body>
    Error(const Req& req, const std::string& code) {
        json::object obj{{"code", code}};

        http::response<http::string_body> res{
            http::status::bad_request, req.version()};

        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    /* =========================
     * Logging
     * ========================= */

    template <typename Request>
    static void LogRequest(const Request& req,
        const boost::asio::ip::tcp::endpoint& endpoint)
    {
        json::object data{
            {"ip", endpoint.address().to_string()},
            {"URI", std::string(req.target())},
            {"method", std::string(req.method_string())}
        };

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data)
            << "request received";
    }

    template <typename Response>
    static void LogResponse(const Response& resp,
        const boost::asio::ip::tcp::endpoint& endpoint,
        int time_ms)
    {
        json::object data{
            {"ip", endpoint.address().to_string()},
            {"response_time", time_ms},
            {"code", resp.result_int()}
        };

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data)
            << "response sent";
    }
};

} // namespace http_handler