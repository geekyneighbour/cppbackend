#include "loot_generator.h"
#include <cmath>

namespace loot_gen {

LootGenerator::LootGenerator(TimeInterval base_interval, double probability, RandomGenerator random_gen)
    : base_interval_(base_interval)
    , probability_(probability)
    , random_gen_(std::move(random_gen)) {
}

unsigned LootGenerator::Generate(TimeInterval time_delta, unsigned loot_count, unsigned looter_count) {
    time_without_loot_ += time_delta;
    if (loot_count >= looter_count) {
        return 0;
    }
    
    double base_interval_ms = static_cast<double>(base_interval_.count());
    double time_delta_ms = static_cast<double>(time_without_loot_.count());
    
    // Вероятность сгенерировать хотя бы один предмет за прошедшее время
    double p = 1.0 - std::pow(1.0 - probability_, time_delta_ms / base_interval_ms);
    double random_value = random_gen_();
    
    if (random_value <= p) {
        unsigned generated_loot = static_cast<unsigned>(std::round(random_value / p));
        unsigned available_slots = looter_count - loot_count;
        unsigned loot_to_add = std::min(generated_loot, available_slots);
        if (loot_to_add > 0) {
            time_without_loot_ = TimeInterval{0};
        }
        return loot_to_add;
    }
    
    return 0;
}

}  // namespace loot_gen