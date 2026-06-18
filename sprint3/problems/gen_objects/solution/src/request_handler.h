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

        send_wrapper(HandleFileRequest(req, target));
    }
	
	void SetTickMode(bool mode) { auto_tick_mode_ = mode; }

private:
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
        {"code", "badRequest"},  
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
		constexpr size_t MAPS_PREFIX_LENGTH = 13;
		constexpr std::string_view MAPS = "/api/v1/maps";
		constexpr std::string_view MAPS2 = "/api/v1/maps/";
		constexpr std::string_view JOIN = "/api/v1/game/join";
		constexpr std::string_view PLAYERS = "/api/v1/game/players";
		constexpr std::string_view STATE = "/api/v1/game/state";
		constexpr std::string_view ACTION = "/api/v1/game/player/action";
		constexpr std::string_view TICK = "/api/v1/game/tick";
		constexpr std::string_view MAP_ID = "/api/v1/maps/{id}";

        std::string path(req.target());
        
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            path = path.substr(0, query_pos);
        }
        
        auto method = req.method();

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
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(arr);
            res.prepare_payload();
            return res;
        }
        

        if (path.starts_with(MAPS2) && path.size() > MAPS_PREFIX_LENGTH) {
            if (method != http::verb::get && method != http::verb::head)
                return InvalidMethod(req, "GET, HEAD");
                
            std::string map_id = path.substr(MAPS_PREFIX_LENGTH);
            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) {
                return NotFound(req);
            }
            

            json::object result;
            result["id"] = *map->GetId();
            result["name"] = map->GetName();
            result["roads"] = boost::json::value_from(map->GetRoads());
			result["buildings"] = boost::json::value_from(map->GetBuildings());
			result["offices"] = boost::json::value_from(map->GetOffices());
			
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "application/json");
            res.set(http::field::cache_control, "no-cache");
            res.body() = json::serialize(result);
            res.prepare_payload();
            return res;
        }

        if (path == JOIN) {
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
                auto& dog = session->AddDog(user, true);
                auto& player = session->AddPlayer(dog);

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

        if (path == PLAYERS) {
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
                players_obj[std::to_string(*p->GetId())] = json::object{
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
        
        if (path == STATE) {
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
		
		if (path == MAP_ID) {
			if (method != http::verb::get)
                return InvalidMethod(req, "GET");
			if (auto* map = game_.FindMap(map_id)) {
    boost::json::object map_json = boost::json::value_from(*map).as_object();
    
    
    if (auto* loot_types = game_.GetExtraDataManager().FindLootTypes(map_id)) {
        map_json["lootTypes"] = *loot_types;
    } else {
        map_json["lootTypes"] = boost::json::array{};
    }
    
    return MakeJsonResponse(http::status::ok, map_json, req.version());
}
		}
		
		if (path == STATE) {
			if (method != http::verb::get)
                return InvalidMethod(req, "GET");
			boost::json::object response_obj;
boost::json::object players_obj;


response_obj["players"] = std::move(players_obj);


boost::json::object lost_obj_json;
for (const auto& [loot_id, loot] : session.GetLostObjects()) {
    boost::json::object item;
    item["type"] = loot.type;
    item["pos"] = boost::json::array{loot.pos.x, loot.pos.y};
    lost_obj_json[std::to_string(loot_id)] = std::move(item);
}
response_obj["lostObjects"] = std::move(lost_obj_json);

return MakeJsonResponse(http::status::ok, response_obj, req.version());
		}
        
        if (path == ACTION) {
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
		
		if (path == TICK) {
			if (auto_tick_mode_) {
        return BadRequest(req, "Invalid endpoint");
    }
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