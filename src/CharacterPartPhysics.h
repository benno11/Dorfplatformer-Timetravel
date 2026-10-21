#pragma once

#include <algorithm>
#include <cmath>

namespace CharacterCreator {
enum class Pose { Idle, Run, Jump, Fall, Slide, Hurt, Death, Skid };

struct PartSpring {
    float position = 0.0f;
    float velocity = 0.0f;

    void step(float target, float stiffness, float damping, float limit, float dt) {
        velocity += ((target - position) * stiffness - velocity * damping) * dt;
        position += velocity * dt;
        if (std::fabs(position) > limit) {
            position = std::clamp(position, -limit, limit);
            // Stop outward motion at the joint limit while allowing recovery.
            if (position * velocity > 0.0f) velocity = 0.0f;
        }
    }
};

// Cosmetic joints live outside Player so they cannot affect collision or movement.
struct PartPhysics {
    PartSpring headX, headY, hair, hatX, hatY, arms, legs;
    float gait = 0.0f;
    float previousVx = 0.0f, previousVy = 0.0f;
    bool wasGrounded = false;
    bool initialized = false;

    void update(float elapsed, float vx, float vy, bool grounded, bool sliding, int facing, float springiness = 1.0f) {
        if (!std::isfinite(elapsed) || !std::isfinite(vx) || !std::isfinite(vy)) {
            *this = PartPhysics{};
            return;
        }
        const float dt = std::clamp(elapsed, 0.0f, 1.0f / 30.0f);
        springiness = std::isfinite(springiness) ? std::clamp(springiness, 0.75f, 1.25f) : 1.0f;
        if (dt <= 0.0f) return;
        vx = std::clamp(vx, -2000.0f, 2000.0f);
        vy = std::clamp(vy, -2000.0f, 2000.0f);
        if (!initialized) {
            previousVx = vx;
            previousVy = vy;
            wasGrounded = grounded;
            initialized = true;
        }
        const float direction = facing < 0 ? -1.0f : 1.0f;
        const float acceleration = std::clamp((vx - previousVx) / dt, -12000.0f, 12000.0f) * direction;
        const float forwardSpeed = vx * direction;
        if (grounded && !wasGrounded) {
            const float impact = std::clamp(previousVy / 1000.0f, 0.0f, 1.8f) * springiness;
            headY.velocity += impact * 14.0f;
            hatY.velocity -= impact * 24.0f;
            hair.velocity += impact * 18.0f;
            arms.velocity += impact * 3.0f;
        } else if (!grounded && wasGrounded && vy < -100.0f) {
            hatY.velocity += 10.0f;
            hair.velocity -= 12.0f;
        }
        const int steps = std::max(1, (int)std::ceil(dt * 240.0f));
        const float h = dt / steps;
        for (int i = 0; i < steps; ++i) {
            const float running = grounded && !sliding ? std::min(std::fabs(vx) / 350.0f, 1.0f) : 0.0f;
            gait = std::fmod(gait + std::fabs(vx) * h * 0.035f, 6.2831853f);
            const float stride = std::sin(gait) * running;
            const float inertia = std::clamp(-forwardSpeed * 0.001f - acceleration * 0.00008f, -1.0f, 1.0f);
            headX.step(inertia * 0.6f, 160, 18, 0.8f, h);
            headY.step((grounded ? std::fabs(stride) * 0.35f : vy * 0.0003f) + (sliding ? 0.3f : 0),
                180, 16, 1.0f, h);
            hair.step((inertia * 1.7f + vy * 0.00035f) * springiness, 65, 7 / springiness, 2.2f, h);
            hatX.step(inertia * 0.8f * springiness, 95, 9 / springiness, 1.2f, h);
            hatY.step(-headY.position * 0.6f, 120, 8 / springiness, 1.5f, h);
            arms.step(stride * 0.6f + inertia * 0.24f, 95, 10 / springiness, 0.85f, h);
            legs.step(-stride * 0.48f, 160, 15 / springiness, 0.6f, h);
        }
        previousVx = vx;
        previousVy = vy;
        wasGrounded = grounded;
    }
};
} // namespace CharacterCreator
