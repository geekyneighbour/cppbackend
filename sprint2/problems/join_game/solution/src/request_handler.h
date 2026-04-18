#pragma once

#include "http_server.h"
#include "model.h"
#include "logging.h"

#include <boost/json.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include <filesystem>
#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <optional>
#include <chrono>

namespace http_handler {

namespace http = boost::beast::http;
namespace json = boost::json;
namespace fs = std::filesystem;
namespace net = boost::asio;

// ================= LOGGING =================
template <typename Handler>
class LoggingRequestHandler {
public:
    explicit LoggingRequestHandler(Handler& handler)
        : handler_(handler) {}

    template <typename Request, typename Send, typename Endpoint>
    void operator()(Request&& req, Send&& send, Endpoint endpoint) {

        json::object data_req{
            {"ip", endpoint.address().to_string()},
            {"URI", std::string(req.target())},
            {"method", std::string(req.method_string())}
        };

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data_req)
            << "request received";

        auto start = std::chrono::steady_clock::now();

        handler_(
            std::forward<Request>(req),
            [start, endpoint, send = std::forward<Send>(send)](auto response) mutable {

                auto end = std::chrono::steady_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

                json::object data_resp{
                    {"ip", endpoint.address().to_string()},
                    {"response_time", duration},
                    {"code", response.result_int()}
                };

                auto it = response.base().find(http::field::content_type);
                data_resp["content_type"] =
                    (it != response.base().end())
                        ? json::value(std::string(it->value()))
                        : json::value(nullptr);

                BOOST_LOG_TRIVIAL(info)
                    << boost::log::add_value(additional_data, data_resp)
                    << "response sent";

                send(std::move(response));
            },
            endpoint
        );
    }

private:
    Handler& handler_;
};

// ================= HANDLER =================
class RequestHandler : public std::enable_shared_from_this<RequestHandler> {
public:
    using Strand = net::strand<net::io_context::executor_type>;

    RequestHandler(fs::path root, Strand strand, model::Game& game)
        : root_(std::move(root))
        , api_strand_(strand)
        , game_(game) {}

    template <typename Body, typename Alloc, typename Send, typename Endpoint>
    void operator()(http::request<Body, http::basic_fields<Alloc>>&& req,
                    Send&& send,
                    Endpoint endpoint)
    {
        const std::string target(req.target());
        const bool is_api = target.starts_with("/api/");

        auto self = shared_from_this();

        auto send_wrapper =
            [send = std::forward<Send>(send)](auto response) mutable {
                send(std::move(response));
            };

        if (is_api) {
            net::dispatch(api_strand_,
                [self, req = std::move(req), send_wrapper = std::move(send_wrapper)]() mutable {
                    try {
                        send_wrapper(self->HandleApiRequest(req));
                    } catch (...) {
                        send_wrapper(self->ServerError(req.version(), req.keep_alive()));
                    }
                });
            return;
        }

        send_wrapper(HandleFileRequest(req));
    }

private:
    model::Game& game_;
    fs::path root_;
    Strand api_strand_;

    model::PlayerTokens tokens_;

    // ================= TOKEN =================
    std::string GenerateToken() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;

        auto hex = [](uint64_t v) {
            std::ostringstream ss;
            ss << std::hex << std::setw(16) << std::setfill('0') << v;
            return ss.str();
        };

        return hex(dist(gen)) + hex(dist(gen));
    }

    template <typename Req>
    std::optional<std::string> ParseToken(const Req& req) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) return std::nullopt;

        std::string v = std::string(it->value());
        const std::string prefix = "Bearer ";

        if (v.rfind(prefix, 0) != 0) return std::nullopt;

        return v.substr(prefix.size());
    }

    // ================= RESPONSES =================

    auto unauthorized(const http::request<auto>& req) {
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");

        json::object error{
            {"code", "invalidToken"},
            {"message", "Authorization header is missing or invalid"}
        };

        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }

    auto bad_request(const http::request<auto>& req, std::string_view msg) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::content_type, "application/json");

        json::object error{
            {"code", "badRequest"},
            {"message", std::string(msg)}
        };

        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }

    auto not_found(const http::request<auto>& req) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "application/json");

        json::object error{
            {"code", "mapNotFound"},
            {"message", "Map not found"}
        };

        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }

    auto invalid_method(const http::request<auto>& req) {
        http::response<http::string_body> res{http::status::method_not_allowed, req.version()};
        res.set(http::field::content_type, "application/json");

        json::object error{
            {"code", "invalidMethod"},
            {"message", "Only POST method is expected"}
        };

        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }

    // ================= API =================
    template <typename Req>
    http::response<http::string_body> HandleApiRequest(const Req& req) {
        std::string path(req.target());
        auto method = req.method();

        if (path == "/api/v1/game/join") {
            if (method != http::verb::post)
                return invalid_method(req);

            try {
                auto body = json::parse(req.body()).as_object();

                if (!body.contains("userName") || !body.contains("mapId"))
                    return bad_request(req, "Join parse error");

                std::string user = json::value_to<std::string>(body.at("userName"));
                std::string map_id = json::value_to<std::string>(body.at("mapId"));

                if (user.empty())
                    return bad_request(req, "Invalid name");

                const auto* map = game_.FindMap(model::Map::Id{map_id});
                if (!map)
                    return not_found(req);

                auto& session = game_.FindOrCreateSession(map);
                auto& dog = session.AddDog(user);
                auto& player = session.AddPlayer(dog);

                std::string token = GenerateToken();
                tokens_.AddPlayer(token, &player);

                http::response<http::string_body> res{http::status::ok, req.version()};
                res.set(http::field::content_type, "application/json");
                res.set(http::field::cache_control, "no-cache");

                json::object result{
                    {"authToken", token},
                    {"playerId", static_cast<int>(player.GetId())}
                };

                res.body() = json::serialize(result);
                res.prepare_payload();
                return res;
            }
            catch (...) {
                return bad_request(req, "Join parse error");
            }
        }

        if (path == "/api/v1/game/maps") {
            json::array arr;

            for (const auto& map : game_.GetMaps()) {
                arr.push_back(json::object{
                    {"id", *map->GetId()},
                    {"name", map->GetName()}
                });
            }

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(arr);
            res.prepare_payload();
            return res;
        }

        return bad_request(req, "Unknown endpoint");
    }

    // ================= FILE =================
    template <typename Req>
    http::response<http::string_body> HandleFileRequest(const Req&) {
        http::response<http::string_body> res{http::status::ok, 11};
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::cache_control, "no-cache");
        res.body() = "static stub";
        res.prepare_payload();
        return res;
    }

    // ✅ FIX: убран template
    http::response<http::string_body>
    ServerError(unsigned v, bool keep_alive) const {
        json::object obj{
            {"code", "internalError"},
            {"message", "Internal server error"}
        };

        http::response<http::string_body> res{http::status::internal_server_error, v};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.keep_alive(keep_alive);
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }
};

} // namespace http_handler