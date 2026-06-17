// geom.h
#pragma once

#include <algorithm>
#include <cmath>

namespace geom {

template <typename T>
struct Point2D {
    T x, y;
};

template <typename T>
struct Size2D {
    T width, height;
};

template <typename T>
struct Rect {
    Point2D<T> pos;
    Size2D<T> size;
};

template <typename T>
bool IsPointInRect(const Point2D<T>& point, const Rect<T>& rect) {
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.size.width &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.size.height;
}

template <typename T>
T Distance(const Point2D<T>& a, const Point2D<T>& b) {
    T dx = a.x - b.x;
    T dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace geom