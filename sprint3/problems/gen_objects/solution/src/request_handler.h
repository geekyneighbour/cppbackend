#pragma once

#include "http_server.h"
#include "model.h"
#include "logging.h"
#include "extra_data.h"

#include <boost/json.hpp>
#include <boost/beast/http.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/asio/strand.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <random>
#include <sstream>
#include <iomanip>
#include <optional>
#include <chrono>
#include <fstream>
#include <memory>

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
        json::object data_req{\
            {"ip", endpoint.address().to_string()},\
            {"URI", std::string(req.target())},\
            {"method", std::string(req.method_string())}\
        };

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data_req)
            << "request received";

        auto start = std::chrono::steady_clock::now();

        handler_(std::forward<Request>(req), [send = std::forward<Send>(send), start](auto&& response) {
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            json::object data_res{\
                {"response_time", duration},\
                {"code", response.result_int()},\
                {"content_type", std::string(response[http::field::content_type])}\
            };

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, data_res)
                << "response sent";

            send(std::forward<decltype(response)>(response));
        }, endpoint);
    }

private:
    Handler& handler_;
};

// ================= MAIN REQUEST HANDLER =================
class RequestHandler : public std::enable_shared_from_this<RequestHandler> {
public:
    using Strand = net::strand<net::io_context::executor_type>;

    RequestHandler(model::Game& game, infra::ExtraData& extra_data, fs::path static_path, Strand api_strand)
        : game_{game}
        , extra_data_{extra_data}
        , static_path_{std::move(static_path)}
        , api_strand_{api_strand} {}

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Fields, typename Send>
    void operator()(http::request<Body, Fields>&& req, Send&& send, net::ip::tcp::endpoint endpoint) {
        std::string target = std::string(req.target());

        if (target.starts_with("/api/")) {
            auto handle_api = [self = shared_from_this(), req = std::move(req), send = std::forward<Send>(send)]() mutable {
                try {
                    auto response = self->HandleApiRequest(req);
                    send(std::move(response));
                } catch (const std::exception& e) {
                    send(self->ServerError(req.version(), req.keep_alive()));
                }
            };
            net::dispatch(api_strand_, std::move(handle_api));
        } else {
            auto response = HandleStaticRequest(req);
            send(std::move(response));
        }
    }

    void SetTickMode(bool manual) {
        manual_tick_ = manual;
    }

private:
    model::Game& game_;
    infra::ExtraData& extra_data_;
    fs::path static_path_;
    Strand api_strand_;
    bool manual_tick_ = false;

    template <typename T>
    http::response<http::string_body> MakeJsonResponse(http::status status, const T& body_data, unsigned version) const {
        http::response<http::string_body> res{status, version};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(body_data);
        res.prepare_payload();
        return res;
    }

