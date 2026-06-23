#include "collision_detector.h"
#include <cassert>
#include <algorithm>
#include <cmath>

namespace collision_detector {

CollectionResult TryCollectPoint(geom::Point2D a, geom::Point2D b, geom::Point2D c) {
    // Проверим, что перемещение ненулевое.
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

    return CollectionResult(sq_distance, proj_ratio);
}

std::vector<GatheringEvent> FindGatherEvents(const ItemGathererProvider& provider) {
    std::vector<GatheringEvent> detected_events;

    for (size_t g = 0; g < provider.GatherersCount(); ++g) {
        Gatherer gatherer = provider.GetGatherer(g);
        
        // Если собиратель не двигался, пропускаем
        if (gatherer.start_pos.x == gatherer.end_pos.x && 
            gatherer.start_pos.y == gatherer.end_pos.y) {
            continue;
        }
        
        for (size_t i = 0; i < provider.ItemsCount(); ++i) {
            Item item = provider.GetItem(i);
            auto collect_result = TryCollectPoint(
                gatherer.start_pos, 
                gatherer.end_pos, 
                item.position
            );

            // Проверяем, что проекция попадает на отрезок
            if (collect_result.proj_ratio < 0 || collect_result.proj_ratio > 1) {
                continue;
            }
            
            // Расстояние от центра предмета до линии движения
            double dist_to_center = std::sqrt(collect_result.sq_distance);
            
            // Сумма радиусов (половина от ширины)
            double sum_radii = (gatherer.width + item.width) / 2.0;
            
            // Если расстояние от центра до линии <= суммы радиусов,
            // то предмет пересекается с собирателем
            if (dist_to_center <= sum_radii) {
                // sq_distance должна хранить квадрат расстояния от КРАЯ предмета до КРАЯ собирателя
                double sq_distance_from_edge = (dist_to_center - sum_radii) * 
                                               (dist_to_center - sum_radii);
                
                detected_events.push_back({
                    i,           // item_id
                    g,           // gatherer_id
                    sq_distance_from_edge,  // расстояние от края до края в квадрате
                    collect_result.proj_ratio  // time
                });
            }
        }
    }

    std::sort(detected_events.begin(), detected_events.end(),
              [](const GatheringEvent& a, const GatheringEvent& b) {
                  return a.time < b.time;
              });

    return detected_events;
}

}  // namespace collision_detector