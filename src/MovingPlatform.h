#pragma once
#include "PlayerController.h"
#include "ObjectIds.h"
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
inline bool standingOn(const Player& player, const SDL_FRect& platform) {
    return !player.freeMove && player.vy >= 0 && touchingTop(player, platform);
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
