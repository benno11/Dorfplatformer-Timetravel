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
    std::array<SDL_Rect, 3> columns{};
    std::vector<SDL_Rect> items;
    std::vector<bool> fixed;
    std::vector<int> scrollLimits;
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
    // Horizontal snap zones share screen space, but never reserve vertical
    // space in another column. A center-only menu retains its full width.
    std::array<int, 9> dockHeights{}, dockWidths{}, dockCounts{};
    auto anchor = [](const Item& item) { return (int)item.vertical * 3 + (int)item.horizontal; };
    bool hasSideZones = false;
    for (const auto& item : items) if (item.docked) {
        const int group = anchor(item);
        dockHeights[group] += std::max(1, item.height) + (dockCounts[group]++ ? gap : 0);
        dockWidths[group] = std::max(dockWidths[group], std::max(1, item.width));
        hasSideZones |= item.horizontal != Align::Center;
    }
    std::array<SDL_Rect, 3> zones;
    for (int column = 0; column < 3; ++column) {
        zones[column] = originalBody;
        if (hasSideZones) {
            zones[column].x += originalBody.w * column / 3;
            zones[column].w = std::max(1, originalBody.w * (column + 1) / 3 - originalBody.w * column / 3);
        }
        auto viewport = zones[column];
        const int top = std::min(dockHeights[column] + (dockCounts[column] ? gap : 0), viewport.h - 1);
        const int bottom = std::min(dockHeights[6 + column] + (dockCounts[6 + column] ? gap : 0), viewport.h - top - 1);
        viewport.y += top; viewport.h -= top + bottom;
        const int trimTop = std::clamp(cutTop, 0, viewport.h - 1);
        const int trimBottom = std::clamp(cutBottom, 0, viewport.h - trimTop - 1);
        viewport.y += trimTop; viewport.h -= trimTop + trimBottom;
        result.columns[column] = viewport;
    }
    result.body = result.columns[(int)Align::Center];
    std::array<std::array<int, 3>, 3> heights{}, counts{}, y{};
    std::array<int, 3> contentHeights{}, scrollLimits{};
    for (const auto& item : items) if (!item.docked) {
        const int column = (int)item.horizontal, group = (int)item.vertical;
        heights[column][group] += std::max(1, item.height) + (counts[column][group]++ ? gap : 0);
    }
    for (int column = 0; column < 3; ++column) {
        int total = 0, groups = 0;
        for (int group = 0; group < 3; ++group) if (counts[column][group]) {
            total += heights[column][group]; ++groups;
        }
        total += std::max(0, groups - 1) * gap;
        contentHeights[column] = std::max(total, result.columns[column].h);
        scrollLimits[column] = std::max(0, total - result.columns[column].h);
        result.maxScroll = std::max(result.maxScroll, scrollLimits[column]);
        const int centerMin = counts[column][0] ? heights[column][0] + gap : 0;
        const int centerMax = contentHeights[column] - heights[column][1] -
            (counts[column][2] ? heights[column][2] + gap : 0);
        y[column] = {0, std::clamp((contentHeights[column] - heights[column][1]) / 2,
            centerMin, std::max(centerMin, centerMax)), contentHeights[column] - heights[column][2]};
    }
    result.contentHeight = result.body.h + result.maxScroll;
    std::array<int, 9> used{};
    for (const auto& item : items) {
        const int column = (int)item.horizontal;
        result.fixed.push_back(item.docked);
        result.scrollLimits.push_back(item.docked ? 0 : scrollLimits[column]);
        if (item.docked) {
            const int group = anchor(item);
            const auto& zone = zones[column];
            const int groupWidth = std::clamp(dockWidths[group], 1, zone.w);
            const int groupHeight = std::max(1, dockHeights[group]);
            const int width = std::clamp(item.width, 1, groupWidth);
            const int height = std::max(1, item.height);
            const int groupX = zone.x + offset(zone.w - groupWidth, item.horizontal);
            const int groupY = zone.y + offset(zone.h - groupHeight, item.vertical);
            result.items.push_back({groupX + offset(groupWidth - width, item.horizontal),
                groupY + used[group], width, height});
            // Middle controls obey only their own column's header/footer cuts.
            const auto& bounds = item.vertical == Align::Center ? result.columns[column] : zone;
            const int clipX = std::max(bounds.x, groupX - gap / 2);
            const int clipY = std::max(bounds.y, groupY - gap / 2);
            result.clips.push_back({clipX, clipY,
                std::max(0, std::min(bounds.x + bounds.w, groupX + groupWidth + gap / 2) - clipX),
                std::max(0, std::min(bounds.y + bounds.h, groupY + groupHeight + gap / 2) - clipY)});
            used[group] += height + gap;
            continue;
        }
        const auto& viewport = result.columns[column];
        result.clips.push_back(viewport);
        const int group = (int)item.vertical;
        const int width = std::clamp(item.width, 1, viewport.w);
        const int height = std::max(1, item.height);
        result.items.push_back({viewport.x + offset(viewport.w - width, item.horizontal),
            viewport.y + y[column][group], width, height});
        y[column][group] += height + gap;
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
    return scrolled(layout.items[item], layout.fixed[item] ? 0.0f :
        std::clamp(scroll, 0.0f, (float)layout.scrollLimits[item]));
}

inline bool hit(const Layout& layout, size_t item, float x, float y, float scroll) {
    return item < layout.items.size() && contains(layout.clips[item], x, y) &&
           contains(itemRect(layout, item, scroll), x, y);
}

inline float reveal(const Layout& layout, size_t item, float scroll) {
    if (item >= layout.items.size()) return std::clamp(scroll, 0.0f, (float)layout.maxScroll);
    if (layout.fixed[item]) return std::clamp(scroll, 0.0f, (float)layout.maxScroll);
    const auto& rect = layout.items[item];
    const auto& viewport = layout.clips[item];
    const float effective = std::clamp(scroll, 0.0f, (float)layout.scrollLimits[item]);
    float next = effective;
    if (rect.h >= viewport.h || rect.y - next < viewport.y) next = (float)(rect.y - viewport.y);
    else if (rect.y + rect.h - next > viewport.y + viewport.h)
        next = (float)(rect.y + rect.h - viewport.y - viewport.h);
    return next == effective ? std::clamp(scroll, 0.0f, (float)layout.maxScroll) :
        std::clamp(next, 0.0f, (float)layout.scrollLimits[item]);
}
}
