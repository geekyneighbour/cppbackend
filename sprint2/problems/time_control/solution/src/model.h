#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <random>
#include <optional>  

#include "tagged.h"

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
    
bool IsPointOnRoad(double x, double y, double dog_width = 0.8) const {
    double half_dog = dog_width / 2.0;
    double tolerance = 1e-6;
    
    if (IsHorizontal()) {
        double road_y = start_.y;
        double y_diff = std::abs(y - road_y);

        if (y_diff > half_dog + tolerance) return false;
        
        double min_x = GetMinX() - half_dog;
        double max_x = GetMaxX() + half_dog;
        
        return (x + tolerance) >= min_x && (x - tolerance) <= max_x;
    } else {
        double road_x = start_.x;
        double x_diff = std::abs(x - road_x);
        
        if (x_diff > half_dog + tolerance) return false;
        
        double min_y = GetMinY() - half_dog;
        double max_y = GetMaxY() + half_dog;
        
        return (y + tolerance) >= min_y && (y - tolerance) <= max_y;
    }
}
    

void ConstrainMovement(double& x, double& y, const PointDouble& old_pos) const {
    double dog_half = 0.4;  
    
    if (IsHorizontal()) {

        double min_x = GetMinX() - dog_half;
        double max_x = GetMaxX() + dog_half;
        

        if (x < min_x) x = min_x;
        if (x > max_x) x = max_x;
        

        double road_y = start_.y;
        double min_y = road_y - dog_half;
        double max_y = road_y + dog_half;
        
        if (y < min_y) y = min_y;
        if (y > max_y) y = max_y;
        
    } else {

        double min_y = GetMinY() - dog_half;
        double max_y = GetMaxY() + dog_half;
        
        if (y < min_y) y = min_y;
        if (y > max_y) y = max_y;
        
        double road_x = start_.x;
        double min_x = road_x - dog_half;
        double max_x = road_x + dog_half;
        
        if (x < min_x) x = min_x;
        if (x > max_x) x = max_x;
    }
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
        : id_(std::move(id))
        , name_(std::move(name)) {
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

    void AddOffice(Office office);
    
    double GetDogSpeed() const noexcept {
        return dog_speed_.has_value() ? *dog_speed_ : default_dog_speed_;
    }
    
    void SetDogSpeed(double speed) {
        dog_speed_ = speed;
    }
    
    static void SetDefaultDogSpeed(double speed) {
        default_dog_speed_ = speed;
    }
	
	const Road* FindRoadAtPoint(double x, double y) const {
        for (const auto& road : roads_) {
            if (road.IsPointOnRoad(x, y)) {
                return &road;
            }
        }
        return nullptr;
    }
    
    const Road* FindNearestRoad(const PointDouble& pos) const {
        const Road* nearest = nullptr;
        double min_dist = 1e9;
        
        for (const auto& road : roads_) {
            double dx = 0, dy = 0;
            if (road.IsHorizontal()) {
                double road_y = road.GetStart().y;
                dy = std::abs(pos.y - road_y);
                if (pos.x >= road.GetMinX() - 0.4 && pos.x <= road.GetMaxX() + 0.4) {
                    if (dy < min_dist) {
                        min_dist = dy;
                        nearest = &road;
                    }
                }
            } else {
                double road_x = road.GetStart().x;
                dx = std::abs(pos.x - road_x);
                if (pos.y >= road.GetMinY() - 0.4 && pos.y <= road.GetMaxY() + 0.4) {
                    if (dx < min_dist) {
                        min_dist = dx;
                        nearest = &road;
                    }
                }
            }
        }
        return nearest;
    }

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
	

private:
    std::string name_;
    uint64_t id_;
    PointDouble pos_;
    Speed speed_;
    Direction dir_;
    inline static uint64_t next_id_ = 0;
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

class GameSession {
public:
    explicit GameSession(const Map* map) : map_(map) {}

    Dog& AddDog(const std::string& name);
    Player& AddPlayer(Dog& dog);
    std::vector<Player*> GetPlayers();
    const Map* GetMap() const { return map_; }
	void UpdateState(double time_delta);
	const std::vector<std::unique_ptr<Dog>>& GetDogs() const { return dogs_; }

private:
    const Map* map_ = nullptr;
    std::vector<std::unique_ptr<Dog>> dogs_;
    std::unordered_map<uint64_t, Player> players_;
    uint64_t next_player_id_ = 0;
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