    template <typename Body, typename Fields>
    http::response<http::string_body> HandleApiRequest(const http::request<Body, Fields>& req) {
        std::string target = std::string(req.target());

        // --- 1. СПИСОК КАРТ ---
        if (target == "/api/v1/maps" || target == "/api/v1/maps/") {
            if (req.method() != http::method::get && req.method() != http::method::head) {
                return MethodNotAllowed("GET, HEAD", req.version());
            }

            json::array maps_json;
            for (const auto& map : game_.GetMaps()) {
                json::object map_obj;
                map_obj["id"] = *map.GetId();
                map_obj["name"] = map.GetName();
                maps_json.push_back(map_obj);
            }
            return MakeJsonResponse(http::status::ok, maps_json, req.version());
        }

        // --- 2. ПОЛУЧЕНИЕ КАРТЫ С LOOT TYPES ---
        if (target.starts_with("/api/v1/maps/")) {
            if (req.method() != http::method::get && req.method() != http::method::head) {
                return MethodNotAllowed("GET, HEAD", req.version());
            }

            std::string map_id = target.substr(13);
            const auto* map = game_.FindMap(model::Map::Id{map_id});
            if (!map) {
                return MakeJsonResponse(http::status::not_found, 
                    json::object{{"code", "mapNotFound"}, {"message", "Map not found"}}, req.version());
            }

            json::value map_json = json::value_from(*map);
            map_json.as_object()["lootTypes"] = extra_data_.GetLootTypes(map->GetId());

            return MakeJsonResponse(http::status::ok, map_json, req.version());
        }

        // --- 3. ВХОД В ИГРУ ---
        if (target == "/api/v1/game/join") {
            if (req.method() != http::method::post) {
                return MethodNotAllowed("POST", req.version());
            }

            try {
                auto body = json::parse(req.body());
                std::string user_name = json::value_to<std::string>(body.as_object().at("userName"));
                std::string map_id = json::value_to<std::string>(body.as_object().at("mapId"));

                if (user_name.empty()) {
                    return MakeJsonResponse(http::status::bad_request,
                        json::object{{"code", "invalidArgument"}, {"message", "Invalid name"}}, req.version());
                }

                const auto* map = game_.FindMap(model::Map::Id{map_id});
                if (!map) {
                    return MakeJsonResponse(http::status::not_found,
                        json::object{{"code", "mapNotFound"}, {"message", "Map not found"}}, req.version());
                }

                auto [player, token] = game_.JoinPlayer(user_name, map_id);

                json::object res_body;
                res_body["authToken"] = token;
                res_body["playerId"] = *player->GetId();

                return MakeJsonResponse(http::status::ok, res_body, req.version());
            } catch (...) {
                return MakeJsonResponse(http::status::bad_request,
                    json::object{{"code", "invalidArgument"}, {"message", "Join game request parse error"}}, req.version());
            }
        }

        // --- 4. СПИСОК ИГРОКОВ ---
        if (target == "/api/v1/game/players" || target == "/api/v1/game/players/") {
            if (req.method() != http::method::get && req.method() != http::method::head) {
                return MethodNotAllowed("GET, HEAD", req.version());
            }

            auto token = ExtractToken(req);
            if (!token) return UnauthorizedError(req.version());

            auto* player = game_.FindPlayerByToken(*token);
            if (!player) return UnknownTokenError(req.version());

            auto* session = player->GetSession();
            json::object root_obj;
            for (const auto& p : session->GetPlayers()) {
                root_obj[std::to_string(*p->GetId())] = json::object{{"name", p->GetName()}};
            }
            return MakeJsonResponse(http::status::ok, root_obj, req.version());
        }

        // --- 5. СОСТОЯНИЕ ИГРЫ (PLAYERS + LOST OBJECTS) ---
        if (target == "/api/v1/game/state" || target == "/api/v1/game/state/") {
            if (req.method() != http::method::get && req.method() != http::method::head) {
                return MethodNotAllowed("GET, HEAD", req.version());
            }

            auto token = ExtractToken(req);
            if (!token) return UnauthorizedError(req.version());

            auto* player = game_.FindPlayerByToken(*token);
            if (!player) return UnknownTokenError(req.version());

            auto* session = player->GetSession();
            json::object root_obj;

            // Игроки
            json::object players_json;
            for (const auto& p : session->GetPlayers()) {
                const auto& dog = p->GetDog();
                json::object dog_json;
                dog_json["pos"] = json::array{dog->GetPosition().x, dog->GetPosition().y};
                dog_json["speed"] = json::array{dog->GetSpeed().vx, dog->GetSpeed().vy};

                std::string dir_str = "N";
                if (dog->GetDirection() == model::Direction::SOUTH) dir_str = "S";
                else if (dog->GetDirection() == model::Direction::WEST) dir_str = "W";
                else if (dog->GetDirection() == model::Direction::EAST) dir_str = "E";
                dog_json["dir"] = dir_str;

                players_json[std::to_string(*p->GetId())] = std::move(dog_json);
            }
            root_obj["players"] = std::move(players_json);

            // Предметы
            json::object lost_objects_json;
            for (const auto& [id, obj] : session->GetLostObjects()) {
                json::object item_json;
                item_json["type"] = obj.type;
                item_json["pos"] = json::array({obj.pos.x, obj.pos.y});
                lost_objects_json[std::to_string(id)] = std::move(item_json);
            }
            root_obj["lostObjects"] = std::move(lost_objects_json);

            return MakeJsonResponse(http::status::ok, root_obj, req.version());
        }

        // --- 6. УПРАВЛЕНИЕ ПЕРСОНАЖЕМ ---
        if (target == "/api/v1/game/player/action") {
            if (req.method() != http::method::post) {
                return MethodNotAllowed("POST", req.version());
            }

            auto token = ExtractToken(req);
            if (!token) return UnauthorizedError(req.version());

            auto* player = game_.FindPlayerByToken(*token);
            if (!player) return UnknownTokenError(req.version());

            try {
                auto body = json::parse(req.body());
                std::string move_dir = json::value_to<std::string>(body.as_object().at("move"));
                player->GetDog()->SetMoveDirection(move_dir, player->GetSession()->GetMap()->GetDogSpeed());
                return MakeJsonResponse(http::status::ok, json::object{}, req.version());
            } catch (...) {
                return MakeJsonResponse(http::status::bad_request,
                    json::object{{"code", "invalidArgument"}, {"message", "Failed to parse action"}}, req.version());
            }
        }

        // --- 7. ИГРОВОЙ ТИК (ДЛЯ РУЧНОГО ОБНОВЛЕНИЯ В ТЕСТАХ) ---
        if (target == "/api/v1/game/tick") {
            if (!manual_tick_) {
                return MakeJsonResponse(http::status::bad_request,
                    json::object{{"code", "badRequest"}, {"message", "Tick invocation is not allowed in automatic mode"}}, req.version());
            }
            if (req.method() != http::method::post) {
                return MethodNotAllowed("POST", req.version());
            }

            try {
                auto body = json::parse(req.body());
                if (!body.as_object().contains("timeDelta")) {
                    return MakeJsonResponse(http::status::bad_request,
                        json::object{{"code", "invalidArgument"}, {"message", "Failed to parse tick request"}}, req.version());
                }

                double delta_ms = 0.0;
                const auto& delta_val = body.as_object().at("timeDelta");
                if (delta_val.is_int64()) {
                    delta_ms = static_cast<double>(delta_val.as_int64());
                } else if (delta_val.is_double()) {
                    delta_ms = delta_val.as_double();
                } else {
                    throw std::invalid_argument("Invalid timeDelta type");
                }

                game_.UpdateAllSessions(delta_ms);
                return MakeJsonResponse(http::status::ok, json::object{}, req.version());
            } catch (const std::exception& e) {
                return MakeJsonResponse(http::status::bad_request,
                    json::object{{"code", "invalidArgument"}, {"message", e.what()}}, req.version());
            }
        }

        return MakeJsonResponse(http::status::bad_request,
            json::object{{"code", "badRequest"}, {"message", "Bad request"}}, req.version());
    }

