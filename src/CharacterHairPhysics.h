#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace CharacterCreator {

// A head-relative strand simulated in world directions, independent of animation frames.
// Fixed storage also supports uncapped hairstyles without allocating a joint per pixel.
struct HairPhysics {
    struct Point { float x = 0, y = 0; };
    static constexpr int jointCount = 12;
    static constexpr float spacing = 2.0f;
    static constexpr float reach = (jointCount - 1) * spacing;
    std::array<Point, jointCount> joints{}, previous{};
    float previousVx = 0, previousVy = 0;
    bool initialized = false;

    void reset() {
        for (int i = 0; i < jointCount; ++i) joints[i] = previous[i] = {0, i * spacing};
        initialized = false;
    }

    HairPhysics() { reset(); }

    void update(float dt, float vx, float vy, float springiness = 1.0f) {
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
        for (int step = 0; step < steps; ++step) {
            for (int i = 1; i < jointCount; ++i) {
                const Point current = joints[i];
                // Inertia and air resistance bend the strand; gravity restores its hanging shape.
                const float fx = (-ax * 0.008f - vx * 0.035f) * springiness - current.x * 4;
                const float fy = 65 - ay * 0.008f - vy * 0.012f;
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
                    const float correction = (distance - spacing) / distance;
                    const float weight = i == 1 ? 1.0f : 0.5f;
                    joints[i].x -= dx * correction * weight;
                    joints[i].y -= dy * correction * weight;
                    if (i > 1) {
                        joints[i - 1].x += dx * correction * weight;
                        joints[i - 1].y += dy * correction * weight;
                    }
                }
            }
            // Bound cosmetic displacement without changing the player's collision box.
            for (int i = 1; i < jointCount; ++i) {
                joints[i].x = std::clamp(joints[i].x, -2.8f, 2.8f);
                joints[i].y = std::clamp(joints[i].y, -reach, i * spacing + 0.25f);
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
