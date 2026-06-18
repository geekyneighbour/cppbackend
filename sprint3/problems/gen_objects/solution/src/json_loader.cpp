#include "json_loader.h"
#include "loot_generator.h"
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
            if (x1 != x0) {
                map.AddRoad(model::Road(model::Road::HORIZONTAL, model::Point{x0, y0}, x1));
            }
        } else if (road_obj.contains("y1")) {
            int y1 = static_cast<int>(road_obj.at("y1").as_int64());
            if (y1 != y0) {
                map.AddRoad(model::Road(model::Road::VERTICAL, model::Point{x0, y0}, y1));
            }
        }
    }
}

void AddBuildings(const boost::json::array& buildings_array, model::Map& map) {
    for (const auto& building_val : buildings_array) {
        if (!building_val.is_object()) continue;
        const auto& building_obj = building_val.as_object();

        int x = static_cast<int>(building_obj.at("x").as_int64());
        int y = static_cast<int>(building_obj.at("y").as_int64());
        int w = static_cast<int>(building_obj.at("w").as_int64());
        int h = static_cast<int>(building_obj.at("h").as_int64());

        map.AddBuilding(model::Building(
            model::Rectangle{
                model::Point{x, y},
                model::Size{w, h}
            }
        ));
    }
}

void AddOffices(const boost::json::array& offices_array, model::Map& map) {
    for (const auto& office_val : offices_array) {
        if (!office_val.is_object()) continue;
        const auto& office_obj = office_val.as_object();

        std::string office_id = boost::json::value_to<std::string>(office_obj.at("id"));
        int x = static_cast<int>(office_obj.at("x").as_int64());
        int y = static_cast<int>(office_obj.at("y").as_int64());
        int offsetX = static_cast<int>(office_obj.at("offsetX").as_int64());
        int offsetY = static_cast<int>(office_obj.at("offsetY").as_int64());

        map.AddOffice(model::Office(
            model::Office::Id{office_id},
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
        std::string json_str = buffer.str();

        boost::json::value jv;
        try {
            jv = boost::json::parse(json_str);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing json: " << e.what() << std::endl;
            return game;
        }

        if (!jv.is_object()) return game;

        const auto& obj = jv.as_object();
        
        LoadGlobalSettings(obj);

        // Безопасное извлечение глобальной конфигурации генератора лута
        if (obj.contains("lootGeneratorConfig")) {
            const auto& loot_config_obj = obj.at("lootGeneratorConfig").as_object();
            double period_sec = boost::json::value_to<double>(loot_config_obj.at("period"));
            double probability = boost::json::value_to<double>(loot_config_obj.at("probability"));

            auto base_interval = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::duration<double>(period_sec)
            );
            
            // Передаем настройки генератора в модель игры
            game.SetLootGeneratorConfig(base_interval, probability);
        }

        if (!obj.contains("maps")) return game;

        for (const auto& map_val : obj.at("maps").as_array()) {
            if (!map_val.is_object()) continue;
            const auto& map_obj = map_val.as_object();

            std::string id = boost::json::value_to<std::string>(map_obj.at("id"));
            std::string name = boost::json::value_to<std::string>(map_obj.at("name"));

            model::Map map(model::Map::Id{id}, name);
            
            if (map_obj.contains("lootTypes")) {
                const auto& loot_types = map_obj.at("lootTypes").as_array();
                map.SetLootTypesCount(loot_types.size());
                
                game.GetExtraDataManager().AddLootTypes(map.GetId(), loot_types);
            }
            
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

            game.AddMap(std::move(map));
        }

    } catch (const std::exception& e) {
        std::cerr << "Error loading game config: " << e.what() << std::endl;
    }

    return game;
}

} // namespace json_loader