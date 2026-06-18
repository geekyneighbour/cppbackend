#include "model.h"
#include <random>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace json = boost::json;

namespace model {

// ================= RANDOM =================
PointDouble GetRandomPointOnRoad(const Road& road) {
    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    auto s = road.GetStart();
    auto e = road.GetEnd();

    if (road.IsHorizontal()) {
        double minx = std::min(s.x, e.x);
        double maxx = std::max(s.x, e.x);
        return {minx + dist(gen) * (maxx - minx), static_cast<double>(s.y)};
    }

    double miny = std::min(s.y, e.y);
    double maxy = std::max(s.y, e.y);
    return {static_cast<double>(s.x), miny + dist(gen) * (maxy - miny)};
}

// ================= GAME =================
GameSession& Game::FindOrCreateSession(const Map* map) {
    return *sessions_.try_emplace(map, std::make_unique<GameSession>(map)).first->second;
}

const Map* Game::FindMap(const Map::Id& id) const {
    for (const auto& m : maps_) {
        if (*m->GetId() == *id) return m.get();
    }
    return nullptr;
}

std::vector<std::unique_ptr<Map>>& Game::GetMaps() noexcept {
    return maps_;
}

void Game::AddMap(Map map) {
    maps_.push_back(std::make_unique<Map>(std::move(map)));
}

void Game::UpdateAllSessions(double dt) {
    for (auto& [_, s] : sessions_) {
        s->UpdateState(dt);
    }
}

void Game::SetLootGeneratorConfig(LootGeneratorConfig cfg) {
    loot_cfg_ = std::move(cfg);
}

const Game::LootGeneratorConfig& Game::GetLootGeneratorConfig() const noexcept {
    return loot_cfg_;
}

// ================= SESSION =================
GameSession::GameSession(const Map* map)
    : map_(map) {}

Dog& GameSession::AddDog(std::string_view name, bool randomize) {
    auto dog = std::make_unique<Dog>(std::string(name));
    dogs_.push_back(std::move(dog));
    return *dogs_.back();
}

Player& GameSession::AddPlayer(Dog& dog) {
    uint64_t id = ++next_player_id_;
    auto [it, _] = players_.emplace(id, Player{id, &dog, this});
    return it->second;
}

std::vector<Player*> GameSession::GetPlayers() {
    std::vector<Player*> res;
    for (auto& [_, p] : players_) {
        res.push_back(&p);
    }
    return res;
}

// ================= LOOT =================
void GameSession::UpdateLoot(double dt) {
    if (!map_ || map_->GetLootTypes().empty()) return;

    if (!loot_generator_) {
        loot_generator_ = std::make_unique<loot_gen::LootGenerator>(
            std::chrono::milliseconds(5000),
            0.5
        );
    }

    unsigned looters = players_.size();
    unsigned current = lost_objects_.size();

    unsigned add = loot_generator_->Generate(
        std::chrono::milliseconds(static_cast<int>(dt * 1000)),
        current,
        looters
    );

    static std::random_device rd;
    static std::mt19937 gen(rd());

    for (unsigned i = 0; i < add; ++i) {
        if (map_->GetLootTypes().empty() || map_->GetRoads().empty()) break;
        
        std::uniform_int_distribution<> type_dist(0, map_->GetLootTypes().size() - 1);
        std::uniform_int_distribution<> road_dist(0, map_->GetRoads().size() - 1);
        
        int type = type_dist(gen);
        auto p = GetRandomPointOnRoad(map_->GetRoads()[road_dist(gen)]);

        json::object obj;
        obj["type"] = type;
        obj["pos"] = json::array{p.x, p.y};

        lost_objects_[std::to_string(lost_objects_.size())] = obj;
    }
}

const boost::json::object& GameSession::GetLostObjects() const noexcept {
    return lost_objects_;
}

void GameSession::UpdateState(double dt) {
    for (auto& d : dogs_) {
        d->UpdatePosition(dt, map_->GetRoads());
    }

    UpdateLoot(dt);
}

// ================= DOG IMPLEMENTATION =================

void Dog::SetAction(const std::string& action, double speed) {
    if (action == "L") {
        SetDirection(Direction::WEST);
        SetSpeed({-speed, 0.0});
    } else if (action == "R") {
        SetDirection(Direction::EAST);
        SetSpeed({speed, 0.0});
    } else if (action == "U") {
        SetDirection(Direction::NORTH);
        SetSpeed({0.0, -speed});
    } else if (action == "D") {
        SetDirection(Direction::SOUTH);
        SetSpeed({0.0, speed});
    } else if (action == "") {
        SetSpeed({0.0, 0.0});
    }
}

void Dog::UpdatePosition(double dt, const std::vector<Road>& roads) {
    if (dt <= 0.0) return;
    if (speed_.vx == 0.0 && speed_.vy == 0.0) return;

    double new_x = pos_.x + speed_.vx * dt;
    double new_y = pos_.y + speed_.vy * dt;

    // Проверка, находится ли позиция на дороге
    bool on_road = false;
    for (const auto& road : roads) {
        auto start = road.GetStart();
        auto end = road.GetEnd();
        
        if (road.IsHorizontal()) {
            double minx = std::min(start.x, end.x);
            double maxx = std::max(start.x, end.x);
            const double epsilon = 1e-6;
            if (new_x >= minx - epsilon && new_x <= maxx + epsilon &&
                std::abs(new_y - start.y) < epsilon) {
                on_road = true;
                break;
            }
        } else {
            double miny = std::min(start.y, end.y);
            double maxy = std::max(start.y, end.y);
            const double epsilon = 1e-6;
            if (std::abs(new_x - start.x) < epsilon &&
                new_y >= miny - epsilon && new_y <= maxy + epsilon) {
                on_road = true;
                break;
            }
        }
    }

    if (on_road) {
        pos_ = {new_x, new_y};
    } else {
        // Если вне дороги - пробуем двигаться по одной оси
        double test_x = pos_.x + speed_.vx * dt;
        double test_y = pos_.y;
        
        bool x_on_road = false;
        for (const auto& road : roads) {
            auto start = road.GetStart();
            auto end = road.GetEnd();
            
            if (road.IsHorizontal()) {
                double minx = std::min(start.x, end.x);
                double maxx = std::max(start.x, end.x);
                const double epsilon = 1e-6;
                if (test_x >= minx - epsilon && test_x <= maxx + epsilon &&
                    std::abs(test_y - start.y) < epsilon) {
                    x_on_road = true;
                    break;
                }
            } else {
                const double epsilon = 1e-6;
                if (std::abs(test_x - start.x) < epsilon &&
                    test_y >= std::min(start.y, end.y) - epsilon &&
                    test_y <= std::max(start.y, end.y) + epsilon) {
                    x_on_road = true;
                    break;
                }
            }
        }
        
        if (x_on_road) {
            pos_.x = test_x;
        } else {
            test_x = pos_.x;
            test_y = pos_.y + speed_.vy * dt;
            
            bool y_on_road = false;
            for (const auto& road : roads) {
                auto start = road.GetStart();
                auto end = road.GetEnd();
                
                if (road.IsHorizontal()) {
                    const double epsilon = 1e-6;
                    if (test_x >= std::min(start.x, end.x) - epsilon &&
                        test_x <= std::max(start.x, end.x) + epsilon &&
                        std::abs(test_y - start.y) < epsilon) {
                        y_on_road = true;
                        break;
                    }
                } else {
                    double miny = std::min(start.y, end.y);
                    double maxy = std::max(start.y, end.y);
                    const double epsilon = 1e-6;
                    if (std::abs(test_x - start.x) < epsilon &&
                        test_y >= miny - epsilon && test_y <= maxy + epsilon) {
                        y_on_road = true;
                        break;
                    }
                }
            }
            
            if (y_on_road) {
                pos_.y = test_y;
            }
        }
    }
}

} // namespace model