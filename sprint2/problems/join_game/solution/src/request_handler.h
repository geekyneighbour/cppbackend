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

namespace http_handler {

namespace http = boost::beast::http;
namespace json = boost::json;
namespace fs = std::filesystem;
namespace net = boost::asio;


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
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;

        auto hex = [](uint64_t v) {
            std::ostringstream ss;
            ss << std::hex << std::setw(16) << std::setfill('0') << v;
            return ss.str();
        };

        return hex(dist(gen)) + hex(dist(gen));
    }

    /* =========================
     * API ROUTER
     * ========================= */
    template <typename Req>
    http::response<http::string_body> HandleApi(const Req& req) {
        const std::string target(req.target());
        const auto method = req.method();

        if (target == "/api/v1/game/join") {
            if (method != http::verb::post) {
                return Error(req, "invalidMethod", "Only POST expected",
                             http::status::method_not_allowed);
            }
            return HandleJoin(req);
        }

        if (target == "/api/v1/game/players") {
            if (method != http::verb::get && method != http::verb::head) {
                return Error(req, "invalidMethod", "Invalid method",
                             http::status::method_not_allowed);
            }
            return HandlePlayers(req);
        }

        if (target == "/api/v1/game/maps") {
            if (method != http::verb::get && method != http::verb::head) {
                return Error(req, "invalidMethod", "Invalid method",
                             http::status::method_not_allowed);
            }
            return HandleMaps(req);
        }

        return Error(req, "badRequest", "Unknown endpoint",
                     http::status::bad_request);
    }

    /* =========================
     * MAPS (FIX FOR TESTS)
     * ========================= */
    template <typename Req>
    http::response<http::string_body> HandleMaps(const Req& req) {
        json::array arr;

        for (const auto& map : game_.GetMaps()) {
            arr.push_back(json::object{
                {"id", *map->GetId()},
                {"name", map->GetName()}
            });
        }

        return Json(req, arr);
    }

    /* =========================
     * JOIN
     * ========================= */
    template <typename Req>
    http::response<http::string_body> HandleJoin(const Req& req) {
        try {
            auto body = json::parse(req.body()).as_object();

            if (!body.contains("userName") || !body.contains("mapId")) {
                return Error(req, "invalidArgument", "Join parse error",
                             http::status::bad_request);
            }

            std::string user = json::value_to<std::string>(body.at("userName"));
            std::string map_id = json::value_to<std::string>(body.at("mapId"));

            if (user.empty()) {
                return Error(req, "invalidArgument", "Invalid name",
                             http::status::bad_request);
            }

            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) {
                return Error(req, "mapNotFound", "Map not found",
                             http::status::not_found);
            }

            auto& session = game_.FindOrCreateSession(map);

            auto& dog = session.AddDog(user);
            auto& player = session.AddPlayer(dog);

            std::string token = GenerateToken();
            tokens_.AddPlayer(token, &player);

            return Json(req, json::object{
                {"authToken", token},
                {"playerId", static_cast<int>(player.GetId())}
            });
        }
        catch (...) {
            return Error(req, "invalidArgument", "Join parse error",
                         http::status::bad_request);
        }
    }

    /* =========================
     * PLAYERS
     * ========================= */
    template <typename Req>
    http::response<http::string_body> HandlePlayers(const Req& req) {
        auto token = ParseToken(req);
        if (!token) {
            return Error(req, "invalidToken", "Missing token",
                         http::status::unauthorized);
        }

        auto* player = tokens_.FindPlayerByToken(*token);
        if (!player) {
            return Error(req, "unknownToken", "Token not found",
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
     * TOKEN PARSE
     * ========================= */
    template <typename Req>
    std::optional<std::string> ParseToken(const Req& req) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) return std::nullopt;

        std::string v = std::string(it->value());
        const std::string p = "Bearer ";

        if (v.rfind(p, 0) != 0) return std::nullopt;

        return v.substr(p.size());
    }


    template <typename Req, typename JsonType>
    http::response<http::string_body> Json(const Req& req, const JsonType& obj) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    http::response<http::string_body> Error(const Req& req,
                                             const std::string& code,
                                             const std::string& msg,
                                             http::status status) {
        json::object obj{
            {"code", code},
            {"message", msg}
        };

        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body>
    ServerError(unsigned v, bool keep_alive) const {
        json::object obj{
            {"code", "internalError"},
            {"message", "Internal server error"}
        };

        http::response<http::string_body> res{
            http::status::internal_server_error, v};

        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.keep_alive(keep_alive);
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    http::response<http::string_body> HandleFile(const Req& req) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::cache_control, "no-cache");
        res.body() = "static file stub";
        res.prepare_payload();
        return res;
    }
};

} // namespace http_handler