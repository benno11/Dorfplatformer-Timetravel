#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace CameraExclusion {
struct Rect { float x, y, w, h; int diagonal = 0; };
struct Plane { float x, y, limit; };
struct Result { float x, y; bool resolved; };
inline bool valid(const Rect& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.x + r.w) &&
        std::isfinite(r.y + r.h) && r.w > 0 && r.h > 0 && r.diagonal >= 0 && r.diagonal <= 4;
}
// Configuration-space planes for a viewport against a rectangle or triangle.
struct Planes {
    std::array<Plane, 5> items{};
    int count = 4;
    const Plane* begin() const { return items.data(); }
    const Plane* end() const { return items.data() + count; }
    const Plane& back() const { return items[count - 1]; }
};
inline Planes planes(const Rect& r, float w, float h) {
    Planes result{{{{-1, 0, w - r.x}, {1, 0, r.x + r.w},
        {0, -1, h - r.y}, {0, 1, r.y + r.h}, {0, 0, 0}}}, 4};
    if (r.diagonal) {
        const float nx = (r.diagonal == 1 || r.diagonal == 4) ? -r.h : r.h;
        const float ny = (r.diagonal == 1 || r.diagonal == 2) ? -r.w : r.w;
        float limit = nx * r.x + ny * r.y;
        if (r.diagonal == 1) limit -= r.w * r.h;
        if (r.diagonal == 3) limit += r.w * r.h;
        limit += std::max(0.0f, -nx * w) + std::max(0.0f, -ny * h);
        const float length = std::hypot(nx, ny);
        result.items[result.count++] = {nx / length, ny / length, limit / length};
    }
    return result;
}
template<class Visit> inline void copies(const Rect& r, float x, float y, float w, float h,
                                         float dx, float dy, float wrapW, float wrapH, Visit visit) {
    const int firstX = wrapW > 0 ? static_cast<int>(std::floor((std::min(x, x + dx) - r.x - r.w) / wrapW)) : 0;
    const int lastX = wrapW > 0 ? static_cast<int>(std::ceil((std::max(x, x + dx) + w - r.x) / wrapW)) : 0;
    const int firstY = wrapH > 0 ? static_cast<int>(std::floor((std::min(y, y + dy) - r.y - r.h) / wrapH)) : 0;
    const int lastY = wrapH > 0 ? static_cast<int>(std::ceil((std::max(y, y + dy) + h - r.y) / wrapH)) : 0;
    for (int iy = firstY; iy <= lastY; ++iy) for (int ix = firstX; ix <= lastX; ++ix) {
        auto copy = r; copy.x += ix * wrapW; copy.y += iy * wrapH;
        if (!visit(copy)) return;
    }
}
inline std::optional<Rect> overlap(float x, float y, float w, float h,
                                   const std::vector<Rect>& zones, float wrapW = 0, float wrapH = 0) {
    for (const auto& zone : zones) {
        if (!valid(zone)) continue;
        std::optional<Rect> found;
        copies(zone, x, y, w, h, 0, 0, wrapW, wrapH, [&](const Rect& copy) {
            bool inside = true;
            for (const auto& plane : planes(copy, w, h))
                if (plane.x * x + plane.y * y >= plane.limit) { inside = false; break; }
            if (inside) found = copy;
            return !inside;
        });
        if (found) return found;
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
        if (hit->diagonal) {
            const auto diagonal = planes(*hit, w, h).back();
            const float distance = diagonal.limit - diagonal.x * point.x - diagonal.y * point.y + 1;
            push(point.x + diagonal.x * distance, point.y + diagonal.y * distance);
        }
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
struct Hit {
    float time = 1;
    std::array<Plane, 8> normals{};
    int count = 0;
    void add(const Plane& normal) {
        for (int i = 0; i < count; ++i)
            if (std::fabs(normals[i].x - normal.x) + std::fabs(normals[i].y - normal.y) < 0.0001f) return;
        if (count < static_cast<int>(normals.size())) normals[count++] = normal;
    }
};
inline Hit sweep(float x, float y, float dx, float dy, float w, float h,
                 const std::vector<Rect>& zones, float wrapW = 0, float wrapH = 0) {
    Hit result;
    for (const auto& zone : zones) {
        if (!valid(zone)) continue;
        copies(zone, x, y, w, h, dx, dy, wrapW, wrapH, [&](const Rect& copy) {
            float enter = -std::numeric_limits<float>::infinity(), leave = std::numeric_limits<float>::infinity();
            Hit contact;
            for (const auto& plane : planes(copy, w, h)) {
                const float distance = plane.limit - plane.x * x - plane.y * y;
                const float speed = plane.x * dx + plane.y * dy;
                if (std::fabs(speed) < 0.000001f) {
                    if (distance <= 0.0001f) return true; // A tangent does not enter the region.
                    continue;
                }
                const float t = distance / speed;
                if (speed > 0) leave = std::min(leave, t);
                else {
                    if (t > enter + 0.00001f) { enter = t; contact.count = 0; contact.add(plane); }
                    else if (std::fabs(t - enter) <= 0.00001f) contact.add(plane);
                }
                if (enter > leave) return true;
            }
            if (leave < 0 || enter < -0.0001f || enter > result.time + 0.00001f) return true;
            if (enter < result.time - 0.00001f) { result.time = std::max(0.0f, enter); result.count = 0; }
            for (int i = 0; i < contact.count; ++i) result.add(contact.normals[i]);
            return true;
        });
    }
    return result;
}
inline float travel(float x, float y, float dx, float dy, float w, float h,
                    const std::vector<Rect>& zones, float wrapW = 0, float wrapH = 0) {
    return sweep(x, y, dx, dy, w, h, zones, wrapW, wrapH).time;
}
inline Result slide(float x, float y, float dx, float dy, float w, float h,
                    const std::vector<Rect>& zones, float wrapW = 0, float wrapH = 0) {
    auto safeFraction = [&](float fraction, float mx, float my) {
        const float magnitude = std::max(std::fabs(mx), std::fabs(my));
        const float slop = 0.0005f + 2 * std::numeric_limits<float>::epsilon() * (std::fabs(x) + std::fabs(y) + w + h);
        return fraction < 1 && magnitude > 0 ? std::max(0.0f, fraction - slop / magnitude) : fraction;
    };
    for (int iteration = 0; iteration < 8; ++iteration) {
        if (std::fabs(dx) + std::fabs(dy) < 0.0001f) break;
        const auto hit = sweep(x, y, dx, dy, w, h, zones, wrapW, wrapH);
        const float fraction = safeFraction(hit.time, dx, dy);
        x += dx * fraction; y += dy * fraction;
        if (fraction >= 1) break;
        dx *= 1 - fraction; dy *= 1 - fraction;
        float bestX = 0, bestY = 0, bestProgress = 0.000001f;
        auto consider = [&](float mx, float my) {
            const float allowed = safeFraction(travel(x, y, mx, my, w, h, zones, wrapW, wrapH), mx, my);
            mx *= allowed; my *= allowed;
            const float progress = mx * dx + my * dy;
            if (progress > bestProgress) { bestProgress = progress; bestX = mx; bestY = my; }
        };
        // Candidate tangents include slanted edges, and every candidate is swept
        // against the whole union to reject internal faces and blocked corners.
        for (int i = 0; i < hit.count; ++i) {
            const auto& n = hit.normals[i];
            const float normal = n.x * dx + n.y * dy;
            consider(dx - n.x * normal, dy - n.y * normal);
        }
        consider(dx, 0); consider(0, dy);
        if (bestX == 0 && bestY == 0) break;
        x += bestX; y += bestY; dx -= bestX; dy -= bestY;
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
        // A wrap toggle changes the set of visible copies immediately. Rebase
        // onto the new target before it can retain an invalid old camera image.
        if (initialized_ && (wrapW != wrapW_ || wrapH != wrapH_)) reset();
        wrapW_ = wrapW; wrapH_ = wrapH;
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
        const float alpha = std::isfinite(dt) && dt > 0 ? 1.0f : 0.0f;
        const auto moved = slide(x_, y_, (targetX - x_) * alpha, (targetY - y_) * alpha,
            w, h, padded, wrapW, wrapH);
        if (moved.resolved) { x_ = moved.x; y_ = moved.y; }
        return {x_, y_, true};
    }
private:
    bool initialized_ = false;
    float x_ = 0, y_ = 0;
    float wrapW_ = 0, wrapH_ = 0;
};

}
