#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <catch2/catch_approx.hpp>
#include <sstream>
#include <cmath>
#include <vector>
#include <algorithm>
#include "../src/collision_detector.h"

using namespace collision_detector;

// ============ StringMaker для вывода событий ============
namespace Catch {
template<>
struct StringMaker<GatheringEvent> {
    static std::string convert(GatheringEvent const& value) {
        std::ostringstream tmp;
        tmp << "{" << value.gatherer_id << "," << value.item_id 
            << "," << value.sq_distance << "," << value.time << "}";
        return tmp.str();
    }
};
}

// ============ Тестовый провайдер ============
class TestProvider : public ItemGathererProvider {
public:
    TestProvider() = default;
    
    TestProvider& AddItem(Item item) {
        items_.push_back(item);
        return *this;
    }
    
    TestProvider& AddGatherer(Gatherer gatherer) {
        gatherers_.push_back(gatherer);
        return *this;
    }
    
    size_t ItemsCount() const override {
        return items_.size();
    }
    
    Item GetItem(size_t idx) const override {
        return items_[idx];
    }
    
    size_t GatherersCount() const override {
        return gatherers_.size();
    }
    
    Gatherer GetGatherer(size_t idx) const override {
        return gatherers_[idx];
    }

private:
    std::vector<Item> items_;
    std::vector<Gatherer> gatherers_;
};

// ============ Вспомогательные функции ============
bool CompareEvents(const GatheringEvent& a, const GatheringEvent& b) {
    if (a.time != b.time) return a.time < b.time;
    if (a.gatherer_id != b.gatherer_id) return a.gatherer_id < b.gatherer_id;
    if (a.item_id != b.item_id) return a.item_id < b.item_id;
    return a.sq_distance < b.sq_distance;
}

void SortAndCheckEvents(std::vector<GatheringEvent>& events) {
    std::sort(events.begin(), events.end(), CompareEvents);
}

// ============ ТЕСТЫ ============

SCENARIO("FindGatherEvents with no items or gatherers") {
    GIVEN("an empty provider") {
        TestProvider provider;
        
        THEN("no events are found") {
            auto events = FindGatherEvents(provider);
            REQUIRE(events.empty());
        }
    }
    
    GIVEN("a provider with gatherers but no items") {
        TestProvider provider;
        provider.AddGatherer({{0, 0}, {10, 0}, 1.0});
        provider.AddGatherer({{0, 0}, {0, 10}, 1.0});
        
        THEN("no events are found") {
            auto events = FindGatherEvents(provider);
            REQUIRE(events.empty());
        }
    }
    
    GIVEN("a provider with items but no gatherers") {
        TestProvider provider;
        provider.AddItem({{5, 0}, 0.5});
        provider.AddItem({{0, 5}, 0.5});
        
        THEN("no events are found") {
            auto events = FindGatherEvents(provider);
            REQUIRE(events.empty());
        }
    }
}

SCENARIO("FindGatherEvents detects direct hits") {
    GIVEN("a gatherer moving directly towards an item") {
        TestProvider provider;
        provider.AddItem({{5, 0}, 0.5});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("exactly one event is found") {
                REQUIRE(events.size() == 1);
                REQUIRE(events[0].gatherer_id == 0);
                REQUIRE(events[0].item_id == 0);
                REQUIRE(events[0].time == Catch::Approx(0.5));
                REQUIRE(events[0].sq_distance == Catch::Approx(0.0));
            }
        }
    }
    
    GIVEN("a gatherer moving directly towards an item with offset") {
        TestProvider provider;
        provider.AddItem({{5, 0.8}, 0.3});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.3});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("event is detected when distance <= sum of radii") {
                REQUIRE(events.size() == 1);
                double expected_distance = 0.8 - (0.3 + 0.3);
                REQUIRE(events[0].sq_distance == Catch::Approx(expected_distance * expected_distance));
                REQUIRE(events[0].time == Catch::Approx(0.5));
            }
        }
    }
}

SCENARIO("FindGatherEvents detects diagonal movement") {
    GIVEN("a gatherer moving diagonally") {
        TestProvider provider;
        provider.AddItem({{5, 5}, 0.5});
        provider.AddGatherer({{0, 0}, {10, 10}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("event is detected") {
                REQUIRE(events.size() == 1);
                REQUIRE(events[0].gatherer_id == 0);
                REQUIRE(events[0].item_id == 0);
                REQUIRE(events[0].time == Catch::Approx(0.5));
                REQUIRE(events[0].sq_distance == Catch::Approx(0.0));
            }
        }
    }
}

SCENARIO("FindGatherEvents detects multiple events") {
    GIVEN("multiple items on the path") {
        TestProvider provider;
        provider.AddItem({{2, 0}, 0.5});
        provider.AddItem({{5, 0}, 0.5});
        provider.AddItem({{8, 0}, 0.5});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("all items are collected in order") {
                REQUIRE(events.size() == 3);
                std::sort(events.begin(), events.end(), CompareEvents);
                REQUIRE(events[0].time == Catch::Approx(0.2));
                REQUIRE(events[1].time == Catch::Approx(0.5));
                REQUIRE(events[2].time == Catch::Approx(0.8));
                REQUIRE(events[0].item_id == 0);
                REQUIRE(events[1].item_id == 1);
                REQUIRE(events[2].item_id == 2);
            }
        }
    }
}

