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
std::shared_ptr<Dog> GameSession::CreateDog(const std::string& name, bool randomize_spawn) {
    PointDouble start_pos{0.0, 0.0};
    const auto& roads = map_->GetRoads();
    if (!roads.empty()) {
        if (randomize_spawn) {
            static std::mt19937 gen(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, roads.size() - 1);
            start_pos = GetRandomPointOnRoad(roads[dist(gen)]);
        } else {
            start_pos = {static_cast<double>(roads[0].GetStart().x), static_cast<double>(roads[0].GetStart().y)};
        }
    }
    
    auto dog = std::make_shared<Dog>(Dog::Id{next_dog_id_++}, name, start_pos);
    dogs_.push_back(dog);
    return dog;
}

void GameSession::MoveDogOnRoads(Dog& dog, double dt) {
    PointDouble pos = dog.GetPosition();
    Speed speed = dog.GetSpeed();
    
    if (speed.vx == 0.0 && speed.vy == 0.0) {
        return;
    }

    PointDouble next_pos = {pos.x + speed.vx * dt, pos.y + speed.vy * dt};
    const auto& roads = map_->GetRoads();
    
    double best_pos_x = pos.x;
    double best_pos_y = pos.y;
    bool is_stopped = true;
    constexpr double ROAD_OFFSET = 0.4;

    // Ищем дороги, на которых находится или куда переходит собака
    for (const auto& road : roads) {
        double min_x = std::min(road.GetStart().x, road.GetEnd().x) - ROAD_OFFSET;
        double max_x = std::max(road.GetStart().x, road.GetEnd().x) + ROAD_OFFSET;
        double min_y = std::min(road.GetStart().y, road.GetEnd().y) - ROAD_OFFSET;
        double max_y = std::max(road.GetStart().y, road.GetEnd().y) + ROAD_OFFSET;

        // Движение по горизонтали
        if (speed.vx != 0.0 && pos.y >= min_y && pos.y <= max_y) {
            if (speed.vx > 0.0 && next_pos.x <= max_x && next_pos.x >= min_x) {
                best_pos_x = std::max(best_pos_x, next_pos.x);
                is_stopped = false;
            } else if (speed.vx > 0.0 && pos.x <= max_x) {
                best_pos_x = std::max(best_pos_x, max_x);
            } else if (speed.vx < 0.0 && next_pos.x >= min_x && next_pos.x <= max_x) {
                if (is_stopped) best_pos_x = next_pos.x;
                else best_pos_x = std::min(best_pos_x, next_pos.x);
                is_stopped = false;
            } else if (speed.vx < 0.0 && pos.x >= min_x) {
                if (is_stopped) best_pos_x = min_x;
                else best_pos_x = std::min(best_pos_x, min_x);
            }
        }
        // Движение по вертикали
        else if (speed.vy != 0.0 && pos.x >= min_x && pos.x <= max_x) {
            if (speed.vy > 0.0 && next_pos.y <= max_y && next_pos.y >= min_y) {
                best_pos_y = std::max(best_pos_y, next_pos.y);
                is_stopped = false;
            } else if (speed.vy > 0.0 && pos.y <= max_y) {
                best_pos_y = std::max(best_pos_y, max_y);
            } else if (speed.vy < 0.0 && next_pos.y >= min_y && next_pos.y <= max_y) {
                if (is_stopped) best_pos_y = next_pos.y;
                else best_pos_y = std::min(best_pos_y, next_pos.y);
                is_stopped = false;
            } else if (speed.vy < 0.0 && pos.y >= min_y) {
                if (is_stopped) best_pos_y = min_y;
                else best_pos_y = std::min(best_pos_y, min_y);
            }
        }
    }

    if (speed.vx != 0.0) {
        dog.SetPosition({best_pos_x, pos.y});
        if (is_stopped) dog.SetSpeed({0.0, 0.0});
    } else if (speed.vy != 0.0) {
        dog.SetPosition({pos.x, best_pos_y});
        if (is_stopped) dog.SetSpeed({0.0, 0.0});
    }
}

void GameSession::Update(std::chrono::milliseconds time_delta, const loot_gen::LootGenerator& loot_generator) {
    double dt = time_delta.count() / 1000.0;
    
    // 1. Двигаем собак
    for (auto& dog : dogs_) {
        MoveDogOnRoads(*dog, dt);
    }

    // 2. Генерируем лут
    unsigned loot_count = lost_objects_.size();
    unsigned looter_count = dogs_.size();
    unsigned count_to_generate = loot_generator.Generate(time_delta, loot_count, looter_count);

    if (count_to_generate > 0 && map_->GetLootTypesCount() > 0 && !map_->GetRoads().empty()) {
        static std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> road_dist(0, map_->GetRoads().size() - 1);
        std::uniform_int_distribution<unsigned> type_dist(0, map_->GetLootTypesCount() - 1);

        for (unsigned i = 0; i < count_to_generate; ++i) {
            const auto& road = map_->GetRoads()[road_dist(gen)];
            PointDouble loot_pos = GetRandomPointOnRoad(road);
            unsigned id = next_object_id_++;
            lost_objects_[id] = LostObject{id, type_dist(gen), loot_pos};
        }
    }
}

// ================= GAME =================
void Game::AddMap(Map map) {
    size_t index = maps_.size();
    maps_.emplace_back(std::move(map));
    map_id_to_index_[maps_[index].GetId()] = index;
}

const Map* Game::FindMap(const Map::Id& id) const {
    auto it = map_id_to_index_.find(id);
    if (it != map_id_to_index_.end()) {
        return &maps_[it->second];
    }
    return nullptr;
}

GameSession* Game::FindOrCreateSession(const Map* map) {
    if (!map) return nullptr;
    auto it = sessions_.find(map);
    if (it != sessions_.end()) {
        return it->second.get();
    }
    auto session = std::make_unique<GameSession>(map);
    auto* ptr = session.get();
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

}  // namespace model