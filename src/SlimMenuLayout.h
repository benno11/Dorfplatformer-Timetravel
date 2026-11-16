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
    bool docked = false;
};

struct Layout {
    float scale = 1.0f;
    SDL_Rect panel{}, body{};
    std::vector<SDL_Rect> items;
    std::vector<bool> fixed;
    std::vector<SDL_Rect> clips;
    int contentHeight = 0;
    int maxScroll = 0;
};

inline Layout build(int screenW, int screenH, float scale, Align horizontal, Align vertical,
                    const std::vector<Item>& items, int cutTop = 0, int cutBottom = 0) {
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
    const SDL_Rect originalBody = result.body;
    // Fixed edges reserve space around the scrolling, clipped center.
    std::array<int, 4> sideHeights{};
    std::array<int, 4> reserve{}; // top, bottom, left, right
    auto edge = [](const Item& item) {
        if (item.vertical == Align::Start) return 0;
        if (item.vertical == Align::End) return 1;
        return item.horizontal == Align::Start ? 2 : 3;
    };
    for (const auto& item : items) if (item.docked) {
        const int e = edge(item);
        if (e < 2) reserve[e] += std::max(1, item.height) + gap;
        else {
            reserve[e] = std::max(reserve[e], std::max(1, item.width) + gap);
            sideHeights[e] += std::max(1, item.height) + gap;
        }
    }
    const int topReserve = std::min(reserve[0], result.body.h - 1);
    const int bottomReserve = std::min(reserve[1], result.body.h - topReserve - 1);
    const int leftReserve = std::min(reserve[2], result.body.w - 1);
    const int rightReserve = std::min(reserve[3], result.body.w - leftReserve - 1);
    result.body.x += leftReserve; result.body.w -= leftReserve + rightReserve;
    result.body.y += topReserve; result.body.h -= topReserve + bottomReserve;
    const int trimTop = std::clamp(cutTop, 0, result.body.h - 1);
    const int trimBottom = std::clamp(cutBottom, 0, result.body.h - trimTop - 1);
    result.body.y += trimTop; result.body.h -= trimTop + trimBottom;
    std::array<int, 3> heights{}, counts{};
    for (const auto& item : items) {
        if (item.docked) continue;
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
    std::array<int, 4> used{};
    for (const auto& item : items) {
        result.fixed.push_back(item.docked);
        if (item.docked) {
            const int e = edge(item);
            SDL_Rect zone = originalBody;
            if (e == 0) zone.h = topReserve;
            if (e == 1) { zone.y += zone.h - bottomReserve; zone.h = bottomReserve; }
            if (e >= 2) {
                zone.y += topReserve; zone.h -= topReserve + bottomReserve;
                zone.w = e == 2 ? leftReserve : rightReserve;
                if (e == 3) zone.x = originalBody.x + originalBody.w - rightReserve;
            }
            const int width = std::clamp(item.width, 1, std::max(1, zone.w));
            const int height = std::max(1, item.height);
            result.items.push_back({zone.x + offset(zone.w - width, item.horizontal),
                zone.y + (e >= 2 ? std::max(0, (zone.h - sideHeights[e] + gap) / 2) : 0) + used[e], width, height});
            result.clips.push_back(zone);
            used[e] += height + gap;
            continue;
        }
        result.clips.push_back(result.body);
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

inline SDL_Rect itemRect(const Layout& layout, size_t item, float scroll) {
    return scrolled(layout.items[item], layout.fixed[item] ? 0.0f : scroll);
}

inline bool hit(const Layout& layout, size_t item, float x, float y, float scroll) {
    return item < layout.items.size() && contains(layout.clips[item], x, y) &&
           contains(itemRect(layout, item, scroll), x, y);
}

inline float reveal(const Layout& layout, size_t item, float scroll) {
    if (item >= layout.items.size()) return std::clamp(scroll, 0.0f, (float)layout.maxScroll);
    if (layout.fixed[item]) return std::clamp(scroll, 0.0f, (float)layout.maxScroll);
    const auto& rect = layout.items[item];
    if (rect.h >= layout.body.h || rect.y - scroll < layout.body.y) scroll = (float)(rect.y - layout.body.y);
    else if (rect.y + rect.h - scroll > layout.body.y + layout.body.h)
        scroll = (float)(rect.y + rect.h - layout.body.y - layout.body.h);
    return std::clamp(scroll, 0.0f, (float)layout.maxScroll);
}
}
