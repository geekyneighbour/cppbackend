#pragma once
#include <string>
#include <unordered_map>
#include <boost/json.hpp>

namespace extra_data {

class ExtraDataManager {
public:
    void AddLootTypes(const std::string& map_id_str, boost::json::array loot_types) {
        map_loot_types_[map_id_str] = std::move(loot_types);
    }

    const boost::json::array* FindLootTypes(const std::string& map_id_str) const {
        auto it = map_loot_types_.find(map_id_str);
        if (it != map_loot_types_.end()) {
            return &it->second;
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string, boost::json::array> map_loot_types_;
};

} // namespace extra_data