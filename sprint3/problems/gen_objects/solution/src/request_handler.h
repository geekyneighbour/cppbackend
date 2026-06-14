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
        json::object data_req{
            {"ip", endpoint.address().to_string()},
            {"URI", std::string(req.target())},
            {"method", std::string(req.method_string())}
        };

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data_req)
            << "request received";

        auto start_time = std::chrono::steady_clock::now();

        auto response_wrapper = [this, send = std::forward<Send>(send), start_time](auto&& response) {
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
        };

        handler_(std::forward<Request>(req), std::move(response_wrapper));
    }

private:
    Handler& handler_;
};

// ================= MAIN REQUEST HANDLER =================
class RequestHandler : public std::enable_shared_from_this<RequestHandler> {
public:
    using Strand = net::strand<net::io_context::executor_type>;

    explicit RequestHandler(model::Game& game, const infra::ExtraData& extra_data, fs::path static_path, Strand api_strand)
        : game_(game)
        , extra_data_(extra_data)
        , static_path_(fs::weakly_canonical(std::move(static_path)))
        , api_strand_(api_strand) {}

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    void SetTickMode(bool mode) {
        tick_mode_ = mode;
    }

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send, net::ip::tcp::endpoint endpoint) {
        auto logging_handler = LoggingRequestHandler<RequestHandler>(*this);
        logging_handler(std::move(req), std::forward<Send>(send), endpoint);
    }

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        std::string_view target = req.target();

        if (target.starts_with("/api/")) {
            net::dispatch(api_strand_, [self = shared_from_this(), req = std::move(req), send = std::forward<Send>(send)]() mutable {
                self->HandleApiRequest(std::move(req), std::move(send));
            });
        } else {
            HandleStaticRequest(std::move(req), std::forward<Send>(send));
        }
    }

private:
    // Вспомогательные методы создания ответов
    static http::response<http::string_body> MakeJsonResponse(http::status status, json::value&& body, unsigned version) {
        http::response<http::string_body> res{status, version};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(body);
        res.prepare_payload();
        return res;
    }

    static http::response<http::string_body> MakeJsonResponse(http::status status, std::string_view code, std::string_view message, unsigned version) {
        json::object obj{{"code", std::string(code)}, {"message", std::string(message)}};
        return MakeJsonResponse(status, std::move(obj), version);
    }

    // Обработка статических файлов
    template <typename Body, typename Allocator, typename Send>
    void HandleStaticRequest(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            send(MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Only GET and HEAD methods are allowed", req.version()));
            return;
        }

        std::string target_str{req.target()};
        size_t query_pos = target_str.find('?');
        if (query_pos != std::string::npos) {
            target_str = target_str.substr(0, query_pos);
        }

        fs::path req_path{target_str};
        fs::path full_path = static_path_ / req_path.relative_path();

        if (fs::is_directory(full_path)) {
            full_path /= "index.html";
        }

        // Защита от Directory Traversal (выхода за пределы корня)
        std::string full_path_str = fs::weakly_canonical(full_path).string();
        std::string static_path_str = static_path_.string();

        if (full_path_str.size() < static_path_str.size() || full_path_str.compare(0, static_path_str.size(), static_path_str) != 0) {
            send(MakeJsonResponse(http::status::bad_request, "badRequest", "Directory traversal detected", req.version()));
            return;
        }

        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            send(MakeJsonResponse(http::status::not_found, "fileNotFound", "File not found", req.version()));
            return;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        std::string content_type = "text/plain";
        std::string ext = full_path.extension().string();
        for (auto& c : ext) c = std::tolower(c);

        if (ext == ".html" || ext == ".htm") content_type = "text/html";
        else if (ext == ".css") content_type = "text/css";
        else if (ext == ".js") content_type = "application/javascript";
        else if (ext == ".svg") content_type = "image/svg+xml";
        else if (ext == ".png") content_type = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") content_type = "image/jpeg";
        else if (ext == ".gif") content_type = "image/gif";

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, content_type);
        res.set(http::field::cache_control, "no-cache");
        if (req.method() == http::verb::get) {
            res.body() = std::move(content);
        }
        res.prepare_payload();
        send(std::move(res));
    }

    // Маршрутизация API-запросов
    template <typename Body, typename Allocator, typename Send>
    void HandleApiRequest(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        std::string_view target = req.target();

        if (target == "/api/v1/maps" || target == "/api/v1/maps/") {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                auto res = MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Invalid method", req.version());
                res.set(http::field::allow, "GET, HEAD");
                send(std::move(res));
                return;
            }
            json::array maps_arr;
            for (const auto& map : game_.GetMaps()) {
                json::object m;
                m["id"] = *map->GetId();
                m["name"] = map->GetName();
                maps_arr.push_back(std::move(m));
            }
            send(MakeJsonResponse(http::status::ok, std::move(maps_arr), req.version()));
            return;
        }

        if (target.starts_with("/api/v1/maps/")) {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                auto res = MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Invalid method", req.version());
                res.set(http::field::allow, "GET, HEAD");
                send(std::move(res));
                return;
            }
            std::string_view map_id_str = target.substr(13);
            if (map_id_str.ends_with('/')) {
                map_id_str.remove_suffix(1);
            }

            auto map_id = model::Map::Id{std::string(map_id_str)};
            const auto* map = game_.FindMap(map_id);
            if (!map) {
                send(MakeJsonResponse(http::status::not_found, "mapNotFound", "Map not found", req.version()));
                return;
            }