    template <typename Body, typename Fields>
    http::response<http::string_body> HandleStaticRequest(const http::request<Body, Fields>& req) {
        std::string target = std::string(req.target());
        
        if (req.method() != http::method::get && req.method() != http::method::head) {
            return MethodNotAllowed("GET, HEAD", req.version());
        }

        std::string req_path = target;
        size_t pos = req_path.find('?');
        if (pos != std::string::npos) {
            req_path = req_path.substr(0, pos);
        }

        fs::path base_dir = fs::weakly_canonical(static_path_);
        fs::path rel_path{req_path.substr(1)};
        fs::path full_path = fs::weakly_canonical(base_dir / rel_path);

        if (fs::is_directory(full_path)) {
            full_path /= "index.html";
        }

        std::string full_path_str = full_path.string();
        std::string base_dir_str = base_dir.string();

        if (full_path_str.size() < base_dir_str.size() || 
            full_path_str.compare(0, base_dir_str.size(), base_dir_str) != 0 || 
            !fs::exists(full_path)) {
            return NotFoundError(req.version());
        }

        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            return NotFoundError(req.version());
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        std::string content_type;
        std::string ext = full_path.extension().string();
        if (ext == ".html" || ext == ".htm") content_type = "text/html";
        else if (ext == ".css") content_type = "text/css";
        else if (ext == ".js") content_type = "application/javascript";
        else if (ext == ".svg") content_type = "image/svg+xml";
        else if (ext == ".png") content_type = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") content_type = "image/jpeg";
        else content_type = "text/plain";
        
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, content_type);
        res.set(http::field::cache_control, "no-cache");
        res.body() = content;
        res.prepare_payload();
        return res;
    }

    std::optional<std::string> ExtractToken(const http::request_header<>& req) const {
        auto auth_it = req.find(http::field::authorization);
        if (auth_it == req.end()) return std::nullopt;

        std::string_view auth_val = auth_it->value();
        if (!auth_val.starts_with("Bearer ") || auth_val.size() <= 7) return std::nullopt;

        std::string token = std::string(auth_val.substr(7));
        while (!token.empty() && token.back() == ' ') token.pop_back();

        if (token.size() != 32) return std::nullopt;
        return token;
    }

    http::response<http::string_body> MethodNotAllowed(std::string_view allow_methods, unsigned version) const {
        http::response<http::string_body> res{http::status::method_not_allowed, version};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::allow, allow_methods);
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(json::object{{"code", "invalidMethod"}, {"message", "Invalid method"}});
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> UnauthorizedError(unsigned version) const {
        return MakeJsonResponse(http::status::unauthorized,
            json::object{{"code", "invalidToken"}, {"message", "Authorization header is missing"}}, version);
    }

    http::response<http::string_body> UnknownTokenError(unsigned version) const {
        return MakeJsonResponse(http::status::unauthorized,
            json::object{{"code", "unknownToken"}, {"message", "Player token has not been found"}}, version);
    }

    http::response<http::string_body> NotFoundError(unsigned version) const {
        http::response<http::string_body> res{http::status::not_found, version};
        res.set(http::field::content_type, "text/plain");
        res.body() = "File not found";
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> ServerError(unsigned v, bool keep_alive) const {
        return MakeJsonResponse(http::status::internal_server_error,
            json::object{{"code", "internalError"}, {"message", "Internal server error"}}, v);
    }
};

} // namespace http_handler