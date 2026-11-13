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
// Sweep the viewport as a point against each region expanded by its dimensions.
// All regions participate in the same earliest-contact calculation.
inline float travel(float x, float y, float dx, float dy, float w, float h,
                    const std::vector<Rect>& zones, float wrapW = 0, float wrapH = 0) {
    float allowed = 1;
    auto axis = [](float p, float delta, float low, float high, float& enter, float& leave) {
        if (std::fabs(delta) < 0.000001f) {
            // Moving along a boundary is legal, including at adjacent tile seams.
            return p > low + 0.0001f && p < high - 0.0001f;
        }
        float a = (low - p) / delta, b = (high - p) / delta;
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a); leave = std::min(leave, b);
        return enter <= leave;
    };
    for (const auto& zone : zones) {
        if (!valid(zone)) continue;
        const int firstX = wrapW > 0 ? static_cast<int>(std::floor((std::min(x, x + dx) - zone.x - zone.w) / wrapW)) : 0;
        const int lastX = wrapW > 0 ? static_cast<int>(std::ceil((std::max(x, x + dx) + w - zone.x) / wrapW)) : 0;
        const int firstY = wrapH > 0 ? static_cast<int>(std::floor((std::min(y, y + dy) - zone.y - zone.h) / wrapH)) : 0;
        const int lastY = wrapH > 0 ? static_cast<int>(std::ceil((std::max(y, y + dy) + h - zone.y) / wrapH)) : 0;
        for (int iy = firstY; iy <= lastY; ++iy) for (int ix = firstX; ix <= lastX; ++ix) {
            const float left = zone.x + ix * wrapW - w, right = zone.x + ix * wrapW + zone.w;
            const float top = zone.y + iy * wrapH - h, bottom = zone.y + iy * wrapH + zone.h;
            float enter = -std::numeric_limits<float>::infinity(), leave = std::numeric_limits<float>::infinity();
            if (!axis(x, dx, left, right, enter, leave) || !axis(y, dy, top, bottom, enter, leave)) continue;
            if (leave < 0 || enter < -0.0001f || enter > allowed) continue;
            allowed = std::max(0.0f, enter);
        }
    }
    return allowed;
}
inline Result slide(float x, float y, float dx, float dy, float w, float h,
                    const std::vector<Rect>& zones, float wrapW = 0, float wrapH = 0) {
    auto permitted = [&](float mx, float my) {
        const float fraction = travel(x, y, mx, my, w, h, zones, wrapW, wrapH);
        const float magnitude = std::max(std::fabs(mx), std::fabs(my));
        const float slop = 0.0005f + 2 * std::numeric_limits<float>::epsilon() * (std::fabs(x) + std::fabs(y) + w + h);
        return fraction < 1 && magnitude > 0 ? std::max(0.0f, fraction - slop / magnitude) : fraction;
    };
    for (int contact = 0; contact < 8; ++contact) {
        if (std::fabs(dx) + std::fabs(dy) < 0.0001f) break;
        const float fraction = permitted(dx, dy);
        x += dx * fraction; y += dy * fraction;
        if (fraction >= 1) break;
        dx *= 1 - fraction; dy *= 1 - fraction;
        // Test tangent movement against the entire union. Internal corners of
        // neighboring objects must not add a second, artificial blocking normal.
        const float alongX = dx * permitted(dx, 0);
        const float alongY = dy * permitted(0, dy);
        if (std::max(std::fabs(alongX), std::fabs(alongY)) < 0.0001f) break;
        if (std::fabs(alongX) > std::fabs(alongY)) { x += alongX; dx -= alongX; }
        else { y += alongY; dy -= alongY; }
    }
    return {x, y, !overlap(x, y, w, h, zones, wrapW, wrapH)};
}
class Controller {
public:
    void reset() { initialized_ = false; }
    Result update(float targetX, float targetY, float w, float h, const std::vector<Rect>& zones,
                  float dt, float wrapW = 0, float wrapH = 0) {
        if (zones.empty()) { reset(); return {targetX, targetY, true}; }
        if (!valid({targetX, targetY, w, h})) return {targetX, targetY, false};
        if (!std::isfinite(wrapW) || wrapW < 0) wrapW = 0;
        if (!std::isfinite(wrapH) || wrapH < 0) wrapH = 0;
        std::vector<Rect> padded;
        padded.reserve(zones.size());
        for (auto zone : zones) if (valid(zone)) {
            zone.x -= 1; zone.y -= 1; zone.w += 2; zone.h += 2;
            padded.push_back(zone);
        }
        // Static recovery is only for initialization or a newly invalid view
        // (e.g. a resized viewport), never for choosing a new side every frame.
        if (!initialized_ || overlap(x_, y_, w, h, padded, wrapW, wrapH)) {
            const auto recovered = resolve(initialized_ ? x_ : targetX, initialized_ ? y_ : targetY,
                w, h, padded, wrapW, wrapH);
            if (!recovered.resolved) return recovered;
            x_ = recovered.x; y_ = recovered.y; initialized_ = true;
        }
        if (wrapW > 0) targetX = x_ + std::remainder(targetX - x_, wrapW);
        if (wrapH > 0) targetY = y_ + std::remainder(targetY - y_, wrapH);
        const float alpha = std::isfinite(dt) && dt > 0 ? -std::expm1(-12.0f * std::min(dt, 0.05f)) : 0;
        const auto moved = slide(x_, y_, (targetX - x_) * alpha, (targetY - y_) * alpha,
            w, h, padded, wrapW, wrapH);
        if (moved.resolved) { x_ = moved.x; y_ = moved.y; }
        return {x_, y_, true};
    }
private:
    bool initialized_ = false;
    float x_ = 0, y_ = 0;
};

}
