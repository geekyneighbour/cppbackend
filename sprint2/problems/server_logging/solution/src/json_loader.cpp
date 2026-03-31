#include "json_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <boost/json.hpp>

namespace json_loader {

    void AddRoads(const boost::json::array& roads_array, model::Map& map) {
        for (const auto& road_val : roads_array) {
            boost::json::object road_obj = road_val.as_object();

            int x0 = static_cast<int>(road_obj.at("x0").as_int64());
            int y0 = static_cast<int>(road_obj.at("y0").as_int64());

            if (road_obj.contains("x1")) {
                int x1 = static_cast<int>(road_obj.at("x1").as_int64());
                map.AddRoad(model::Road(model::Road::HORIZONTAL,
                    model::Point{ x0, y0 },
                    x1));
            }
            else if (road_obj.contains("y1")) {

                int y1 = static_cast<int>(road_obj.at("y1").as_int64());
                map.AddRoad(model::Road(model::Road::VERTICAL,
                    model::Point{ x0, y0 },
                    y1));
            }
        }
 }

    void AddBuildings(const boost::json::array& buildings_array, model::Map& map) {
        for (const auto& building_val : buildings_array) {
            boost::json::object building_obj = building_val.as_object();

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
            boost::json::object office_obj = office_val.as_object();

            std::string office_id = boost::json::value_to<std::string>(office_obj.at("id"));
            int x = static_cast<int>(office_obj.at("x").as_int64());
            int y = static_cast<int>(office_obj.at("y").as_int64());
            int offsetX = static_cast<int>(office_obj.at("offsetX").as_int64());
            int offsetY = static_cast<int>(office_obj.at("offsetY").as_int64());

            map.AddOffice(model::Office(
                model::Office::Id(office_id),
                model::Point{ x, y },
                model::Offset{ offsetX, offsetY }
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
        }

        catch (const std::exception& e) {
            std::cerr << "Error parsing json: " << e.what() << std::endl;
			return game;
        }
        
		if (!jv.is_object()) {
    std::cerr << "JSON root is not an object" << std::endl;
    return game;
}
boost::json::object obj = jv.as_object();
        boost::json::object obj = jv.as_object();
        
        if (!obj.contains("maps")) {
            return game;
        }
        
        boost::json::array maps_array = obj["maps"].as_array();
        
        for (const auto& map_val : maps_array) {
            boost::json::object map_obj = map_val.as_object();
            
            std::string id = boost::json::value_to<std::string>(map_obj.at("id"));
            std::string name = boost::json::value_to<std::string>(map_obj.at("name"));
            
            model::Map map(model::Map::Id(id), name);
            
            auto roads = map_obj.if_contains("roads");
            auto buildings = map_obj.if_contains("buildings");
            auto offices = map_obj.if_contains("offices");
            
            if (roads) {
                AddRoads(roads->as_array(), map);
            }
            
            if (buildings) {
                AddBuildings(buildings->as_array(), map);
            }
            
            if (offices) {
                AddOffices(offices->as_array(), map);
            }
            
            game.AddMap(std::move(map));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading game config: " << e.what() << std::endl;
    }
    
    return game;
}

}  // namespace json_loader