#pragma once
#include "GameSupport.h"
#include "TextRenderer.h"
#include <algorithm>

namespace TouchPauseButton {
inline SDL_Rect layout(SDL_Window* window, int width, int height, bool left = false) {
    const int margin = std::max(12, height / 50);
    int buttonW = std::min(width / 3, std::clamp(height / 6, 110, 180));
    int buttonH = std::min(height, std::clamp(height / 16, 48, 72));
    int safeLeft = 0, safeRight = 0, safeTop = 0, windowW = 0, windowH = 0;
    SDL_Rect safe{};
    if (window && SDL_GetWindowSize(window, &windowW, &windowH) && windowW > 0 && windowH > 0) {
        // Window units are points on Retina displays, rather than output pixels.
        buttonW = std::min(width / 3, std::max(buttonW, 88 * width / windowW));
        buttonH = std::min(height, std::max(buttonH, 44 * height / windowH));
        if (SDL_GetWindowSafeArea(window, &safe)) {
            safeLeft = safe.x * width / windowW;
            safeRight = (windowW - safe.x - safe.w) * width / windowW;
            safeTop = safe.y * height / windowH;
        }
    }
    return {std::clamp(left ? safeLeft + margin : width - safeRight - margin - buttonW, 0, std::max(0, width - buttonW)),
        std::clamp(safeTop + margin, 0, std::max(0, height - buttonH)), buttonW, buttonH};
}
inline bool pressed(const SDL_Event& event, SDL_Window* window, const SDL_Rect& button, int width, int height) {
    SDL_Point point{};
    const auto id = SDL_GetWindowID(window);
    if (event.type == SDL_EVENT_FINGER_DOWN && (event.tfinger.windowID == 0 || event.tfinger.windowID == id)) {
        point = {static_cast<int>(event.tfinger.x * width), static_cast<int>(event.tfinger.y * height)};
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
               event.button.which != SDL_TOUCH_MOUSEID && event.button.windowID == id) {
        int windowW = 0, windowH = 0;
        if (!SDL_GetWindowSize(window, &windowW, &windowH) || windowW <= 0 || windowH <= 0) return false;
        point = {static_cast<int>(event.button.x * width / windowW), static_cast<int>(event.button.y * height / windowH)};
    } else return false;
    return SDL_PointInRect(&point, &button);
}
inline void draw(SDL_Renderer* renderer, const SDL_Rect& button, const std::string& label) {
    SDL_SetRenderDrawColor(renderer, 32, 46, 65, 255);
    SDL_RenderFillRect(renderer, &button);
    SDL_SetRenderDrawColor(renderer, 210, 225, 245, 255);
    SDL_RenderRect(renderer, &button);
    int scale = std::clamp(button.h / 20, 2, 5);
    while (scale > 1 && MeasureTextWidth(scale, label) > button.w - 12) --scale;
    DrawGameText(renderer, button.x + (button.w - MeasureTextWidth(scale, label)) / 2,
        button.y + (button.h - scale * 10) / 2, scale, label);
}
}
