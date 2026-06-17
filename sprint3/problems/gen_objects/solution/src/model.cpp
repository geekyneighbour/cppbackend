#include "model.h"
#include <random>
#include <stdexcept>
#include <boost/json.hpp>

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

// ================= DOG =================
void Dog::UpdatePosition(double time_delta) {
    pos_.x += speed_.vx * time_delta;
    pos_.y += speed_.vy * time_delta;
}

// ================= GAME SESSION =================
Dog* GameSession::AddDog(const std::string& dog_name, bool randomize_spawn) {
    auto dog = std::make_shared<Dog>(dog_name, next_dog_id_++);
    if (randomize_spawn && !map_->GetRoads().empty()) {
        static std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, map_->GetRoads().size() - 1);
        size_t road_idx = dist(gen);
        dog->SetPosition(GetRandomPointOnRoad(map_->GetRoads()[road_idx]));
    } else if (!map_->GetRoads().empty()) {
        Point start = map_->GetRoads()[0].GetStart();
        dog->SetPosition({static_cast<double>(start.x), static_cast<double>(start.y)});
    }
    dogs_.push_back(dog);
    return dog.get();
}

void GameSession::Update(double time_delta) {
    for (auto& dog : dogs_) {
        dog->UpdatePosition(time_delta);
    }

    if (map_->GetLootTypesCount() == 0) {
        return;
    }

    std::chrono::milliseconds delta_ms(static_cast<long long>(time_delta * 1000.0));
    unsigned loot_to_generate = loot_generator_.Generate(delta_ms, lost_objects_.size(), dogs_.size());

    if (loot_to_generate > 0 && !map_->GetRoads().empty()) {
        static std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> road_dist(0, map_->GetRoads().size() - 1);
        std::uniform_int_distribution<int> type_dist(0, static_cast<int>(map_->GetLootTypesCount() - 1));

        for (unsigned i = 0; i < loot_to_generate; ++i) {
            size_t road_idx = road_dist(gen);
            int loot_type = type_dist(gen);
            PointDouble loot_pos = GetRandomPointOnRoad(map_->GetRoads()[road_idx]);

            uint32_t loot_id = next_loot_id_++;
            lost_objects_[loot_id] = LostObject{loot_id, loot_type, loot_pos};
        }
    }
}

// ================= GAME =================
std::shared_ptr<GameSession> Game::GetOrCreateSession(const Map* map) {
    if (!sessions_.contains(map)) {
        std::chrono::milliseconds base_interval(static_cast<long long>(loot_period_ * 1000.0));
        loot_gen::LootGenerator loot_gen(base_interval, loot_probability_, []() {
            static std::mt19937 d_gen(std::random_device{}());
            std::uniform_real_distribution<double> d_dist(0.0, 1.0);
            return d_dist(d_gen);
        });
        sessions_[map] = std::make_shared<GameSession>(map, std::move(loot_gen));
    }
    return sessions_[map];
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

void Game::AddMap(Map map) {
    size_t index = maps_.size();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Duplicate map id");
    }
    maps_.emplace_back(std::make_unique<Map>(std::move(map)));
}

void Game::UpdateAllSessions(double time_delta) {
    for (auto& [map, session] : sessions_) {
        session->Update(time_delta);
    }
}

// ================= TAG_INVOKE =================
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