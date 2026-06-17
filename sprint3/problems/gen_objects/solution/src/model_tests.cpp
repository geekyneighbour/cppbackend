// tests/model_tests.cpp
#include <catch2/catch_test_macros.hpp>
#include "model.h"

using namespace model;

TEST_CASE("Game can add maps", "[model]") {
    Game game;
    Map map(Map::Id{"test"}, "Test Map");
    game.AddMap(std::move(map));
    
    REQUIRE(game.GetMaps().size() == 1);
    REQUIRE(game.FindMap(Map::Id{"test"}) != nullptr);
}

TEST_CASE("Game finds map by id", "[model]") {
    Game game;
    Map map1(Map::Id{"map1"}, "Map 1");
    Map map2(Map::Id{"map2"}, "Map 2");
    game.AddMap(std::move(map1));
    game.AddMap(std::move(map2));
    
    REQUIRE(game.FindMap(Map::Id{"map1"}) != nullptr);
    REQUIRE(game.FindMap(Map::Id{"map2"}) != nullptr);
    REQUIRE(game.FindMap(Map::Id{"nonexistent"}) == nullptr);
}

TEST_CASE("Game session manages players", "[model]") {
    Game game;
    Map map(Map::Id{"test"}, "Test Map");
    map.AddRoad(Road(Road::HORIZONTAL, Point{0, 0}, 10));
    game.AddMap(std::move(map));
    
    const Map* map_ptr = game.FindMap(Map::Id{"test"});
    auto& session = game.FindOrCreateSession(map_ptr);
    
    auto& dog = session.AddDog("TestDog", false);
    auto& player = session.AddPlayer(dog);
    
    REQUIRE(session.GetPlayers().size() == 1);
    REQUIRE(session.GetPlayers()[0]->GetId() == player.GetId());
    REQUIRE(session.GetPlayers()[0]->GetDog()->GetName() == "TestDog");
}

TEST_CASE("Dog moves correctly", "[model]") {
    Map map(Map::Id{"test"}, "Test Map");
    map.AddRoad(Road(Road::HORIZONTAL, Point{0, 0}, 10));
    map.SetDogSpeed(1.0);
    
    Dog dog("TestDog");
    dog.SetPos(0, 0);
    dog.SetAction("R", 1.0);
    
    dog.UpdatePosition(1.0, map.GetRoads());
    auto pos = dog.GetPos();
    REQUIRE(pos.x == Approx(1.0));
    REQUIRE(pos.y == Approx(0.0));
}

TEST_CASE("Dog stops at road boundaries", "[model]") {
    Map map(Map::Id{"test"}, "Test Map");
    map.AddRoad(Road(Road::HORIZONTAL, Point{0, 0}, 10));
    map.SetDogSpeed(1.0);
    
    Dog dog("TestDog");
    dog.SetPos(9.5, 0);
    dog.SetAction("R", 1.0);
    
    dog.UpdatePosition(1.0, map.GetRoads());
    auto pos = dog.GetPos();
    REQUIRE(pos.x <= 10.0);
    REQUIRE(pos.x >= 9.0);
}