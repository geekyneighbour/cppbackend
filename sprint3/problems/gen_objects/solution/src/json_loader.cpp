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
        const auto& road_obj = road_val.as_object();

        int x0 = static_cast<int>(road_obj.if_contains("x0") ? road_obj.at("x0").as_int64() : 0);
        int y0 = static_cast<int>(road_obj.if_contains("y0") ? road_obj.at("y0").as_int64() : 0);

        if (road_obj.contains("x1")) {
            int x1 = static_cast<int>(road_obj.at("x1").as_int64());
            map.AddRoad(model::Road(model::Road::HORIZONTAL, model::Point{x0, y0}, x1));
        } else if (road_obj.contains("y1")) {
            int y1 = static_cast<int>(road_obj.at("y1").as_int64());
            map.AddRoad(model::Road(model::Road::VERTICAL, model::Point{x0, y0}, y1));
        }
    }
}

void AddBuildings(const boost::json::array& buildings_array, model::Map& map) {
    for (const auto& b_val : buildings_array) {
        if (!b_val.is_object()) continue;
        const auto& b_obj = b_val.as_object();
        int x = static_cast<int>(b_obj.at("x").as_int64());
        int y = static_cast<int>(b_obj.at("y").as_int64());
        int w = static_cast<int>(b_obj.at("w").as_int64());
        int h = static_cast<int>(b_obj.at("h").as_int64());
        map.AddBuilding(model::Building(model::Rectangle{model::Point{x, y}, model::Size{w, h}}));
    }
}

void AddOffices(const boost::json::array& offices_array, model::Map& map) {
    for (const auto& o_val : offices_array) {
        if (!o_val.is_object()) continue;
        const auto& o_obj = o_val.as_object();
        std::string id = boost::json::value_to<std::string>(o_obj.at("id"));
        int x = static_cast<int>(o_obj.at("x").as_int64());
        int y = static_cast<int>(o_obj.at("y").as_int64());
        int ox = static_cast<int>(o_obj.at("offsetX").as_int64());
        int oy = static_cast<int>(o_obj.at("offsetY").as_int64());
        map.AddOffice(model::Office(model::Office::Id{id}, model::Point{x, y}, model::Offset{ox, oy}));
    }
}

model::Game LoadGame(const std::filesystem::path& json_path, infra::ExtraData& extra_data) {
    model::Game game;

    try {
        std::ifstream ifs(json_path);
        if (!ifs.is_open()) {
            throw std::runtime_error("Failed to open JSON file");
        }

        std::stringstream ss;
        ss << ifs.rdbuf();
        auto root_value = boost::json::parse(ss.str());
        const auto& obj = root_value.as_object();
        
        LoadGlobalSettings(obj);

        if (obj.contains("lootGeneratorConfig")) {
            const auto& loot_config = obj.at("lootGeneratorConfig").as_object();
            double period_sec = loot_config.at("period").as_double();
            double probability = loot_config.at("probability").as_double();
            game.SetLootGeneratorConfig(
                std::chrono::milliseconds(static_cast<int64_t>(period_sec * 1000.0)),
                probability
            );
        }

        if (!obj.contains("maps")) return game;

        for (const auto& map_val : obj.at("maps").as_array()) {
            if (!map_val.is_object()) continue;
            const auto& map_obj = map_val.as_object();

            std::string id = boost::json::value_to<std::string>(map_obj.at("id"));
            std::string name = boost::json::value_to<std::string>(map_obj.at("name"));

            model::Map map(model::Map::Id{id}, name);
            
            if (auto speed = map_obj.if_contains("dogSpeed")) {
                if (speed->is_double()) {
                    map.SetDogSpeed(speed->as_double());
                } else if (speed->is_int64()) {
                    map.SetDogSpeed(static_cast<double>(speed->as_int64()));
                }
            }

            if (auto roads = map_obj.if_contains("roads")) {
                AddRoads(roads->as_array(), map);
            }

            if (auto buildings = map_obj.if_contains("buildings")) {
                AddBuildings(buildings->as_array(), map);
            }

            if (auto offices = map_obj.if_contains("offices")) {
                AddOffices(offices->as_array(), map);
            }
			
			auto& inserted_map = game.AddMap(std::move(map));

            if (map_obj.contains("lootTypes")) {
				auto loot_types_arr = map_obj.at("lootTypes").as_array();
				inserted_map.SetLootTypesCount(loot_types_arr.size());
				extra_data.SaveLootTypes(inserted_map.GetId(), loot_types_arr);
			}


        }

    } catch (const std::exception& e) {
        std::cerr << "Error loading game config: " << e.what() << std::endl;
        throw;
    }

    return game;
}

}  // namespace json_loader