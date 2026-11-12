#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "GameSupport.h"

// Animate the drawing rectangle only; input keeps using the stable layout rectangle.
class ButtonAnimations {
    struct State {
        Uint64 ticks = 0;
        Uint64 activated = 0;
        float focus = 0;
        float press = 0;
    };
    std::unordered_map<int, State> states;
public:
    void clear() { states.clear(); }
    void activate(int id) { states[id].activated = SDL_GetTicksNS(); }

    SDL_Rect drawRect(int id, SDL_Rect rect, bool focused, SDL_Window* window,
                      int baseW, int baseH, float scale = 1.0f, bool enabled = true) {
        float mx = 0, my = 0;
        const auto buttons = SDL_GetMouseState(&mx, &my);
        int w = 0, h = 0, x = 0, y = 0;
        SDL_GetWindowSize(window, &w, &h);
        const bool hover = enabled && SDL_GetMouseFocus() == window &&
            windowToGamePoint((int)mx, (int)my, w, h, baseW, baseH, x, y) &&
            x / scale >= rect.x && x / scale < rect.x + rect.w &&
            y / scale >= rect.y && y / scale < rect.y + rect.h;
        bool touching = false;
        if (enabled) {
            int count = 0;
            SDL_TouchID* devices = SDL_GetTouchDevices(&count);
            for (int i = 0; i < count; ++i) {
                int fingersCount = 0;
                SDL_Finger** fingers = SDL_GetTouchFingers(devices[i], &fingersCount);
                for (int j = 0; j < fingersCount; ++j) {
                    if (windowToGamePoint((int)(fingers[j]->x * w), (int)(fingers[j]->y * h),
                        w, h, baseW, baseH, x, y) && x / scale >= rect.x &&
                        x / scale < rect.x + rect.w && y / scale >= rect.y && y / scale < rect.y + rect.h)
                        touching = true;
                }
                SDL_free(fingers);
            }
            SDL_free(devices);
        }
        auto& state = states[id];
        const Uint64 now = SDL_GetTicksNS();
        const float dt = state.ticks ? std::min(0.05f, (float)((now - state.ticks) / 1e9)) : 0.0f;
        state.ticks = now;
        const bool pressed = enabled && (touching || (hover && (buttons & SDL_BUTTON_LMASK)) ||
            (state.activated && now - state.activated < 100000000));
        state.focus += ((enabled && (focused || hover) ? 1.0f : 0.0f) - state.focus) * (1 - std::exp(-18 * dt));
        state.press += ((pressed ? 1.0f : 0.0f) - state.press) * (1 - std::exp(-35 * dt));
        const float size = 1 + 0.025f * state.focus - 0.055f * state.press;
        const int width = std::max(1, (int)std::lround(rect.w * size));
        const int height = std::max(1, (int)std::lround(rect.h * size));
        return {rect.x + (rect.w - width) / 2, rect.y + (rect.h - height) / 2 -
            (int)std::lround(2 * state.focus - 2 * state.press), width, height};
    }
};
