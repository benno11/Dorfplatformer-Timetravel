#pragma once
#include "CharacterAppearance.h"
#include "PlayerController.h"

namespace CharacterCreator {
inline float attributeMultiplier(int index) {
    return statPercent[std::clamp(index, 0, 4)] / 100.0f;
}
inline MovementConfig movementFor(const Appearance& a, MovementConfig base, bool experimental) {
    if (!experimental || !a.enabled) return base;
    const float speed = attributeMultiplier(a.speed), acceleration = attributeMultiplier(a.acceleration);
    const float jump = attributeMultiplier(a.jump), gravity = attributeMultiplier(a.gravity);
    base.maxSpeedGround *= speed;
    base.maxSpeedInWater *= speed;
    base.accelGround *= acceleration;
    base.accelInWater *= acceleration;
    base.jumpSpeed *= jump;
    base.jumpCutSpeed *= jump;
    base.gravityGround *= gravity;
    base.gravityInWater *= gravity;
    base.jumpHoldGravity *= gravity;
    return base;
}
} // namespace CharacterCreator
