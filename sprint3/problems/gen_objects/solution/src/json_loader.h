// json_loader.h - обновленная версия
#pragma once

#include <filesystem>
#include <boost/json.hpp>

#include "model.h"
#include "loot_generator.h"

namespace json_loader {

struct LootGeneratorConfig {
    std::chrono::milliseconds period;
    double probability;
};

struct LootTypesStorage {
    std::unordered_map<model::Map::Id, boost::json::array, util::TaggedHasher<model::Map::Id>> map_loot_types;
};

model::Game LoadGame(const std::filesystem::path& json_path);
LootGeneratorConfig LoadLootGeneratorConfig(const std::filesystem::path& json_path);
LootTypesStorage LoadLootTypes(const std::filesystem::path& json_path);

void AddRoads(const boost::json::array& roads_array, model::Map& map);
void AddBuildings(const boost::json::array& buildings_array, model::Map& map);
void AddOffices(const boost::json::array& offices_array, model::Map& map);
void LoadGlobalSettings(const boost::json::object& root_obj);

}  // namespace json_loader