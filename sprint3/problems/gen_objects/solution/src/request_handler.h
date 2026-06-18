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
#include <algorithm>
#include <fstream>


namespace boost::json {

// Сериализация Point
inline void tag_invoke(const value_from_tag&, value& v, const model::Point& p) {
    v = object{{"x", p.x}, {"y", p.y}};
}

// Сериализация PointDouble
inline void tag_invoke(const value_from_tag&, value& v, const model::PointDouble& p) {
    v = object{{"x", p.x}, {"y", p.y}};
}

// Сериализация Road
inline void tag_invoke(const value_from_tag&, value& v, const model::Road& r) {
    auto start = r.GetStart();
    auto end = r.GetEnd();
    object obj;
    obj["x0"] = start.x;
    obj["y0"] = start.y;
    if (r.IsHorizontal()) {
        obj["x1"] = end.x;
    } else {
        obj["y1"] = end.y;
    }
    v = obj;
}

// Сериализация Building
inline void tag_invoke(const value_from_tag&, value& v, const model::Building& b) {
    auto rect = b.GetBounds();
    v = object{
        {"x", rect.position.x},
        {"y", rect.position.y},
        {"w", rect.size.width},
        {"h", rect.size.height}
    };
}

// Сериализация Office
inline void tag_invoke(const value_from_tag&, value& v, const model::Office& o) {
    auto pos = o.GetPosition();
    auto off = o.GetOffset();
    v = object{
        {"id", *o.GetId()},
        {"x", pos.x},
        {"y", pos.y},
        {"offsetX", off.dx},
        {"offsetY", off.dy}
    };
}

} // namespace boost::json

namespace http_handler {

namespace http = boost::beast::http;
namespace json = boost::json;
namespace fs = std::filesystem;
namespace net = boost::asio;

// ================= LOGGING WRAPPER =================
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

// ================= REQUEST HANDLER =================
class RequestHandler : public std::enable_shared_from_this<RequestHandler> {
public:
    using Strand = net::strand<net::io_context::executor_type>;

    RequestHandler(fs::path root, Strand strand, model::Game& game)
        : root_(std::move(root))
        , api_strand_(strand)
        , game_(game) {}

    void SetTickMode(bool mode) { auto_tick_mode_ = mode; }

    template <typename Body, typename Alloc, typename Send, typename Endpoint>
    void operator()(http::request<Body, http::basic_fields<Alloc>>&& req,
                    Send&& send,
                    Endpoint endpoint)
    {
        std::string target(req.target());

        if (auto p = target.find('?'); p != std::string::npos)
            target = target.substr(0, p);

        bool is_api = target.starts_with("/api/");

        auto self = shared_from_this();

        auto send_wrapper = [send = std::forward<Send>(send)](auto response) mutable {
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
    model::Game& game_;
    fs::path root_;
    Strand api_strand_;
    model::PlayerTokens tokens_;
    bool auto_tick_mode_ = false;

    // ================= TOKEN =================
    std::optional<std::string> ParseToken(const auto& req) {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) return std::nullopt;

        std::string v = std::string(it->value());
        const std::string prefix = "Bearer ";

        if (!v.starts_with(prefix)) return std::nullopt;

        return v.substr(prefix.size());
    }

    bool IsValidToken(const std::string& t) {
        if (t.size() != 32) return false;
        return std::all_of(t.begin(), t.end(),
            [](unsigned char c){ return std::isxdigit(c); });
    }



std::string GenerateToken() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());

    auto hex = [](uint64_t v) -> std::string {
        std::ostringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << v;
        return ss.str();
    };

    return hex(gen()) + hex(gen());
}

