#pragma once

#include <algorithm>
#include <cmath>
#include "CharacterHairPhysics.h"
#include "CharacterHairFloorPhysics.h"
#include "CharacterAppearance.h"

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
    HairPhysics builtInHair;
    HairPhysics fringeHair;
    HairPhysics::CollisionScene hairCollisions;
    std::array<HairFloorPhysics, 2> floorHair;
    float floorTrailDirection = 0;
    float gait = 0.0f;
    float previousVx = 0.0f, previousVy = 0.0f;
    bool wasGrounded = false;
    bool initialized = false;

    void update(float elapsed, float vx, float vy, bool grounded, bool sliding, int facing, float springiness = 1.0f,
        const HairPhysics::CollisionScene* collisions = nullptr, const Appearance* appearance = nullptr) {
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
            hair.velocity += impact * 8.0f;
            arms.velocity += impact * 3.0f;
        } else if (!grounded && wasGrounded && vy < -100.0f) {
            hatY.velocity += 10.0f;
            hair.velocity -= 5.0f;
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
            hair.step((inertia + vy * 0.00015f) * springiness, 85, 12 / springiness, 1.4f, h);
            hatX.step(inertia * 0.8f * springiness, 95, 9 / springiness, 1.2f, h);
            hatY.step(-headY.position * 0.6f, 120, 8 / springiness, 1.5f, h);
            arms.step(stride * 0.6f + inertia * 0.24f, 95, 10 / springiness, 0.85f, h);
            legs.step(-stride * 0.48f, 160, 15 / springiness, 0.6f, h);
        }
        hairCollisions = collisions ? *collisions : HairPhysics::CollisionScene{};
        hairCollisions.root.x += headX.position * direction;
        hairCollisions.root.y += headY.position * 1.3f;
        HairPhysics::Profile backProfile, fringeProfile;
        if (appearance) {
            const int style = appearance->hairStyle;
            backProfile.length = style == customPartsHairStyle ? (float)appearance->hairShape[2] :
                (style == 2 ? 9.0f : (style == customHairStyle ? 10.0f : 3.0f));
            backProfile.stiffness = style == 1 ? 1.7f : (style == 2 || style == customHairStyle ? 0.75f : 1.1f);
            backProfile.mass = style == customPartsHairStyle ? 0.7f + appearance->hairShape[3] * 0.15f : 1;
            fringeProfile = backProfile;
            fringeProfile.length = style == customPartsHairStyle ? (float)appearance->hairShape[1] : 3;
            fringeProfile.stiffness *= 1.15f;
        }
        // The attachment's bounce contributes to inertia in world directions.
        const float hairVx = vx + (appearance ? headX.velocity * direction * 8 : 0);
        const float hairVy = vy + (appearance ? headY.velocity * 10 : 0);
        builtInHair.update(dt, hairVx, hairVy, springiness, &hairCollisions, appearance ? &backProfile : nullptr);
        auto fringeCollisions = hairCollisions;
        if (appearance && appearance->hairStyle == customPartsHairStyle)
            fringeCollisions.root.y += appearance->hairShape[0] - 3;
        fringeHair.update(dt, hairVx, hairVy, springiness, &fringeCollisions, appearance ? &fringeProfile : nullptr);
        if (floorTrailDirection == 0) floorTrailDirection = -direction;
        if (std::fabs(vx) > 20) floorTrailDirection = vx > 0 ? -1.0f : 1.0f;
        if (appearance && appearance->hairStyle == customPartsHairStyle && appearance->uncapHairLength) {
            const float scale = appearance->build == 0 ? 0.88f : (appearance->build == 2 ? 1.08f : 1.0f);
            const float width = appearance->hairShape[3] * scale;
            const float lean = (sliding ? 2.0f : 0) + headX.position;
            float rootX = 8 + (3 + lean - 8) * scale + width * 0.5f;
            if (facing < 0) rootX = 16 - rootX;
            const float head = 3 + headY.position * 1.3f;
            const HairPhysics::Point airflow{hairVx / (hairCollisions.hasWorldTransform ? hairCollisions.worldScale.x : 16),
                hairVy / (hairCollisions.hasWorldTransform ? hairCollisions.worldScale.y : 28)};
            for (int strand = 0; strand < 2; ++strand) {
                const float startY = head + (strand == 0 ? appearance->hairShape[0] : 3) + 0.5f;
                const auto& source = strand == 0 ? fringeHair : builtInHair;
                const float verticalStrain = source.stretch / (source.restSpacing * (HairPhysics::jointCount - 1));
                floorHair[strand].update(dt, {rootX, startY}, appearance->hairShape[strand + 1],
                    width, floorTrailDirection, hairCollisions, springiness, airflow, verticalStrain);
            }
        } else floorHair = {};
        previousVx = vx;
        previousVy = vy;
        wasGrounded = grounded;
    }
};
} // namespace CharacterCreator
