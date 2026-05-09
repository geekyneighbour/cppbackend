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
        Point end = first_road.GetEnd();
        
        double x, y;
        if (first_road.IsHorizontal()) {
            x = (start.x + end.x) / 2.0;
            y = start.y;
        } else {
            x = start.x; 
            y = (start.y + end.y) / 2.0;
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
        if (session) {
            session->UpdateState(dt);
        }
    }
}

// ================= DOG =================
void Dog::UpdatePosition(double dt, const std::vector<Road>& roads) {
    if (speed_.vx == 0.0 && speed_.vy == 0.0) return;

    double new_x = pos_.x + speed_.vx * dt;
    double new_y = pos_.y + speed_.vy * dt;
    
    const double ROAD_OFFSET = 0.4;
    const double EPS = 1e-9;
    
    const Road* horizontal_road = nullptr;
    const Road* vertical_road = nullptr;
    
    for (const auto& road : roads) {
        if (road.IsHorizontal()) {
            double road_y = road.GetStart().y;
            if (std::abs(pos_.y - road_y) <= ROAD_OFFSET + EPS) {
                double min_x = road.GetMinX() - ROAD_OFFSET;
                double max_x = road.GetMaxX() + ROAD_OFFSET;
                if (pos_.x >= min_x - EPS && pos_.x <= max_x + EPS) {
                    horizontal_road = &road;
                }
            }
        } else if (road.IsVertical()) {
            double road_x = road.GetStart().x;
            if (std::abs(pos_.x - road_x) <= ROAD_OFFSET + EPS) {
                double min_y = road.GetMinY() - ROAD_OFFSET;
                double max_y = road.GetMaxY() + ROAD_OFFSET;
                if (pos_.y >= min_y - EPS && pos_.y <= max_y + EPS) {
                    vertical_road = &road;
                }
            }
        }
    }
    
    if (!horizontal_road && !vertical_road) return;
    
    bool hit_border = false;
    
    if (speed_.vx != 0 && horizontal_road) {
        double border;
        if (speed_.vx > 0) {
            border = horizontal_road->GetMaxX() + ROAD_OFFSET;
            if (new_x >= border - EPS) {
                new_x = border;
                hit_border = true;
            } else {
                new_x = std::min(border, new_x);
            }
        } else {
            border = horizontal_road->GetMinX() - ROAD_OFFSET;
            if (new_x <= border + EPS) {
                new_x = border;
                hit_border = true;
            } else {
                new_x = std::max(border, new_x);
            }
        }
    }
    
    if (speed_.vy != 0 && vertical_road) {
        double border;
        if (speed_.vy > 0) {
            border = vertical_road->GetMaxY() + ROAD_OFFSET;
            if (new_y >= border - EPS) {
                new_y = border;
                hit_border = true;
            } else {
                new_y = std::min(border, new_y);
            }
        } else {
            border = vertical_road->GetMinY() - ROAD_OFFSET;
            if (new_y <= border + EPS) {
                new_y = border;
                hit_border = true;
            } else {
                new_y = std::max(border, new_y);
            }
        }
    }
    

    if (hit_border) {
        speed_ = {0.0, 0.0};
    }
    

    if (horizontal_road && vertical_road) {
        double min_x = horizontal_road->GetMinX() - ROAD_OFFSET;
        double max_x = horizontal_road->GetMaxX() + ROAD_OFFSET;
        double min_y = vertical_road->GetMinY() - ROAD_OFFSET;
        double max_y = vertical_road->GetMaxY() + ROAD_OFFSET;
        
        double old_x = new_x, old_y = new_y;
        new_x = std::max(min_x, std::min(max_x, new_x));
        new_y = std::max(min_y, std::min(max_y, new_y));
        
        if (std::abs(new_x - old_x) > EPS || std::abs(new_y - old_y) > EPS) {
            speed_ = {0.0, 0.0};
        }
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

} // namespace model