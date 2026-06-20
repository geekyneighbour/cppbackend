#pragma once
#include <boost/json.hpp>
#include <unordered_map>
#include <string>

class MapLootTypes {
public:
    static MapLootTypes& Instance() {
        static MapLootTypes instance;
        return instance;
    }
    
    void SetLootTypes(const std::string& map_id, const boost::json::array& types) {
        loot_types_[map_id] = types;
    }
    
    const boost::json::array* GetLootTypes(const std::string& map_id) const {
        auto it = loot_types_.find(map_id);
        return it != loot_types_.end() ? &it->second : nullptr;
    }
    
private:
    std::unordered_map<std::string, boost::json::array> loot_types_;
};