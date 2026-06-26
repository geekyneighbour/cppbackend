#pragma once

#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/utility.hpp>

#include "model.h"
#include "geom.h"

namespace geom {

template <typename Archive>
void serialize(Archive& ar, Point2D& point, [[maybe_unused]] const unsigned version) {
    ar& point.x;
    ar& point.y;
}

template <typename Archive>
void serialize(Archive& ar, Vec2D& vec, [[maybe_unused]] const unsigned version) {
    ar& vec.x;
    ar& vec.y;
}

}  // namespace geom

namespace model {

template <typename Archive>
void serialize(Archive& ar, FoundObject& obj, [[maybe_unused]] const unsigned version) {
    ar& *obj.id;
    ar& obj.type;
}

template <typename Archive>
void serialize(Archive& ar, LostObject& obj, [[maybe_unused]] const unsigned version) {
    ar& obj.id;
    ar& obj.type;
    ar& obj.pos;
    ar& obj.value;
}

template <typename Archive>
void serialize(Archive& ar, Direction& dir, [[maybe_unused]] const unsigned version) {
    int d = static_cast<int>(dir);
    ar& d;
    dir = static_cast<Direction>(d);
}

template <typename Archive>
void serialize(Archive& ar, BagItem& item, [[maybe_unused]] const unsigned version) {
    ar& item.id;
    ar& item.type;
}

}  // namespace model

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
        , score_(dog.GetScore()) {
        for (const auto& item : dog.GetBagContent()) {
    bag_content_.push_back(item);
}
    }

    [[nodiscard]] model::Dog Restore() const {
        model::Dog dog{model::Dog::Id{id_}, name_, {pos_.x, pos_.y}, bag_capacity_};
        dog.SetSpeed({speed_.x, speed_.y});
        dog.SetDirection(direction_);
        dog.SetScore(score_);
        for (const auto& item : bag_content_) {
            if (!dog.PutToBag(item)) {
                throw std::runtime_error("Failed to put bag content");
            }
        }
        return dog;
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
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
        , value_(obj.value) {
    }
    
    model::LostObject Restore() const {
        return model::LostObject{type_, {pos_.x, pos_.y}, value_, id_};
    }
    
    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
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

// GameSessionRepr - для сериализации игровой сессии
class GameSessionRepr {
public:
    GameSessionRepr() = default;
    
    explicit GameSessionRepr(const model::GameSession& session)
        : map_id_(*session.GetMap()->GetId())
        , next_loot_id_(session.GetNextLootId()) {
        for (const auto& dog : session.GetDogs()) {
            dogs_.push_back(DogRepr(*dog));
        }
        for (const auto& obj : session.GetLostObjects()) {
            lost_objects_.push_back(LostObjectRepr(obj));
        }
    }
    
    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& map_id_;
        ar& dogs_;
        ar& lost_objects_;
        ar& next_loot_id_;
    }
    
    void Restore(model::Game& game, 
                 std::unordered_map<uint64_t, model::Dog*>& dog_id_map) const {
        auto* map = game.FindMap(model::Map::Id{map_id_});
        if (!map) {
            throw std::runtime_error("Map not found: " + map_id_);
        }
        
        auto& session = game.FindOrCreateSession(map);
        session.SetNextLootId(next_loot_id_);
        
        for (const auto& dog_repr : dogs_) {
            auto dog = dog_repr.Restore();
            uint64_t dog_id = dog_repr.GetDogId();
            session.RestoreDog(std::move(dog));
            // Сохраняем соответствие ID -> указатель на собаку
            // Нужно получить указатель на последнюю добавленную собаку
            const auto& dogs = session.GetDogs();
            if (!dogs.empty()) {
                dog_id_map[dog_id] = dogs.back().get();
            }
        }
        
        for (const auto& obj_repr : lost_objects_) {
            session.AddLostObject(obj_repr.Restore());
        }
    }
    
private:
    std::string map_id_;
    std::vector<DogRepr> dogs_;
    std::vector<LostObjectRepr> lost_objects_;
    size_t next_loot_id_ = 0;
};

// PlayerTokenRepr - для сериализации токенов
struct PlayerTokenRepr {
    std::string token;
    uint64_t player_id;
    uint64_t dog_id;
    std::string map_id;
    
    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& token;
        ar& player_id;
        ar& dog_id;
        ar& map_id;
    }
};

// GameStateRepr - полное состояние игры
class GameStateRepr {
public:
    GameStateRepr() = default;
    
    explicit GameStateRepr(const model::Game& game, 
                          const std::unordered_map<std::string, model::Player*>& tokens)
        : next_player_id_(0) {
        for (const auto& [map, session] : game.GetSessions()) {
            sessions_.push_back(GameSessionRepr(*session));
        }
        
        for (const auto& [token, player] : tokens) {
            PlayerTokenRepr repr;
            repr.token = token;
            repr.player_id = player->GetId();
            repr.dog_id = *player->GetDog()->GetId();
            repr.map_id = *player->GetSession()->GetMap()->GetId();
            tokens_.push_back(repr);
        }
    }
    
    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& sessions_;
        ar& tokens_;
        ar& next_player_id_;
    }
    
    void Restore(model::Game& game, 
                 std::unordered_map<std::string, model::Player*>& tokens) const {
        // Сначала восстанавливаем сессии и собак
        std::unordered_map<uint64_t, model::Dog*> dog_id_map;
        for (const auto& session_repr : sessions_) {
            session_repr.Restore(game, dog_id_map);
        }
        
        // Теперь восстанавливаем токены
        // Нам нужно найти игрока по карте и ID собаки
        for (const auto& token_repr : tokens_) {
            // Ищем сессию по карте
            const auto* map = game.FindMap(model::Map::Id{token_repr.map_id});
            if (!map) {
                throw std::runtime_error("Map not found for token: " + token_repr.map_id);
            }
            
            auto& session = game.FindOrCreateSession(map);
            
            // Ищем игрока в сессии по ID собаки
            model::Player* found_player = nullptr;
            for (auto* player : session.GetPlayers()) {
                if (*player->GetDog()->GetId() == token_repr.dog_id) {
                    found_player = player;
                    break;
                }
            }
            
            if (!found_player) {
                // Если игрок не найден, возможно он еще не создан
                // Создаем нового игрока для этой собаки
                const auto& dogs = session.GetDogs();
                for (const auto& dog_ptr : dogs) {
                    if (*dog_ptr->GetId() == token_repr.dog_id) {
                        auto& player = session.AddPlayer(*dog_ptr);
                        found_player = &player;
                        break;
                    }
                }
            }
            
            if (found_player) {
                tokens[token_repr.token] = found_player;
            } else {
                throw std::runtime_error("Failed to restore player for token: " + token_repr.token);
            }
        }
    }
    
private:
    std::vector<GameSessionRepr> sessions_;
    std::vector<PlayerTokenRepr> tokens_;
    uint64_t next_player_id_ = 0;
};

}  // namespace serialization