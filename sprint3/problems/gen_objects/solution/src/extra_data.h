#pragma once
#include <unordered_map>
#include <boost/json.hpp>
#include "model.h"

namespace infra {

class ExtraData {
public:
    void SaveLootTypes(const model::Map::Id& map_id, boost::json::array loot_types) {
        loot_types_[map_id] = std::move(loot_types);
    }

    const boost::json::array& GetLootTypes(const model::Map::Id& map_id) const {
        static const boost::json::array empty;
        if (auto it = loot_types_.find(map_id); it != loot_types_.end()) {
            return it->second;
        }
        return empty;
    }

private:
    std::unordered_map<model::Map::Id, boost::json::array, util::TaggedHasher<model::Map::Id>> loot_types_;
};

} // namespace infra