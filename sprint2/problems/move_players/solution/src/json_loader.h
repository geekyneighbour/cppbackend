#pragma once

#include <filesystem>
#include <boost/json.hpp>

#include "model.h"

namespace json_loader {

model::Game LoadGame(const std::filesystem::path& json_path);

void AddRoads(const boost::json::array& roads_array, model::Map& map);

void AddBuildings(const boost::json::array& buildings_array, model::Map& map);

void AddOffices(const boost::json::array& offices_array, model::Map& map);

void LoadGlobalSettings(const boost::json::object& root_obj);

}  // namespace json_loader
