#include "json_loader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <boost/json.hpp>

namespace json_loader {

void LoadGlobalSettings(const boost::json::object& root_obj) {
    if (auto speed = root_obj.if_contains("defaultDogSpeed")) {
        if (speed->is_double()) {
            model::Map::SetDefaultDogSpeed(speed->as_double());
        } else if (speed->is_int64()) {
            model::Map::SetDefaultDogSpeed(static_cast<double>(speed->as_int64()));
        }
    }
}

void AddRoads(const boost::json::array& roads_array, model::Map& map) {
    for (const auto& road_val : roads_array) {
        if (!road_val.is_object()) continue;

        const auto& obj = road_val.as_object();

        int x0 = obj.contains("x0") ? static_cast<int>(obj.at("x0").as_int64()) : 0;
        int y0 = obj.contains("y0") ? static_cast<int>(obj.at("y0").as_int64()) : 0;

        if (obj.contains("x1")) {
            int x1 = static_cast<int>(obj.at("x1").as_int64());
            if (x1 != x0) {
                map.AddRoad(model::Road(
                    model::Road::HORIZONTAL,
                    model::Point{x0, y0},
                    x1
                ));
            }
        } else if (obj.contains("y1")) {
            int y1 = static_cast<int>(obj.at("y1").as_int64());
            if (y1 != y0) {
                map.AddRoad(model::Road(
                    model::Road::VERTICAL,
                    model::Point{x0, y0},
                    y1
                ));
            }
        }
    }
}

void AddBuildings(const boost::json::array& buildings_array, model::Map& map) {
    for (const auto& val : buildings_array) {
        if (!val.is_object()) continue;

        const auto& obj = val.as_object();

        if (!obj.contains("x") || !obj.contains("y") ||
            !obj.contains("w") || !obj.contains("h")) {
            continue;
        }

        int x = static_cast<int>(obj.at("x").as_int64());
        int y = static_cast<int>(obj.at("y").as_int64());
        int w = static_cast<int>(obj.at("w").as_int64());
        int h = static_cast<int>(obj.at("h").as_int64());

        map.AddBuilding(model::Building(
            model::Rectangle{
                model::Point{x, y},
                model::Size{w, h}
            }
        ));
    }
}

void AddOffices(const boost::json::array& offices_array, model::Map& map) {
    for (const auto& val : offices_array) {
        if (!val.is_object()) continue;

        const auto& obj = val.as_object();

        if (!obj.contains("id") ||
            !obj.contains("x") || !obj.contains("y") ||
            !obj.contains("offsetX") || !obj.contains("offsetY")) {
            continue;
        }

        std::string id = boost::json::value_to<std::string>(obj.at("id"));

        int x = static_cast<int>(obj.at("x").as_int64());
        int y = static_cast<int>(obj.at("y").as_int64());
        int offsetX = static_cast<int>(obj.at("offsetX").as_int64());
        int offsetY = static_cast<int>(obj.at("offsetY").as_int64());

        map.AddOffice(model::Office(
            model::Office::Id{id},
            model::Point{x, y},
            model::Offset{offsetX, offsetY}
        ));
    }
}

model::Game LoadGame(const std::filesystem::path& json_path) {
    model::Game game;

    try {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + json_path.string());
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        auto jv = boost::json::parse(buffer.str());
        if (!jv.is_object()) return game;

        const auto& root = jv.as_object();

        LoadGlobalSettings(root);

        if (!root.contains("maps") || !root.at("maps").is_array()) {
            return game;
        }

        for (const auto& map_val : root.at("maps").as_array()) {
            if (!map_val.is_object()) continue;

            const auto& obj = map_val.as_object();

            std::string id = boost::json::value_to<std::string>(obj.at("id"));
            std::string name = boost::json::value_to<std::string>(obj.at("name"));

            model::Map map(model::Map::Id{id}, name);

            if (auto speed = obj.if_contains("dogSpeed")) {
                if (speed->is_double()) {
                    map.SetDogSpeed(speed->as_double());
                } else if (speed->is_int64()) {
                    map.SetDogSpeed(static_cast<double>(speed->as_int64()));
                }
            }

            if (auto roads = obj.if_contains("roads")) {
                AddRoads(roads->as_array(), map);
            }

            if (auto buildings = obj.if_contains("buildings")) {
                AddBuildings(buildings->as_array(), map);
            }

            if (auto offices = obj.if_contains("offices")) {
                AddOffices(offices->as_array(), map);
            }

            game.AddMap(std::move(map));
        }

    } catch (const std::exception& e) {
        std::cerr << "Error loading game config: " << e.what() << std::endl;
    }

    return game;
}

} // namespace json_loader