#pragma once

#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/unique_ptr.hpp>

#include "model.h"
#include "geom.h"

namespace geom {

template <typename Archive>
void serialize(Archive& ar, Point2D& point, const unsigned) {
    ar& point.x;
    ar& point.y;
}

template <typename Archive>
void serialize(Archive& ar, Vec2D& vec, const unsigned) {
    ar& vec.x;
    ar& vec.y;
}

} // namespace geom

namespace model {

template <typename Archive>
void serialize(Archive& ar, FoundObject& obj, const unsigned) {
    ar& *obj.id;
    ar& obj.type;
}

template <typename Archive>
void serialize(Archive& ar, LostObject& obj, const unsigned) {
    ar& obj.id;
    ar& obj.type;
    ar& obj.pos;
    ar& obj.value;
}

template <typename Archive>
void serialize(Archive& ar, Direction& dir, const unsigned) {
    int d = static_cast<int>(dir);
    ar& d;
    dir = static_cast<Direction>(d);
}

template <typename Archive>
void serialize(Archive& ar, BagItem& item, const unsigned) {
    ar& item.id;
    ar& item.type;
}

} // namespace model

namespace serialization {

class DogRepr {
public:
    DogRepr() = default;

    explicit DogRepr(const model::Dog& dog)
        : id_(*dog.GetId())
        , name_(dog.GetName())
        , pos_(dog.GetPos().x, dog.GetPos().y)
        , bag_capacity_(dog.GetBagCapacity())
        , speed_{dog.GetSpeed().x, dog.GetSpeed().y}
        , direction_(dog.GetDirection())
        , score_(dog.GetScore())
    {
        for (const auto& item : dog.GetBagContent()) {
            bag_content_.push_back(item);
        }
    }

    model::Dog Restore() const {
        model::Dog dog{model::Dog::Id{id_}, name_, {pos_.x, pos_.y}, bag_capacity_};

        dog.SetSpeed({speed_.x, speed_.y});
        dog.SetDirection(direction_);
        dog.SetScore(score_);

        for (const auto& item : bag_content_) {
            dog.PutToBag(item);
        }

        return dog;
    }

    template <typename Archive>
    void serialize(Archive& ar, const unsigned) {
        ar& id_;
        ar& name_;
        ar& pos_;
        ar& bag_capacity_;
        ar& speed_;
        ar& direction_;
        ar& score_;
        ar& bag_content_;
    }

    uint64_t GetDogId() const { return id_; }

private:
    uint64_t id_ = 0;
    std::string name_;
    geom::Point2D pos_;
    size_t bag_capacity_ = 0;
    geom::Vec2D speed_;
    model::Direction direction_ = model::Direction::NORTH;
    int score_ = 0;
    std::vector<model::BagItem> bag_content_;
};

class LostObjectRepr {
public:
    LostObjectRepr() = default;

    explicit LostObjectRepr(const model::LostObject& obj)
        : id_(obj.id)
        , type_(obj.type)
        , pos_(obj.pos.x, obj.pos.y)
        , value_(obj.value)
    {}

    model::LostObject Restore() const {
        return model::LostObject{type_, {pos_.x, pos_.y}, value_, id_};
    }

    template <typename Archive>
    void serialize(Archive& ar, const unsigned) {
        ar& id_;
        ar& type_;
        ar& pos_;
        ar& value_;
    }

private:
    size_t id_ = 0;
    size_t type_ = 0;
    geom::Point2D pos_;
    int value_ = 0;
};

class GameSessionRepr {
public:
    GameSessionRepr() = default;

    explicit GameSessionRepr(const model::GameSession& session)
        : map_id_(*session.GetMap()->GetId())
        , next_loot_id_(session.GetNextLootId())
        , next_player_id_(session.GetNextPlayerId())
    {
        for (const auto& dog : session.GetDogs()) {
            dogs_.push_back(DogRepr(*dog));
        }

        for (const auto& obj : session.GetLostObjects()) {
            lost_objects_.push_back(LostObjectRepr(obj));
        }
    }

    void Restore(model::Game& game,
                 std::unordered_map<uint64_t, model::Dog*>& dog_id_map) const {

        auto* map = game.FindMap(model::Map::Id{map_id_});
        if (!map) {
            throw std::runtime_error("Map not found: " + map_id_);
        }

        auto& session = game.FindOrCreateSession(map);

        session.SetNextLootId(next_loot_id_);
        session.SetNextPlayerId(next_player_id_);

        for (const auto& dog_repr : dogs_) {
            model::Dog dog = dog_repr.Restore();
            model::Dog* ptr = session.RestoreDog(std::move(dog));
            dog_id_map[dog_repr.GetDogId()] = ptr;
        }

        for (const auto& obj : lost_objects_) {
            session.AddLostObject(obj.Restore());
        }
    }

    template <typename Archive>
    void serialize(Archive& ar, const unsigned) {
        ar& map_id_;
        ar& dogs_;
        ar& lost_objects_;
        ar& next_loot_id_;
        ar& next_player_id_;
    }

private:
    std::string map_id_;
    std::vector<DogRepr> dogs_;
    std::vector<LostObjectRepr> lost_objects_;
    size_t next_loot_id_ = 0;
    uint64_t next_player_id_ = 0;
};

struct PlayerTokenRepr {
    std::string token;
    uint64_t player_id;
    uint64_t dog_id;
    std::string map_id;

    template <typename Archive>
    void serialize(Archive& ar, const unsigned) {
        ar& token;
        ar& player_id;
        ar& dog_id;
        ar& map_id;
    }
};

class GameStateRepr {
public:
    GameStateRepr() = default;

    GameStateRepr(const model::Game& game,
                  const std::unordered_map<std::string, model::Player*>& tokens)
    {
        for (const auto& [map, session] : game.GetSessions()) {
            sessions_.push_back(GameSessionRepr(*session));
        }

        for (const auto& [token, player] : tokens) {
            PlayerTokenRepr r;
            r.token = token;
            r.player_id = player->GetId();
            r.dog_id = *player->GetDog()->GetId();
            r.map_id = *player->GetSession()->GetMap()->GetId();
            tokens_.push_back(r);
        }
    }

    void Restore(model::Game& game,
                 std::unordered_map<std::string, model::Player*>& tokens) const {

        std::unordered_map<uint64_t, model::Dog*> dog_map;

        for (const auto& s : sessions_) {
            s.Restore(game, dog_map);
        }

        for (const auto& t : tokens_) {
            auto* map = game.FindMap(model::Map::Id{t.map_id});
            if (!map) {
                throw std::runtime_error("Map not found: " + t.map_id);
            }

            auto& session = game.FindOrCreateSession(map);

            model::Player* player = nullptr;

            for (auto* p : session.GetPlayers()) {
                if (p->GetId() == t.player_id) {
                    player = p;
                    break;
                }
            }

            if (!player) {
                auto it = dog_map.find(t.dog_id);
                if (it != dog_map.end()) {
                    player = &session.AddPlayerWithId(*it->second, t.player_id);
                }
            }

            if (!player) {
                throw std::runtime_error("Failed restore token: " + t.token);
            }

            tokens[t.token] = player;
        }
    }

    template <typename Archive>
    void serialize(Archive& ar, const unsigned) {
        ar& sessions_;
        ar& tokens_;
    }

private:
    std::vector<GameSessionRepr> sessions_;
    std::vector<PlayerTokenRepr> tokens_;
};

} // namespace serialization