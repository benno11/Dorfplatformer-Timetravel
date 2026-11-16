#pragma once

#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <string>
#include <cctype>

namespace SlimMenuLayout {
enum class Align { Start, Center, End };

inline bool parseSnap(std::string first, std::string second, Align& horizontal, Align& vertical) {
    for (char& c : first) c = (char)std::tolower((unsigned char)c);
    for (char& c : second) c = (char)std::tolower((unsigned char)c);
    auto axis = [](const std::string& token, bool x, Align& result) {
        if (token == "center" || token == "middle") result = Align::Center;
        else if (token == "start" || token == (x ? "left" : "top")) result = Align::Start;
        else if (token == "end" || token == (x ? "right" : "bottom")) result = Align::End;
        else return false;
        return true;
    };
    Align x, y;
    if (!(axis(first, true, x) && axis(second, false, y)) &&
        !(axis(second, true, x) && axis(first, false, y))) return false;
    horizontal = x; vertical = y;
    return true;
}

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
    // Each of the nine anchor positions owns its own stack. Reserving the
    // maximum height at an edge clips the center without concatenating columns.
    std::array<int, 9> dockHeights{}, dockWidths{}, dockCounts{};
    auto anchor = [](const Item& item) {
        return (int)item.vertical * 3 + (int)item.horizontal;
    };
    for (const auto& item : items) if (item.docked) {
        const int group = anchor(item);
        dockHeights[group] += std::max(1, item.height) + (dockCounts[group]++ ? gap : 0);
        dockWidths[group] = std::max(dockWidths[group], std::max(1, item.width));
    }
    const std::array<int, 4> reserve{
        std::max({dockHeights[0], dockHeights[1], dockHeights[2]}) +
            (dockCounts[0] || dockCounts[1] || dockCounts[2] ? gap : 0),
        std::max({dockHeights[6], dockHeights[7], dockHeights[8]}) +
            (dockCounts[6] || dockCounts[7] || dockCounts[8] ? gap : 0),
        dockWidths[3] + (dockCounts[3] ? gap : 0),
        dockWidths[5] + (dockCounts[5] ? gap : 0)
    };
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
    std::array<int, 9> used{};
    for (const auto& item : items) {
        result.fixed.push_back(item.docked);
        if (item.docked) {
            const int group = anchor(item);
            const int groupWidth = std::clamp(dockWidths[group], 1, originalBody.w);
            const int groupHeight = std::max(1, dockHeights[group]);
            const int width = std::clamp(item.width, 1, groupWidth);
            const int height = std::max(1, item.height);
            const int groupX = originalBody.x + offset(originalBody.w - groupWidth, item.horizontal);
            const int groupY = originalBody.y + offset(originalBody.h - groupHeight, item.vertical);
            result.items.push_back({groupX + offset(groupWidth - width, item.horizontal),
                groupY + used[group], width, height});
            // Allow the existing hover animation to grow a few pixels without
            // clipping its border, while keeping drawing inside the menu.
            const int clipX = std::max(originalBody.x, groupX - gap / 2);
            const int clipY = std::max(originalBody.y, groupY - gap / 2);
            SDL_Rect clip{clipX, clipY,
                std::max(0, std::min(originalBody.x + originalBody.w, groupX + groupWidth + gap / 2) - clipX),
                std::max(0, std::min(originalBody.y + originalBody.h, groupY + groupHeight + gap / 2) - clipY)};
            result.clips.push_back(clip);
            used[group] += height + gap;
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
