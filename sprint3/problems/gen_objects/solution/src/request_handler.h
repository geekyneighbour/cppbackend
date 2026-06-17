#pragma once

#include "http_server.h"
#include "model.h"
#include "logging.h"
#include "json_loader.h"

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

        auto start_time = std::chrono::steady_clock::now();

        handler_(std::forward<Request>(req), [send = std::forward<Send>(send), start_time](auto&& response) {
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

            json::object data_resp{
                {"response_time", duration},
                {"code", response.result_int()},
                {"content_type", std::string(response[http::field::content_type])}
            };

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, data_resp)
                << "response sent";

            send(std::forward<decltype(response)>(response));
        }, endpoint);
    }

private:
    Handler& handler_;
};

// ================= API REQUEST HANDLER =================
class ApiRequestHandler {
public:
    explicit ApiRequestHandler(model::Game& game, model::PlayerTokens& tokens, bool& randomize_spawn)
        : game_(game), tokens_(tokens), randomize_spawn_(randomize_spawn) {}

    template <typename Request>
    auto HandleRequest(Request&& req) const {
        std::string_view target = req.target();
        unsigned version = req.version();
        bool keep_alive = req.keep_alive();

        if (target == "/api/v1/maps" || target == "/api/v1/maps/") {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return MethodNotAllowed("GET, HEAD"sv, version, keep_alive);
            }
            return GetMaps(version, keep_alive);
        }

        if (target.starts_with("/api/v1/maps/")) {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return MethodNotAllowed("GET, HEAD"sv, version, keep_alive);
            }
            std::string_view map_id = target.substr("/api/v1/maps/"sv.size());
            return GetMapById(map_id, version, keep_alive);
        }

        if (target == "/api/v1/game/join" || target == "/api/v1/game/join/") {
            if (req.method() != http::verb::post) {
                return MethodNotAllowed("POST"sv, version, keep_alive);
            }
            return JoinGame(std::forward<Request>(req));
        }

        if (target == "/api/v1/game/players" || target == "/api/v1/game/players/") {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return MethodNotAllowed("GET, HEAD"sv, version, keep_alive);
            }
            return ExecuteWithToken(std::forward<Request>(req), [this](std::string_view token, unsigned v, bool ka) {
                return GetPlayers(token, v, ka);
            });
        }

        if (target == "/api/v1/game/state" || target == "/api/v1/game/state/") {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return MethodNotAllowed("GET, HEAD"sv, version, keep_alive);
            }
            return ExecuteWithToken(std::forward<Request>(req), [this](std::string_view token, unsigned v, bool ka) {
                return GetGameState(token, v, ka);
            });
        }

        if (target == "/api/v1/game/player/action" || target == "/api/v1/game/player/action/") {
            if (req.method() != http::verb::post) {
                return MethodNotAllowed("POST"sv, version, keep_alive);
            }
            return ExecuteWithToken(std::forward<Request>(req), [this, &req](std::string_view token, unsigned v, bool ka) {
                return SetPlayerAction(token, std::forward<Request>(req));
            });
        }

        if (target == "/api/v1/game/tick" || target == "/api/v1/game/tick/") {
            if (req.method() != http::verb::post) {
                return MethodNotAllowed("POST"sv, version, keep_alive);
            }
            return HandleTick(std::forward<Request>(req));
        }

        return MakeJsonResponse(http::status::bad_request, {
            {"code", "badRequest"},
            {"message", "Bad request"}
        }, version, keep_alive);
    }

    void SetTickMode(bool manual) {
        manual_tick_ = manual;
    }

