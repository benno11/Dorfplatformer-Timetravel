#pragma once

#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <array>

namespace DifficultySelector {
struct Layout {
    SDL_Rect panel{};
    // 0 = unrated, 1-9 = difficulty, 10 = apply, 11 = cancel.
    std::array<SDL_Rect, 12> buttons{};
};

inline Layout build(int width, int height) {
    Layout result;
    const int w = std::max(1, std::min(440, width - 24));
    const int h = std::max(1, std::min(420, height - 24));
    result.panel = {(width - w) / 2, (height - h) / 2, w, h};
    const int pad = std::min(16, w / 12);
    const int gap = std::min(8, w / 24);
    const int cellW = std::max(1, (w - pad * 2 - gap * 2) / 3);
    const int cellH = std::max(1, h * 14 / 100 - gap);
    for (int value = 1; value <= 9; ++value) {
        result.buttons[value] = {result.panel.x + pad + ((value - 1) % 3) * (cellW + gap),
            result.panel.y + h * 23 / 100 + ((value - 1) / 3) * (cellH + gap), cellW, cellH};
    }
    result.buttons[0] = {result.panel.x + pad, result.panel.y + h * 69 / 100, w - pad * 2, std::max(1, h / 10)};
    const int actionW = std::max(1, (w - pad * 2 - gap) / 2);
    for (int action = 0; action < 2; ++action)
        result.buttons[10 + action] = {result.panel.x + pad + action * (actionW + gap),
            result.panel.y + h * 85 / 100, actionW, std::max(1, h / 10)};
    return result;
}

inline int hit(const Layout& layout, float x, float y) {
    for (int i = 0; i < (int)layout.buttons.size(); ++i) {
        const auto& r = layout.buttons[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
    }
    return -1;
}

inline int navigate(int focus, int dx, int dy) {
    focus = std::clamp(focus, 0, 11);
    if (focus >= 1 && focus <= 9) {
        if (dx) return 1 + (focus - 1 + dx + 9) % 9;
        if (dy < 0) return focus <= 3 ? 10 : focus - 3;
        if (dy > 0) return focus >= 7 ? 0 : focus + 3;
    } else if (focus == 0) {
        if (dy < 0) return 8;
        if (dy > 0) return 10;
    } else {
        if (dx) return focus == 10 ? 11 : 10;
        if (dy < 0) return 0;
        if (dy > 0) return 2;
    }
    return focus;
}
}
