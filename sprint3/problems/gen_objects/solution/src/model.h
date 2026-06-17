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
        , end_{end_y, start.x} {
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

    void AddRoad(const Road& road) {
        roads_.emplace_back(road);
    }

    void AddBuilding(const Building& building) {
        buildings_.emplace_back(building);
    }

    void SetDogSpeed(double speed) {
        dog_speed_ = speed;
    }

    double GetDogSpeed() const noexcept {
        return dog_speed_.value_or(default_dog_speed_);
    }

    static void SetDefaultDogSpeed(double speed) {
        default_dog_speed_ = speed;
    }

    static double GetDefaultDogSpeed() noexcept {
        return default_dog_speed_;
    }

    void AddOffice(Office office);

    // Установка количества типов трофеев для генерации в правильном диапазоне
    void SetLootTypesCount(size_t count) noexcept {
        loot_types_count_ = count;
    }

    size_t GetLootTypesCount() const noexcept {
        return loot_types_count_;
    }

private:
    using OfficeIdHasher = util::TaggedHasher<Office::Id>;
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, OfficeIdHasher>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    size_t loot_types_count_ = 0;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
    std::optional<double> dog_speed_;
    inline static double default_dog_speed_ = 1.0;
};

class Dog {
public:
    explicit Dog(std::string name, uint32_t id) 
        : name_(std::move(name)), id_(id), pos_{0.0, 0.0}, speed_{0.0, 0.0}, dir_(Direction::NORTH) {}

    uint32_t GetId() const noexcept { return id_; }
    const std::string& GetName() const noexcept { return name_; }
    
    void SetPosition(PointDouble pos) { pos_ = pos; }
    PointDouble GetPosition() const noexcept { return pos_; }

    void SetSpeed(Speed speed) { speed_ = speed; }
    Speed GetSpeed() const noexcept { return speed_; }

    void SetDirection(Direction dir) { dir_ = dir; }
    Direction GetDirection() const noexcept { return dir_; }

    void UpdatePosition(double time_delta);

private:
    std::string name_;
    uint32_t id_;
    PointDouble pos_;
    Speed speed_;
    Direction dir_;
};

struct LostObject {
    uint32_t id;
    int type;
    PointDouble pos;
};

class GameSession {
public:
    explicit GameSession(const Map* map, loot_gen::LootGenerator loot_gen) 
        : map_(map), loot_generator_(std::move(loot_gen)) {}

    const Map* GetMap() const noexcept { return map_; }
    
    Dog* AddDog(const std::string& dog_name, bool randomize_spawn);
    const std::vector<std::shared_ptr<Dog>>& GetDogs() const noexcept { return dogs_; }

    const std::unordered_map<uint32_t, LostObject>& GetLostObjects() const noexcept {
        return lost_objects_;
    }

    void Update(double time_delta);

private:
    const Map* map_;
    std::vector<std::shared_ptr<Dog>> dogs_;
    std::unordered_map<uint32_t, LostObject> lost_objects_;
    loot_gen::LootGenerator loot_generator_;
    uint32_t next_dog_id_ = 0;
    uint32_t next_loot_id_ = 0;
};

class Player {
public:
    Player(uint32_t id, std::shared_ptr<GameSession> session, std::shared_ptr<Dog> dog)
        : id_(id), session_(std::move(session)), dog_(std::move(dog)) {}

    uint32_t GetId() const noexcept { return id_; }
    std::shared_ptr<GameSession> GetSession() const noexcept { return session_; }
    std::shared_ptr<Dog> GetDog() const noexcept { return dog_; }

private:
    uint32_t id_;
    std::shared_ptr<GameSession> session_;
    std::shared_ptr<Dog> dog_;
};

class Game {
public:
    using Maps = std::vector<std::unique_ptr<Map>>;

    void SetLootGeneratorConfig(double period, double probability) {
        loot_period_ = period;
        loot_probability_ = probability;
    }

    std::shared_ptr<GameSession> GetOrCreateSession(const Map* map);
    const Map* FindMap(const Map::Id& id) const;
    const Maps& GetMaps() const noexcept;
    void AddMap(Map map);
    void UpdateAllSessions(double time_delta);

private:
    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;

    std::vector<std::unique_ptr<Map>> maps_;
    MapIdToIndex map_id_to_index_;
    std::unordered_map<const Map*, std::shared_ptr<GameSession>> sessions_;
    
    double loot_period_ = 1.0;
    double loot_probability_ = 0.0;
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

namespace boost {
namespace json {
    class value;
    struct value_from_tag;
} // namespace json
} // namespace boost

namespace model {
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Road& road);
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Building& building);
void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Office& office);
} // namespace model