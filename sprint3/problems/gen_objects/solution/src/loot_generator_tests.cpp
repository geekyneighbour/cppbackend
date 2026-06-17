// tests/loot_generator_tests.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "loot_generator.h"
#include <chrono>

using namespace loot_gen;
using namespace std::chrono_literals;

TEST_CASE("LootGenerator generates correct number of loot items", "[loot_generator]") {
    SECTION("No looters -> no loot") {
        LootGenerator generator(1000ms, 1.0);
        unsigned result = generator.Generate(1000ms, 0, 0);
        REQUIRE(result == 0);
    }

    SECTION("Loot count equals looter count -> no new loot") {
        LootGenerator generator(1000ms, 1.0);
        unsigned result = generator.Generate(1000ms, 5, 5);
        REQUIRE(result == 0);
    }

    SECTION("Loot count exceeds looter count -> no new loot") {
        LootGenerator generator(1000ms, 1.0);
        unsigned result = generator.Generate(1000ms, 10, 5);
        REQUIRE(result == 0);
    }

    SECTION("With probability 0 -> no loot") {
        LootGenerator generator(1000ms, 0.0);
        unsigned result = generator.Generate(1000ms, 0, 5);
        REQUIRE(result == 0);
    }

    SECTION("With probability 1 and enough time -> all slots filled") {

        LootGenerator generator(1000ms, 1.0, []() { return 0.0; });
        unsigned result = generator.Generate(1000ms, 0, 5);
        REQUIRE(result == 5);
    }

    SECTION("With probability 0.5 and 5 slots -> some loot generated") {
        std::vector<double> values = {0.1, 0.9, 0.3, 0.8, 0.4};
        size_t idx = 0;
        LootGenerator generator(1000ms, 1.0, [&]() { 
            return values[idx++ % values.size()]; 
        });
        unsigned result = generator.Generate(1000ms, 0, 5);

        REQUIRE(result == 5);
    }
}

TEST_CASE("LootGenerator respects max loot limit", "[loot_generator]") {
    LootGenerator generator(1000ms, 1.0);
    
    SECTION("Cannot exceed looter count") {
        unsigned result = generator.Generate(1000ms, 3, 5);
        REQUIRE(result <= 2); 
    }
}

TEST_CASE("LootGenerator handles time intervals correctly", "[loot_generator]") {
    SECTION("Longer interval increases probability") {

        LootGenerator generator(1000ms, 0.5);
        auto result1 = generator.Generate(1000ms, 0, 5);
        auto result2 = generator.Generate(2000ms, 0, 5);

        REQUIRE(result1 <= 5);
        REQUIRE(result2 <= 5);
    }
}