private:
    model::Game& game_;
    model::PlayerTokens& tokens_;
    bool& randomize_spawn_;
    inline static bool manual_tick_ = false;

    static http::response<http::string_body> MakeJsonResponse(
        http::status status, const json::value& body, unsigned version, bool keep_alive) {
        http::response<http::string_body> res{status, version};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(body);
        res.prepare_payload();
        res.keep_alive(keep_alive);
        return res;
    }

    http::response<http::string_body> MethodNotAllowed(
        std::string_view allowed, unsigned version, bool keep_alive) const {
        http::response<http::string_body> res{http::status::method_not_allowed, version};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.set(http::field::allow, allowed);
        res.body() = json::serialize(json::object{
            {"code", "invalidMethod"},
            {"message", "Invalid method"}
        });
        res.prepare_payload();
        res.keep_alive(keep_alive);
        return res;
    }

    http::response<http::string_body> GetMaps(unsigned version, bool keep_alive) const {
        json::array maps_arr;
        for (const auto& map : game_.GetMaps()) {
            maps_arr.push_back(json::object{
                {"id", *map->GetId()},
                {"name", map->GetName()}
            });
        }
        return MakeJsonResponse(http::status::ok, maps_arr, version, keep_alive);
    }

    http::response<http::string_body> GetMapById(std::string_view map_id_str, unsigned version, bool keep_alive) const {
        model::Map::Id id{std::string(map_id_str)};
        const auto* map = game_.FindMap(id);
        if (!map) {
            return MakeJsonResponse(http::status::not_found, {
                {"code", "mapNotFound"},
                {"message", "Map not found"}
            }, version, keep_alive);
        }

        json::object map_obj;
        map_obj["id"] = *map->GetId();
        map_obj["name"] = map->GetName();
        map_obj["roads"] = json::value_from(map->GetRoads());
        map_obj["buildings"] = json::value_from(map->GetBuildings());
        map_obj["offices"] = json::value_from(map->GetOffices());

        // Загрузка служебной информации lootTypes из внешнего хранилища,
        // сохраняя независимость бизнес-модели от структуры JSON
        if (const auto* loot_types = json_loader::ExtraDataStorage::GetInstance().GetLootTypes(*map->GetId())) {
            map_obj["lootTypes"] = *loot_types;
        } else {
            map_obj["lootTypes"] = json::array{};
        }

        return MakeJsonResponse(http::status::ok, map_obj, version, keep_alive);
    }

    template <typename Request>
    http::response<http::string_body> JoinGame(Request&& req) const {
        unsigned version = req.version();
        bool keep_alive = req.keep_alive();

        try {
            auto root = json::parse(req.body());
            if (!root.is_object()) throw std::exception();
            
            const auto& obj = root.as_object();
            if (!obj.contains("userName") || !obj.contains("mapId")) throw std::exception();

            std::string user_name = json::value_to<std::string>(obj.at("userName"));
            std::string map_id_str = json::value_to<std::string>(obj.at("mapId"));

            if (user_name.empty()) {
                return MakeJsonResponse(http::status::bad_request, {
                    {"code", "invalidArgument"},
                    {"message", "Invalid name"}
                }, version, keep_alive);
            }

            model::Map::Id map_id{map_id_str};
            const auto* map = game_.FindMap(map_id);
            if (!map) {
                return MakeJsonResponse(http::status::not_found, {
                    {"code", "mapNotFound"},
                    {"message", "Map not found"}
                }, version, keep_alive);
            }

            auto session = game_.GetOrCreateSession(map);
            auto* dog = session->AddDog(user_name, randomize_spawn_);

            static std::mt19937_64 generator{std::random_device{}()};
            std::stringstream ss;
            ss << std::hex << std::setw(16) << std::setfill('0') << generator()
               << std::setw(16) << std::setfill('0') << generator();
            std::string token = ss.str();

            static uint32_t next_player_id = 0;
            auto player = std::make_unique<model::Player>(next_player_id++, session, std::shared_ptr<model::Dog>(session->GetDogs().back()));
            
            static std::vector<std::unique_ptr<model::Player>> global_players;
            global_players.push_back(std::move(player));
            
            tokens_.AddPlayer(token, global_players.back().get());

            return MakeJsonResponse(http::status::ok, json::object{
                {"authToken", token},
                {"playerId", global_players.back()->GetId()}
            }, version, keep_alive);

        } catch (...) {
            return MakeJsonResponse(http::status::bad_request, {
                {"code", "invalidArgument"},
                {"message", "Join game request parse error"}
            }, version, keep_alive);
        }
    }

    template <typename Request, typename Func>
    http::response<http::string_body> ExecuteWithToken(Request&& req, Func&& func) const {
        unsigned version = req.version();
        bool keep_alive = req.keep_alive();

        auto it = req.find(http::field::authorization);
        if (it == req.end()) {
            return MakeJsonResponse(http::status::unauthorized, {
                {"code", "invalidToken"},
                {"message", "Authorization header is required"}
            }, version, keep_alive);
        }

        std::string_view auth_val = it->value();
        std::string_view prefix = "Bearer ";
        if (!auth_val.starts_with(prefix)) {
            return MakeJsonResponse(http::status::unauthorized, {
                {"code", "invalidToken"},
                {"message", "Authorization header must begin with Bearer"}
            }, version, keep_alive);
        }

        std::string_view token = auth_val.substr(prefix.size());
        if (token.size() != 32) {
            return MakeJsonResponse(http::status::unauthorized, {
                {"code", "invalidToken"},
                {"message", "Token is incorrect"}
            }, version, keep_alive);
        }

        return func(token, version, keep_alive);
    }

    http::response<http::string_body> GetPlayers(std::string_view token, unsigned version, bool keep_alive) const {
        auto* player = tokens_.FindPlayerByToken(std::string(token));
        if (!player) {
            return MakeJsonResponse(http::status::unauthorized, {
                {"code", "unknownToken"},
                {"message", "Player token not found"}
            }, version, keep_alive);
        }

        auto session = player->GetSession();
        json::object response_obj;
        for (const auto& dog : session->GetDogs()) {
            response_obj[std::to_string(dog->GetId())] = json::object{{"name", dog->GetName()}};
        }

        return MakeJsonResponse(http::status::ok, response_obj, version, keep_alive);
    }

    http::response<http::string_body> GetGameState(std::string_view token, unsigned version, bool keep_alive) const {
        auto* player = tokens_.FindPlayerByToken(std::string(token));
        if (!player) {
            return MakeJsonResponse(http::status::unauthorized, {
                {"code", "unknownToken"},
                {"message", "Player token not found"}
            }, version, keep_alive);
        }

        auto session = player->GetSession();
        json::object response_obj;
        
        // Массив игроков и их характеристик
        json::object players_obj;
        for (const auto& dog : session->GetDogs()) {
            json::object dog_obj;
            dog_obj["pos"] = json::array{dog->GetPosition().x, dog->GetPosition().y};
            dog_obj["speed"] = json::array{dog->GetSpeed().vx, dog->GetSpeed().vy};
            
            std::string dir_str = "U";
            switch (dog->GetDirection()) {
                case model::Direction::NORTH: dir_str = "U"; break;
                case model::Direction::SOUTH: dir_str = "D"; break;
                case model::Direction::WEST:  dir_str = "L"; break;
                case model::Direction::EAST:  dir_str = "R"; break;
            }
            dog_obj["dir"] = dir_str;
            players_obj[std::to_string(dog->GetId())] = dog_obj;
        }
        response_obj["players"] = players_obj;

        // Массив потерянных предметов (lostObjects) на карте
        json::object lost_obj;
        for (const auto& [id, item] : session->GetLostObjects()) {
            json::object item_json;
            item_json["type"] = item.type;
            item_json["pos"] = json::array{item.pos.x, item.pos.y};
            lost_obj[std::to_string(id)] = item_json;
        }
        response_obj["lostObjects"] = lost_obj;

        return MakeJsonResponse(http::status::ok, response_obj, version, keep_alive);
    }

    template <typename Request>
    http::response<http::string_body> SetPlayerAction(std::string_view token, Request&& req) const {
        unsigned version = req.version();
        bool keep_alive = req.keep_alive();

        auto* player = tokens_.FindPlayerByToken(std::string(token));
        if (!player) {
            return MakeJsonResponse(http::status::unauthorized, {
                {"code", "unknownToken"},
                {"message", "Player token not found"}
            }, version, keep_alive);
        }

        try {
            auto root = json::parse(req.body());
            if (!root.is_object()) throw std::exception();

            const auto& obj = root.as_object();
            if (!obj.contains("move")) throw std::exception();

            std::string move_dir = json::value_to<std::string>(obj.at("move"));
            auto dog = player->GetDog();
            double speed_val = player->GetSession()->GetMap()->GetDogSpeed();

            if (move_dir == "L") {
                dog->SetSpeed({-speed_val, 0.0});
                dog->SetDirection(model::Direction::WEST);
            } else if (move_dir == "R") {
                dog->SetSpeed({speed_val, 0.0});
                dog->SetDirection(model::Direction::EAST);
            } else if (move_dir == "U") {
                dog->SetSpeed({0.0, -speed_val});
                dog->SetDirection(model::Direction::NORTH);
            } else if (move_dir == "D") {
                dog->SetSpeed({0.0, speed_val});
                dog->SetDirection(model::Direction::SOUTH);
            } else if (move_dir.empty()) {
                dog->SetSpeed({0.0, 0.0});
            } else {
                throw std::exception();
            }

            return MakeJsonResponse(http::status::ok, json::object{}, version, keep_alive);

        } catch (...) {
            return MakeJsonResponse(http::status::bad_request, {
                {"code", "invalidArgument"},
                {"message", "Failed to parse action"}
            }, version, keep_alive);
        }
    }

    template <typename Request>
    http::response<http::string_body> HandleTick(Request&& req) const {
        unsigned version = req.version();
        bool keep_alive = req.keep_alive();

        if (!manual_tick_) {
            return MakeJsonResponse(http::status::bad_request, {
                {"code", "badRequest"},
                {"message", "Invalid endpoint in automatic tick mode"}
            }, version, keep_alive);
        }

        try {
            auto root = json::parse(req.body());
            if (!root.is_object()) throw std::exception();

            const auto& obj = root.as_object();
            if (!obj.contains("timeDelta")) throw std::exception();

            int64_t delta_ms = obj.at("timeDelta").as_int64();
            if (delta_ms < 0) throw std::exception();

            double delta_seconds = static_cast<double>(delta_ms) / 1000.0;
            game_.UpdateAllSessions(delta_seconds);

            return MakeJsonResponse(http::status::ok, json::object{}, version, keep_alive);

        } catch (...) {
            return MakeJsonResponse(http::status::bad_request, {
                {"code", "invalidArgument"},
                {"message", "Failed to parse tick request"}
            }, version, keep_alive);
        }
    }
};

