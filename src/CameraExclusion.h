#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace CameraExclusion {
struct Rect { float x, y, w, h; };
struct Result { float x, y; bool resolved; };
inline bool valid(const Rect& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.x + r.w) &&
        std::isfinite(r.y + r.h) && r.w > 0 && r.h > 0;
}
// Return an actual overlapping periodic copy, without enumerating a whole world.
inline std::optional<Rect> overlap(float x, float y, float w, float h,
                                   const std::vector<Rect>& zones, float wrapW = 0, float wrapH = 0) {
    for (auto zone : zones) {
        if (!valid(zone)) continue;
        if (wrapW > 0) zone.x += (std::floor((x - zone.x - zone.w) / wrapW) + 1) * wrapW;
        if (wrapH > 0) zone.y += (std::floor((y - zone.y - zone.h) / wrapH) + 1) * wrapH;
        if (x < zone.x + zone.w && x + w > zone.x && y < zone.y + zone.h && y + h > zone.y)
            return zone;
    }
    return std::nullopt;
}
inline Result resolve(float x, float y, float w, float h, const std::vector<Rect>& zones,
                      float wrapW = 0, float wrapH = 0) {
    if (!valid({x, y, w, h})) return {x, y, false};
    if (!std::isfinite(wrapW)) wrapW = 0;
    if (!std::isfinite(wrapH)) wrapH = 0;
    if (!overlap(x, y, w, h, zones, wrapW, wrapH)) return {x, y, true};
    struct Candidate { float x, y; double cost; unsigned order; };
    auto later = [](const Candidate& a, const Candidate& b) {
        return a.cost != b.cost ? a.cost > b.cost : a.order > b.order;
    };
    std::priority_queue<Candidate, std::vector<Candidate>, decltype(later)> pending(later);
    std::set<std::pair<float, float>> visited;
    unsigned order = 0;
    auto push = [&](float cx, float cy) {
        if (!std::isfinite(cx) || !std::isfinite(cy) || !visited.emplace(cx, cy).second) return;
        const double dx = double(cx) - x, dy = double(cy) - y;
        pending.push({cx, cy, dx * dx + dy * dy, order++});
    };
    push(x, y);
    // Each conflicting region offers exits in all four directions. Check combined
    // exits too, rather than oscillating between two neighboring regions.
    for (int attempts = 0; attempts < 256 && !pending.empty(); ++attempts) {
        const auto point = pending.top(); pending.pop();
        const auto hit = overlap(point.x, point.y, w, h, zones, wrapW, wrapH);
        if (!hit) return {point.x, point.y, true};
        push(std::floor(hit->x - w) - 1, point.y);
        push(std::ceil(hit->x + hit->w) + 1, point.y);
        push(point.x, std::floor(hit->y - h) - 1);
        push(point.x, std::ceil(hit->y + hit->h) + 1);
    }
    // A dense set still has a safe exterior on any non-wrapping axis. Exclusion
    // has priority over level bounds, so these positions must not be clamped back.
    float left = std::numeric_limits<float>::infinity(), top = left;
    float right = -left, bottom = -left;
    for (const auto& zone : zones) if (valid(zone)) {
        left = std::min(left, zone.x); right = std::max(right, zone.x + zone.w);
        top = std::min(top, zone.y); bottom = std::max(bottom, zone.y + zone.h);
    }
    Result best{x, y, false}; double cost = std::numeric_limits<double>::infinity();
    auto consider = [&](float cx, float cy) {
        if (overlap(cx, cy, w, h, zones, wrapW, wrapH)) return;
        const double dx = double(cx) - x, dy = double(cy) - y, distance = dx * dx + dy * dy;
        if (distance < cost) { best = {cx, cy, true}; cost = distance; }
    };
    if (!(wrapW > 0)) { consider(std::floor(left - w) - 1, y); consider(std::ceil(right) + 1, y); }
    if (!(wrapH > 0)) { consider(x, std::floor(top - h) - 1); consider(x, std::ceil(bottom) + 1); }
    // Fully wrapped layouts can have no valid view. Keep the existing camera
    // stable rather than looping or allowing unbounded camera movement.
    return best;
}
}
