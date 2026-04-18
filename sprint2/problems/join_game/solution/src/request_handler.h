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
            [start, endpoint, send = std::forward<Send>(send)](auto&& response) mutable {

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

                send(std::forward<decltype(response)>(response));
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
            [send = std::forward<Send>(send)](auto&& response) mutable {
                send(std::forward<decltype(response)>(response));
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

    std::optional<std::string> ParseToken(const auto& req) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) return std::nullopt;

        std::string v = std::string(it->value());
        const std::string prefix = "Bearer ";

        if (v.rfind(prefix, 0) != 0) return std::nullopt;

        return v.substr(prefix.size());
    }

    // ================= API =================
    template <typename Req>
    http::response<http::string_body> HandleApiRequest(const Req& req) {
        std::string path(req.target());
        auto method = req.method();

        // ================= YOUR ERROR LAMBDAS (UNCHANGED LOGIC) =================

        auto const bad_request = [&](std::string_view message) {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "application/json");

            json::object error;
            error["code"] = "badRequest";
            error["message"] = std::string(message);

            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        };

        auto const not_found_api = [&]() {
            http::response<http::string_body> res{http::status::not_found, req.version()};
            res.set(http::field::content_type, "application/json");

            json::object error;
            error["code"] = "mapNotFound";
            error["message"] = "Map not found";

            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        };

        auto const not_auth_found_api = [&]() {
            http::response<http::string_body> res{http::status::not_found, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");

            json::object error;
            error["code"] = "mapNotFound";
            error["message"] = "Map not found";

            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        };

        auto const parse_error = [&]() {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");

            json::object error;
            error["code"] = "invalidArgument";
            error["message"] = "Join game request parse error";

            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        };

        auto const user_not_found = [&]() {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");

            json::object error;
            error["code"] = "invalidArgument";
            error["message"] = "Invalid name";

            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        };

        auto const invalid_method = [&]() {
            http::response<http::string_body> res{http::status::method_not_allowed, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");

            json::object error;
            error["code"] = "invalidMethod";
            error["message"] = "Only POST method is expected";

            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        };

        // ================= ROUTING =================

        if (path == "/api/v1/game/join") {
            if (method != http::verb::post)
                return invalid_method();

            try {
                auto body = json::parse(req.body()).as_object();

                if (!body.contains("userName") || !body.contains("mapId"))
                    return parse_error();

                std::string user = json::value_to<std::string>(body.at("userName"));
                std::string map_id = json::value_to<std::string>(body.at("mapId"));

                if (user.empty())
                    return user_not_found();

                const auto* map = game_.FindMap(model::Map::Id{map_id});
                if (!map)
                    return not_found_api();

                auto& session = game_.FindOrCreateSession(map);
                auto& dog = session.AddDog(user);
                auto& player = session.AddPlayer(dog);

                std::string token = GenerateToken();
                tokens_.AddPlayer(token, &player);

                http::response<http::string_body> res{http::status::ok, req.version()};
                res.set(http::field::content_type, "application/json");
                res.set(http::field::cache_control, "no-cache");

                json::object result;
                result["authToken"] = token;
                result["playerId"] = static_cast<int>(player.GetId());

                res.body() = json::serialize(result);
                res.prepare_payload();
                return res;
            }
            catch (...) {
                return parse_error();
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

        if (path == "/api/v1/game/players") {
            auto token = ParseToken(req);
            if (!token)
                return user_not_found();

            auto* player = tokens_.FindPlayerByToken(*token);
            if (!player)
                return user_not_found();

            auto session = player->GetSession();

            json::object obj;
            for (auto* p : session->GetPlayers()) {
                obj[std::to_string(p->GetId())] = json::object{
                    {"name", p->GetDog()->GetName()}
                };
            }

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(obj);
            res.prepare_payload();
            return res;
        }

        return bad_request("Unknown endpoint");
    }

    // ================= FILE =================
    template <typename Req>
    http::response<http::string_body> HandleFileRequest(const Req&) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::cache_control, "no-cache");
        res.body() = "static stub";
        res.prepare_payload();
        return res;
    }

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