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
        Point worldOrigin{}, worldScale{1, 1};
        bool hasWorldTransform = false;

        bool blocksWorld(float x, float y, float w, float h) const {
            for (const auto& box : boxes)
                if (!box.body && x < box.x + box.w && x + w > box.x && y < box.y + box.h && y + h > box.y)
                    return true;
            return false;
        }

        // Rendered strand rows use their actual width and one-pixel height. Turn
        // unused downward length along a support; subsequent rows fall off ledges.
        Point drape(Point from, Point target, float width, float direction) const {
            const Point localFrom{from.x - root.x, from.y - root.y};
            const Point localTarget{target.x - root.x, target.y - root.y};
            Point contact = resolve(localFrom, localTarget, true, width * 0.5f, 0.5f);
            if (target.y > from.y && contact.y < localTarget.y - 0.0001f) {
                const float remaining = localTarget.y - contact.y;
                contact = resolve(contact, {contact.x + direction * remaining, contact.y}, true, width * 0.5f, 0.5f);
            }
            return {contact.x + root.x, contact.y + root.y};
        }

        Point resolve(Point from, Point target, bool worldOnly = false, float halfWidth = -1, float halfHeight = -1) const {
            const float rx = halfWidth >= 0 ? halfWidth : radius;
            const float ry = halfHeight >= 0 ? halfHeight : radius;
            from.x += root.x; from.y += root.y;
            target.x += root.x; target.y += root.y;
            if (!worldOnly) {
                for (const auto& box : boxes) {
                    if (!box.body) continue;
                    const float left = box.x - rx, right = box.x + box.w + rx;
                    if (target.y <= box.y - ry || target.y >= box.y + box.h + ry ||
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
                    const float left = box.x - rx, right = box.x + box.w + rx;
                    const float top = box.y - ry, bottom = box.y + box.h + ry;
                    if (box.oneWay && (from.y > top + 0.01f || target.y < from.y)) continue;
                    if (from.x > left && from.x < right && from.y > top && from.y < bottom) {
                        if (target.x <= left || target.x >= right || target.y <= top || target.y >= bottom) continue;
                        // Recover against the whole surface. An individual tile's nearest
                        // face can be inside its neighbor, making hair climb wall seams.
                        Point best = target;
                        float bestDistance = INFINITY;
                        for (int side = 0; side < 4; ++side) {
                            Point candidate = target;
                            bool moved;
                            do {
                                moved = false;
                                for (const auto& obstacle : boxes) {
                                    if (obstacle.body) continue;
                                    const float l = obstacle.x - rx, r = obstacle.x + obstacle.w + rx;
                                    const float t = obstacle.y - ry, b = obstacle.y + obstacle.h + ry;
                                    if (obstacle.oneWay && (from.y > t + 0.01f || target.y < from.y)) continue;
                                    if (candidate.x <= l || candidate.x >= r || candidate.y <= t || candidate.y >= b) continue;
                                    if (side == 0) candidate.x = l - 0.001f;
                                    else if (side == 1) candidate.x = r + 0.001f;
                                    else if (side == 2) candidate.y = t - 0.001f;
                                    else candidate.y = b + 0.001f;
                                    moved = true;
                                }
                            } while (moved);
                            const float distance = std::fabs(candidate.x - target.x) + std::fabs(candidate.y - target.y);
                            if (distance < bestDistance) { bestDistance = distance; best = candidate; }
                        }
                        target = best;
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
    struct Profile {
        float length = reach;
        float stiffness = 1;
        float mass = 1;
    };
    static float airDrag(float speed, float exposure, float mass) {
        return -speed * (0.025f + std::fabs(speed) * 0.00002f) * exposure / mass;
    }
    std::array<Point, jointCount> joints{}, previous{};
    float restSpacing = spacing;
    float previousVx = 0, previousVy = 0;
    float stretch = 0, stretchVelocity = 0;
    bool initialized = false;

    void reset() {
        for (int i = 0; i < jointCount; ++i) joints[i] = previous[i] = {0, i * spacing};
        initialized = false;
        stretch = stretchVelocity = 0;
        restSpacing = spacing;
    }

    HairPhysics() { reset(); }

    void update(float dt, float vx, float vy, float springiness = 1.0f, const CollisionScene* collisions = nullptr,
        const Profile* profile = nullptr) {
        if (!std::isfinite(dt) || !std::isfinite(vx) || !std::isfinite(vy)) {
            reset();
            return;
        }
        if (dt <= 0) return;
        dt = std::min(dt, 1.0f / 30);
        vx = std::clamp(vx, -2000.0f, 2000.0f);
        vy = std::clamp(vy, -2000.0f, 2000.0f);
        springiness = std::isfinite(springiness) ? std::clamp(springiness, 0.75f, 1.25f) : 1.0f;
        const auto finiteValue = [](float value, float low, float high, float fallback) {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        };
        const float length = profile ? finiteValue(profile->length, 2, reach, reach) : reach;
        const float stiffness = profile ? finiteValue(profile->stiffness, 0.5f, 2, 1) : 1;
        const float mass = profile ? finiteValue(profile->mass, 0.5f, 2, 1) : 1;
        const float newSpacing = length / (jointCount - 1);
        if (std::fabs(newSpacing - restSpacing) > 0.0001f) {
            const float ratio = newSpacing / restSpacing;
            for (int i = 1; i < jointCount; ++i) {
                joints[i].x *= ratio; joints[i].y *= ratio;
                previous[i].x *= ratio; previous[i].y *= ratio;
            }
            restSpacing = newSpacing;
            stretch = stretchVelocity = 0;
        }
        if (!initialized) {
            previousVx = vx;
            previousVy = vy;
            initialized = true;
        }
        const float ax = std::clamp((vx - previousVx) / dt, -12000.0f, 12000.0f);
        const float ay = std::clamp((vy - previousVy) / dt, -12000.0f, 12000.0f);
        const int steps = std::max(1, (int)std::ceil(dt * 240));
        const float h = dt / steps;
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
            // Upward acceleration pulls the locks behind the head; falling airflow
            // lifts them. Express the response as strain so short locks show it too.
            const float verticalStrain = std::clamp((-ay * 0.00003f - vy * 0.00028f) * springiness / stiffness, -0.32f, 0.22f);
            const float stretchTarget = verticalStrain * length;
            stretchVelocity += ((stretchTarget - stretch) * 110 - stretchVelocity * 9 / springiness) * h;
            stretch = std::clamp(stretch + stretchVelocity * h, -std::min(6.0f, length * 0.35f), std::min(3.5f, length * 0.25f));
            const float elasticSpacing = restSpacing + stretch / (jointCount - 1);
            for (int i = 1; i < jointCount; ++i) {
                const Point current = joints[i];
                // Roots resist bending; exposed tips react more strongly to airflow.
                // Width adds mass, reducing wind response without increasing gravity.
                const float along = (float)i / (jointCount - 1);
                const float exposure = 0.3f + along * 0.7f;
                const float drag = std::exp(-(7 - along * 2) * h / (springiness * std::sqrt(mass)));
                const float airflow = airDrag(vx, exposure, mass);
                const float fx = (-ax * 0.008f + airflow) * springiness - current.x * (4 + (1 - along) * 8 * stiffness);
                const float fy = 65 - ay * 0.012f * exposure + airDrag(vy, exposure, mass) * springiness +
                    (i * elasticSpacing - current.y) * 18;
                joints[i].x += (current.x - previous[i].x) * drag + fx * h * h;
                joints[i].y += (current.y - previous[i].y) * drag + fy * h * h;
                previous[i] = current;
            }
            for (int iteration = 0; iteration < 8; ++iteration) {
                joints[0] = {};
                // Bend resistance distributes a disturbance along the strand instead
                // of letting individual links kink. Flexible tips retain follow-through.
                for (int i = 1; i < jointCount - 1; ++i) {
                    const float along = (float)i / (jointCount - 1);
                    const float bend = (0.08f - along * 0.05f) * stiffness / springiness;
                    const Point correction{((joints[i - 1].x + joints[i + 1].x) * 0.5f - joints[i].x) * bend,
                        ((joints[i - 1].y + joints[i + 1].y) * 0.5f - joints[i].y) * bend};
                    joints[i].x += correction.x; joints[i].y += correction.y;
                    if (i > 1) { joints[i - 1].x -= correction.x * 0.5f; joints[i - 1].y -= correction.y * 0.5f; }
                    joints[i + 1].x -= correction.x * 0.5f; joints[i + 1].y -= correction.y * 0.5f;
                }
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
                        previous[i].x = resolved.x - (resolved.x - previous[i].x) * std::exp(-12 * h);
                    }
                    joints[i] = resolved;
                }
            }
        }
        previousVx = vx;
        previousVy = vy;
    }

    Point offset(float distance) const {
        const float t = std::clamp(distance / restSpacing, 0.0f, (float)(jointCount - 1));
        const int i = std::min((int)t, jointCount - 2);
        const float blend = t - i;
        return {joints[i].x + (joints[i + 1].x - joints[i].x) * blend,
            joints[i].y + (joints[i + 1].y - joints[i].y) * blend - t * restSpacing};
    }
};
} // namespace CharacterCreator
