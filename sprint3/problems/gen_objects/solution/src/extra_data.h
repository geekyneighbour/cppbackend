#pragma once
#include <string>
#include <unordered_map>
#include <boost/json.hpp>
#include "model.h"

namespace extra_data {

class ExtraDataManager {
public:
    void AddLootTypes(const model::Map::Id& map_id, boost::json::array loot_types) {
        map_loot_types_[map_id] = std::move(loot_types);
    }

    const boost::json::array* FindLootTypes(const model::Map::Id& map_id) const {
        auto it = map_loot_types_.find(map_id);
        if (it != map_loot_types_.end()) {
            return &it->second;
        }
        return nullptr;
    }

private:
    std::unordered_map<model::Map::Id, boost::json::array, util::TaggedHasher<model::Map::Id>> map_loot_types_;
};

} // namespace extra_data