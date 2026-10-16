#pragma once

#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace SlimMenuLayout {
enum class Align { Start, Center, End };

inline int offset(int space, Align align) {
    return align == Align::Start ? 0 : (align == Align::End ? space : space / 2);
}

struct Item {
    int width = 0;
    int height = 0;
    Align horizontal = Align::Center;
    Align vertical = Align::Center;
};

struct Layout {
    float scale = 1.0f;
    SDL_Rect panel{}, body{};
    std::vector<SDL_Rect> items;
    int contentHeight = 0;
    int maxScroll = 0;
};

inline Layout build(int screenW, int screenH, float scale, Align horizontal, Align vertical,
                    const std::vector<Item>& items) {
    Layout result;
    result.scale = std::isfinite(scale) ? std::clamp(scale, 0.25f, 4.0f) : 1.0f;
    const float s = result.scale;
    const int availableW = std::max(1, screenW - 32);
    const int availableH = std::max(1, screenH - 24);
    const int physicalW = std::max(1, (int)std::lround(availableW * std::min(s, 1.0f)));
    const int physicalH = std::max(1, (int)std::lround(availableH * std::min(s, 1.0f)));
    result.panel = {
        (int)std::lround((16 + offset(availableW - physicalW, horizontal)) / s),
        (int)std::lround((12 + offset(availableH - physicalH, vertical)) / s),
        std::max(1, (int)std::lround(physicalW / s)),
        std::max(1, (int)std::lround(physicalH / s))
    };
    const int leftPad = std::min(20, (result.panel.w - 1) / 2);
    const int rightPad = std::min(32, result.panel.w - leftPad - 1);
    const int topPad = std::min(94, result.panel.h - 1);
    const int bottomPad = std::min(20, result.panel.h - topPad - 1);
    result.body = {result.panel.x + leftPad, result.panel.y + topPad,
                   result.panel.w - leftPad - rightPad, result.panel.h - topPad - bottomPad};
    constexpr int gap = 12;
    std::array<int, 3> heights{}, counts{};
    for (const auto& item : items) {
        const int group = (int)item.vertical;
        heights[group] += std::max(1, item.height) + (counts[group]++ ? gap : 0);
    }
    int total = 0, groups = 0;
    for (int i = 0; i < 3; ++i) {
        if (counts[i]) { total += heights[i]; ++groups; }
    }
    total += std::max(0, groups - 1) * gap;
    result.contentHeight = std::max(total, result.body.h);
    result.maxScroll = std::max(0, total - result.body.h);
    const int centerMin = counts[0] ? heights[0] + gap : 0;
    const int centerMax = result.contentHeight - heights[1] - (counts[2] ? heights[2] + gap : 0);
    std::array<int, 3> y{0,
        std::clamp((result.contentHeight - heights[1]) / 2, centerMin, std::max(centerMin, centerMax)),
        result.contentHeight - heights[2]};
    for (const auto& item : items) {
        const int group = (int)item.vertical;
        const int width = std::clamp(item.width, 1, result.body.w);
        const int height = std::max(1, item.height);
        result.items.push_back({result.body.x + offset(result.body.w - width, item.horizontal),
                                result.body.y + y[group], width, height});
        y[group] += height + gap;
    }
    return result;
}

inline SDL_Rect scrolled(const SDL_Rect& rect, float scroll) {
    SDL_Rect result = rect;
    result.y -= (int)std::lround(scroll);
    return result;
}

inline bool contains(const SDL_Rect& rect, float x, float y) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

inline bool hit(const Layout& layout, size_t item, float x, float y, float scroll) {
    return item < layout.items.size() && contains(layout.body, x, y) &&
           contains(scrolled(layout.items[item], scroll), x, y);
}

inline float reveal(const Layout& layout, size_t item, float scroll) {
    if (item >= layout.items.size()) return std::clamp(scroll, 0.0f, (float)layout.maxScroll);
    const auto& rect = layout.items[item];
    if (rect.h >= layout.body.h || rect.y - scroll < layout.body.y) scroll = (float)(rect.y - layout.body.y);
    else if (rect.y + rect.h - scroll > layout.body.y + layout.body.h)
        scroll = (float)(rect.y + rect.h - layout.body.y - layout.body.h);
    return std::clamp(scroll, 0.0f, (float)layout.maxScroll);
}
}
