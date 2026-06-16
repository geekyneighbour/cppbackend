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

        auto start_time = std::chrono::steady_clock::now();

        auto response_wrapper = [this, send = std::forward<Send>(send), start_time](auto&& response) {
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

            json::object data_resp{\
                {"response_time", duration},\
                {"code", response.result_int()},\
                {"content_type", std::string(response[http::field::content_type])}\
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
    std::string GenerateToken() {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 4; ++i) {
            ss << std::setw(8) << random_device_();
        }
        return ss.str();
    }

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

            json::object map_json = json::value_from(*map).as_object();
            
            const auto& loot_types = extra_data_.GetLootTypes(map_id);
            if (!loot_types.empty()) {
                map_json["lootTypes"] = loot_types;
            }

            send(MakeJsonResponse(http::status::ok, std::move(map_json), req.version()));
            return;
        }


        if (target == "/api/v1/game/join" || target == "/api/v1/game/join/") {
            if (req.method() != http::verb::post) {
                auto res = MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Only POST method is allowed", req.version());
                res.set(http::field::allow, "POST");
                send(std::move(res));
                return;
            }

            try {
                auto body = json::parse(req.body()).as_object();
                if (!body.contains("userName") || !body.contains("mapId")) {
                    send(MakeJsonResponse(http::status::bad_request, "invalidArgument", "Invalid join game JSON", req.version()));
                    return;
                }

                std::string user_name = json::value_to<std::string>(body.at("userName"));
                std::string map_id_str = json::value_to<std::string>(body.at("mapId"));

                if (user_name.empty()) {
                    send(MakeJsonResponse(http::status::bad_request, "invalidArgument", "Invalid name", req.version()));
                    return;
                }

                auto map_id = model::Map::Id{map_id_str};
                const auto* map = game_.FindMap(map_id);
                if (!map) {
                    send(MakeJsonResponse(http::status::not_found, "mapNotFound", "Map not found", req.version()));
                    return;
                }

                auto* session = game_.FindOrCreateSession(map);
                if (!session) {
                    send(MakeJsonResponse(http::status::bad_request, "mapNotFound", "Session not found", req.version()));
                    return;
                }
				session->GetLootGenerator().ForceInitialGeneration();
                
                uint32_t next_id = static_cast<uint32_t>(session->GetPlayers().size() + 1);
                auto player_id = model::Player::Id{next_id};
                auto dog_id = model::Dog::Id{next_id};
                
                model::PointDouble start_pos{0.0, 0.0};
                if (!map->GetRoads().empty()) {
                    start_pos = model::GetRandomPointOnRoad(map->GetRoads().front());
                }

                auto dog_ptr = std::make_shared<model::Dog>(dog_id, user_name, start_pos);
                auto player_ptr = std::make_shared<model::Player>(
    player_id,
    dog_ptr,
    map_id_str
);

               model::Player* player = player_ptr.get();
                
                session->AddPlayer(player_ptr);

                std::string token = GenerateToken();
                player_tokens_.AddPlayer(token, player);

                json::object resp_obj;
                resp_obj["authToken"] = token;
                resp_obj["playerId"] = *player->GetId();

                send(MakeJsonResponse(http::status::ok, std::move(resp_obj), req.version()));
            } catch (...) {
                send(MakeJsonResponse(http::status::bad_request, "invalidArgument", "Join game failure", req.version()));
            }
            return;
        }


        if (target == "/api/v1/game/state" || target == "/api/v1/game/state/") {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                auto res = MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Invalid method", req.version());
                res.set(http::field::allow, "GET, HEAD");
                send(std::move(res));
                return;
            }

            auto auth_header = req[http::field::authorization];
            if (auth_header.empty() || !auth_header.starts_with("Bearer ") || auth_header.size() <= 7) {
                send(MakeJsonResponse(http::status::unauthorized, "invalidToken", "Authorization header is required", req.version()));
                return;
            }

            std::string token = std::string(auth_header.substr(7));
            if (token.size() != 32) {
                send(MakeJsonResponse(http::status::unauthorized, "invalidToken", "Invalid token format", req.version()));
                return;
            }

            auto* player = player_tokens_.FindPlayerByToken(token);
            if (!player) {
                send(MakeJsonResponse(http::status::unauthorized, "unknownToken", "Player token not found", req.version()));
                return;
            }

            auto map_id = model::Map::Id{player->GetSessionId()};
            const auto* map = game_.FindMap(map_id);
            
            if (!map) {
                json::object empty_state;
                empty_state["players"] = json::object{};
                empty_state["lostObjects"] = json::object{};
                send(MakeJsonResponse(http::status::ok, std::move(empty_state), req.version()));
                return;
            }

            auto* session = game_.FindOrCreateSession(map);
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


        if (target == "/api/v1/game/tick" || target == "/api/v1/game/tick/") {
            if (req.method() != http::verb::post) {
                send(MakeJsonResponse(http::status::method_not_allowed, "invalidMethod", "Only POST method is allowed", req.version()));
                return;
            }

            try {
                auto body_json = json::parse(req.body());
                if (!body_json.as_object().contains("timeDelta")) {
                    send(MakeJsonResponse(http::status::bad_request, "invalidArgument", "Failed to parse tick request JSON", req.version()));
                    return;
                }

                int64_t delta_ms = body_json.as_object().at("timeDelta").as_int64();
				game.UpdateAllSessions(std::chrono::milliseconds(delta_ms));

                send(MakeJsonResponse(http::status::ok, json::object{}, req.version()));
            } catch (...) {
                send(MakeJsonResponse(http::status::bad_request, "invalidArgument", "Invalid JSON format", req.version()));
            }
            return;
        }

        send(MakeJsonResponse(http::status::bad_request, "badRequest", "Bad request", req.version()));
    }

    json::object GetSessionState(const model::GameSession* session) {
    json::object root_obj;

    json::object players_json;
    for (const auto& player : session->GetPlayers()) {
        const auto& dog = player->GetDog();
        json::object dog_json;
        dog_json["pos"] = json::array({dog->GetPosition().x, dog->GetPosition().y});
        dog_json["speed"] = json::array({dog->GetSpeed().vx, dog->GetSpeed().vy});
        

        std::string dir_str = "U"; // NORTH -> U (Up)
		if (dog->GetDirection() == model::Direction::SOUTH) dir_str = "D"; 
		else if (dog->GetDirection() == model::Direction::WEST) dir_str = "L";  
		else if (dog->GetDirection() == model::Direction::EAST) dir_str = "R";  
		dog_json["dir"] = dir_str;

        players_json[std::to_string(*player->GetId())] = std::move(dog_json);
    }
    root_obj["players"] = std::move(players_json);

    json::object lost_objects_json;
    for (const auto& [id, obj] : session->GetLostObjects()) {
        json::object item_json;
        item_json["type"] = static_cast<int64_t>(obj.type);
        item_json["pos"] = json::array({obj.pos.x, obj.pos.y});
        lost_objects_json[std::to_string(id)] = std::move(item_json);
    }
    root_obj["lostObjects"] = std::move(lost_objects_json);

    return root_obj;
}

private:
    model::Game& game_;
    const infra::ExtraData& extra_data_;
    fs::path static_path_;
    Strand api_strand_;
    bool tick_mode_ = false;

    model::PlayerTokens player_tokens_;
    std::random_device random_device_;
};

} // namespace http_handler