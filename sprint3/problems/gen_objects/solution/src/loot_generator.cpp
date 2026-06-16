#include "loot_generator.h"

#include <algorithm>
#include <cmath>

namespace loot_gen {

unsigned LootGenerator::Generate(TimeInterval time_delta,
                                 unsigned loot_count,
                                 unsigned looter_count)
{
    time_without_loot_ += time_delta;

    unsigned generated = 0;

    while (time_without_loot_ >= base_interval_) {
        time_without_loot_ -= base_interval_;

        if (random_generator_() < probability_) {
            ++generated;
        }
    }

    unsigned loot_shortage =
        (loot_count < looter_count)
        ? (looter_count - loot_count)
        : 0;

    if (generated > loot_shortage) {
        generated = loot_shortage;
    }

    return generated;
}
}

} // namespace loot_gen