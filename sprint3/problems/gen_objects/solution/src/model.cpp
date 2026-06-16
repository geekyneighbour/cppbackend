#include "model.h"
#include <random>
#include <stdexcept>
#include <boost/json.hpp>
#include <cmath>

namespace model {

// ================= RANDOM POINT =================
PointDouble GetRandomPointOnRoad(const Road& road) {
    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    Point start = road.GetStart();
    Point end = road.GetEnd();

    if (road.IsHorizontal()) {
        double min_x = std::min(start.x, end.x);
        double max_x = std::max(start.x, end.x);
        return {min_x + dist(gen) * (max_x - min_x), static_cast<double>(start.y)};
    } 
    double min_y = std::min(start.y, end.y);
    double max_y = std::max(start.y, end.y);
    return {(double)start.x, min_y + dist(gen) * (max_y - min_y)};
}

// ================= MAP =================
void Map::AddOffice(Office office) {
    if (warehouse_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate warehouse");
    }

    size_t index = offices_.size();
    Office& o = offices_.emplace_back(std::move(office));
    warehouse_id_to_index_[o.GetId()] = index;
}

// ================= GAME SESSION =================
void GameSession::Update(std::chrono::milliseconds time_delta) {
    unsigned looter_count = static_cast<unsigned>(players_.size());
    unsigned loot_count = static_cast<unsigned>(lost_objects_.size());
    
    unsigned count_to_generate = loot_gen_.Generate(time_delta, loot_count, looter_count);
    
    if (count_to_generate > 0 && !map_->GetRoads().empty() && map_->GetLootTypesCount() > 0) {
        static std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> road_dist(0, map_->GetRoads().size() - 1);
        std::uniform_int_distribution<uint32_t> type_dist(0, map_->GetLootTypesCount() - 1);
        
        for (unsigned i = 0; i < count_to_generate; ++i) {
            const auto& roads = map_->GetRoads();
            const auto& random_road = roads[road_dist(gen)];
            
            PointDouble spawn_pos = GetRandomPointOnRoad(random_road);
            uint32_t loot_type = type_dist(gen);
            
            uint32_t id = next_loot_id_++;
            lost_objects_[id] = LostObject{id, loot_type, spawn_pos};
        }
    }
}

// ================= GAME =================
GameSession* Game::FindOrCreateSession(const Map* map) {
    if (!sessions_.contains(map)) {
        sessions_[map] = std::make_unique<GameSession>(map, loot_period_, loot_probability_);
        sessions_[map]->Update(std::chrono::milliseconds(100));
    }
    return sessions_.at(map).get();
}

const Map* Game::FindMap(const Map::Id& id) const {
    if (auto it = map_id_to_index_.find(id); it != map_id_to_index_.end()) {
        return maps_.at(it->second).get();
    }
    return nullptr;
}

const Game::Maps& Game::GetMaps() const noexcept {
    return maps_;
}

Map& Game::AddMap(Map map) {
    const size_t index = maps_.size();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Duplicate map");
    }
    maps_.push_back(std::make_unique<Map>(std::move(map)));
    return *maps_.back();
}


void Game::UpdateAllSessions(double time_delta_seconds) {
    std::chrono::milliseconds delta_ms{
        static_cast<int64_t>(std::round(time_delta_seconds * 1000.0))
    };
    
    for (auto& [map_ptr, session] : sessions_) {
        session->Update(delta_ms);
    }
}

// ================= JSON TAG INVOKE =================
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Road& road) {
    boost::json::object obj;
    obj["x0"] = road.GetStart().x;
    obj["y0"] = road.GetStart().y;
    if (road.IsHorizontal()) {
        obj["x1"] = road.GetEnd().x;
    } else {
        obj["y1"] = road.GetEnd().y;
    }
    jv = std::move(obj);
}

void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Building& building) {
    const auto& bounds = building.GetBounds();
    jv = boost::json::object{
        {"x", bounds.position.x},
        {"y", bounds.position.y},
        {"w", bounds.size.width},
        {"h", bounds.size.height}
    };
}

void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Office& office) {
    jv = boost::json::object{
        {"id", *office.GetId()},
        {"x", office.GetPosition().x},
        {"y", office.GetPosition().y},
        {"offsetX", office.GetOffset().dx},
        {"offsetY", office.GetOffset().dy}
    };
}

void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Map& map) {
    boost::json::object obj;
    obj["id"] = *map.GetId();
    obj["name"] = map.GetName();
    obj["roads"] = boost::json::value_from(map.GetRoads());
    obj["buildings"] = boost::json::value_from(map.GetBuildings());
    obj["offices"] = boost::json::value_from(map.GetOffices());
    jv = std::move(obj);
}

} // namespace model