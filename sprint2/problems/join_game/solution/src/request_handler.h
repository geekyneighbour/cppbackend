#pragma once

#include "http_server.h"
#include "model.h"
#include "logging.h"

#include <boost/json.hpp>
#include <boost/beast/http/file_body.hpp>

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

        auto to_hex = [](uint64_t v) {
            std::ostringstream ss;
            ss << std::hex << std::setw(16) << std::setfill('0') << v;
            return ss.str();
        };

        return to_hex(dist(gen)) + to_hex(dist(gen)); // 32 hex chars
    }

    // ================= HELPERS =================

    http::response<http::string_body>
    json_response(http::status status, unsigned version,
                  json::object obj,
                  bool keep_alive = false)
    {
        http::response<http::string_body> res{status, version};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.keep_alive(keep_alive);

        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body>
    bad_request(unsigned v, bool ka, std::string msg)
    {
        return json_response(
            http::status::bad_request, v,
            {
                {"code", "invalidArgument"},
                {"message", msg}
            }, ka);
    }

    http::response<http::string_body>
    not_found(unsigned v, bool ka)
    {
        return json_response(
            http::status::not_found, v,
            {
                {"code", "mapNotFound"},
                {"message", "Map not found"}
            }, ka);
    }

    http::response<http::string_body>
    unauthorized(unsigned v, bool ka, std::string code, std::string msg)
    {
        return json_response(
            http::status::unauthorized, v,
            {
                {"code", code},
                {"message", msg}
            }, ka);
    }

    http::response<http::string_body>
    invalid_method(unsigned v, bool ka)
    {
        http::response<http::string_body> res{http::status::method_not_allowed, v};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.set(http::field::allow, "POST");
        res.keep_alive(ka);

        json::object obj{
            {"code", "invalidMethod"},
            {"message", "Only POST method is expected"}
        };

        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    // ================= TOKEN PARSE =================
    std::optional<std::string> ParseToken(const auto& req)
    {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) return std::nullopt;

        std::string v = std::string(it->value());
        const std::string prefix = "Bearer ";

        if (v.rfind(prefix, 0) != 0)
            return std::nullopt;

        return v.substr(prefix.size());
    }

    // ================= API =================

    template <typename Req>
    http::response<http::string_body> HandleApiRequest(const Req& req)
    {
        std::string path(req.target());
        auto method = req.method();

        // ================= JOIN =================
        if (path == "/api/v1/game/join")
        {
            if (method != http::verb::post)
                return invalid_method(req.version(), req.keep_alive());

            json::object body;
            try {
                body = json::parse(req.body()).as_object();
            }
            catch (...) {
                return bad_request(req.version(), req.keep_alive(), "Join game request parse error");
            }

            if (!body.contains("userName") || !body.contains("mapId"))
                return bad_request(req.version(), req.keep_alive(), "Join game request parse error");

            std::string user = json::value_to<std::string>(body.at("userName"));
            std::string map_id = json::value_to<std::string>(body.at("mapId"));

            if (user.empty())
                return bad_request(req.version(), req.keep_alive(), "Invalid name");

            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map)
                return not_found(req.version(), req.keep_alive());

            auto& session = game_.FindOrCreateSession(map);
            auto& dog = session.AddDog(user);
            auto& player = session.AddPlayer(dog);

            std::string token = GenerateToken();
            tokens_.AddPlayer(token, &player);

            return json_response(
                http::status::ok, req.version(),
                {
                    {"authToken", token},
                    {"playerId", static_cast<int>(player.GetId())}
                }, req.keep_alive());
        }

        // ================= MAPS =================
        if (path == "/api/v1/game/maps")
        {
            json::array arr;

            for (const auto& m : game_.GetMaps())
            {
                arr.push_back(json::object{
                    {"id", *m->GetId()},
                    {"name", m->GetName()}
                });
            }

            return json_response(http::status::ok, req.version(), arr);
        }

        // ================= PLAYERS =================
        if (path == "/api/v1/game/players")
        {
            auto token = ParseToken(req);
            if (!token)
                return unauthorized(req.version(), req.keep_alive(),
                                    "invalidToken",
                                    "Authorization header is missing");

            auto* player = tokens_.FindPlayerByToken(*token);
            if (!player)
                return unauthorized(req.version(), req.keep_alive(),
                                    "unknownToken",
                                    "Player token has not been found");

            auto session = player->GetSession();
            json::object result;

            for (auto* p : session->GetPlayers())
            {
                result[std::to_string(p->GetId())] = json::object{
                    {"name", p->GetDog()->GetName()}
                };
            }

            return json_response(http::status::ok, req.version(), result);
        }

        return bad_request(req.version(), req.keep_alive(), "Unknown endpoint");
    }

    // ================= STATIC =================
    template <typename Req>
    http::response<http::string_body> HandleFileRequest(const Req&)
    {
        http::response<http::string_body> res{http::status::ok, 11};
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::cache_control, "no-cache");
        res.body() = "static stub";
        res.prepare_payload();
        return res;
    }

    // ================= ERROR =================
    http::response<http::string_body>
    ServerError(unsigned v, bool ka)
    {
        json::object obj{
            {"code", "internalError"},
            {"message", "Internal server error"}
        };

        return json_response(http::status::internal_server_error, v, obj, ka);
    }
};

} // namespace http_handler