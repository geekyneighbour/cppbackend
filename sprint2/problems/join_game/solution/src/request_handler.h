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

namespace http_handler {

namespace http = boost::beast::http;
namespace json = boost::json;
namespace fs = std::filesystem;
namespace net = boost::asio;

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
                 send_wrapper = std::move(send_wrapper)]() mutable {

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
    /* =========================
     * CORE DATA
     * ========================= */
    model::Game& game_;
    fs::path root_;
    Strand api_strand_;

    /* token -> player */
    model::PlayerTokens tokens_;

    /* =========================
     * TOKEN GENERATION
     * ========================= */
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

    /* =========================
     * API HANDLER
     * ========================= */
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
            if (req.method() != http::verb::get &&
                req.method() != http::verb::head) {
                return Error(req, "invalidMethod",
                    "Invalid method",
                    http::status::method_not_allowed);
            }
            return HandlePlayers(req);
        }

        return Error(req, "badRequest", "Unknown endpoint", http::status::bad_request);
    }

    /* =========================
     * JOIN GAME
     * ========================= */
    template <typename Req>
    http::response<http::string_body> HandleJoin(const Req& req) {
        try {
            json::value body = json::parse(req.body());
            const auto& obj = body.as_object();

            if (!obj.contains("userName") || !obj.contains("mapId")) {
                return Error(req, "invalidArgument",
                    "Join game request parse error",
                    http::status::bad_request);
            }

            std::string user_name = json::value_to<std::string>(obj.at("userName"));
            std::string map_id = json::value_to<std::string>(obj.at("mapId"));

            if (user_name.empty()) {
                return Error(req, "invalidArgument",
                    "Invalid name",
                    http::status::bad_request);
            }

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

    /* =========================
     * PLAYERS LIST
     * ========================= */
    template <typename Req>
    http::response<http::string_body> HandlePlayers(const Req& req) {
        auto token = ParseToken(req);
        if (!token) {
            return Error(req, "invalidToken",
                "Authorization header is missing",
                http::status::unauthorized);
        }

        model::Player* player = tokens_.FindPlayerByToken(*token);
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

    /* =========================
     * TOKEN PARSING
     * ========================= */
    template <typename Req>
    std::optional<std::string> ParseToken(const Req& req) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) {
            return std::nullopt;
        }

        std::string value = std::string(it->value());

        const std::string prefix = "Bearer ";
        if (value.rfind(prefix, 0) != 0) {
            return std::nullopt;
        }

        return value.substr(prefix.size());
    }

    /* =========================
     * RESPONSES
     * ========================= */
    template <typename Req, typename Json>
    http::response<http::string_body> Json(const Req& req, const Json& obj) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
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
        res.set(http::field::cache_control, "no-cache");
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
        res.set(http::field::cache_control, "no-cache");
        res.keep_alive(keep_alive);
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    /* =========================
     * FILES (stub)
     * ========================= */
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