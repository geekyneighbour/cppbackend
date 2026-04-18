#pragma once

#include "http_server.h"
#include "model.h"
#include "logging.h"

#include <boost/json.hpp>
#include <boost/asio.hpp>

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

/* =========================
 * Logging wrapper
 * ========================= */
template <typename Handler>
class LoggingRequestHandler {
public:
    explicit LoggingRequestHandler(Handler& handler)
        : handler_(handler) {}

    template <typename Request, typename Send, typename Endpoint>
    void operator()(Request&& req, Send&& send, Endpoint endpoint) {
        LogRequest(req, endpoint);

        auto start = std::chrono::steady_clock::now();

        handler_(
            std::forward<Request>(req),
            [this, endpoint, start, send = std::forward<Send>(send)]
            (auto&& response) mutable {

                auto end = std::chrono::steady_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - start).count();

                LogResponse(response, endpoint, duration);

                send(std::forward<decltype(response)>(response));
            },
            endpoint
        );
    }

private:
    Handler& handler_;

    template <typename Request, typename Endpoint>
    static void LogRequest(const Request& req, const Endpoint& endpoint) {
        json::object data{
            {"ip", endpoint.address().to_string()},
            {"URI", std::string(req.target())},
            {"method", std::string(req.method_string())}
        };

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data)
            << "request received";
    }

    template <typename Response, typename Endpoint>
    static void LogResponse(const Response& resp, const Endpoint& endpoint, int time_ms) {
        json::object data{
            {"ip", endpoint.address().to_string()},
            {"response_time", time_ms},
            {"code", resp.result_int()}
        };

        auto it = resp.base().find("Content-Type");
        if (it != resp.base().end()) {
            std::string content_type(it->value().data(), it->value().size());
            data.as_object()["content_type"] = content_type;
        } else {
            data.as_object()["content_type"] = nullptr;
        }

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data)
            << "response sent";
    }
};

/* =========================
 * RequestHandler
 * ========================= */
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
        const auto target = std::string(req.target());
        const bool is_api = target.starts_with("/api/");

        auto self = shared_from_this();

        auto send_wrapper =
            [send = std::forward<Send>(send)](auto&& response) mutable {
                send(std::forward<decltype(response)>(response));
            };

        if (is_api) {
            net::dispatch(api_strand_,
                [self,
                 req = std::move(req),
                 send_wrapper = std::move(send_wrapper),
                 endpoint]() mutable {

                    try {
                        send_wrapper(self->HandleApi(req));
                    } catch (...) {
                        send_wrapper(self->ServerError(req.version(), req.keep_alive()));
                    }
                });
            return;
        }

        send_wrapper(HandleFile(req));
    }

private:
    model::Game& game_;
    fs::path root_;
    Strand api_strand_;
    model::PlayerTokens tokens_;

    std::string GenerateToken() {
        static std::random_device rd;
        static std::mt19937_64 gen1(rd());
        static std::mt19937_64 gen2(rd());
        static std::uniform_int_distribution<uint64_t> dist;

        auto to_hex = [](uint64_t v) {
            std::ostringstream ss;
            ss << std::hex << std::setw(16) << std::setfill('0') << v;
            return ss.str();
        };

        return to_hex(dist(gen1)) + to_hex(dist(gen2));
    }

    template <typename Req>
    http::response<http::string_body> HandleApi(const Req& req) {
        const std::string target(req.target());

        if (target == "/api/v1/game/join") {
            if (req.method() != http::verb::post) {
                return Error(req, "invalidMethod",
                    "Only POST method is expected",
                    http::status::method_not_allowed);
            }
            return HandleJoin(req);
        }

        if (target == "/api/v1/game/players") {
            return HandlePlayers(req);
        }

        return Error(req, "badRequest", "Unknown endpoint", http::status::bad_request);
    }

    template <typename Req>
    http::response<http::string_body> HandleJoin(const Req& req) {
        try {
            json::value body = json::parse(req.body());
            const auto& obj = body.as_object();

            std::string user_name = json::value_to<std::string>(obj.at("userName"));
            std::string map_id = json::value_to<std::string>(obj.at("mapId"));

            const model::Map* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) {
                return Error(req, "mapNotFound",
                    "Map not found",
                    http::status::not_found);
            }

            auto& session = game_.FindOrCreateSession(map);

            model::Dog& dog = session.AddDog(user_name);
            model::Player& player = session.AddPlayer(dog);

            std::string token = GenerateToken();
            tokens_.AddPlayer(token, &player);

            json::object response{
                {"authToken", token},
                {"playerId", static_cast<int>(player.GetId())}
            };

            return Json(req, response);
        }
        catch (...) {
            return Error(req, "invalidArgument",
                "Join game request parse error",
                http::status::bad_request);
        }
    }

    template <typename Req>
    http::response<http::string_body> HandlePlayers(const Req& req) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) {
            return Error(req, "invalidToken",
                "Authorization header is missing",
                http::status::unauthorized);
        }

        std::string value = std::string(it->value());
        const std::string prefix = "Bearer ";

        if (value.rfind(prefix, 0) != 0) {
            return Error(req, "invalidToken",
                "Invalid token format",
                http::status::unauthorized);
        }

        auto token = value.substr(prefix.size());

        model::Player* player = tokens_.FindPlayerByToken(token);
        if (!player) {
            return Error(req, "unknownToken",
                "Player token has not been found",
                http::status::unauthorized);
        }

        auto session = player->GetSession();
        json::object result;

        for (auto* p : session->GetPlayers()) {
            result[std::to_string(p->GetId())] = json::object{
                {"name", p->GetDog()->GetName()}
            };
        }

        return Json(req, result);
    }

    template <typename Req, typename Json>
    http::response<http::string_body> Json(const Req& req, const Json& obj) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    http::response<http::string_body> Error(
        const Req& req,
        const std::string& code,
        const std::string& message,
        http::status status)
    {
        json::object obj{
            {"code", code},
            {"message", message}
        };

        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body>
    ServerError(unsigned version, bool keep_alive) const {
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

    template <typename Req>
    http::response<http::string_body> HandleFile(const Req& req) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.body() = "static file stub";
        res.prepare_payload();
        return res;
    }
};

} // namespace http_handler