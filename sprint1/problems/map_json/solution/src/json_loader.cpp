#include "json_loader.h"
#include <fstream>
#include <sstream>
#include <boost/json.hpp>
#include <iostream>

namespace json_loader {

model::Game LoadGame(const std::filesystem::path& json_path) {
    model::Game game;
    
    try {
        // Читаем файл
        std::ifstream file(json_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + json_path.string());
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json_str = buffer.str();
        
        // Парсим JSON
        boost::json::value jv = boost::json::parse(json_str);
        boost::json::object obj = jv.as_object();
        
        // Получаем массив карт
        if (!obj.contains("maps")) {
            return game;
        }
        
        boost::json::array maps_array = obj["maps"].as_array();
        
        for (const auto& map_val : maps_array) {
            boost::json::object map_obj = map_val.as_object();
            
            // Получаем id и name карты
            std::string id = boost::json::value_to<std::string>(map_obj.at("id"));
            std::string name = boost::json::value_to<std::string>(map_obj.at("name"));
            
            model::Map map(model::Map::Id(id), name);
            
            // Загружаем дороги
            if (map_obj.contains("roads")) {
                boost::json::array roads_array = map_obj.at("roads").as_array();
                for (const auto& road_val : roads_array) {
                    boost::json::object road_obj = road_val.as_object();
                    
                    // Явное преобразование int64_t -> int
                    int x0 = static_cast<int>(road_obj.at("x0").as_int64());
                    int y0 = static_cast<int>(road_obj.at("y0").as_int64());
                    
                    if (road_obj.contains("x1")) {
                        // Горизонтальная дорога
                        int x1 = static_cast<int>(road_obj.at("x1").as_int64());
                        map.AddRoad(model::Road(model::Road::HORIZONTAL, 
                                                model::Point{x0, y0}, 
                                                x1));
                    } else if (road_obj.contains("y1")) {
                        // Вертикальная дорога
                        int y1 = static_cast<int>(road_obj.at("y1").as_int64());
                        map.AddRoad(model::Road(model::Road::VERTICAL, 
                                                model::Point{x0, y0}, 
                                                y1));
                    }
                }
            }
            
            // Загружаем здания
            if (map_obj.contains("buildings")) {
                boost::json::array buildings_array = map_obj.at("buildings").as_array();
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
            
            // Загружаем офисы
            if (map_obj.contains("offices")) {
                boost::json::array offices_array = map_obj.at("offices").as_array();
                for (const auto& office_val : offices_array) {
                    boost::json::object office_obj = office_val.as_object();
                    
                    std::string office_id = boost::json::value_to<std::string>(office_obj.at("id"));
                    int x = static_cast<int>(office_obj.at("x").as_int64());
                    int y = static_cast<int>(office_obj.at("y").as_int64());
                    int offsetX = static_cast<int>(office_obj.at("offsetX").as_int64());
                    int offsetY = static_cast<int>(office_obj.at("offsetY").as_int64());
                    
                    map.AddOffice(model::Office(
                        model::Office::Id(office_id),
                        model::Point{x, y},
                        model::Offset{offsetX, offsetY}
                    ));
                }
            }
            
            game.AddMap(std::move(map));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading game config: " << e.what() << std::endl;
    }
    
    return game;
}

}  // namespace json_loader