#pragma once

#include <filesystem>
#include <boost/json.hpp>
#include <unordered_map>
#include <string>

#include "model.h"

namespace json_loader {

class ExtraDataStorage {
public:
    static ExtraDataStorage& GetInstance() {
        static ExtraDataStorage instance;
        return instance;
    }

    void SaveLootTypes(const std::string& map_id, boost::json::array loot_types) {
        storage_[map_id] = std::move(loot_types);
    }

    const boost::json::array* GetLootTypes(const std::string& map_id) const {
        auto it = storage_.find(map_id);
        if (it != storage_.end()) {
            return &it->second;
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string, boost::json::array> storage_;
    ExtraDataStorage() = default;
};

model::Game LoadGame(const std::filesystem::path& json_path);

void AddRoads(const boost::json::array& roads_array, model::Map& map);
void AddBuildings(const boost::json::array& buildings_array, model::Map& map);
void AddOffices(const boost::json::array& offices_array, model::Map& map);
void LoadGlobalSettings(const boost::json::object& root_obj);

}  // namespace json_loader