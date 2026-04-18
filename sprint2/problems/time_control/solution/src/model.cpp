#include "model.h"
#include <random>
#include <stdexcept>

namespace model {

PointDouble GetRandomPointOnRoad(const Road& road) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    
    Point start = road.GetStart();
    Point end = road.GetEnd();
    
    if (road.IsHorizontal()) {
        double min_x = std::min(start.x, end.x);
        double max_x = std::max(start.x, end.x);
        double x = min_x + dist(gen) * (max_x - min_x);
        return {x, static_cast<double>(start.y)};
    } else { // Vertical
        double min_y = std::min(start.y, end.y);
        double max_y = std::max(start.y, end.y);
        double y = min_y + dist(gen) * (max_y - min_y);
        return {static_cast<double>(start.x), y};
    }
}

void Map::AddOffice(Office office) {
    if (warehouse_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate warehouse");
    }

    const size_t index = offices_.size();
    Office& o = offices_.emplace_back(std::move(office));
    try {
        warehouse_id_to_index_.emplace(o.GetId(), index);
    } catch (...) {
        offices_.pop_back();
        throw;
    }
}

void Game::AddMap(Map map) {
    const size_t index = maps_.size();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Map with id "s + *map.GetId() + " already exists"s);
    } else {
        try {
            maps_.emplace_back(std::make_unique<Map>(std::move(map)));
        } catch (...) {
            map_id_to_index_.erase(it);
            throw;
        }
    }
}

Dog& GameSession::AddDog(const std::string& name) {
    dogs_.push_back(std::make_unique<Dog>(name));
    Dog& dog = *dogs_.back();
    
    if (map_ && !map_->GetRoads().empty()) {
        const Road& first_road = map_->GetRoads()[0];
        Point start = first_road.GetStart();
        if (first_road.IsHorizontal()) {
            dog.SetPos(static_cast<double>(start.x), static_cast<double>(start.y));
        } else {
            dog.SetPos(static_cast<double>(start.x), static_cast<double>(start.y));
        }
    }
    
    return dog;
}

Player& GameSession::AddPlayer(Dog& dog) {
    uint64_t id = ++next_player_id_;
    auto [it, ok] = players_.emplace(
        id, Player{ id, &dog, this }
    );
    return it->second;
}

std::vector<Player*> GameSession::GetPlayers() {
    std::vector<Player*> res;
    for (auto& [_, p] : players_) {
        res.push_back(&p);
    }
    return res;
}

void GameSession::UpdateState(double time_delta) {
    if (!map_) return;
    
    const auto& roads = map_->GetRoads();
    for (auto& dog_ptr : dogs_) {
        dog_ptr->UpdatePosition(time_delta, roads);
    }
}

GameSession& Game::FindOrCreateSession(const Map* map) {
    auto& ptr = sessions_[map];
    if (!ptr) {
        ptr = std::make_unique<GameSession>(map);
    }
    return *ptr;
}

const Map* Game::FindMap(const Map::Id& id) const {
    for (auto& m : maps_) {
        if (*m->GetId() == *id) return m.get();
    }
    return nullptr;
}

const Game::Maps& Game::GetMaps() const noexcept {
    return maps_;
}

void Game::UpdateAllSessions(double time_delta) {
    for (auto& [map, session] : sessions_) {
        if (session) {
            session->UpdateState(time_delta);
        }
    }
}

void Dog::UpdatePosition(double time_delta, const std::vector<model::Road>& roads) {
    if (speed_.vx == 0.0 && speed_.vy == 0.0) return;
    
    double new_x = pos_.x + speed_.vx * time_delta;
    double new_y = pos_.y + speed_.vy * time_delta;
    
    // Find which road the dog is currently on
    const Road* current_road = nullptr;
    for (const auto& road : roads) {
        if (road.IsPointOnRoad(pos_.x, pos_.y)) {
            current_road = &road;
            break;
        }
    }
    
    if (!current_road) {
        // If not on any road, try to find the nearest road
        for (const auto& road : roads) {
            if (road.IsPointOnRoad(new_x, new_y)) {
                current_road = &road;
                break;
            }
        }
        if (!current_road) {
            pos_.x = new_x;
            pos_.y = new_y;
            return;
        }
    }
    
    const double half_width = 0.4;
    
    if (current_road->IsHorizontal()) {
        double min_x = current_road->GetMinX() - half_width;
        double max_x = current_road->GetMaxX() + half_width;
        
        // Calculate the maximum possible movement before hitting the boundary
        if (new_x < min_x) {
            // Would go beyond left boundary
            if (pos_.x > min_x) {
                // Try to move only to the boundary
                double max_move = min_x - pos_.x;
                if (std::abs(max_move) < std::abs(speed_.vx * time_delta)) {
                    new_x = min_x;
                    speed_.vx = 0.0;
                }
            } else {
                new_x = min_x;
                speed_.vx = 0.0;
            }
        } else if (new_x > max_x) {
            // Would go beyond right boundary
            if (pos_.x < max_x) {
                double max_move = max_x - pos_.x;
                if (std::abs(max_move) < std::abs(speed_.vx * time_delta)) {
                    new_x = max_x;
                    speed_.vx = 0.0;
                }
            } else {
                new_x = max_x;
                speed_.vx = 0.0;
            }
        }
        
        new_y = current_road->GetStart().y;
    } else { // Vertical road
        double min_y = current_road->GetMinY() - half_width;
        double max_y = current_road->GetMaxY() + half_width;
        
        // Calculate the maximum possible movement before hitting the boundary
        if (new_y < min_y) {
            // Would go beyond top boundary
            if (pos_.y > min_y) {
                double max_move = min_y - pos_.y;
                if (std::abs(max_move) < std::abs(speed_.vy * time_delta)) {
                    new_y = min_y;
                    speed_.vy = 0.0;
                }
            } else {
                new_y = min_y;
                speed_.vy = 0.0;
            }
        } else if (new_y > max_y) {
            // Would go beyond bottom boundary
            if (pos_.y < max_y) {
                double max_move = max_y - pos_.y;
                if (std::abs(max_move) < std::abs(speed_.vy * time_delta)) {
                    new_y = max_y;
                    speed_.vy = 0.0;
                }
            } else {
                new_y = max_y;
                speed_.vy = 0.0;
            }
        }
        
        new_x = current_road->GetStart().x;
    }
    
    pos_.x = new_x;
    pos_.y = new_y;
}

void Dog::SetAction(const std::string& action, double speed) {
    if (action.empty()) {
        speed_ = {0.0, 0.0};
        return;
    }
    
    if (action == "L") {
        speed_ = {-speed, 0.0};
        dir_ = Direction::WEST;
    } else if (action == "R") {
        speed_ = {speed, 0.0};
        dir_ = Direction::EAST;
    } else if (action == "U") {
        speed_ = {0.0, -speed};
        dir_ = Direction::NORTH;
    } else if (action == "D") {
        speed_ = {0.0, speed};
        dir_ = Direction::SOUTH;
    }
}

}  // namespace model