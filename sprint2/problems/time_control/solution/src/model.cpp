#include "model.h"
#include <random>
#include <stdexcept>

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
        return {min_x + dist(gen) * (max_x - min_x), (double)start.y};
    } else {
        double min_y = std::min(start.y, end.y);
        double max_y = std::max(start.y, end.y);
        return {(double)start.x, min_y + dist(gen) * (max_y - min_y)};
    }
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
Dog& GameSession::AddDog(const std::string& name) {
    dogs_.push_back(std::make_unique<Dog>(name));
    Dog& dog = *dogs_.back();

    if (map_ && !map_->GetRoads().empty()) {
        static std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, map_->GetRoads().size() - 1);

        const Road& road = map_->GetRoads()[dist(gen)];
        dog.SetPos(GetRandomPointOnRoad(road));
    }

    return dog;
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
    if (it == map_id_to_index_.end()) return nullptr;
    return maps_[it->second].get();
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

    // Найти границы карты
    double min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9;
    for (const auto& road : roads) {
        min_x = std::min(min_x, road.GetMinX());
        max_x = std::max(max_x, road.GetMaxX());
        min_y = std::min(min_y, road.GetMinY());
        max_y = std::max(max_y, road.GetMaxY());
    }
    
    // Расширяем границы с учетом ширины дороги
    const double road_width = 1.0;
    double half_width = road_width / 2.0;
    min_x -= half_width;
    max_x += half_width;
    min_y -= half_width;
    max_y += half_width;
    
    // Проверяем, находится ли новая позиция в границах карты
    bool within_bounds = (new_x >= min_x && new_x <= max_x && 
                          new_y >= min_y && new_y <= max_y);
    
    // Дополнительно проверяем, на дороге ли собака
    bool on_road = false;
    for (const auto& road : roads) {
        if (road.IsPointOnRoad(new_x, new_y, road_width)) {
            on_road = true;
            break;
        }
    }
    
    if (on_road && within_bounds) {
        pos_ = {new_x, new_y};
    } else {
        // Останавливаем собаку
        speed_ = {0.0, 0.0};
        // Не обновляем позицию
    }
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

} // namespace model