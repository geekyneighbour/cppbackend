#pragma once

#include <chrono>
#include <functional>

namespace loot_gen {

class LootGenerator {
public:
    using RandomGenerator = std::function<double()>;
    using TimeInterval = std::chrono::milliseconds;

    LootGenerator(TimeInterval base_interval, double probability,
                  RandomGenerator random_gen = DefaultGenerator);

    unsigned Generate(TimeInterval time_delta, unsigned loot_count, unsigned looter_count);

private:
    static double DefaultGenerator() noexcept {
        return 1.0;
    }

    TimeInterval base_interval_;
    double probability_;
    RandomGenerator random_gen_;
    TimeInterval time_without_loot_{0};
};

}  // namespace loot_gen