s
            json::value map_json = json::value_from(*map);
            map_json.as_object()["lootTypes"] = extra_data_.GetLootTypes(map_id);

            send(MakeJsonResponse(http::status::ok, std::move(map_json), req.version()));
            return;
        }


        if (target == "/api/v1/game/state") {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                auto res = MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Invalid method", req.version());
                res.set(http::field::allow, "GET, HEAD");
                send(std::move(res));
                return;
            }


            const model::GameSession* session = nullptr; 

            if (!session) {
                json::object empty_state;
                empty_state["players"] = json::object{};
                empty_state["lostObjects"] = json::object{};
                send(MakeJsonResponse(http::status::ok, std::move(empty_state), req.version()));
                return;
            }

            send(MakeJsonResponse(http::status::ok, GetSessionState(session), req.version()));
            return;
        }

        if (target == "/api/v1/game/tick") {
            if (req.method() != http::verb::post) {
                send(MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Only POST method is allowed", req.version()));
                return;
            }
            if (!tick_mode_) {
                send(MakeJsonResponse(http::status::bad_request, "badRequest", "Tick invocation is not allowed in automatic mode", req.version()));
                return;
            }

            try {
                auto body_json = json::parse(req.body());
                if (!body_json.as_object().contains("timeDelta") || !body_json.as_object().at("timeDelta").is_number()) {
                    send(MakeJsonResponse(http::status::bad_request, "invalidArgument", "Failed to parse tick request JSON", req.version()));
                    return;
                }

                double delta_ms = body_json.as_object().at("timeDelta").as_double();
                game_.UpdateAllSessions(delta_ms / 1000.0);

                send(MakeJsonResponse(http::status::ok, json::object{}, req.version()));
            } catch (...) {
                send(MakeJsonResponse(http::status::bad_request, "invalidArgument", "Invalid JSON format", req.version()));
            }
            return;
        }

        send(MakeJsonResponse(http::status::bad_request, "badRequest", "Bad request", req.version()));
    }

    // Формирование JSON-состояния сессии (собаки + потерянные предметы)
    json::object GetSessionState(const model::GameSession* session) {
        json::object root_obj;

        // 1. Сериализация игроков/собак в блок "players"
        json::object players_json;
        for (const auto& player : session->GetPlayers()) {
            const auto& dog = player->GetDog();
            json::object dog_json;
            dog_json["pos"] = json::array{dog->GetPosition().x, dog->GetPosition().y};
            dog_json["speed"] = json::array{dog->GetSpeed().vx, dog->GetSpeed().vy};
            
            std::string dir_str = "N";
            if (dog->GetDirection() == model::Direction::SOUTH) dir_str = "S";
            else if (dog->GetDirection() == model::Direction::WEST) dir_str = "W";
            else if (dog->GetDirection() == model::Direction::EAST) dir_str = "E";
            dog_json["dir"] = dir_str;

            players_json[std::to_string(*player->GetId())] = std::move(dog_json);
        }
        root_obj["players"] = std::move(players_json);

        // 2. Сериализация динамически сгенерированных предметов в "lostObjects"
        json::object lost_objects_json;
        for (const auto& [id, obj] : session->GetLostObjects()) {
            json::object item_json;
            item_json["type"] = obj.type;
            item_json["pos"] = json::array{obj.pos.x, obj.pos.y};
            
            lost_objects_json[std::to_string(id)] = std::move(item_json);
        }
        root_obj["lostObjects"] = std::move(lost_objects_json);

        return root_obj;
    }

    model::Game& game_;
    const infra::ExtraData& extra_data_;
    fs::path static_path_;
    Strand api_strand_;
    bool tick_mode_ = false;
	infra::ExtraData& extra_data_;
};

} // namespace http_handler#pragma once

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

    // Конструктор теперь сохраняет ссылку на extra_data
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
    infra::ExtraData& extra_data_; // Ссылка на хранилище lootTypes
    fs::path static_path_;
    Strand api_strand_;
    bool manual_tick_ = false;

    // Вспомогательный метод для сборки JSON ответов
    template <typename T>
    http::response<http::string_body> MakeJsonResponse(http::status status, const T& body_data, unsigned version) const {
        http::response<http::string_body> res{status, version};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = json::serialize(body_data);
        res.prepare_payload();
        return res;
    }

    // Обработка всех API запросов
    template <typename Body, typename Fields>
    http::response<http::string_body> HandleApiRequest(const http::request<Body, Fields>& req) {
        std::string target = std::string(req.target());

        // --- СПИСОК КАРТ ---
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

        // --- КОНКРЕТНАЯ КАРТА ---
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

            // Сериализуем карту и ОБЯЗАТЕЛЬНО подмешиваем lootTypes
            json::value map_json = json::value_from(*map);
            map_json.as_object()["lootTypes"] = extra_data_.GetLootTypes(map->GetId());

            return MakeJsonResponse(http::status::ok, map_json, req.version());
        }

        // --- ВХОД В ИГРУ ---
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

        // --- СПИСОК ИГРОКОВ ---
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

        // --- СОСТОЯНИЕ ИГРЫ (Игроки + Предметы) ---
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

            // 1. Игроки
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

            // 2. Предметы (lostObjects)
            json::object lost_objects_json;
            for (const auto& [id, obj] : session->GetLostObjects()) {
                json::object item_json;
                item_json["type"] = obj.type;
                item_json["pos"] = json::array{obj.pos.x, obj.pos.y};
                lost_objects_json[std::to_string(id)] = std::move(item_json);
            }
            root_obj["lostObjects"] = std::move(lost_objects_json);

            return MakeJsonResponse(http::status::ok, root_obj, req.version());
        }

        // --- ДЕЙСТВИЕ ИГРОКА (Управление) ---
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

        // --- ДОБАВЛЕННАЯ РУЧКА: ИГРОВОЙ ТИК (/api/v1/game/tick) ---
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

                // Обновляем состояние всей игры
                game_.UpdateAllSessions(delta_ms);

                return MakeJsonResponse(http::status::ok, json::object{}, req.version());
            } catch (const std::exception& e) {
                return MakeJsonResponse(http::status::bad_request,
                    json::object{{"code", "invalidArgument"}, {"message", e.what()}}, req.version());
            }
        }

        // Неизвестный эндпоинт API
        return MakeJsonResponse(http::status::bad_request,
            json::object{{"code", "badRequest"}, {"message", "Bad request"}}, req.version());
    }

    // Обработка статического контента
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