SCENARIO("FindGatherEvents handles multiple gatherers") {
    GIVEN("multiple gatherers and items") {
        TestProvider provider;
        provider.AddItem({{3, 0}, 0.5});
        provider.AddItem({{7, 0}, 0.5});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.5});
        provider.AddGatherer({{10, 10}, {0, 10}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            SortAndCheckEvents(events);
            
            THEN("events are properly assigned") {
                REQUIRE(events.size() == 2);
                REQUIRE(events[0].gatherer_id == 0);
                REQUIRE(events[1].gatherer_id == 0);
            }
        }
    }
}

SCENARIO("FindGatherEvents handles items outside path") {
    GIVEN("items outside the gatherer's path") {
        TestProvider provider;
        provider.AddItem({{5, 2.0}, 0.3});
        provider.AddItem({{5, -2.0}, 0.3});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.3});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("no events are detected for items outside range") {
                REQUIRE(events.empty());
            }
        }
    }
    
    GIVEN("items just beyond reach") {
        TestProvider provider;
        provider.AddItem({{5, 0.61}, 0.3});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.3});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("items outside the radius are not detected") {
                REQUIRE(events.empty());
            }
        }
    }
}

SCENARIO("FindGatherEvents handles stationary gatherer") {
    GIVEN("a gatherer that doesn't move") {
        TestProvider provider;
        provider.AddItem({{5, 0}, 0.5});
        provider.AddGatherer({{0, 0}, {0, 0}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("no events are detected") {
                REQUIRE(events.empty());
            }
        }
    }
}

SCENARIO("FindGatherEvents handles edge cases") {
    GIVEN("an item exactly at the gatherer's start position") {
        TestProvider provider;
        provider.AddItem({{0, 0}, 0.5});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("event is detected at time 0") {
                REQUIRE(events.size() == 1);
                REQUIRE(events[0].time == Catch::Approx(0.0));
                REQUIRE(events[0].sq_distance == Catch::Approx(0.0));
            }
        }
    }
    
    GIVEN("an item exactly at the gatherer's end position") {
        TestProvider provider;
        provider.AddItem({{10, 0}, 0.5});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("event is detected at time 1") {
                REQUIRE(events.size() == 1);
                REQUIRE(events[0].time == Catch::Approx(1.0));
                REQUIRE(events[0].sq_distance == Catch::Approx(0.0));
            }
        }
    }
}

SCENARIO("FindGatherEvents handles zero width items and gatherers") {
    GIVEN("zero width item and gatherer") {
        TestProvider provider;
        provider.AddItem({{5, 0}, 0.0});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.0});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("event is detected when exactly on the line") {
                REQUIRE(events.size() == 1);
                REQUIRE(events[0].time == Catch::Approx(0.5));
                REQUIRE(events[0].sq_distance == Catch::Approx(0.0));
            }
        }
    }
}

SCENARIO("FindGatherEvents verifies chronological order") {
    GIVEN("items at various positions along the path") {
        TestProvider provider;
        provider.AddItem({{1, 0}, 0.5});
        provider.AddItem({{3, 0}, 0.5});
        provider.AddItem({{6, 0}, 0.5});
        provider.AddItem({{9, 0}, 0.5});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.5});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("events are in chronological order") {
                REQUIRE(events.size() == 4);
                for (size_t i = 1; i < events.size(); ++i) {
                    REQUIRE(events[i-1].time <= events[i].time);
                }
            }
        }
    }
}

SCENARIO("FindGatherEvents handles overlapping items") {
    GIVEN("items at the same position") {
        TestProvider provider;
        provider.AddItem({{5, 0}, 0.3});
        provider.AddItem({{5, 0}, 0.4});
        provider.AddGatherer({{0, 0}, {10, 0}, 0.3});
        
        WHEN("finding events") {
            auto events = FindGatherEvents(provider);
            
            THEN("both items are detected") {
                REQUIRE(events.size() == 2);
                REQUIRE(events[0].item_id != events[1].item_id);
                REQUIRE(events[0].time == events[1].time);
            }
        }
    }
}