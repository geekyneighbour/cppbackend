#include "model.h"
#include <random>
#include <stdexcept>
#include <boost/json.hpp>
#include <cmath>
#include <algorithm>

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
    new_dog->SetBagCapacity(map_->GetBagCapacity());
    
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

// ================= COLLISION HELPER =================
bool CheckSegmentPointCollision(double x1, double y1, double x2, double y2,
                                 double px, double py, double threshold) {
    // Вектор от начала отрезка к концу
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len2 = dx * dx + dy * dy;
    
    if (len2 < 1e-10) {
        // Отрезок нулевой длины - проверяем точку
        return std::sqrt((px - x1) * (px - x1) + (py - y1) * (py - y1)) <= threshold;
    }
    
    // Параметр t для ближайшей точки на отрезке
    double t = ((px - x1) * dx + (py - y1) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));
    
    // Ближайшая точка на отрезке
    double near_x = x1 + t * dx;
    double near_y = y1 + t * dy;
    
    // Расстояние от точки до отрезка
    double dist = std::sqrt((px - near_x) * (px - near_x) + 
                            (py - near_y) * (py - near_y));
    
    return dist <= threshold;
}

// ================= COLLISIONS =================
void GameSession::ProcessCollisions(double dt) {
    // Обрабатываем каждую собаку
    for (auto& dog_ptr : dogs_) {
        Dog& dog = *dog_ptr;
        
        // Начальная и конечная позиции за тик
        double start_x = dog.GetPos().x - dog.GetSpeed().x * dt;
        double start_y = dog.GetPos().y - dog.GetSpeed().y * dt;
        double end_x = dog.GetPos().x;
        double end_y = dog.GetPos().y;
        
        // Обработка сбора предметов
        CollectItems(dog, start_x, start_y, end_x, end_y);
        
        // Обработка сдачи предметов на базу
        ReturnItemsToBase(dog, end_x, end_y);
    }
}

void GameSession::CollectItems(Dog& dog, double start_x, double start_y,
                                double end_x, double end_y) {
    const double DOG_HALF = 0.3;  // 0.6 / 2
    const double ITEM_HALF = 0.0; // предметы - точки
    
    // Собираем предметы, которые нужно удалить
    std::vector<size_t> items_to_remove;
    
    for (size_t i = 0; i < lost_objects_.size(); ++i) {
        const auto& item = lost_objects_[i];
        
        if (CheckSegmentPointCollision(start_x, start_y, end_x, end_y,
                                        item.pos.x, item.pos.y,
                                        DOG_HALF + ITEM_HALF)) {
            if (!dog.IsBagFull()) {
                dog.AddToBag(item.id, item.type);
                items_to_remove.push_back(i);
            }
        }
    }
    
    // Удаляем собранные предметы (в обратном порядке)
    for (auto it = items_to_remove.rbegin(); it != items_to_remove.rend(); ++it) {
        lost_objects_.erase(lost_objects_.begin() + *it);
    }
}

void GameSession::ReturnItemsToBase(Dog& dog, double x, double y) {
    const double DOG_HALF = 0.3;   // 0.6 / 2
    const double BASE_HALF = 0.25; // 0.5 / 2
    const double COLLISION_DIST = DOG_HALF + BASE_HALF;
    
    if (dog.GetBagSize() == 0) return;
    
    // Проверяем все офисы на карте
    for (const auto& office : map_->GetOffices()) {
        double office_x = office.GetPosition().x + office.GetOffset().dx;
        double office_y = office.GetPosition().y + office.GetOffset().dy;
        
        double dist = std::sqrt((x - office_x) * (x - office_x) + 
                               (y - office_y) * (y - office_y));
        
        if (dist <= COLLISION_DIST) {
            // Начисляем очки за каждый предмет в рюкзаке
            int total_score = 0;
            for (const auto& item : dog.GetBag()) {
                total_score += map_->GetLootTypeValue(item.type);
            }
            dog.AddScore(total_score);
            dog.ClearBag();
            break;
        }
    }
}

void GameSession::UpdateState(double dt) {
    if (!map_) return;
    
    // Обновляем позиции собак
    for (auto& dog : dogs_) {
        dog->UpdatePosition(dt, map_->GetRoads());
    }
    
    // Обрабатываем коллизии
    ProcessCollisions(dt);
    
    // Генерируем новые предметы
    auto ms_dt = std::chrono::milliseconds(static_cast<long long>(dt * 1000));
    auto& config = map_->GetLootConfig();
    
    if (!loot_generator_initialized_) {
        loot_generator_ = loot_gen::LootGenerator(
            std::chrono::milliseconds(static_cast<long long>(config.period * 1000)),
            config.probability
        );
        loot_generator_initialized_ = true;
    }
    
    unsigned new_loot_count = loot_generator_->Generate(
        ms_dt,
        lost_objects_.size(),
        dogs_.size()
    );
    
    for (unsigned i = 0; i < new_loot_count; ++i) {
        PointDouble pos = map_->GetRandomPointOnRoad();
        size_t type = map_->GetRandomLootType();
        int value = map_->GetLootTypeValue(type);
        lost_objects_.push_back(LostObject{type, pos, value, next_loot_id_++});
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
	static constexpr double DOG_HALF_WIDTH = 0.4;
	
    if (speed_.x == 0.0 && speed_.y == 0.0) return;

    double new_x = pos_.x + speed_.x * dt;
    double new_y = pos_.y + speed_.y * dt;

    double min_x = pos_.x, max_x = pos_.x;
    double min_y = pos_.y, max_y = pos_.y;

    bool found_road = false;

    for (const auto& road : roads) {
        double road_min_x = std::min(road.GetStart().x, road.GetEnd().x) - DOG_HALF_WIDTH;
        double road_max_x = std::max(road.GetStart().x, road.GetEnd().x) + DOG_HALF_WIDTH;
        double road_min_y = std::min(road.GetStart().y, road.GetEnd().y) - DOG_HALF_WIDTH;
        double road_max_y = std::max(road.GetStart().y, road.GetEnd().y) + DOG_HALF_WIDTH;

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
        speed_.x = 0.0;
    } else if (new_x > max_x) {
        new_x = max_x;
        speed_.x = 0.0;
    }

    if (new_y < min_y) {
        new_y = min_y;
        speed_.y = 0.0;
    } else if (new_y > max_y) {
        new_y = max_y;
        speed_.y = 0.0;
    }

    pos_ = {new_x, new_y};
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

// ================= JSON SERIALIZATION =================
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
    } 
    double y = start.y + t * (end.y - start.y);
    return {static_cast<double>(start.x), y};
}

size_t Map::GetRandomLootType() const {
    if (loot_types_count_ == 0) return 0;
    std::uniform_int_distribution<size_t> type_dist(0, loot_types_count_ - 1);
    return type_dist(rng_);
}

void GameSession::RestoreDog(Dog&& dog) {
    dogs_.push_back(std::make_unique<Dog>(std::move(dog)));
}

void GameSession::AddLostObject(const LostObject& obj) {
    lost_objects_.push_back(obj);
}

} // namespace model