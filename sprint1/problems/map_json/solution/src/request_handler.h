#pragma once
#include "http_server.h"
#include "model.h"
#include <boost/json.hpp>
#include <string_view>

namespace http_handler {
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;

class RequestHandler {
public:
    explicit RequestHandler(model::Game& game)
        : game_{game} {
    }

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

        if (!path.starts_with("/api/")) {
            return bad_request("Bad request");
        }

        if (req.method() != http::verb::get) {
            return bad_request("Bad request");
        }

        if (path == "/api/v1/maps") {
            json::array result;

            for (const auto& map : game_.GetMaps()) {
                json::object obj;
                obj["id"] = std::string(*map.GetId());  
                obj["name"] = map.GetName();  
                result.push_back(obj);
            }

            http::response<http::string_body> res{ http::status::ok, req.version() };
            res.set(http::field::content_type, "application/json");
            res.body() = json::serialize(result);
            res.prepare_payload();

            return send(std::move(res));
        }

        if (path.starts_with("/api/v1/maps/")) {
            std::string id = path.substr(std::string("/api/v1/maps/").size());

            const auto* map = game_.FindMap(model::Map::Id{ id });

            if (!map) {
                return not_found();
            }

            json::object obj;
            obj["id"] = std::string(*map->GetId()); 
            obj["name"] = map->GetName();

            json::array roads;
            for (const auto& r : map->GetRoads()) {
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
            obj["roads"] = roads;

            json::array buildings;
            for (const auto& b : map->GetBuildings()) {
                json::object building;
                auto bounds = b.GetBounds();
                building["x"] = bounds.position.x;
                building["y"] = bounds.position.y;
                building["w"] = bounds.size.width;
                building["h"] = bounds.size.height;
                buildings.push_back(building);
            }
            obj["buildings"] = buildings;

            json::array offices;
            for (const auto& o : map->GetOffices()) {
                json::object office;
                office["id"] = std::string(*o.GetId());  
                office["x"] = o.GetPosition().x;
                office["y"] = o.GetPosition().y;
                office["offsetX"] = o.GetOffset().dx;
                office["offsetY"] = o.GetOffset().dy;
                offices.push_back(office);
            }
            obj["offices"] = offices;

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
};

}  // namespace http_handler