#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace CharacterCreator {

// A head-relative strand simulated in world directions, independent of animation frames.
// Fixed storage also supports uncapped hairstyles without allocating a joint per pixel.
struct HairPhysics {
    struct Point { float x = 0, y = 0; };
    struct Box {
        float x, y, w, h;
        bool body = false;
        bool oneWay = false;
    };
    struct CollisionScene {
        std::vector<Box> boxes;
        Point root{0, 0};
        float radius = 0.75f;
        int bodySide = 0;

        bool blocksWorld(float x, float y, float w, float h) const {
            for (const auto& box : boxes)
                if (!box.body && x < box.x + box.w && x + w > box.x && y < box.y + box.h && y + h > box.y)
                    return true;
            return false;
        }

        Point resolve(Point from, Point target, bool worldOnly = false) const {
            from.x += root.x; from.y += root.y;
            target.x += root.x; target.y += root.y;
            if (!worldOnly) {
                for (const auto& box : boxes) {
                    if (!box.body) continue;
                    const float left = box.x - radius, right = box.x + box.w + radius;
                    if (target.y <= box.y - radius || target.y >= box.y + box.h + radius ||
                        target.x <= left || target.x >= right) continue;
                    // The strand attaches beside the head. Escape toward that side rather
                    // than its nearest top face, which can pin every joint above the head.
                    const int side = bodySide ? bodySide : (root.x < box.x + box.w * 0.5f ? -1 : 1);
                    target.x = side < 0 ? left - 0.001f : right + 0.001f;
                }
            }
            // Sweep against expanded boxes, then slide along the contacted surface.
            for (int pass = 0; pass < 3; ++pass) {
                float earliest = 1;
                Point normal{};
                bool embedded = false;
                for (const auto& box : boxes) {
                    if (box.body) continue;
                    const float left = box.x - radius, right = box.x + box.w + radius;
                    const float top = box.y - radius, bottom = box.y + box.h + radius;
                    if (box.oneWay && (from.y > top + 0.01f || target.y < from.y)) continue;
                    if (from.x > left && from.x < right && from.y > top && from.y < bottom) {
                        if (target.x <= left || target.x >= right || target.y <= top || target.y >= bottom) continue;
                        const float distances[]{target.x - left, right - target.x, target.y - top, bottom - target.y};
                        const int side = (int)(std::min_element(distances, distances + 4) - distances);
                        if (side == 0) target.x = left - 0.001f;
                        else if (side == 1) target.x = right + 0.001f;
                        else if (side == 2) target.y = top - 0.001f;
                        else target.y = bottom + 0.001f;
                        from = target;
                        embedded = true;
                        break;
                    }
                    const Point delta{target.x - from.x, target.y - from.y};
                    float enter = 0, leave = 1;
                    Point hitNormal{};
                    auto slab = [&](float start, float movement, float low, float high, Point lowNormal) {
                        if (std::fabs(movement) < 0.000001f) return start >= low && start <= high;
                        float entryTime = (low - start) / movement, exitTime = (high - start) / movement;
                        if (entryTime > exitTime) { std::swap(entryTime, exitTime); lowNormal.x = -lowNormal.x; lowNormal.y = -lowNormal.y; }
                        if (entryTime >= enter) { enter = entryTime; hitNormal = lowNormal; }
                        leave = std::min(leave, exitTime);
                        return enter <= leave;
                    };
                    if (!slab(from.x, delta.x, left, right, {-1, 0}) ||
                        !slab(from.y, delta.y, top, bottom, {0, -1}) || enter < 0 || enter > earliest || leave < 0) continue;
                    if (hitNormal.x == 0 && hitNormal.y == 0) continue;
                    earliest = enter;
                    normal = hitNormal;
                }
                if (embedded) continue;
                if (normal.x == 0 && normal.y == 0) break;
                Point contact{from.x + (target.x - from.x) * earliest + normal.x * 0.001f,
                    from.y + (target.y - from.y) * earliest + normal.y * 0.001f};
                Point remainder{target.x - contact.x, target.y - contact.y};
                if (normal.x) remainder.x = 0;
                if (normal.y) remainder.y = 0;
                from = contact;
                target = {contact.x + remainder.x, contact.y + remainder.y};
            }
            return {target.x - root.x, target.y - root.y};
        }
    };
    static constexpr int jointCount = 12;
    static constexpr float spacing = 2.0f;
    static constexpr float reach = (jointCount - 1) * spacing;
    std::array<Point, jointCount> joints{}, previous{};
    float previousVx = 0, previousVy = 0;
    float stretch = 0, stretchVelocity = 0;
    bool initialized = false;

    void reset() {
        for (int i = 0; i < jointCount; ++i) joints[i] = previous[i] = {0, i * spacing};
        initialized = false;
        stretch = stretchVelocity = 0;
    }

    HairPhysics() { reset(); }

