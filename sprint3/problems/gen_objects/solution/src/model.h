#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <random>
#include <optional>
#include <boost/json.hpp>

#include "tagged.h"
#include "loot_generator.h"

namespace model {

using namespace std::string_literals;

using Dimension = int;
using Coord = Dimension;

struct Point { Coord x, y; };
struct Size { Dimension width, height; };
struct Rectangle { Point position; Size size; };
struct Offset { Dimension dx, dy; };

struct PointDouble { double x, y; };
struct Speed { double vx, vy; };

enum class Direction {
    NORTH, SOUTH, WEST, EAST
};

// ================= ROAD =================
class Road {
public:
    struct HorizontalTag {};
    struct VerticalTag {};

    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
        : start_{start}, end_{end_x, start.y} {}

    Road(VerticalTag, Point start, Coord end_y) noexcept
        : start_{start}, end_{start.x, end_y} {}

    bool IsHorizontal() const noexcept { return start_.y == end_.y; }
    bool IsVertical() const noexcept { return start_.x == end_.x; }

    Point GetStart() const noexcept { return start_; }
    Point GetEnd() const noexcept { return end_; }

private:
    Point start_, end_;
};

// ================= BUILDING =================
class Building {
public:
    explicit Building(Rectangle bounds) noexcept : bounds_{bounds} {}
    const Rectangle& GetBounds() const noexcept { return bounds_; }

private:
    Rectangle bounds_;
};

// ================= OFFICE =================
class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_{std::move(id)}, position_{position}, offset_{offset} {}

    const Id& GetId() const noexcept { return id_; }
    Point GetPosition() const noexcept { return position_; }
    Offset GetOffset() const noexcept { return offset_; }

private:
    Id id_;
    Point position_;
    Offset offset_;
};

// ================= MAP =================
class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;
    using LootTypes = boost::json::array;

    Map(Id id, std::string name) noexcept
        : id_(std::move(id)), name_(std::move(name)) {}

    const Id& GetId() const noexcept { return id_; }
    const std::string& GetName() const noexcept { return name_; }

    const Roads& GetRoads() const noexcept { return roads_; }
    const Buildings& GetBuildings() const noexcept { return buildings_; }
    const Offices& GetOffices() const noexcept { return offices_; }

    void AddRoad(const Road& r) { roads_.push_back(r); }
    void AddBuilding(const Building& b) { buildings_.push_back(b); }
    void AddOffice(const Office& o) { offices_.push_back(o); }

    const LootTypes& GetLootTypes() const noexcept { return loot_types_; }
    void SetLootTypes(LootTypes lt) { loot_types_ = std::move(lt); }

    size_t GetLootTypesCount() const noexcept { return loot_types_.size(); }

    void SetDogSpeed(double speed) noexcept { dog_speed_ = speed; }
    double GetDogSpeed() const noexcept { return dog_speed_; }

    static void SetDefaultDogSpeed(double speed) noexcept { default_dog_speed_ = speed; }
    static double GetDefaultDogSpeed() noexcept { return default_dog_speed_; }

private:
    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    Offices offices_;
    LootTypes loot_types_;
    double dog_speed_ = default_dog_speed_;
    static inline double default_dog_speed_ = 1.0;
};


// ================= DOG =================
class Dog {
public:
    explicit Dog(std::string name)
        : name_(std::move(name)), id_(++next_id_) {}

    void SetPos(double x, double y) { pos_ = {x, y}; }
    PointDouble GetPos() const { return pos_; }

    void SetSpeed(Speed s) { speed_ = s; }
    Speed GetSpeed() const { return speed_; }

    void SetDirection(Direction d) { dir_ = d; }
    Direction GetDirection() const { return dir_; }

    void SetAction(const std::string& action, double speed);
    void UpdatePosition(double dt, const std::vector<Road>& roads);

private:
    std::string name_;
    uint64_t id_;
    PointDouble pos_{};
    Speed speed_{};
    Direction dir_{Direction::NORTH};

    inline static uint64_t next_id_ = 0;
};

// ================= PLAYER =================
class GameSession;

class Player {
public:
    Player(uint64_t id, Dog* dog, GameSession* session)
        : id_(id), dog_(dog), session_(session) {}

    uint64_t GetId() const { return id_; }
    Dog* GetDog() const { return dog_; }
    GameSession* GetSession() const { return session_; }

private:
    uint64_t id_;
    Dog* dog_;
    GameSession* session_;
};

// ================= GAME SESSION =================
class GameSession {
public:
    explicit GameSession(const Map* map);

    Dog& AddDog(std::string_view name, bool randomize);
    Player& AddPlayer(Dog& dog);

    std::vector<Player*> GetPlayers();

    const Map* GetMap() const { return map_; }

    void UpdateState(double dt);

    const std::vector<std::unique_ptr<Dog>>& GetDogs() const { return dogs_; }

    const boost::json::object& GetLostObjects() const noexcept;

private:
    void UpdateLoot(double dt);

    const Map* map_{};

    std::vector<std::unique_ptr<Dog>> dogs_;
    std::unordered_map<uint64_t, Player> players_;

    uint64_t next_player_id_ = 0;

    boost::json::object lost_objects_;

    std::unique_ptr<loot_gen::LootGenerator> loot_generator_;
};

class PlayerTokens {
public:
    void AddPlayer(const std::string& token, Player* player) {
        tokens_[token] = player;
    }

    Player* FindPlayerByToken(const std::string& token) const {
        auto it = tokens_.find(token);
        return it != tokens_.end() ? it->second : nullptr;
    }

private:
    std::unordered_map<std::string, Player*> tokens_;
};

// ================= GAME =================
class Game {
public:
    struct LootGeneratorConfig {
        std::chrono::milliseconds period;
        double probability;
    };

    void AddMap(Map map);
    const Map* FindMap(const Map::Id& id) const;
    std::vector<std::unique_ptr<Map>>& GetMaps() noexcept;
    GameSession& FindOrCreateSession(const Map* map);
    void UpdateAllSessions(double dt);
    void SetLootGeneratorConfig(LootGeneratorConfig cfg);
    const LootGeneratorConfig& GetLootGeneratorConfig() const noexcept;

private:
    std::vector<std::unique_ptr<Map>> maps_;
    std::unordered_map<const Map*, std::unique_ptr<GameSession>> sessions_;
    LootGeneratorConfig loot_cfg_{std::chrono::milliseconds(5000), 0.5};
};

// ================= HELPERS =================
PointDouble GetRandomPointOnRoad(const Road& road);

} // namespace model