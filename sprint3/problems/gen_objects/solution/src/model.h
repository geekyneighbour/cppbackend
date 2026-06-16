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

struct LostObject {
    uint32_t id;
    uint32_t type;
    PointDouble pos;
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

    const Offices& GetOffices() const noexcept {
        return offices_;
    }

    const Roads& GetRoads() const noexcept {
        return roads_;
    }

    void AddRoad(Road road) {
        roads_.push_back(std::move(road));
    }

    void AddBuilding(Building building) {
        buildings_.push_back(std::move(building));
    }

    void AddOffice(Office office);

    void SetDogSpeed(double speed) noexcept {
        dog_speed_ = speed;
    }

    std::optional<double> GetDogSpeed() const noexcept {
        return dog_speed_;
    }

    static void SetDefaultDogSpeed(double speed) noexcept {
        default_dog_speed_ = speed;
    }

    static double GetDefaultDogSpeed() noexcept {
        return default_dog_speed_;
    }

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
    std::optional<double> dog_speed_;
    static inline double default_dog_speed_ = 1.0;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
    size_t loot_types_count_ = 0;
};

class Dog {
public:
    using Id = util::Tagged<uint32_t, Dog>;
    
    Dog(Id id, std::string name, PointDouble pos)
        : id_(id)
        , name_(std::move(name))
        , pos_(pos) {}

    const Id& GetId() const noexcept { return id_; }
    const std::string& GetName() const noexcept { return name_; }
    const PointDouble& GetPosition() const noexcept { return pos_; }
    const Speed& GetSpeed() const noexcept { return speed_; }
    const Direction& GetDirection() const noexcept { return dir_; }

    void SetSpeed(Speed speed) { speed_ = speed; }
    void SetDirection(Direction dir) { dir_ = dir; }
    void SetPosition(PointDouble pos) { pos_ = pos; }

private:
    Id id_;
    std::string name_;
    PointDouble pos_;
    Speed speed_{0.0, 0.0};
    Direction dir_{Direction::NORTH};
};

class Player {
public:
    using Id = util::Tagged<uint32_t, Player>;

    Player(Id id, std::shared_ptr<Dog> dog, std::string session_id)
        : id_(id)
        , dog_(std::move(dog))
        , session_id_(std::move(session_id)) {}

    const Id& GetId() const noexcept { return id_; }
    const std::shared_ptr<Dog>& GetDog() const noexcept { return dog_; }
    const std::string& GetSessionId() const noexcept { return session_id_; }

private:
    Id id_;
    std::shared_ptr<Dog> dog_;
    std::string session_id_;
};

class GameSession {
public:
    GameSession(const Map* map, const std::chrono::milliseconds& loot_period, double loot_probability)
        : map_(map)
        , loot_gen_(loot_period, loot_probability, []() {
            static std::mt19937 d_gen(std::random_device{}());
            static std::uniform_real_distribution<double> d_dist(0.0, 1.0);
            return d_dist(d_gen);
        }) {}

    const Map* GetMap() const noexcept { return map_; }
    
    void AddPlayer(std::shared_ptr<Player> player) {
        players_.push_back(player);
    }
    
    const std::vector<std::shared_ptr<Player>>& GetPlayers() const noexcept {
        return players_;
    }

    const std::unordered_map<uint32_t, LostObject>& GetLostObjects() const noexcept {
        return lost_objects_;
    }

    void Update(std::chrono::milliseconds time_delta);

private:
    const Map* map_;
    std::vector<std::shared_ptr<Player>> players_;
    std::unordered_map<uint32_t, LostObject> lost_objects_;
    uint32_t next_loot_id_ = 0;
    loot_gen::LootGenerator loot_gen_;
};

class Game {
public:
    using Maps = std::vector<std::unique_ptr<Map>>;

    GameSession* FindOrCreateSession(const Map* map);
    const Map* FindMap(const Map::Id& id) const;
    const Maps& GetMaps() const noexcept;
    Map& AddMap(Map map);
    void UpdateAllSessions(double time_delta_seconds);

    void SetLootGeneratorConfig(std::chrono::milliseconds period, double probability) {
		loot_period_ = period;
		loot_probability_ = probability;
	}

private:
    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;

    std::vector<std::unique_ptr<Map>> maps_;
    MapIdToIndex map_id_to_index_;
    std::unordered_map<const Map*, std::unique_ptr<GameSession>> sessions_;
    std::chrono::milliseconds loot_period_{5000};
    double loot_probability_ = 0.5;
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
    void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, const Map& map);
} // namespace model