#pragma once

#include "PlayerController.h"
#include <algorithm>
#include <cmath>

namespace Bumper {
inline constexpr float kImpactDuration = 0.42f;
inline constexpr float kEffectCooldown = 0.25f;

// Feedback is throttled independently of the player's bounce response.
struct Impact {
    float remaining = 0;
    float cooldownRemaining = 0;

    bool trigger() {
        if (cooldownRemaining > 0) return false;
        remaining = kImpactDuration;
        cooldownRemaining = kEffectCooldown;
        return true;
    }
    void update(float dt) {
        if (!std::isfinite(dt) || dt <= 0) return;
        remaining = std::max(0.0f, remaining - dt);
        cooldownRemaining = std::max(0.0f, cooldownRemaining - dt);
    }
};

inline bool bounce(Player& player, const TileMap& map, float targetY, float launchVy) {
    const float oldY = player.y;
    player.y = targetY;
    // Keep the player out of solids near bumpers placed against a floor or ceiling.
    const float nudgeDir = launchVy < 0 ? 1.0f : -1.0f;
    int nudgeSteps = 0;
    while (RectHitsSolid(map, player.x, player.y, player.w, player.h) && nudgeSteps < 64) {
        player.y += nudgeDir;
        ++nudgeSteps;
    }
    if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
        player.y = oldY;
        return false;
    }
    const float probeY = player.y + (launchVy < 0 ? -2.0f : 2.0f);
    if (RectHitsSolid(map, player.x, probeY, player.w, player.h)) {
        player.vy = 0;
        player.onGround = launchVy > 0;
        return false;
    }
    player.vy = launchVy;
    player.onGround = false;
    return true;
}
} // namespace Bumper
