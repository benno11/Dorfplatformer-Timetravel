#pragma once
#include "PlayerController.h"
#include "ObjectIds.h"
#include "LevelLoader.h"
#include <algorithm>
#include <cmath>

namespace MovingPlatform {
inline constexpr float kDuration = 6.7f;
inline constexpr float kDistanceInBlocks = 11.0f;
struct State {
    float elapsed = 0.0f;
    bool started = false;
};
inline void synchronize(std::vector<State>& states, const std::vector<int>& ids, bool trigger) {
    float elapsed = 0;
    bool started = trigger;
    for (size_t i = 0; i < states.size() && i < ids.size(); ++i) {
        if (ids[i] != ObjectIds::MovingPlatform || !states[i].started) continue;
        started = true;
        elapsed = std::max(elapsed, states[i].elapsed);
    }
    if (!started) return;
    for (size_t i = 0; i < states.size() && i < ids.size(); ++i) {
        if (ids[i] != ObjectIds::MovingPlatform) continue;
        states[i].started = true;
        states[i].elapsed = elapsed;
    }
}
inline SDL_FPoint advance(State& state, float dt, float tileSize) {
    if (!state.started || !std::isfinite(dt) || dt <= 0 || tileSize <= 0) return {0, 0};
    const float previous = std::clamp(state.elapsed, 0.0f, kDuration);
    state.elapsed = std::min(kDuration, previous + dt);
    const float distance = (state.elapsed - previous) / kDuration * kDistanceInBlocks * tileSize;
    return {-distance, distance};
}
inline std::vector<SDL_FPoint> advanceObjects(std::vector<ObjectInstance>& objects,
        std::vector<State>& states, const std::vector<int>& ids, float dt, float tileSize,
        float wrapW = 0, float wrapH = 0) {
    std::vector<SDL_FPoint> deltas(objects.size(), SDL_FPoint{0, 0});
    std::vector<bool> carried(objects.size(), false);
    const size_t count = std::min(objects.size(), std::min(states.size(), ids.size()));
    // Find riders before moving anything, so object ordering cannot change support.
    for (size_t platform = 0; platform < count; ++platform) {
        if (ids[platform] != ObjectIds::MovingPlatform) continue;
        const auto delta = advance(states[platform], dt, tileSize);
        deltas[platform] = delta;
        for (size_t rider = 0; rider < objects.size(); ++rider) {
            // Platforms already move as a synchronized group; never carry them twice.
            if (carried[rider] || (rider < ids.size() && ids[rider] == ObjectIds::MovingPlatform)) continue;
            float dx = objects[rider].x - objects[platform].x;
            float dy = objects[rider].y - objects[platform].y;
            if (wrapW > 0) dx -= std::floor((dx + wrapW * 0.5f) / wrapW) * wrapW;
            if (wrapH > 0) dy -= std::floor((dy + wrapH * 0.5f) / wrapH) * wrapH;
            if (std::fabs(dx) >= tileSize || std::fabs(dy + tileSize) > 1.5f) continue;
            deltas[rider] = delta;
            carried[rider] = true;
        }
    }
    for (size_t index = 0; index < objects.size(); ++index) {
        objects[index].x += deltas[index].x;
        objects[index].y += deltas[index].y;
    }
    return deltas;
}
inline SDL_FRect nearPlayer(float x, float y, float tileSize, const Player& player,
                            float wrapW = 0, float wrapH = 0) {
    if (wrapW > 0) x -= std::floor((x - player.x - player.w * 0.5f + wrapW * 0.5f) / wrapW) * wrapW;
    if (wrapH > 0) y -= std::floor((y - player.y - player.h * 0.5f + wrapH * 0.5f) / wrapH) * wrapH;
    return {x - tileSize * 0.5f, y - tileSize * 0.5f, tileSize, tileSize};
}
inline bool touchingTop(const Player& player, const SDL_FRect& platform) {
    return player.x + player.w > platform.x && player.x < platform.x + platform.w &&
        std::fabs(player.y + player.h - platform.y) <= 1.5f;
}
inline bool touching(const Player& player, const SDL_FRect& platform) {
    // Match the subpixel contact tolerance used for riders, on all four faces.
    const bool overlapX = player.x + player.w > platform.x && player.x < platform.x + platform.w;
    const bool overlapY = player.y + player.h > platform.y && player.y < platform.y + platform.h;
    return (overlapX && player.y + player.h >= platform.y - 1.5f && player.y <= platform.y + platform.h + 1.5f) ||
        (overlapY && player.x + player.w >= platform.x - 1.5f && player.x <= platform.x + platform.w + 1.5f);
}
inline bool standingOn(const Player& player, const SDL_FRect& platform) {
    return !player.freeMove && player.vy >= 0 && touchingTop(player, platform);
}
inline bool settleRider(Player& player, const TileMap& map, const SDL_FRect& platform) {
    if (!standingOn(player, platform)) return false;
    const float y = platform.y - player.h;
    if (RectHitsSolid(map, player.x, y, player.w, player.h)) return false;
    // Gravity and pixel-sized collision steps otherwise cycle through a subpixel
    // gap/overlap each frame, even while the platform carries the player.
    player.y = y;
    player.vy = 0;
    player.onGround = true;
    return true;
}
inline void carry(Player& player, const TileMap& map, const SDL_FPoint& delta) {
    // Resolve the rider against map solids even if the platform's route crosses them.
    const int steps = std::max(1, (int)std::ceil(std::max(std::fabs(delta.x), std::fabs(delta.y))));
    const float dx = delta.x / steps, dy = delta.y / steps;
    for (int i = 0; i < steps; ++i) {
        if (!RectHitsSolid(map, player.x + dx, player.y, player.w, player.h)) player.x += dx;
        if (!RectHitsSolid(map, player.x, player.y + dy, player.w, player.h)) player.y += dy;
    }
}
} // namespace MovingPlatform
