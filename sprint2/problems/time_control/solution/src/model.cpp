#include "model.h"
#include <random>
#include <stdexcept>
#include <algorithm>

namespace model {

// ================= RANDOM =================
PointDouble GetRandomPointOnRoad(const Road& road) {
    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    Point a = road.GetStart();
    Point b = road.GetEnd();

    if (road.IsHorizontal()) {
        double x1 = std::min(a.x, b.x);
        double x2 = std::max(a.x, b.x);
        return {x1 + dist(gen) * (x2 - x1), static_cast<double>(a.y)};
    } else {
        double y1 = std::min(a.y, b.y);
        double y2 = std::max(a.y, b.y);
        return {static_cast<double>(a.x), y1 + dist(gen) * (y2 - y1)};
    }
}

// ================= MAP =================
void Map::AddOffice(Office office) {
    if (warehouse_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate warehouse");
    }

    size_t index = offices_.size();
    offices_.push_back(std::move(office));

    try {
        warehouse_id_to_index_.emplace(offices_.back().GetId(), index);
    } catch (...) {
        offices_.pop_back();
        throw;
    }
}

// ================= GAME =================
void Game::AddMap(Map map) {
    if (map_id_to_index_.contains(map.GetId())) {
        throw std::invalid_argument("Map already exists");
    }

    size_t index = maps_.size();
    map_id_to_index_[map.GetId()] = index;
    maps_.push_back(std::make_unique<Map>(std::move(map)));
}

// ================= SESSION =================
Dog& GameSession::AddDog(const std::string& name) {
    dogs_.push_back(std::make_unique<Dog>(name));
    Dog& dog = *dogs_.back();

    // ВАЖНО: старт строго в первой дороге (как требует тест)
    if (map_ && !map_->GetRoads().empty()) {
        const Road& r = map_->GetRoads().front();
        Point p = r.GetStart();
        dog.SetPos(static_cast<double>(p.x),
                   static_cast<double>(p.y));
    }

    return dog;
}

Player& GameSession::AddPlayer(Dog& dog) {
    uint64_t id = ++next_player_id_;
    auto [it, _] = players_.emplace(id, Player{id, &dog, this});
    return it->second;
}

std::vector<Player*> GameSession::GetPlayers() {
    std::vector<Player*> res;
    res.reserve(players_.size());

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

// ================= DOG MOVEMENT =================
void Dog::UpdatePosition(double dt, const std::vector<Road>& roads) {
    if (speed_.vx == 0.0 && speed_.vy == 0.0) {
        return;
    }

    const Road* road = nullptr;

    // ищем дорогу, на которой сейчас стоим
    for (const auto& r : roads) {
        if (r.IsPointOnRoad(pos_.x, pos_.y)) {
            road = &r;
            break;
        }
    }

    if (!road) {
        speed_ = {0.0, 0.0};
        return;
    }

    double new_x = pos_.x + speed_.vx * dt;
    double new_y = pos_.y + speed_.vy * dt;

    // если движение остаётся внутри дороги
    if (road->IsPointOnRoad(new_x, new_y)) {
        pos_.x = new_x;
        pos_.y = new_y;
        return;
    }

    const double half_width = 0.4;

    // горизонтальная дорога → ограничиваем X
    if (road->IsHorizontal()) {
        double min_x = std::min(road->GetStart().x, road->GetEnd().x) - half_width;
        double max_x = std::max(road->GetStart().x, road->GetEnd().x) + half_width;

        pos_.x = std::clamp(new_x, min_x, max_x);
        pos_.y = road->GetStart().y;
    }
    // вертикальная дорога → ограничиваем Y
    else {
        double min_y = std::min(road->GetStart().y, road->GetEnd().y) - half_width;
        double max_y = std::max(road->GetStart().y, road->GetEnd().y) + half_width;

        pos_.y = std::clamp(new_y, min_y, max_y);
        pos_.x = road->GetStart().x;
    }

    speed_ = {0.0, 0.0};
}

// ================= ACTION =================
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

} // namespace model