#include "model.h"
#include <stdexcept>
#include <algorithm>
#include <boost/json.hpp>

namespace model {

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
    return {static_cast<double>(start.x), min_y + dist(gen) * (max_y - min_y)};
}

// ================= MAP =================
void Map::AddOffice(Office office) {
    if (warehouse_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate warehouse");
    }

    size_t index = offices_.size();
    offices_.emplace_back(std::move(office));
    warehouse_id_to_index_[offices_[index].GetId()] = index;
}

// ================= GAME SESSION =================
void GameSession::Update(std::chrono::milliseconds time_delta, loot_gen::LootGenerator& loot_generator) {
    unsigned loot_count = 0;
    unsigned looter_count = players_.size();
    unsigned count_to_generate = loot_generator.Generate(time_delta, loot_count, looter_count);
}

// ================= GAME =================
void Game::AddMap(Map map) {
    const size_t index = maps_.size();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Duplicate map");
    }
    maps_.emplace_back(std::move(map));
}

GameSession* Game::CreateSession(const Map* map) {
    if (sessions_.contains(map)) {
        return sessions_[map].get();
    }
    auto session = std::make_unique<GameSession>();
    GameSession* ptr = session.get();
    sessions_[map] = std::move(session);
    return ptr;
}

void Game::UpdateAllSessions(double time_delta_seconds) {
    std::chrono::milliseconds delta(static_cast<long long>(time_delta_seconds * 1000.0));
    loot_gen::LootGenerator loot_gen(loot_period_, loot_probability_);
    
    for (auto& [map, session] : sessions_) {
        session->Update(delta, loot_gen);
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

} // namespace model