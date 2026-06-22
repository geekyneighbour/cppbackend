#include "collision_detector.h"
#include <cmath>
#include <algorithm>
#include <cassert>

namespace collision_detector {

struct CollectionResult {
    bool IsCollected(double collect_radius) const {
        return proj_ratio >= 0 &&
               proj_ratio <= 1 &&
               sq_distance <= collect_radius * collect_radius;
    }

    double sq_distance;
    double proj_ratio;
};

CollectionResult TryCollectPoint(Point a, Point b, Point c) {
    assert(b.x != a.x || b.y != a.y);
    
    const double u_x = c.x - a.x;
    const double u_y = c.y - a.y;
    const double v_x = b.x - a.x;
    const double v_y = b.y - a.y;
    const double u_dot_v = u_x * v_x + u_y * v_y;
    const double u_len2 = u_x * u_x + u_y * u_y;
    const double v_len2 = v_x * v_x + v_y * v_y;
    const double proj_ratio = u_dot_v / v_len2;
    const double sq_distance = u_len2 - (u_dot_v * u_dot_v) / v_len2;

    return {sq_distance, proj_ratio};
}

std::vector<GatheringEvent> FindGatherEvents(const ItemGathererProvider& provider) {
    std::vector<GatheringEvent> events;
    
    size_t gatherer_count = provider.GatherersCount();
    size_t item_count = provider.ItemsCount();
    
    if (gatherer_count == 0 || item_count == 0) {
        return events;
    }
    
    for (size_t g = 0; g < gatherer_count; ++g) {
        auto gatherer = provider.GetGatherer(g);
        
        // Если собиратель не двигался, пропускаем
        if (gatherer.start_pos.x == gatherer.end_pos.x && 
            gatherer.start_pos.y == gatherer.end_pos.y) {
            continue;
        }
        
        for (size_t i = 0; i < item_count; ++i) {
            auto item = provider.GetItem(i);
            

            double total_radius = (gatherer.width + item.width) / 2.0;
            

            auto result = TryCollectPoint(
                {gatherer.start_pos.x, gatherer.start_pos.y},
                {gatherer.end_pos.x, gatherer.end_pos.y},
                {item.position.x, item.position.y}
            );
            
            if (result.IsCollected(total_radius)) {
                events.push_back({
                    i,  // item_id
                    g,  // gatherer_id
                    result.sq_distance,
                    result.proj_ratio
                });
            }
        }
    }
    

    std::sort(events.begin(), events.end(), 
        [](const GatheringEvent& a, const GatheringEvent& b) {
            if (a.time != b.time) return a.time < b.time;
            if (a.gatherer_id != b.gatherer_id) return a.gatherer_id < b.gatherer_id;
            if (a.item_id != b.item_id) return a.item_id < b.item_id;
            return a.sq_distance < b.sq_distance;
        });
    
    return events;
}

} // namespace collision_detector