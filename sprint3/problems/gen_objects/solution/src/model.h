#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>

#include "tagged.h"
#include "extra_data.h"

namespace model {

using Dimension = int;

struct Point {
    Dimension x, y;
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
    double vx = 0.0;
    double vy = 0.0;
};

enum class Direction {
    NORTH, SOUTH, WEST, EAST, NONE
};

class Dog {
public:
    Dog() = default;

    const std::string& GetName() const { return name_; }
    void SetName(std::string name) { name_ = std::move(name); }

    PointDouble GetPos() const noexcept { return pos_; }
    void SetPos(PointDouble p) { pos_ = p; }

    Speed GetSpeed() const noexcept { return speed_; }
    void SetSpeed(Speed s) { speed_ = s; }

    Direction GetDirection() const noexcept { return dir_; }
    void SetDirection(Direction d) { dir_ = d; }

    void SetAction(const std::string& move, double speed) {
        speed_ = {0, 0};
        dir_ = Direction::NONE;

        if (move == "L") { speed_.vx = -speed; dir_ = Direction::WEST; }
        if (move == "R") { speed_.vx = speed;  dir_ = Direction::EAST; }
        if (move == "U") { speed_.vy = -speed; dir_ = Direction::NORTH; }
        if (move == "D") { speed_.vy = speed;  dir_ = Direction::SOUTH; }
    }

private:
    std::string name_;
    PointDouble pos_{0,0};
    Speed speed_{};
    Direction dir_{Direction::NONE};
};

class GameSession;

class Player {
public:
    using Id = util::Tagged<unsigned, Player>;

    Player(Id id, Dog* dog, GameSession* session)
        : id_(id), dog_(dog), session_(session) {}

    const Id& GetId() const noexcept { return id_; }
    Dog* GetDog() const noexcept { return dog_; }
    GameSession* GetSession() const noexcept { return session_; }

private:
    Id id_;
    Dog* dog_;
    GameSession* session_;
};

class GameSession {
public:
    Player& AddPlayer(const std::string& name) {
        dogs_.push_back(std::make_unique<Dog>());
        dogs_.back()->SetName(name);

        players_.push_back(std::make_unique<Player>(
            Player::Id{next_id_++},
            dogs_.back().get(),
            this
        ));

        return *players_.back();
    }

    const std::vector<std::unique_ptr<Player>>& GetPlayers() const {
        return players_;
    }

private:
    std::vector<std::unique_ptr<Player>> players_;
    std::vector<std::unique_ptr<Dog>> dogs_;
    unsigned next_id_ = 1;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;

    Map(Id id, std::string name)
        : id_(std::move(id)), name_(std::move(name)) {}

    const Id& GetId() const { return id_; }
    const std::string& GetName() const { return name_; }

    double GetDogSpeed() const { return dog_speed_; }
    void SetDogSpeed(double s) { dog_speed_ = s; }

private:
    Id id_;
    std::string name_;
    double dog_speed_ = 1.0;
};

class Game {
public:
    GameSession& CreateSession(const Map* map) {
        auto& ptr = sessions_[map];
        if (!ptr) {
            ptr = std::make_unique<GameSession>();
        }
        return *ptr;
    }
	
	const Map* FindMap(const Map::Id& id) const {
    for (const auto& m : maps_) {
        if (*m.GetId() == *id) return &m;
    }
    return nullptr;
}

private:
    std::unordered_map<const Map*, std::unique_ptr<GameSession>> sessions_;
};

class PlayerTokens {
public:
    void Add(const std::string& token, Player* player) {
        tokens_[token] = player;
    }

    Player* Find(const std::string& token) const {
        auto it = tokens_.find(token);
        return it == tokens_.end() ? nullptr : it->second;
    }

private:
    std::unordered_map<std::string, Player*> tokens_;
};

} // namespace model