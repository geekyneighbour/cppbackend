#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <random>
#include <optional>  
#include <cmath>

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

struct BagItem {
    size_t id;
    size_t type;
    
    BagItem(size_t i, size_t t) : id(i), type(t) {}
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
	
    double GetMinX() const noexcept {
        return std::min(start_.x, end_.x);
    }
    
    double GetMaxX() const noexcept {
        return std::max(start_.x, end_.x);
    }
    
    double GetMinY() const noexcept {
        return std::min(start_.y, end_.y);
    }
    
    double GetMaxY() const noexcept {
        return std::max(start_.y, end_.y);
    }
    
    bool IsPointOnRoad(double x, double y, double dog_width = DEFAULT_DOG_WIDTH_) const {
        double half_dog = dog_width / 2.0;
        
        if (IsHorizontal()) {
            double road_y = start_.y;
            if (std::abs(y - road_y) > half_dog + TOLERANCE_) return false;
            
            double min_x = GetMinX() - half_dog;
            double max_x = GetMaxX() + half_dog;
            
            return x >= min_x - TOLERANCE_ && x <= max_x + TOLERANCE_;
            
        } else {
            double road_x = start_.x;
            if (std::abs(x - road_x) > half_dog + TOLERANCE_) return false;
            
            double min_y = GetMinY() - half_dog;
            double max_y = GetMaxY() + half_dog;
            
            return y >= min_y - TOLERANCE_ && y <= max_y + TOLERANCE_;
        }
    }
    
    void ConstrainMovement(double& x, double& y, const PointDouble& old_pos) const {
    if (IsHorizontal()) {
        
        double min_x = GetMinX();
        double max_x = GetMaxX();
        double road_y = start_.y;
        
        ConstrainAxis(x, y, min_x, max_x, road_y);
    } else {
        
        double min_y = GetMinY();
        double max_y = GetMaxY();
        double road_x = start_.x;
        
        ConstrainAxis(y, x, min_y, max_y, road_x);
    }
}

private:
	void ConstrainAxis(double& along, double& across,
                         double min_along, double max_along,
                         double across_value) const {
    
    if (along < min_along) along = min_along;
    if (along > max_along) along = max_along;
    
    
    double min_across = across_value - DOG_HALF_WIDTH_;
    double max_across = across_value + DOG_HALF_WIDTH_;
    
    if (across < min_across) across = min_across;
    if (across > max_across) across = max_across;
}
    Point start_;
    Point end_;
    static constexpr double DEFAULT_DOG_WIDTH_ = 0.8;
    static constexpr double TOLERANCE_ = 1e-9;
	static constexpr double DOG_HALF_WIDTH_ = 0.4;

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

struct LootGeneratorConfig {
    double period = 1.0;
    double probability = 0.5;
};

struct LostObject {
    size_t type;
    PointDouble pos;
    int value;
    
    LostObject(size_t t, PointDouble p, int v = 0) : type(t), pos(p), value(v) {}
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name) noexcept
        : id_(std::move(id))
        , name_(std::move(name)) {
    }

    const Id& GetId() const noexcept { return id_; }
    const std::string& GetName() const noexcept { return name_; }
    const Buildings& GetBuildings() const noexcept { return buildings_; }
    const Roads& GetRoads() const noexcept { return roads_; }
    const Offices& GetOffices() const noexcept { return offices_; }

    void AddRoad(const Road& road) { roads_.emplace_back(road); }
    void AddBuilding(const Building& building) { buildings_.emplace_back(building); }
    void AddOffice(Office office);
    
    double GetDogSpeed() const noexcept {
        return dog_speed_.has_value() ? *dog_speed_ : default_dog_speed_;
    }
    
    void SetDogSpeed(double speed) { dog_speed_ = speed; }
    
    static void SetDefaultDogSpeed(double speed) { default_dog_speed_ = speed; }
    
    size_t GetLootTypesCount() const noexcept { return loot_types_count_; }
    void SetLootTypesCount(size_t count) { loot_types_count_ = count; }
    
    const LootGeneratorConfig& GetLootConfig() const { return loot_config_; }
    void SetLootConfig(const LootGeneratorConfig& config) { loot_config_ = config; }
    
    size_t GetBagCapacity() const { return bag_capacity_; }
    void SetBagCapacity(size_t capacity) { bag_capacity_ = capacity; }
    
    void AddLootTypeValue(size_t type, int value) {
        if (type >= loot_type_values_.size()) {
            loot_type_values_.resize(type + 1, 0);
        }
        loot_type_values_[type] = value;
    }
    
    int GetLootTypeValue(size_t type) const {
        return (type < loot_type_values_.size()) ? loot_type_values_[type] : 0;
    }
    
    PointDouble GetRandomPointOnRoad() const;
    size_t GetRandomLootType() const;
    
