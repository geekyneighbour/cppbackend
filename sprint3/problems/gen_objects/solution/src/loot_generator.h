// loot_generator.h
#pragma once

#include <chrono>
#include <functional>
#include <random>

namespace loot_gen {

class LootGenerator {
public:
    using RandomGenerator = std::function<double()>;
    using TimeInterval = std::chrono::milliseconds;

    static double DefaultGenerator() noexcept {
        static std::mt19937 gen(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(gen);
    }

    LootGenerator(TimeInterval base_interval, double probability,
                  RandomGenerator random_gen = DefaultGenerator);

    unsigned Generate(TimeInterval time_delta, unsigned loot_count, unsigned looter_count);

private:
    TimeInterval base_interval_;
    double probability_;
    RandomGenerator random_gen_;
};

} // namespace loot_gen