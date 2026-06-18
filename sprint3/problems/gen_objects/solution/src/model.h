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

struct LostObject {
    unsigned id;
    unsigned type;
    PointDouble pos;
};

class Dog {
public:
    using Id = util::Tagged<uint32_t, Dog>;

    Dog(Id id, std::string name, PointDouble position)
        : id_(id), name_(std::move(name)), position_(position) {}

    const Id& GetId() const noexcept { return id_; }
    const std::string& GetName() const noexcept { return name_; }
    
    PointDouble GetPosition() const noexcept { return position_; }
    void SetPosition(PointDouble pos) noexcept { position_ = pos; }

    Speed GetSpeed() const noexcept { return speed_; }
    void SetSpeed(Speed speed) noexcept { speed_ = speed; }

    std::string GetDirection() const noexcept { return direction_; }
    void SetDirection(std::string dir) noexcept { direction_ = std::move(dir); }

private:
    Id id_;
    std::string name_;
    PointDouble position_;
    Speed speed_{0.0, 0.0};
    std::string direction_ = "U";
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

    const Id& GetId() const noexcept { return id_; }
    const std::string& GetName() const noexcept { return name_; }
    const Buildings& GetBuildings() const noexcept { return buildings_; }
    const Roads& GetRoads() const noexcept { return roads_; }
    const Offices& GetOffices() const noexcept { return offices_; }

    void AddRoad(const Road& road) { roads_.emplace_back(road); }
    void AddBuilding(const Building& building) { buildings_.emplace_back(building); }
    void AddOffice(Office office);

    void SetDogSpeed(double speed) noexcept { dog_speed_ = speed; }
    double GetDogSpeed() const noexcept { return dog_speed_ ? *dog_speed_ : default_dog_speed_; }

    static void SetDefaultDogSpeed(double speed) noexcept { default_dog_speed_ = speed; }
    static double GetDefaultDogSpeed() noexcept { return default_dog_speed; }

    void SetLootTypesCount(size_t count) noexcept { loot_types_count_ = count; }
    size_t GetLootTypesCount() const noexcept { return loot_types_count_; }

private:
    using OfficeIdHasher = util::TaggedHasher<Office::Id>;
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, OfficeIdHasher>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    Offices offices_;
    OfficeIdToIndex warehouse_id_to_index_;
    
    std::optional<double> dog_speed_;
    inline static double default_dog_speed_ = 1.0;
    size_t loot_types_count_ = 0;
};

class GameSession {
public:
    using Dogs = std::vector<std::shared_ptr<Dog>>;

    explicit GameSession(const Map* map) : map_(map) {}

    const Map* GetMap() const noexcept { return map_; }
    const Dogs& GetDogs() const noexcept { return dogs_; }
    const std::unordered_map<unsigned, LostObject>& GetLostObjects() const noexcept { return lost_objects_; }

    std::shared_ptr<Dog> CreateDog(const std::string& name, bool randomize_spawn = false);
    void Update(std::chrono::milliseconds time_delta, const loot_gen::LootGenerator& loot_generator);

private:
    const Map* map_;
    Dogs dogs_;
    std::unordered_map<unsigned, LostObject> lost_objects_;
    unsigned next_object_id_ = 0;
    unsigned next_dog_id_ = 0;

    void MoveDogOnRoads(Dog& dog, double dt);
};

class Player {
public:
    using Id = util::Tagged<uint32_t, Player>;

    Player(Id id, std::shared_ptr<Dog> dog, GameSession* session)
        : id_(id), dog_(std::move(dog)), session_(session) {}

    const Id& GetId() const noexcept { return id_; }
    std::shared_ptr<Dog> GetDog() const noexcept { return dog_; }
    GameSession* GetSession() const noexcept { return session_; }

private:
    Id id_;
    std::shared_ptr<Dog> dog_;
    GameSession* session_;
};

class Game {
public:
    using Maps = std::vector<Map>;

    void AddMap(Map map);
    const Maps& GetMaps() const noexcept { return maps_; }
    const Map* FindMap(const Map::Id& id) const;

    GameSession* FindOrCreateSession(const Map* map);
    void UpdateAllSessions(double time_delta_seconds);

    void SetLootSettings(std::chrono::milliseconds period, double probability) {
        loot_period_ = period;
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

namespace boost::json {
    class value;
    struct value_from_tag;
}

namespace model {
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Road& road);
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Building& building);
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Office& office);
}