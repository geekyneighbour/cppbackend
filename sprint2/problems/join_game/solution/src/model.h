#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#include "tagged.h"

namespace model {

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

private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
};


class Dog {
public:
    explicit Dog(std::string name)
        : name_(std::move(name)), id_(++next_id_) {
    }

    const std::string& GetName() const { return name_; }
    uint64_t GetId() const { return id_; }

private:
    std::string name_;
    uint64_t id_;
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

    Dog& AddDog(const std::string& name) {
        dogs_.push_back(std::make_unique<Dog>(name));
        return *dogs_.back();
    }

    Player& AddPlayer(Dog& dog) {
        uint64_t id = ++next_player_id_;
        auto [it, ok] = players_.emplace(
            id, Player{ id, &dog, this }
        );
        return it->second;
    }

    std::vector<Player*> GetPlayers() {
        std::vector<Player*> res;
        for (auto& [_, p] : players_) {
            res.push_back(&p);
        }
        return res;
    }

private:
    const Map* map_ = nullptr;

    std::vector<std::unique_ptr<Dog>> dogs_;
    std::unordered_map<uint64_t, Player> players_;
    uint64_t next_player_id_ = 0;
};



class Game {
public:
	using Maps = std::vector<std::unique_ptr<Map>>;
    GameSession& FindOrCreateSession(const Map* map) {
        auto& ptr = sessions_[map];
        if (!ptr) {
            ptr = std::make_unique<GameSession>(map);
        }
        return *ptr;
    }

    const Map* FindMap(const Map::Id& id) const {
        for (auto& m : maps_) {
            if (*m->GetId() == *id) return m.get();
        }
        return nullptr;
    }

    const Maps& GetMaps() const noexcept {
        return maps_;
    }
	
	void AddMap(Map map);

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



}  // namespace model
