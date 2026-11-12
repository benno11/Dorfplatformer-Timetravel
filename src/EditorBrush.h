#pragma once
#include <algorithm>
#include <utility>
#include <vector>

namespace EditorBrush {
inline constexpr int maxSize = 9;
inline std::vector<std::pair<int, int>> cells(int width, int height, int x, int y, int size) {
    size = std::clamp(size, 1, maxSize) | 1;
    const int radius = size / 2;
    std::vector<std::pair<int, int>> result;
    for (int cy = std::max(0, y - radius); cy <= std::min(height - 1, y + radius); ++cy)
        for (int cx = std::max(0, x - radius); cx <= std::min(width - 1, x + radius); ++cx)
            result.emplace_back(cx, cy);
    return result;
}
}
