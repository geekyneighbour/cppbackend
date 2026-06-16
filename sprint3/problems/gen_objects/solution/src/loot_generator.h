#pragma once
#include <chrono>
#include <functional>

namespace loot_gen {


class LootGenerator {
public:
    using RandomGenerator = std::function<double()>;
    using TimeInterval = std::chrono::milliseconds;

    
    LootGenerator(TimeInterval base_interval, double probability,
                  RandomGenerator random_gen = DefaultGenerator)
        : base_interval_{base_interval}
        , probability_{probability}
        , random_generator_{std::move(random_gen)} {
    }
	
	 void ForceInitialGeneration() {
        time_without_loot_ = base_interval_;
    }

    
    unsigned Generate(TimeInterval time_delta, unsigned loot_count, unsigned looter_count);

private:
    static double DefaultGenerator() noexcept {
        return 1.0;
    };

    TimeInterval base_interval_;
    double probability_;
    RandomGenerator random_generator_;
    TimeInterval time_without_loot_{0}; // Накопленное время с момента последнего спавна трофеев
};

} // namespace loot_gen