    void update(float dt, float vx, float vy, float springiness = 1.0f, const CollisionScene* collisions = nullptr) {
        if (!std::isfinite(dt) || !std::isfinite(vx) || !std::isfinite(vy)) {
            reset();
            return;
        }
        if (dt <= 0) return;
        dt = std::min(dt, 1.0f / 30);
        vx = std::clamp(vx, -2000.0f, 2000.0f);
        vy = std::clamp(vy, -2000.0f, 2000.0f);
        springiness = std::isfinite(springiness) ? std::clamp(springiness, 0.75f, 1.25f) : 1.0f;
        if (!initialized) {
            previousVx = vx;
            previousVy = vy;
            initialized = true;
        }
        const float ax = std::clamp((vx - previousVx) / dt, -12000.0f, 12000.0f);
        const float ay = std::clamp((vy - previousVy) / dt, -12000.0f, 12000.0f);
        const int steps = std::max(1, (int)std::ceil(dt * 240));
        const float h = dt / steps;
        const float drag = std::exp(-6.0f * h / springiness);
        float lateralLimit = 2.8f;
        if (collisions && collisions->bodySide) {
            for (const auto& box : collisions->boxes) {
                if (!box.body) continue;
                const float clearance = collisions->bodySide < 0 ? collisions->root.x - box.x + collisions->radius :
                    box.x + box.w + collisions->radius - collisions->root.x;
                lateralLimit = std::max(lateralLimit, clearance + 0.01f);
            }
        }
        for (int step = 0; step < steps; ++step) {
            const float stretchTarget = std::clamp(-ay * 0.0002f - vy * 0.0008f, -2.5f, 2.5f) * springiness;
            stretchVelocity += ((stretchTarget - stretch) * 110 - stretchVelocity * 9 / springiness) * h;
            stretch = std::clamp(stretch + stretchVelocity * h, -3.5f, 3.5f);
            const float elasticSpacing = spacing + stretch / (jointCount - 1);
            for (int i = 1; i < jointCount; ++i) {
                const Point current = joints[i];
                // Inertia and air resistance bend the strand; gravity restores its hanging shape.
                const float fx = (-ax * 0.008f - vx * 0.035f) * springiness - current.x * 4;
                const float fy = 65 - ay * 0.008f - vy * 0.012f + (i * elasticSpacing - current.y) * 18;
                joints[i].x += (current.x - previous[i].x) * drag + fx * h * h;
                joints[i].y += (current.y - previous[i].y) * drag + fy * h * h;
                previous[i] = current;
            }
            for (int iteration = 0; iteration < 8; ++iteration) {
                joints[0] = {};
                for (int i = 1; i < jointCount; ++i) {
                    const float dx = joints[i].x - joints[i - 1].x;
                    const float dy = joints[i].y - joints[i - 1].y;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < 0.00001f) continue;
                    const float correction = (distance - elasticSpacing) / distance;
                    const float weight = i == 1 ? 1.0f : 0.5f;
                    joints[i].x -= dx * correction * weight;
                    joints[i].y -= dy * correction * weight;
                    if (i > 1) {
                        joints[i - 1].x += dx * correction * weight;
                        joints[i - 1].y += dy * correction * weight;
                    }
                }
                // Resist folds that leave neighboring links doubled back on the head.
                // This is a soft bend constraint; contacts still get the final say.
                for (int i = 2; i < jointCount; ++i) {
                    float dx = joints[i].x - joints[i - 2].x;
                    float dy = joints[i].y - joints[i - 2].y;
                    float distance = std::sqrt(dx * dx + dy * dy);
                    const float minimum = elasticSpacing * 1.4f;
                    if (distance >= minimum) continue;
                    if (distance < 0.00001f) { dx = 0; dy = 1; distance = 1; }
                    const float correction = (minimum - distance) / distance * 0.4f;
                    joints[i].x += dx * correction;
                    joints[i].y += dy * correction;
                    if (i > 2) {
                        joints[i - 2].x -= dx * correction;
                        joints[i - 2].y -= dy * correction;
                    }
                }
            }
            // Bound cosmetic displacement without changing the player's collision box.
            for (int i = 1; i < jointCount; ++i) {
                joints[i].x = std::clamp(joints[i].x, -lateralLimit, lateralLimit);
                joints[i].y = std::clamp(joints[i].y, -reach, i * spacing + 3.75f);
                if (collisions && !collisions->boxes.empty()) {
                    const Point target = joints[i];
                    // Sweep from the parent as well so spawning or moving a surface cannot
                    // leave the strand threaded through it with its tip on the far side.
                    const Point resolved = collisions->resolve(joints[i - 1], collisions->resolve(previous[i], target), true);
                    if (std::fabs(resolved.x - target.x) > 0.00001f) {
                        previous[i].x = resolved.x + (target.x - previous[i].x) * 0.15f;
                    }
                    if (std::fabs(resolved.y - target.y) > 0.00001f) {
                        previous[i].y = resolved.y + (target.y - previous[i].y) * 0.15f;
                    }
                    joints[i] = resolved;
                }
            }
        }
        previousVx = vx;
        previousVy = vy;
    }

    Point offset(float distance) const {
        const float t = std::clamp(distance / spacing, 0.0f, (float)(jointCount - 1));
        const int i = std::min((int)t, jointCount - 2);
        const float blend = t - i;
        return {joints[i].x + (joints[i + 1].x - joints[i].x) * blend,
            joints[i].y + (joints[i + 1].y - joints[i].y) * blend - t * spacing};
    }
};
} // namespace CharacterCreator
