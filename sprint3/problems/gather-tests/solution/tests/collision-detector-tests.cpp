#define CATCH_CONFIG_RUNNER
#define _USE_MATH_DEFINES
#include <cassert>
#include <catch2/catch_all.hpp>
#include <vector>

#include "../src/collision_detector.h"

class TestGathererProvider : public collision_detector::ItemGathererProvider {
public:
  TestGathererProvider() = default;

public:
  void AddItem(collision_detector::Item item) { items_.push_back(item); }

  void AddGatherer(collision_detector::Gatherer gatherer) {
    gatherers_.push_back(gatherer);
  }
  size_t ItemsCount() const override { return items_.size(); }

  collision_detector::Item GetItem(size_t idx) const override {
    assert(idx >= 0 || idx < ItemsCount());
    return items_.at(idx);
  }
  size_t GatherersCount() const override { return gatherers_.size(); }

  collision_detector::Gatherer GetGatherer(size_t idx) const override {
    assert(idx >= 0 || idx < GatherersCount());
    return gatherers_.at(idx);
  }

private:
  std::vector<collision_detector::Item> items_;
  std::vector<collision_detector::Gatherer> gatherers_;
};

int main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }

TEST_CASE("FindGatherEvents returns empty vector when no gatherers") {
  TestGathererProvider provider;
  auto events = collision_detector::FindGatherEvents(provider);
  REQUIRE(events.empty());
}

TEST_CASE("FindGatherEvents returns empty when no items") {
  TestGathererProvider provider;

  provider.AddGatherer(
      {.start_pos = {0.0, 0.0}, .end_pos = {10.0, 0.0}, .width = 1.0});

  auto events = collision_detector::FindGatherEvents(provider);
  REQUIRE(events.empty());
}

TEST_CASE("FindGatherEvents detects item collection") {
  TestGathererProvider provider;

  provider.AddItem({.position = {5.0, 0.0}, .width = 0.5});

  provider.AddGatherer(
      {.start_pos = {0.0, 0.0}, .end_pos = {10.0, 0.0}, .width = 1.0});

  auto events = collision_detector::FindGatherEvents(provider);
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].gatherer_id == 0);
  REQUIRE(events[0].item_id == 0);

  REQUIRE(events[0].time >= 0.0);
  REQUIRE(events[0].time <= 1.0);
}

TEST_CASE("FindGatherEvents handles multiple gatherers and items") {
  TestGathererProvider provider;

  provider.AddItem({.position = {3.0, 0.0}, .width = 0.5});
  provider.AddItem({.position = {7.0, 0.0}, .width = 0.5});
  provider.AddItem({.position = {10.0, 0.0}, .width = 0.5});

  provider.AddGatherer(
      {.start_pos = {0.0, 0.0}, .end_pos = {5.0, 0.0}, .width = 1.0});
  provider.AddGatherer(
      {.start_pos = {5.0, 0.0}, .end_pos = {12.0, 0.0}, .width = 1.0});

  auto events = collision_detector::FindGatherEvents(provider);

  REQUIRE(events.size() == 3);
}

TEST_CASE("FindGatherEvents ignores items outside gatherer path") {
  TestGathererProvider provider;

  provider.AddItem({.position = {5.0, 10.0}, .width = 0.5});

  provider.AddGatherer(
      {.start_pos = {0.0, 0.0}, .end_pos = {10.0, 0.0}, .width = 1.0});

  auto events = collision_detector::FindGatherEvents(provider);
  REQUIRE(events.empty());
}