    // ================= RESPONSES =================
    auto BadRequest(const auto& req, const std::string& msg) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::content_type, "application/json");

        res.body() = json::serialize(json::object{
            {"code", "badRequest"},
            {"message", msg}
        });

        res.prepare_payload();
        return res;
    }

    auto Unauthorized(const auto& req, const std::string& code, const std::string& msg) {
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.set(http::field::content_type, "application/json");

        res.body() = json::serialize(json::object{
            {"code", code},
            {"message", msg}
        });

        res.prepare_payload();
        return res;
    }

    auto NotFound(const auto& req) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "application/json");

        res.body() = json::serialize(json::object{
            {"code", "mapNotFound"},
            {"message", "Map not found"}
        });

        res.prepare_payload();
        return res;
    }

    auto InvalidMethod(const auto& req, const std::string& allowed) {
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

    // ================= FILE =================
    template <typename Req>
    http::response<http::string_body>
    HandleFileRequest(const Req& req, const std::string& target) {
        std::string path = target;
        if (path.empty() || path == "/")
            path = "/index.html";

        if (path.front() == '/')
            path.erase(path.begin());

        fs::path full = root_ / path;

        try {
            auto root_c = fs::canonical(root_);
            auto full_c = fs::canonical(full);

            if (full_c.string().find(root_c.string()) != 0) {
                return BadRequest(req, "Bad request");
            }
        } catch (...) {
            http::response<http::string_body> res{http::status::not_found, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "File not found";
            res.prepare_payload();
            return res;
        }

        if (!fs::exists(full) || fs::is_directory(full)) {
            http::response<http::string_body> res{http::status::not_found, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "File not found";
            res.prepare_payload();
            return res;
        }

        std::ifstream file(full, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)), {});

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.body() = std::move(content);
        res.set(http::field::content_type, "text/html");
        res.prepare_payload();
        return res;
    }

    // ================= API =================
    template <typename Req>
    http::response<http::string_body>
    HandleApiRequest(const Req& req)
    {
        std::string path(req.target());
        if (auto p = path.find('?'); p != std::string::npos)
            path = path.substr(0, p);

        auto method = req.method();

        constexpr std::string_view MAPS = "/api/v1/maps";
        constexpr std::string_view MAPS_ID = "/api/v1/maps/";
        constexpr std::string_view JOIN = "/api/v1/game/join";
        constexpr std::string_view PLAYERS = "/api/v1/game/players";
        constexpr std::string_view STATE = "/api/v1/game/state";
        constexpr std::string_view ACTION = "/api/v1/game/player/action";
        constexpr std::string_view TICK = "/api/v1/game/tick";

        // ========== MAPS ==========
        if (path == MAPS) {
            if (method != http::verb::get)
                return InvalidMethod(req, "GET");

            json::array arr;
            for (const auto& m : game_.GetMaps()) {
                arr.push_back(json::object{
                    {"id", *m->GetId()},
                    {"name", m->GetName()}
                });
            }

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.body() = json::serialize(arr);
            res.set(http::field::content_type, "application/json");
            res.prepare_payload();
            return res;
        }

        // ========== MAP BY ID ==========
        if (path.starts_with(MAPS_ID)) {
            if (method != http::verb::get)
                return InvalidMethod(req, "GET");

            std::string id = path.substr(MAPS_ID.size());
            const auto* map = game_.FindMap(model::Map::Id{id});
            if (!map) return NotFound(req);

            json::object obj;
obj["id"] = *map->GetId();
obj["name"] = map->GetName();
obj["roads"] = json::value_from(map->GetRoads());
obj["buildings"] = json::value_from(map->GetBuildings());
obj["offices"] = json::value_from(map->GetOffices());

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.body() = json::serialize(obj);
            res.set(http::field::content_type, "application/json");
            res.prepare_payload();
            return res;
        }

        // ========== JOIN ==========
        if (path == JOIN) {
            if (method != http::verb::post)
                return InvalidMethod(req, "POST");

            auto body = json::parse(req.body()).as_object();

            std::string name = json::value_to<std::string>(body.at("userName"));
            std::string map_id = json::value_to<std::string>(body.at("mapId"));

            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) return NotFound(req);

            auto& session = game_.FindOrCreateSession(map);
            auto& dog = session.AddDog(name, true);
            auto& player = session.AddPlayer(dog);

            std::string token = GenerateToken();
            tokens_.AddPlayer(token, &player);

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.body() = json::serialize(json::object{
                {"authToken", token},
                {"playerId", (int)player.GetId()}
            });

            res.set(http::field::content_type, "application/json");
            res.prepare_payload();
            return res;
        }

        // ========== PLAYERS ==========
        if (path == PLAYERS) {
    auto token = ParseToken(req);
    if (!token) return Unauthorized(req, "invalidToken", "no token");
    if (!IsValidToken(*token)) return Unauthorized(req, "invalidToken", "bad token");

    auto* player = tokens_.FindPlayerByToken(*token);
    if (!player) return Unauthorized(req, "unknownToken", "not found");

    json::object out;
    for (auto* p : player->GetSession()->GetPlayers()) {
        json::object player_obj;
        player_obj["name"] = p->GetDog()->GetName();
        out[std::to_string(p->GetId())] = player_obj;
    }

    http::response<http::string_body> res{http::status::ok, req.version()};
    res.body() = json::serialize(out);
    res.set(http::field::content_type, "application/json");
    res.prepare_payload();
    return res;
}

        // ========== STATE ==========
        if (path == STATE) {
            auto token = ParseToken(req);
            if (!token) return Unauthorized(req, "invalidToken", "no token");

            auto* player = tokens_.FindPlayerByToken(*token);
            if (!player) return Unauthorized(req, "unknownToken", "not found");

            json::object players;

            for (auto* p : player->GetSession()->GetPlayers()) {
                auto* dog = p->GetDog();
                auto pos = dog->GetPos();
                auto spd = dog->GetSpeed();

                std::string dir;
                switch (dog->GetDirection()) {
                    case model::Direction::NORTH: dir = "U"; break;
                    case model::Direction::SOUTH: dir = "D"; break;
                    case model::Direction::WEST: dir = "L"; break;
                    case model::Direction::EAST: dir = "R"; break;
                }

                players[std::to_string(p->GetId())] = json::object{
                    {"pos", json::array{pos.x, pos.y}},
                    {"speed", json::array{spd.vx, spd.vy}},
                    {"dir", dir}
                };
            }

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.body() = json::serialize(json::object{{"players", players}});
            res.set(http::field::content_type, "application/json");
            res.prepare_payload();
            return res;
        }

        // ========== ACTION ==========
        if (path == ACTION) {
            if (method != http::verb::post)
                return InvalidMethod(req, "POST");

            auto token = ParseToken(req);
            if (!token) return Unauthorized(req, "invalidToken", "no token");

            auto* player = tokens_.FindPlayerByToken(*token);
            if (!player) return Unauthorized(req, "unknownToken", "not found");

            auto body = json::parse(req.body()).as_object();
            std::string move = json::value_to<std::string>(body.at("move"));

            const auto* map = player->GetSession()->GetMap();
            player->GetDog()->SetAction(move, map->GetDogSpeed());

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.body() = "{}";
            res.set(http::field::content_type, "application/json");
            res.prepare_payload();
            return res;
        }

        // ========== TICK ==========
        if (path == TICK) {
            if (auto_tick_mode_)
                return BadRequest(req, "manual tick disabled");

            if (method != http::verb::post)
                return InvalidMethod(req, "POST");

            auto body = json::parse(req.body()).as_object();
            int64_t dt = body.at("timeDelta").as_int64();
            if (dt <= 0) return BadRequest(req, "bad timeDelta");

            game_.UpdateAllSessions(dt / 1000.0);

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.body() = "{}";
            res.set(http::field::content_type, "application/json");
            res.prepare_payload();
            return res;
        }

        return BadRequest(req, "unknown endpoint");
    }

    // ================= SERVER ERROR =================
    http::response<http::string_body>
    ServerError(unsigned v, bool keep_alive) const {
        http::response<http::string_body> res{http::status::internal_server_error, v};
        res.set(http::field::content_type, "application/json");
        res.keep_alive(keep_alive);

        res.body() = json::serialize(json::object{
            {"code", "internalError"},
            {"message", "internal server error"}
        });

        res.prepare_payload();
        return res;
    }
};

} // namespace http_handler