// ================= FILE REQUEST HANDLER =================
class FileRequestHandler {
public:
    explicit FileRequestHandler(fs::path www_root)
        : www_root_(std::move(www_root)) {}

    template <typename Request>
    auto HandleRequest(Request&& req) const {
        unsigned version = req.version();
        bool keep_alive = req.keep_alive();

        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            return MethodNotAllowed("GET, HEAD"sv, version, keep_alive);
        }

        std::string target_str = std::string(req.target());
        size_t query_pos = target_str.find('?');
        if (query_pos != std::string::npos) {
            target_str = target_str.substr(0, query_pos);
        }

        std::string decoded_path;
        decoded_path.reserve(target_str.size());
        for (size_t i = 0; i < target_str.size(); ++i) {
            if (target_str[i] == '%' && i + 2 < target_str.size()) {
                int value = 0;
                std::istringstream is(target_str.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    decoded_path += static_cast<char>(value);
                    i += 2;
                } else {
                    decoded_path += target_str[i];
                }
            } else if (target_str[i] == '+') {
                decoded_path += ' ';
            } else {
                decoded_path += target_str[i];
            }
        }

        if (decoded_path == "/") {
            decoded_path = "/index.html";
        }

        fs::path full_path = fs::weakly_canonical(www_root_ / decoded_path.substr(1));