    const Road* FindRoadAtPoint(double x, double y) const;
    const Road* FindNearestRoad(const PointDouble& pos) const;

private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
    
    std::optional<double> dog_speed_;
    inline static double default_dog_speed_ = 1.0;
    static constexpr double HALF_WIDTH_ = 0.4;
    
    size_t loot_types_count_ = 0;
    LootGeneratorConfig loot_config_;
    mutable std::mt19937 rng_{std::random_device{}()};
    
    size_t bag_capacity_ = 3;
    std::vector<int> loot_type_values_;
};

class Dog {
public:
    explicit Dog(std::string name)
        : name_(std::move(name))
        , id_(++next_id_)
        , pos_{0.0, 0.0}
        , speed_{0.0, 0.0}
        , dir_(Direction::NORTH) {
    }

    const std::string& GetName() const { return name_; }
    uint64_t GetId() const { return id_; }
    
    PointDouble GetPos() const { return pos_; }
    Speed GetSpeed() const { return speed_; }
    Direction GetDirection() const { return dir_; }
    
    void SetPos(PointDouble pos) { pos_ = pos; }
    void SetPos(double x, double y) { pos_ = {x, y}; }
    void SetSpeed(Speed speed) { speed_ = speed; }
    void SetSpeed(double vx, double vy) { speed_ = {vx, vy}; }
    void SetDirection(Direction dir) { dir_ = dir; }
    
    void SetAction(const std::string& action, double speed);
    
    void UpdatePosition(double time_delta, const std::vector<model::Road>& roads);
    
    const std::vector<BagItem>& GetBag() const { return bag_; }
    void AddToBag(size_t item_id, size_t type) {
        if (bag_.size() < bag_capacity_) {
            bag_.push_back({item_id, type});
        }
    }
    void ClearBag() { bag_.clear(); }
    size_t GetBagSize() const { return bag_.size(); }
    size_t GetBagCapacity() const { return bag_capacity_; }
    void SetBagCapacity(size_t capacity) { bag_capacity_ = capacity; }
    bool IsBagFull() const { return bag_.size() >= bag_capacity_; }
    
    int GetScore() const { return score_; }
    void AddScore(int points) { score_ += points; }
    void SetScore(int score) { score_ = score; }

private:
    std::string name_;
    uint64_t id_;
    PointDouble pos_;
    Speed speed_;
    Direction dir_;
    inline static uint64_t next_id_ = 0;
    
    std::vector<BagItem> bag_;
    size_t bag_capacity_ = 3;
    int score_ = 0;
};

class GameSession;

class Player {
public:
    Player(uint64_t id, Dog* dog, GameSession* session)
        : id_(id), dog_(dog), session_(session) {
    }

    uint64_t GetId() const { return id_; }
    Dog* GetDog() const { return dog_; }
    GameSession* GetSession() const { return session_; }

private:
    uint64_t id_;
    Dog* dog_;
    GameSession* session_;
};

bool CheckSegmentPointCollision(double x1, double y1, double x2, double y2,
                                 double px, double py, double threshold);

class GameSession {
public:
    explicit GameSession(const Map* map) : map_(map) {}

    Dog& AddDog(std::string_view name, bool randomize);
    Player& AddPlayer(Dog& dog);
    std::vector<Player*> GetPlayers();
    const Map* GetMap() const { return map_; }
    void UpdateState(double time_delta);
    const std::vector<std::unique_ptr<Dog>>& GetDogs() const { return dogs_; }
    const std::vector<LostObject>& GetLostObjects() const { return lost_objects_; }
    
    void ProcessCollisions(double dt);
    void CollectItems(Dog& dog, double start_x, double start_y, 
                      double end_x, double end_y);
    void ReturnItemsToBase(Dog& dog, double x, double y);

private:
    const Map* map_ = nullptr;
    std::vector<std::unique_ptr<Dog>> dogs_;
    std::unordered_map<uint64_t, Player> players_;
    uint64_t next_player_id_ = 0;
    std::mt19937 random_gen_{std::random_device{}()};
    
    std::vector<LostObject> lost_objects_;
    std::optional<loot_gen::LootGenerator> loot_generator_;
    bool loot_generator_initialized_ = false;
    size_t next_loot_id_ = 0;
};

class Game {
public:
    using Maps = std::vector<std::unique_ptr<Map>>;
    
    GameSession& FindOrCreateSession(const Map* map);
    const Map* FindMap(const Map::Id& id) const;
    const Maps& GetMaps() const noexcept;
    void AddMap(Map map);
    void UpdateAllSessions(double time_delta);

private:
    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;

    std::vector<std::unique_ptr<Map>> maps_;
    MapIdToIndex map_id_to_index_;
    std::unordered_map<const Map*, std::unique_ptr<GameSession>> sessions_;
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