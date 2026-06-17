// loot_generator.cpp
#include "loot_generator.h"

namespace loot_gen {

LootGenerator::LootGenerator(TimeInterval base_interval, double probability,
                             RandomGenerator random_gen)
    : base_interval_(base_interval)
    , probability_(probability)
    , random_gen_(std::move(random_gen)) {
}

unsigned LootGenerator::Generate(TimeInterval time_delta, unsigned loot_count, unsigned looter_count) {
    if (looter_count == 0) {
        return 0;
    }

    if (loot_count >= looter_count) {
        return 0;
    }


    unsigned max_loot = looter_count;
    unsigned available_slots = max_loot - loot_count;

    if (available_slots == 0) {
        return 0;
    }


    double intervals = static_cast<double>(time_delta.count()) / static_cast<double>(base_interval_.count());
    

    double prob = 1.0 - std::pow(1.0 - probability_, intervals);
    
    unsigned generated = 0;
    for (unsigned i = 0; i < available_slots; ++i) {
        if (random_gen_() < prob) {
            ++generated;
        }
    }
    
    return generated;
}

} // namespace loot_gen