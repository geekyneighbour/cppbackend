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
        dog.SetPos(static_cast<double>(start.x), static_cast<double>(start.y));
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
    for (auto& dog_ptr : dogs_) {
        dog_ptr->UpdatePosition(time_delta);
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

void Dog::UpdatePosition(double time_delta) {
    if (speed_.vx == 0.0 && speed_.vy == 0.0) return;
    
    double new_x = pos_.x + speed_.vx * time_delta;
    double new_y = pos_.y + speed_.vy * time_delta;
    pos_ = {new_x, new_y};
}

}  // namespace model