        if (full_path.string().find(www_root_.string()) != 0) {
            return MakeTextResponse(http::status::bad_request, "Bad Request", version, keep_alive);
        }

        if (!fs::exists(full_path) || fs::is_directory(full_path)) {
            return MakeTextResponse(http::status::not_found, "File Not Found", version, keep_alive);
        }

        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            return MakeTextResponse(http::status::internal_server_error, "Internal Error", version, keep_alive);
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        std::string content_type;
        std::string ext = full_path.extension().string();
        for (char& c : ext) c = std::tolower(c);

        if (ext == ".html" || ext == ".htm") content_type = "text/html";
        else if (ext == ".css") content_type = "text/css";
        else if (ext == ".js") content_type = "application/javascript";
        else if (ext == ".svg") content_type = "image/svg+xml";
        else if (ext == ".png") content_type = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") content_type = "image/jpeg";
        else if (ext == ".gif") content_type = "image/gif";
        else if (ext == ".ico") content_type = "image/x-icon";
        else content_type = "application/octet-stream";

        http::response<http::string_body> res{http::status::ok, version};
        res.set(http::field::content_type, content_type);
        res.body() = std::move(content);
        res.prepare_payload();
        res.keep_alive(keep_alive);
        return res;
    }

private:
    fs::path www_root_;

    static http::response<http::string_body> MakeTextResponse(
        http::status status, std::string_view text, unsigned version, bool keep_alive) {
        http::response<http::string_body> res{status, version};
        res.set(http::field::content_type, "text/plain");
        res.body() = std::string(text);
        res.prepare_payload();
        res.keep_alive(keep_alive);
        return res;
    }

    http::response<http::string_body> MethodNotAllowed(
        std::string_view allowed, unsigned version, bool keep_alive) const {
        http::response<http::string_body> res{http::status::method_not_allowed, version};
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::allow, allowed);
        res.body() = "Method Not Allowed";
        res.prepare_payload();
        res.keep_alive(keep_alive);
        return res;
    }
};

// ================= REQUEST HANDLER CONTAINER =================
class RequestHandler {
public:
    explicit RequestHandler(model::Game& game, model::PlayerTokens& tokens, fs::path www_root, bool& randomize_spawn)
        : api_handler_(game, tokens, randomize_spawn)
        , file_handler_(std::move(www_root)) {}

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Request, typename Send>
    void operator()(Request&& req, Send&& send, net::ip::tcp::endpoint endpoint) {
        std::string_view target = req.target();
        if (target.starts_with("/api/")) {
            send(api_handler_.HandleRequest(std::forward<Request>(req)));
        } else {
            send(file_handler_.HandleRequest(std::forward<Request>(req)));
        }
    }

    void SetTickMode(bool manual)