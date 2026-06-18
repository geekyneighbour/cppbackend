#pragma once

#include "http_server.h"
#include "model.h"
#include "logging.h"

#include <boost/json.hpp>
#include <boost/beast/http.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include <filesystem>
#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <optional>
#include <chrono>
#include <fstream>

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

    void SetTickMode(bool mode) { auto_tick_mode_ = mode; }

    bool IsValidToken(const std::string& token) {
        if (token.size() != 32) return false;
        for (char c : token)
            if (!std::isxdigit(static_cast<unsigned char>(c)))
                return false;
        return true;
    }

    template <typename Body, typename Alloc, typename Send, typename Endpoint>
    void operator()(http::request<Body, http::basic_fields<Alloc>>&& req,
                    Send&& send,
                    Endpoint endpoint) {

        std::string target(req.target());

        if (auto pos = target.find('?'); pos != std::string::npos)
            target = target.substr(0, pos);

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

        send_wrapper(HandleFileRequest(req, target));
    }

private:
    // ================= STATE =================
    model::Game& game_;
    fs::path root_;
    Strand api_strand_;
    model::PlayerTokens tokens_;
    bool auto_tick_mode_ = false;

    // ================= TOKEN =================
    std::string GenerateToken() {
        static std::random_device rd;
        static std::mt19937_64 gen1(rd());
        static std::mt19937_64 gen2(rd());

        auto hex = [](uint64_t v) {
            std::ostringstream ss;
            ss << std::hex << std::setw(16) << std::setfill('0') << v;
            return ss.str();
        };

        return hex(gen1()) + hex(gen2());
    }

    template <typename Req>
    std::optional<std::string> ParseToken(const Req& req) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) return std::nullopt;

        std::string v = std::string(it->value());
        const std::string prefix = "Bearer ";

        if (!v.starts_with(prefix)) return std::nullopt;

        std::string token = v.substr(prefix.size());
        if (token.empty()) return std::nullopt;

        return token;
    }

    // ================= ERRORS =================
    template <typename Req>
    auto BadRequest(const Req& req, const std::string& message) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(json::object{
            {"code", "badRequest"},
            {"message", message}
        });
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    auto NotFound(const Req& req) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(json::object{
            {"code", "mapNotFound"},
            {"message", "Map not found"}
        });
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    auto InvalidMethod(const Req& req, std::string allowed) {
        http::response<http::string_body> res{http::status::method_not_allowed, req.version()};
        res.set(http::field::allow, allowed);
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(json::object{
            {"code", "invalidMethod"},
            {"message", "Only " + allowed + " method is expected"}
        });
        res.prepare_payload();
        return res;
    }

    template <typename Req>
    auto Unauthorized(const Req& req, std::string code, std::string msg) {
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(json::object{
            {"code", code},
            {"message", msg}
        });
        res.prepare_payload();
        return res;
    }

    // ================= API =================
    template <typename Req>
    http::response<http::string_body> HandleApiRequest(const Req& req) {

        std::string path(req.target());
        if (auto p = path.find('?'); p != std::string::npos)
            path = path.substr(0, p);

        const auto method = req.method();

        constexpr std::string_view MAPS = "/api/v1/maps";
        constexpr std::string_view MAPS_PREFIX = "/api/v1/maps/";
        constexpr size_t MAPS_PREFIX_LEN = MAPS_PREFIX.size();

        constexpr std::string_view JOIN = "/api/v1/game/join";
        constexpr std::string_view PLAYERS = "/api/v1/game/players";
        constexpr std::string_view STATE = "/api/v1/game/state";
        constexpr std::string_view ACTION = "/api/v1/game/player/action";
        constexpr std::string_view TICK = "/api/v1/game/tick";

        // -------- MAPS --------
        if (path == MAPS) {
            if (method != http::verb::get && method != http::verb::head)
                return InvalidMethod(req, "GET, HEAD");

            json::array arr;
            for (const auto& map : game_.GetMaps()) {
                arr.push_back(json::object{
                    {"id", *map.GetId()},
                    {"name", map.GetName()}
                });
            }

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.body() = json::serialize(arr);
            res.prepare_payload();
            return res;
        }

        // -------- MAP BY ID --------
        if (path.starts_with(MAPS_PREFIX)) {
            if (method != http::verb::get && method != http::verb::head)
                return InvalidMethod(req, "GET, HEAD");

            std::string map_id = path.substr(MAPS_PREFIX_LEN);
            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) return NotFound(req);

            json::object obj{
                {"id", *map->GetId()},
                {"name", map->GetName()},
                {"roads", boost::json::value_from(map->GetRoads())},
                {"buildings", boost::json::value_from(map->GetBuildings())},
                {"offices", boost::json::value_from(map->GetOffices())}
            };

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.body() = json::serialize(obj);
            res.prepare_payload();
            return res;
        }

        // -------- JOIN --------
        if (path == JOIN) {
            if (method != http::verb::post)
                return InvalidMethod(req, "POST");

            auto body = json::parse(req.body()).as_object();

            std::string user = json::value_to<std::string>(body.at("userName"));
            std::string map_id = json::value_to<std::string>(body.at("mapId"));

            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) return NotFound(req);

            auto& session = game_.FindOrCreateSession(map);
            auto& dog = session->AddDog(user, true);
            auto& player = session->AddPlayer(dog);

            std::string token = GenerateToken();
            tokens_.AddPlayer(token, &player);

            json::object res_obj{
                {"authToken", token},
                {"playerId", static_cast<int>(player.GetId())}
            };

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.body() = json::serialize(res_obj);
            res.prepare_payload();
            return res;
        }

        // -------- PLAYERS --------
        if (path == PLAYERS) {
            auto token = ParseToken(req);
            if (!token) return Unauthorized(req, "invalidToken", "No token");

            auto* player = tokens_.FindPlayerByToken(*token);
            if (!player) return Unauthorized(req, "unknownToken", "Not found");

            auto* session = player->GetSession();

            json::object players;
            for (auto* p : session->GetPlayers()) {
                players[std::to_string(*p->GetId())] = json::object{
                    {"name", p->GetDog()->GetName()}
                };
            }

            return MakeJson(req, players);
        }

        // -------- STATE --------
        if (path == STATE) {
            auto token = ParseToken(req);
            if (!token) return Unauthorized(req, "invalidToken", "No token");

            auto* player = tokens_.FindPlayerByToken(*token);
            if (!player) return Unauthorized(req, "unknownToken", "Not found");

            auto* session = player->GetSession();

            json::object players;

            for (auto* p : session->GetPlayers()) {
                auto* dog = p->GetDog();

                json::array pos{dog->GetPos().x, dog->GetPos().y};
                json::array speed{dog->GetSpeed().vx, dog->GetSpeed().vy};

                std::string dir;
                switch (dog->GetDirection()) {
                    case model::Direction::NORTH: dir = "U"; break;
                    case model::Direction::SOUTH: dir = "D"; break;
                    case model::Direction::WEST:  dir = "L"; break;
                    case model::Direction::EAST:  dir = "R"; break;
                }

                players[std::to_string(*p->GetId())] = json::object{
                    {"pos", pos},
                    {"speed", speed},
                    {"dir", dir}
                };
            }

            return MakeJson(req, json::object{{"players", players}});
        }

        // -------- ACTION --------
        if (path == ACTION) {
            if (method != http::verb::post)
                return InvalidMethod(req, "POST");

            auto body = json::parse(req.body()).as_object();
            std::string move = json::value_to<std::string>(body.at("move"));

            auto token = ParseToken(req);
            auto* player = tokens_.FindPlayerByToken(*token);

            player->GetDog()->SetAction(move, player->GetSession()->GetMap()->GetDogSpeed());

            return MakeJson(req, json::object{});
        }

        // -------- TICK --------
        if (path == TICK) {
            if (auto_tick_mode_)
                return BadRequest(req, "auto tick enabled");

            auto body = json::parse(req.body()).as_object();
            int64_t dt = body.at("timeDelta").as_int64();

            game_.UpdateAllSessions(dt / 1000.0);

            return MakeJson(req, json::object{});
        }

        return BadRequest(req, "Unknown endpoint");
    }

    // helper
    template <typename Req>
    http::response<http::string_body>
    MakeJson(const Req& req, json::object obj) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.body() = json::serialize(obj);
        res.prepare_payload();
        return res;
    }

    // ================= FILE =================
    template <typename Req>
    http::response<http::string_body>
    HandleFileRequest(const Req& req, const std::string& target) {

        std::string path = (target == "/" ? "/index.html" : target);
        if (path.front() == '/') path.erase(0, 1);

        fs::path full = root_ / path;

        if (!fs::exists(full) || fs::is_directory(full)) {
            http::response<http::string_body> res{http::status::not_found, req.version()};
            res.body() = "File not found";
            res.prepare_payload();
            return res;
        }

        std::ifstream file(full, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)), {});

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.body() = content;
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body>
    ServerError(unsigned v, bool keep_alive) const {
        http::response<http::string_body> res{http::status::internal_server_error, v};
        res.body() = json::serialize(json::object{
            {"code", "internalError"},
            {"message", "Internal server error"}
        });
        res.prepare_payload();
        return res;
    }
};

} // namespace http_handler