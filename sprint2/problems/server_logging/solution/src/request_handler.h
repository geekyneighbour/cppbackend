#pragma once
#include "http_server.h"
#include "model.h"
#include "logging.h"
#include <boost/json.hpp>
#include <string_view>
#include <boost/log/trivial.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <chrono>

namespace http_handler {
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace json = boost::json;
    namespace logging = boost::log;

    // Логирующий обёрточный хэндлер
    template <typename Handler>
    class LoggingRequestHandler {
    public:
        explicit LoggingRequestHandler(Handler& handler)
            : handler_(handler) {}

        template <typename Request, typename Send, typename Endpoint>
        void operator()(Request&& req, Send&& send, Endpoint&& endpoint) {
            LogRequest(req, endpoint);

            auto start = std::chrono::steady_clock::now();

            handler_(std::move(req),
                [this, &endpoint, start, send = std::forward<Send>(send)]
                (auto&& response) mutable {
                    auto end = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                    LogResponse(response, endpoint, duration);
                    send(std::forward<decltype(response)>(response));
                });
        }

    private:
        Handler& handler_;

        template <typename Request>
        static void LogRequest(const Request& req, const auto& endpoint) {
            json::value data{
                {"ip", endpoint.address().to_string()},
                {"URI", std::string(req.target())},
                {"method", std::string(req.method_string())}
            };

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, data)
                << "request received";
        }

        template <typename Response>
        static void LogResponse(const Response& resp, const auto& endpoint, int time_ms) {
            json::value data{
                {"ip", endpoint.address().to_string()},
                {"response_time", time_ms},
                {"code", resp.result_int()}
            };

            auto it = resp.base().find("Content-Type");
            if (it != resp.base().end()) {
                std::string content_type(it->value().data(), it->value().size());
                data.as_object()["content_type"] = content_type;
            } else {
                data.as_object()["content_type"] = nullptr;
            }

            BOOST_LOG_TRIVIAL(info)
                << boost::log::add_value(additional_data, data)
                << "response sent";
        }
    };

    // Основной хэндлер
    class RequestHandler {
    public:
        explicit RequestHandler(model::Game& game)
            : game_{ game } {}

        RequestHandler(const RequestHandler&) = delete;
        RequestHandler& operator=(const RequestHandler&) = delete;

        template <typename Body, typename Allocator, typename Send>
        void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
            using namespace std::literals;

            auto const bad_request = [&](std::string_view message) {
                http::response<http::string_body> res{ http::status::bad_request, req.version() };
                res.set(http::field::content_type, "application/json");

                json::object error;
                error["code"] = "badRequest";
                error["message"] = std::string(message);
                res.body() = json::serialize(error);
                res.prepare_payload();
                send(std::move(res));
            };

            auto const not_found = [&]() {
                http::response<http::string_body> res{ http::status::not_found, req.version() };
                res.set(http::field::content_type, "application/json");

                json::object error;
                error["code"] = "mapNotFound";
                error["message"] = "Map not found";
                res.body() = json::serialize(error);
                res.prepare_payload();
                send(std::move(res));
            };

            std::string path = std::string(req.target());

            if (req.method() == http::verb::get &&
                (path == "/api/v1/maps" || path.starts_with("/api/v1/maps/"))) {

                if (path == "/api/v1/maps") {
                    json::array result;

                    for (const auto& map : game_.GetMaps()) {
                        json::object obj;
                        obj["id"] = std::string(*map.GetId()); // исправлено для Tagged
                        obj["name"] = map.GetName();
                        result.push_back(obj);
                    }

                    http::response<http::string_body> res{ http::status::ok, req.version() };
                    res.set(http::field::content_type, "application/json");
                    res.body() = json::serialize(result);
                    res.prepare_payload();

                    return send(std::move(res));
                }

                std::string id = path.substr(std::string("/api/v1/maps/").size());
                const auto* map = game_.FindMap(model::Map::Id{ id });

                if (!map) {
                    return not_found();
                }

                json::object obj;
                obj["id"] = std::string(*map->GetId()); // исправлено для Tagged
                obj["name"] = map->GetName();

                // Сериализация содержимого карты
                obj["roads"] = SerializeRoads(map->GetRoads());
                obj["buildings"] = SerializeBuildings(map->GetBuildings());
                obj["offices"] = SerializeOffices(map->GetOffices());

                http::response<http::string_body> res{ http::status::ok, req.version() };
                res.set(http::field::content_type, "application/json");
                res.body() = json::serialize(obj);
                res.prepare_payload();

                return send(std::move(res));
            }

            return bad_request("Bad request");
        }

    private:
        model::Game& game_;

        json::array SerializeRoads(const std::vector<model::Road>& map_roads) {
            json::array roads;
            for (const auto& r : map_roads) {
                json::object road;
                road["x0"] = r.GetStart().x;
                road["y0"] = r.GetStart().y;

                if (r.IsHorizontal()) {
                    road["x1"] = r.GetEnd().x;
                } else {
                    road["y1"] = r.GetEnd().y;
                }

                roads.push_back(road);
            }
            return roads;
        }

        json::array SerializeBuildings(const std::vector<model::Building>& map_buildings) {
            json::array buildings;
            for (const auto& b : map_buildings) {
                json::object building;
                auto bounds = b.GetBounds();
                building["x"] = bounds.position.x;
                building["y"] = bounds.position.y;
                building["w"] = bounds.size.width;
                building["h"] = bounds.size.height;
                buildings.push_back(building);
            }
            return buildings;
        }

        json::array SerializeOffices(const std::vector<model::Office>& map_offices) {
            json::array offices;
            for (const auto& o : map_offices) {
                json::object office;
                office["id"] = std::string(*o.GetId()); // исправлено для Tagged
                office["x"] = o.GetPosition().x;
                office["y"] = o.GetPosition().y;
                office["offsetX"] = o.GetOffset().dx;
                office["offsetY"] = o.GetOffset().dy;
                offices.push_back(office);
            }
            return offices;
        }
    };

}  // namespace http_handler