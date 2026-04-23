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
        const Road& first_road = map_->GetRoads()[0];
        Point start = first_road.GetStart();
        
        double x = static_cast<double>(start.x);
        double y = static_cast<double>(start.y);
        
        if (first_road.IsVertical()) {
            x += 0.4;  
        } else if (first_road.IsHorizontal()) {
            y += 0.4;  
        }
        
        dog.SetPos(x, y);
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
void Player::UpdatePosition(double delta_time, const Map& map) {
    if (speed_.x == 0 && speed_.y == 0) {
        return;
    }

    const Road* current_road = map.FindRoadByPosition(pos_.x, pos_.y);
    if (!current_road) {
        return;
    }

    const auto& start = current_road->GetStart();
    const auto& end = current_road->GetEnd();

    // Границы дороги с учетом ширины (±0.4)
    double min_x = std::min(start.x, end.x) - 0.4;
    double max_x = std::max(start.x, end.x) + 0.4;
    double min_y = std::min(start.y, end.y) - 0.4;
    double max_y = std::max(start.y, end.y) + 0.4;

    double delta_x = speed_.x * delta_time;
    double delta_y = speed_.y * delta_time;

    switch (dir_) {
        case Direction::RIGHT: {
            double new_x = pos_.x + delta_x;
            if (new_x <= max_x) {
                pos_.x = new_x;
            } else {
                pos_.x = max_x;
                Stop();
            }
            break;
        }
        case Direction::LEFT: {
            double new_x = pos_.x + delta_x;
            if (new_x >= min_x) {
                pos_.x = new_x;
            } else {
                pos_.x = min_x;
                Stop();
            }
            break;
        }
        case Direction::DOWN: {
            double new_y = pos_.y + delta_y;
            if (new_y <= max_y) {
                pos_.y = new_y;
            } else {
                pos_.y = max_y;
                Stop();
            }
            break;
        }
        case Direction::UP: {
            double new_y = pos_.y + delta_y;
            if (new_y >= min_y) {
                pos_.y = new_y;
            } else {
                pos_.y = min_y;
                Stop();
            }
            break;
        }
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