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
        
    bool IsValidToken(const std::string& token) {
        if (token.size() != 32)
            return false;

        for (char c : token) {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
                return false;
        }

        return true;
    }	

    template <typename Body, typename Alloc, typename Send, typename Endpoint>
    void operator()(http::request<Body, http::basic_fields<Alloc>>&& req,
                    Send&& send,
                    Endpoint endpoint)
    {
        std::string target(req.target());
        
        // Удаляем query string если есть
        size_t query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            target = target.substr(0, query_pos);
        }
        
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
                    } catch (const std::exception& e) {
                        send_wrapper(self->ServerError(req.version(), req.keep_alive()));
                    }
                });
            return;
        }

        // Обработка статических файлов
        send_wrapper(HandleFileRequest(req, target));
    }

private:
    model::Game& game_;
    fs::path root_;
    Strand api_strand_;
    model::PlayerTokens tokens_;

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

        if (v.length() <= prefix.length() || v.substr(0, prefix.length()) != prefix)
            return std::nullopt;

        std::string token = v.substr(prefix.length());
        if (token.empty()) return std::nullopt;

        return token;
    }

    // ================= RESPONSES =================

    auto Unauthorized(const http::request<auto>& req, const std::string& code, const std::string& message) {
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");

        json::object error{
            {"code", code},
            {"message", message}
        };

        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }

    auto BadRequest(const http::request<auto>& req, const std::string& message) {
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::content_type, "application/json");
    res.set(http::field::cache_control, "no-cache");

    json::object error{
        {"code", "badRequest"},  // <-- ИЗМЕНЕНО: было "invalidArgument", стало "badRequest"
        {"message", message}
    };

    res.body() = json::serialize(error);
    res.prepare_payload();
    return res;
}

    auto NotFound(const http::request<auto>& req, const std::string& code = "mapNotFound", const std::string& message = "Map not found") {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");

        json::object error{
            {"code", code},
            {"message", message}
        };

        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }

    auto InvalidMethod(const http::request<auto>& req, const std::string& allowed_methods) {
        http::response<http::string_body> res{http::status::method_not_allowed, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.set(http::field::allow, allowed_methods);

        json::object error{
            {"code", "invalidMethod"},
            {"message", "Only " + allowed_methods + " method is expected"}
        };

        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }

    // ================= API =================
    template <typename Req>
    http::response<http::string_body> HandleApiRequest(const Req& req) {
        std::string path(req.target());
        
        // Удаляем query string если есть
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            path = path.substr(0, query_pos);
        }
        
        auto method = req.method();

        // Handle /api/v1/maps (GET)
        if (path == "/api/v1/maps") {
            if (method != http::verb::get && method != http::verb::head)
                return InvalidMethod(req, "GET, HEAD");

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
        
        // Handle /api/v1/maps/{id} (GET)
        if (path.starts_with("/api/v1/maps/") && path.size() > 13) {
            if (method != http::verb::get && method != http::verb::head)
                return InvalidMethod(req, "GET, HEAD");
                
            std::string map_id = path.substr(13);
            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) {
                return NotFound(req);
            }
            
            // Строим полный ответ с картой
            json::object result;
            result["id"] = *map->GetId();
            result["name"] = map->GetName();
            
            // Добавляем дороги
            json::array roads_array;
            for (const auto& road : map->GetRoads()) {
                json::object road_obj;
                road_obj["x0"] = road.GetStart().x;
                road_obj["y0"] = road.GetStart().y;
                if (road.IsHorizontal()) {
                    road_obj["x1"] = road.GetEnd().x;
                } else {
                    road_obj["y1"] = road.GetEnd().y;
                }
                roads_array.push_back(road_obj);
            }
            result["roads"] = roads_array;
            
            // Добавляем здания
            json::array buildings_array;
            for (const auto& building : map->GetBuildings()) {
                json::object building_obj;
                building_obj["x"] = building.GetBounds().position.x;
                building_obj["y"] = building.GetBounds().position.y;
                building_obj["w"] = building.GetBounds().size.width;
                building_obj["h"] = building.GetBounds().size.height;
                buildings_array.push_back(building_obj);
            }
            result["buildings"] = buildings_array;
            
            // Добавляем офисы
            json::array offices_array;
            for (const auto& office : map->GetOffices()) {
                json::object office_obj;
                office_obj["id"] = *office.GetId();
                office_obj["x"] = office.GetPosition().x;
                office_obj["y"] = office.GetPosition().y;
                office_obj["offsetX"] = office.GetOffset().dx;
                office_obj["offsetY"] = office.GetOffset().dy;
                offices_array.push_back(office_obj);
            }
            result["offices"] = offices_array;
            
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(result);
            res.prepare_payload();
            return res;
        }

        // Handle /api/v1/game/join (POST)
        if (path == "/api/v1/game/join") {
            if (method != http::verb::post)
                return InvalidMethod(req, "POST");
                
            auto content_type = req.find(http::field::content_type);
            if (content_type == req.end() || content_type->value() != "application/json") {
                return BadRequest(req, "Invalid content type");
            }

            try {
                auto body = json::parse(req.body()).as_object();

                if (!body.contains("userName") || !body.contains("mapId"))
                    return BadRequest(req, "Join game request parse error");

                std::string user = json::value_to<std::string>(body.at("userName"));
                std::string map_id = json::value_to<std::string>(body.at("mapId"));

                if (user.empty())
                    return BadRequest(req, "Invalid name");

                const auto* map = game_.FindMap(model::Map::Id{map_id});
                if (!map)
                    return NotFound(req);

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
            catch (const std::exception& e) {
                return BadRequest(req, "Join game request parse error");
            }
        }

        // Handle /api/v1/game/players (GET)
        if (path == "/api/v1/game/players") {
            if (method != http::verb::get && method != http::verb::head)
                return InvalidMethod(req, "GET, HEAD");

            auto token_opt = ParseToken(req);
            if (!token_opt)
                return Unauthorized(req, "invalidToken", "Authorization header is required");

            if (!IsValidToken(*token_opt)) {
                return Unauthorized(req, "invalidToken", "Invalid token");
            }

            model::Player* player = tokens_.FindPlayerByToken(*token_opt);
            if (!player) {
                return Unauthorized(req, "unknownToken", "Player token has not been found");
            }

            model::GameSession* session = player->GetSession();
            json::object players_obj;

            for (model::Player* p : session->GetPlayers()) {
                players_obj[std::to_string(p->GetId())] = json::object{
                    {"name", p->GetDog()->GetName()}
                };
            }

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(players_obj);
            res.prepare_payload();
            return res;
        }
        
        // Handle /api/v1/game/state (GET)
        if (path == "/api/v1/game/state") {
            if (method != http::verb::get && method != http::verb::head)
                return InvalidMethod(req, "GET, HEAD");

            auto token_opt = ParseToken(req);
            if (!token_opt)
                return Unauthorized(req, "invalidToken", "Authorization header is required");

            if (!IsValidToken(*token_opt)) {
                return Unauthorized(req, "invalidToken", "Invalid token");
            }

            model::Player* player = tokens_.FindPlayerByToken(*token_opt);
            if (!player) {
                return Unauthorized(req, "unknownToken", "Player token has not been found");
            }

            model::GameSession* session = player->GetSession();
            json::object players_obj;

            for (model::Player* p : session->GetPlayers()) {
                model::Dog* dog = p->GetDog();
                model::PointDouble pos = dog->GetPos();
                model::Speed speed = dog->GetSpeed();
                model::Direction dir = dog->GetDirection();
                
                std::string dir_str;
                switch (dir) {
                    case model::Direction::NORTH: dir_str = "U"; break;
                    case model::Direction::SOUTH: dir_str = "D"; break;
                    case model::Direction::WEST:  dir_str = "L"; break;
                    case model::Direction::EAST:  dir_str = "R"; break;
                }
                
                players_obj[std::to_string(p->GetId())] = json::object{
                    {"pos", json::array{pos.x, pos.y}},
                    {"speed", json::array{speed.vx, speed.vy}},
                    {"dir", dir_str}
                };
            }

            json::object response_obj{
                {"players", players_obj}
            };

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(response_obj);
            res.prepare_payload();
            return res;
        }
        
        // Handle /api/v1/game/player/action (POST)
        if (path == "/api/v1/game/player/action") {
            if (method != http::verb::post)
                return InvalidMethod(req, "POST");
                
            auto content_type = req.find(http::field::content_type);
            if (content_type == req.end() || content_type->value() != "application/json") {
                return BadRequest(req, "Invalid content type");
            }

            auto token_opt = ParseToken(req);
            if (!token_opt)
                return Unauthorized(req, "invalidToken", "Authorization header is required");

            if (!IsValidToken(*token_opt)) {
                return Unauthorized(req, "invalidToken", "Invalid token");
            }

            model::Player* player = tokens_.FindPlayerByToken(*token_opt);
            if (!player) {
                return Unauthorized(req, "unknownToken", "Player token has not been found");
            }

            try {
                auto body = json::parse(req.body()).as_object();
                
                if (!body.contains("move")) {
                    return BadRequest(req, "Failed to parse action");
                }
                
                std::string move = json::value_to<std::string>(body.at("move"));
                
                if (move != "L" && move != "R" && move != "U" && move != "D" && !move.empty()) {
                    return BadRequest(req, "Failed to parse action");
                }
                
                const model::Map* map = player->GetSession()->GetMap();
                double dog_speed = map->GetDogSpeed();
                
                player->GetDog()->SetAction(move, dog_speed);
                
                http::response<http::string_body> res{http::status::ok, req.version()};
                res.set(http::field::content_type, "application/json");
                res.set(http::field::cache_control, "no-cache");
                res.body() = "{}";
                res.prepare_payload();
                return res;
                
            } catch (const std::exception& e) {
                return BadRequest(req, "Failed to parse action");
            }
        }
		
		if (path == "/api/v1/game/tick") {
    if (method != http::verb::post)
        return InvalidMethod(req, "POST");
        
    auto content_type = req.find(http::field::content_type);
    if (content_type == req.end() || content_type->value() != "application/json") {
        return BadRequest(req, "Invalid content type");
    }

    try {
        auto body = json::parse(req.body()).as_object();
        
        if (!body.contains("timeDelta")) {
            json::object error{
                {"code", "invalidArgument"},
                {"message", "Missing timeDelta field"}
            };
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        }
        
        if (!body.at("timeDelta").is_int64()) {
            json::object error{
                {"code", "invalidArgument"},
                {"message", "timeDelta must be an integer"}
            };
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        }
        
        int64_t time_delta_ms = body.at("timeDelta").as_int64();
        if (time_delta_ms <= 0) {
            json::object error{
                {"code", "invalidArgument"},
                {"message", "timeDelta must be positive"}
            };
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(error);
            res.prepare_payload();
            return res;
        }
        

        double time_delta_sec = static_cast<double>(time_delta_ms) / 1000.0;
        
        game_.UpdateAllSessions(time_delta_sec);
        
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = "{}";
        res.prepare_payload();
        return res;
        
    } catch (const std::exception& e) {
        json::object error{
            {"code", "invalidArgument"},
            {"message", "Failed to parse tick request JSON"}
        };
        
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(error);
        res.prepare_payload();
        return res;
    }
}

        return BadRequest(req, "Unknown endpoint");
    }

    // ================= FILE =================
    template <typename Req>
http::response<http::string_body> HandleFileRequest(const Req& req, const std::string& target) {
    std::string path = target;
    if (path.empty() || path == "/") {
        path = "/index.html";
    }
    
    
    if (path.front() == '/') {
        path = path.substr(1);
    }
    
    fs::path full_path = root_ / path;
    
    
    try {
        auto canonical_root = fs::canonical(root_);
        auto canonical_full = fs::canonical(full_path);
        
        if (canonical_full.string().find(canonical_root.string()) != 0) {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "text/plain");  
            res.set(http::field::cache_control, "no-cache");
            res.body() = "Bad request";
            res.prepare_payload();
            return res;
        }
    } catch (const fs::filesystem_error&) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "text/plain");  
        res.set(http::field::cache_control, "no-cache");
        res.body() = "File not found";
        res.prepare_payload();
        return res;
    }
    
    if (!fs::exists(full_path) || fs::is_directory(full_path)) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "text/plain");  // <-- ДОБАВИТЬ
        res.set(http::field::cache_control, "no-cache");
        res.body() = "File not found";
        res.prepare_payload();
        return res;
    }
    
    
    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::content_type, "text/plain");  
        res.set(http::field::cache_control, "no-cache");
        res.body() = "File not found";
        res.prepare_payload();
        return res;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    
    std::string content_type;
    std::string ext = full_path.extension().string();
    if (ext == ".html" || ext == ".htm") {
        content_type = "text/html";
    } else if (ext == ".css") {
        content_type = "text/css";
    } else if (ext == ".js") {
        content_type = "application/javascript";
    } else if (ext == ".svg") {
        content_type = "image/svg+xml";
    } else if (ext == ".png") {
        content_type = "image/png";
    } else if (ext == ".jpg" || ext == ".jpeg") {
        content_type = "image/jpeg";
    } else {
        content_type = "text/plain";
    }
    
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::content_type, content_type);
    res.set(http::field::cache_control, "no-cache");
    res.body() = content;
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