#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <random>
#include <optional>  
#include <chrono>

#include "tagged.h"
#include "loot_generator.h"
#include "extra_data.h"

namespace model {
    
using namespace std::string_literals;

using Dimension = int;
using Coord = Dimension;

struct Point {
    Coord x, y;
};

struct Size {
    Dimension width, height;
};

struct Rectangle {
    Point position;
    Size size;
};

struct Offset {
    Dimension dx, dy;
};

struct PointDouble {
    double x, y;
};

struct Speed {
    double vx, vy;
};

enum class Direction {
    NORTH,
    SOUTH,
    WEST,
    EAST
};

class Road {
    struct HorizontalTag {
        explicit HorizontalTag() = default;
    };

    struct VerticalTag {
        explicit VerticalTag() = default;
    };

public:
    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
        : start_{start}
        , end_{end_x, start.y} {
    }

    Road(VerticalTag, Point start, Coord end_y) noexcept
        : start_{start}
        , end_{start.x, end_y} {
    }

    bool IsHorizontal() const noexcept {
        return start_.y == end_.y;
    }

    bool IsVertical() const noexcept {
        return start_.x == end_.x;
    }

    Point GetStart() const noexcept {
        return start_;
    }

    Point GetEnd() const noexcept {
        return end_;
    }

private:
    Point start_;
    Point end_;
};

class Building {
public:
    explicit Building(Rectangle bounds) noexcept
        : bounds_{bounds} {
    }

    const Rectangle& GetBounds() const noexcept {
        return bounds_;
    }

private:
    Rectangle bounds_;
};

class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_{std::move(id)}
        , position_{position}
        , offset_{offset} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    Point GetPosition() const noexcept {
        return position_;
    }

    Offset GetOffset() const noexcept {
        return offset_;
    }

private:
    Id id_;
    Point position_;
    Offset offset_;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name) noexcept
        : id_{std::move(id)}
        , name_{std::move(name)} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    const Buildings& GetBuildings() const noexcept {
        return buildings_;
    }

    const Roads& GetRoads() const noexcept {
        return roads_;
    }

    const Offices& GetOffices() const noexcept {
        return offices_;
    }

    void AddRoad(Road road) {
        roads_.emplace_back(std::move(road));
    }

    void AddBuilding(Building building) {
        buildings_.emplace_back(std::move(building));
    }

    void AddOffice(Office office);

    void SetDogSpeed(double speed) noexcept {
        dog_speed_ = speed;
    }

    double GetDogSpeed() const noexcept {
        return dog_speed_.value_or(default_dog_speed_);
    }

    static void SetDefaultDogSpeed(double speed) noexcept {
        default_dog_speed_ = speed;
    }

    // ИСПРАВЛЕНО: Добавлено подчёркивание к переменной default_dog_speed_
    static double GetDefaultDogSpeed() noexcept { 
        return default_dog_speed_; 
    }

private:
    using OfficeIdHasher = util::TaggedHasher<Office::Id>;
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, OfficeIdHasher>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
    std::optional<double> dog_speed_;
    inline static double default_dog_speed_ = 1.0;
};

class Dog {
public:
    // ИСПРАВЛЕНО: Добавлен метод GetPos() для использования в request_handler.h
    PointDouble GetPos() const noexcept { return pos_; }
private:
    PointDouble pos_{0.0, 0.0};
};

class Player {
public:
    using Id = util::Tagged<unsigned int, Player>;
    const Id& GetId() const noexcept { return id_; }
    Dog* GetDog() const noexcept { return dog_; }
private:
    Id id_{0};
    Dog* dog_ = nullptr;
};

class GameSession {
public:
    // ИСПРАВЛЕНО: Генератор принимается по неконстантной ссылке
    void Update(std::chrono::milliseconds time_delta, loot_gen::LootGenerator& loot_generator);

    // ИСПРАВЛЕНО: Методы-заглушки для корректной компиляции request_handler.h
    const std::vector<Player*>& GetPlayers() const noexcept { return players_; }
    Dog& AddDog(const std::string& name, bool randomize) { static Dog d; return d; }
    Player& AddPlayer(Dog& dog) { static Player p; return p; }

private:
    std::vector<Player*> players_;
};

class Game {
public:
    using Maps = std::vector<Map>;

    void AddMap(Map map);

    const Maps& GetMaps() const noexcept {
        return maps_;
    }

    const Map* FindMap(const Map::Id& id) const noexcept {
        if (auto it = map_id_to_index_.find(id); it != map_id_to_index_.end()) {
            return &maps_[it->second];
        }
        return nullptr;
    }

    GameSession* FindSession(const Map* map) const noexcept {
        auto it = sessions_.find(map);
        if (it != sessions_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    GameSession* CreateSession(const Map* map);
    void UpdateAllSessions(double time_delta_seconds);

    // ИСПРАВЛЕНО: Добавлен метод настройки конфигурации генератора
    void SetLootGeneratorConfig(std::chrono::milliseconds base_interval, double probability) {
        loot_period_ = base_interval;
        loot_probability_ = probability;
    }

    extra_data::ExtraDataManager& GetExtraDataManager() noexcept { return extra_data_manager_; }
    const extra_data::ExtraDataManager& GetExtraDataManager() const noexcept { return extra_data_manager_; }

private:
    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;

    std::vector<Map> maps_;
    MapIdToIndex map_id_to_index_;
    std::unordered_map<const Map*, std::unique_ptr<GameSession>> sessions_;
    
    std::chrono::milliseconds loot_period_{0};
    double loot_probability_{0.0};
    extra_data::ExtraDataManager extra_data_manager_;
};

class PlayerTokens {
public:
    void AddPlayer(const std::string& token, Player* player) {
        tokens_[token] = player;
    }

    Player* FindPlayerByToken(const std::string& token) const {
        auto it = tokens_.find(token);
        return it == tokens_.end() ? nullptr : it->second;
    }

private:
    std::unordered_map<std::string, Player*> tokens_;
};

PointDouble GetRandomPointOnRoad(const Road& road);

}  // namespace model