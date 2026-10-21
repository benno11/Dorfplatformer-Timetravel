#pragma once
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <cmath>

namespace ShellChraker {
inline constexpr float kExtendTime = 0.22f;
inline constexpr float kRetractTime = 0.30f;
inline constexpr float kRecoveryTime = 0.65f;

struct State {
    float time = 0.0f;
    int facing = -1;
    bool cycling = false;
};

inline void update(State& state, float dt, float playerDx, float playerDy, float tileSize) {
    if (dt <= 0.0f || tileSize <= 0.0f) return;
    if (!state.cycling) {
        if (playerDx >= (tileSize * 5.0f)) {
            state.facing = 1;
            state.cycling = true;
            state.time = 0.0f;
        } else if (playerDx <= -(tileSize * 5.0f)) {
            state.facing = -1;
            state.cycling = true;
            state.time = 0.0f;
        }
    }
    state.time += dt;
    if (state.time >= kExtendTime + kRetractTime + kRecoveryTime) {
        state.time = 0.0f;
        state.cycling = false;
    }
}

inline float extension(const State& state, float tileSize) {
    if (!state.cycling) return 0.0f;
    const float progress = state.time <= kExtendTime ? state.time / kExtendTime :
        1.0f - (state.time - kExtendTime) / kRetractTime;
    return std::clamp(progress, 0.0f, 1.0f) * tileSize * 2.5f;
}

struct Geometry {
    SDL_FRect body;
    SDL_FRect arm;
    SDL_FRect exposedArm;
    float centerX;
    int facing;
};
inline Geometry geometry(const State& state, float x, float y, float tileSize) {
    const float bodyW = tileSize;
    const float bodyH = bodyW * 519.0f / 588.0f;
    const float armW = tileSize * 2.5f;
    const float armH = armW * 176.0f / 958.0f;
    const float distance = extension(state, tileSize);
    const float armY = y + bodyH * 0.12f - armH * 0.5f;
    const float exposed = std::max(0.0f, distance - bodyW * 0.5f);
    return {{x - bodyW * 0.5f, y - bodyH * 0.5f, bodyW, bodyH},
        {state.facing < 0 ? x - distance : x + distance - armW, armY, armW, armH},
        {state.facing < 0 ? x - distance : x + bodyW * 0.5f, armY + armH * 0.12f, exposed, armH * 0.76f},
        x, state.facing};
}
} // namespace ShellChraker
