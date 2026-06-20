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

    try {
        warehouse_id_to_index_.emplace(o.GetId(), index);
    } catch (...) {
        offices_.pop_back();
        throw;
    }
}

// ================= GAME =================
void Game::AddMap(Map map) {
    size_t index = maps_.size();

    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Map already exists");
    }

    maps_.push_back(std::make_unique<Map>(std::move(map)));
}

// ================= SESSION =================
Dog& GameSession::AddDog(std::string_view name, bool randomize) {
    const auto& roads = map_->GetRoads();
    if (roads.empty()) {
        throw std::runtime_error("Map has no roads");
    }

    PointDouble spawn_pos;

    if (randomize) {

        std::uniform_int_distribution<size_t> dist(0, roads.size() - 1);
        const auto& road = roads[dist(random_gen_)];
        spawn_pos = GetRandomPointOnRoad(road);
    } else {

        const auto& first_road = roads[0];
        spawn_pos = {
            static_cast<double>(first_road.GetStart().x),
            static_cast<double>(first_road.GetStart().y)
        };
    }


    auto new_dog = std::make_unique<Dog>(std::string(name));
    new_dog->SetPos(spawn_pos.x, spawn_pos.y);
    
    dogs_.push_back(std::move(new_dog));
    return *dogs_.back();
}

Player& GameSession::AddPlayer(Dog& dog) {
    uint64_t id = ++next_player_id_;
    auto [it, ok] = players_.emplace(id, Player{id, &dog, this});
    return it->second;
}

std::vector<Player*> GameSession::GetPlayers() {
    std::vector<Player*> res;
    for (auto& [_, p] : players_) {
        res.push_back(&p);
    }
    return res;
}

void GameSession::UpdateState(double dt) {
    if (!map_) return;
    

    for (auto& dog : dogs_) {
        dog->UpdatePosition(dt, map_->GetRoads());
    }
    

    auto ms_dt = std::chrono::milliseconds(static_cast<long long>(dt * 1000));
    auto& config = map_->GetLootConfig();
    

    static bool generator_initialized = false;
    if (!generator_initialized) {
        loot_generator_ = loot_gen::LootGenerator(
            std::chrono::milliseconds(static_cast<long long>(config.period * 1000)),
            config.probability
        );
        generator_initialized = true;
    }
    
    unsigned new_loot_count = loot_generator_.Generate(
        ms_dt,
        lost_objects_.size(),
        dogs_.size()
    );
    
    for (unsigned i = 0; i < new_loot_count; ++i) {
        PointDouble pos = map_->GetRandomPointOnRoad();
        size_t type = map_->GetRandomLootType();
        lost_objects_.push_back(LostObject{type, pos});
    }
}

// ================= GAME =================
GameSession& Game::FindOrCreateSession(const Map* map) {
    auto& ptr = sessions_[map];
    if (!ptr) {
        ptr = std::make_unique<GameSession>(map);
    }
    return *ptr;
}

const Map* Game::FindMap(const Map::Id& id) const {
    auto it = map_id_to_index_.find(id);
	return (it == map_id_to_index_.end() ? nullptr : maps_[it->second].get());
}

const Game::Maps& Game::GetMaps() const noexcept {
    return maps_;
}

void Game::UpdateAllSessions(double dt) {
    for (auto& [_, session] : sessions_) {
        session->UpdateState(dt);
    }
}

// ================= DOG =================
void Dog::UpdatePosition(double dt, const std::vector<Road>& roads) {
    if (speed_.vx == 0.0 && speed_.vy == 0.0) return;

    double new_x = pos_.x + speed_.vx * dt;
    double new_y = pos_.y + speed_.vy * dt;


    double min_x = pos_.x, max_x = pos_.x;
    double min_y = pos_.y, max_y = pos_.y;

    bool found_road = false;

    for (const auto& road : roads) {

        double road_min_x = std::min(road.GetStart().x, road.GetEnd().x) - 0.4;
        double road_max_x = std::max(road.GetStart().x, road.GetEnd().x) + 0.4;
        double road_min_y = std::min(road.GetStart().y, road.GetEnd().y) - 0.4;
        double road_max_y = std::max(road.GetStart().y, road.GetEnd().y) + 0.4;

        if (pos_.x >= road_min_x && pos_.x <= road_max_x &&
            pos_.y >= road_min_y && pos_.y <= road_max_y) {
            
            if (!found_road) {

                min_x = road_min_x; max_x = road_max_x;
                min_y = road_min_y; max_y = road_max_y;
                found_road = true;
            } else {

                min_x = std::min(min_x, road_min_x);
                max_x = std::max(max_x, road_max_x);
                min_y = std::min(min_y, road_min_y);
                max_y = std::max(max_y, road_max_y);
            }
        }
    }


    if (!found_road) return;


    if (new_x < min_x) {
        new_x = min_x;
        speed_.vx = 0.0;
    } else if (new_x > max_x) {
        new_x = max_x;
        speed_.vx = 0.0;
    }

    if (new_y < min_y) {
        new_y = min_y;
        speed_.vy = 0.0;
    } else if (new_y > max_y) {
        new_y = max_y;
        speed_.vy = 0.0;
    }

    pos_ = {new_x, new_y};
}

// ================= ACTION =================
void Dog::SetAction(const std::string& action, double speed) {
    if (action.empty()) {
        speed_ = {0, 0};
        return;
    }

    if (action == "L") {
        speed_ = {-speed, 0};
        dir_ = Direction::WEST;
    } else if (action == "R") {
        speed_ = {speed, 0};
        dir_ = Direction::EAST;
    } else if (action == "U") {
        speed_ = {0, -speed};
        dir_ = Direction::NORTH;
    } else if (action == "D") {
        speed_ = {0, speed};
        dir_ = Direction::SOUTH;
    }
}

// Функции tag_invoke теперь корректно находятся внутри namespace model
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
    jv = boost::json::object{
        {"id", *map.GetId()},
        {"name", map.GetName()},
        {"roads", boost::json::value_from(map.GetRoads())},
        {"buildings", boost::json::value_from(map.GetBuildings())},
        {"offices", boost::json::value_from(map.GetOffices())}
    };
}

PointDouble Map::GetRandomPointOnRoad() const {
    if (roads_.empty()) return {0, 0};
    
    std::uniform_int_distribution<size_t> road_dist(0, roads_.size() - 1);
    const auto& road = roads_[road_dist(rng_)];
    
    std::uniform_real_distribution<double> pos_dist(0.0, 1.0);
    double t = pos_dist(rng_);
    
    Point start = road.GetStart();
    Point end = road.GetEnd();
    
    if (road.IsHorizontal()) {
        double x = start.x + t * (end.x - start.x);
        return {x, static_cast<double>(start.y)};
    } else {
        double y = start.y + t * (end.y - start.y);
        return {static_cast<double>(start.x), y};
    }
}

size_t Map::GetRandomLootType() const {
    if (loot_types_count_ == 0) return 0;
    std::uniform_int_distribution<size_t> type_dist(0, loot_types_count_ - 1);
    return type_dist(rng_);
}


} // namespace model