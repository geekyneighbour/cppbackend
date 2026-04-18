#include "model.h"
#include <random>
#include <stdexcept>

namespace model {
using namespace std::literals;

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
        // Удаляем офис из вектора, если не удалось вставить в unordered_map
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
            maps_.emplace_back(std::make_unique<Map>(map));
        } catch (...) {
            map_id_to_index_.erase(it);
            throw;
        }
    }
}

}  // namespace model
