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
void Dog::UpdatePosition(double dt, const std::vector<Road>& roads) {
    if (speed_.vx == 0.0 && speed_.vy == 0.0) return;

    double new_x = pos_.x + speed_.vx * dt;
    double new_y = pos_.y + speed_.vy * dt;

    const Road* current_road = nullptr;
    for (const auto& road : roads) {
        if (road.IsPointOnRoad(pos_.x, pos_.y)) {
            current_road = &road;
            break;
        }
    }

    if (!current_road) return;

    bool will_be_on_road = false;
    for (const auto& road : roads) {
        if (road.IsPointOnRoad(new_x, new_y)) {
            will_be_on_road = true;
            break;
        }
    }

    if (will_be_on_road) {
        pos_ = {new_x, new_y};
    } else {
        double constrained_x = new_x;
        double constrained_y = new_y;
        

        if (current_road->IsHorizontal()) {
            double min_x = current_road->GetMinX() - 0.4;
            double max_x = current_road->GetMaxX() + 0.4;
            if (constrained_x < min_x) constrained_x = min_x;
            if (constrained_x > max_x) constrained_x = max_x;
            double road_y = current_road->GetStart().y;
            if (constrained_y < road_y - 0.4) constrained_y = road_y - 0.4;
            if (constrained_y > road_y + 0.4) constrained_y = road_y + 0.4;
        } else {
            double min_y = current_road->GetMinY() - 0.4;
            double max_y = current_road->GetMaxY() + 0.4;
            if (constrained_y < min_y) constrained_y = min_y;
            if (constrained_y > max_y) constrained_y = max_y;
            double road_x = current_road->GetStart().x;
            if (constrained_x < road_x - 0.4) constrained_x = road_x - 0.4;
            if (constrained_x > road_x + 0.4) constrained_x = road_x + 0.4;
        }
        

        if (constrained_x != new_x || constrained_y != new_y) {
            pos_ = {constrained_x, constrained_y};
            speed_ = {0.0, 0.0};
        } else {
            pos_ = {new_x, new_y};
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

} // namespace model#include "model.h"
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
void Dog::UpdatePosition(double dt, const std::vector<Road>& roads)
{
    if (speed_.vx == 0.0 && speed_.vy == 0.0) {
        return;
    }

    const Road* road = nullptr;


    for (const auto& r : roads) {
        if (r.IsPointOnRoad(pos_.x, pos_.y)) {
            road = &r;
            break;
        }
    }

    if (!road) {
        return;
    }

    double new_x = pos_.x + speed_.vx * dt;
    double new_y = pos_.y + speed_.vy * dt;

    double min_x = road->GetMinX() - 0.4;
    double max_x = road->GetMaxX() + 0.4;
    double min_y = road->GetMinY() - 0.4;
    double max_y = road->GetMaxY() + 0.4;

    PointDouble new_pos = {new_x, new_y};


    if (road->IsHorizontal()) {


        new_pos.y = pos_.y;


        if (new_pos.x < min_x) {
            new_pos.x = min_x;
            speed_ = {0.0, 0.0};
        }
        else if (new_pos.x > max_x) {
            new_pos.x = max_x;
            speed_ = {0.0, 0.0};
        }

        pos_ = new_pos;
        return;
    }


    if (road->IsVertical()) {


        new_pos.x = pos_.x;

        if (new_pos.y < min_y) {
            new_pos.y = min_y;
            speed_ = {0.0, 0.0};
        }
        else if (new_pos.y > max_y) {
            new_pos.y = max_y;
            speed_ = {0.0, 0.0};
        }

        pos_ = new_pos;
        return;
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