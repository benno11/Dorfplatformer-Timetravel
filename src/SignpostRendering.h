#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace SignpostRendering {
// All animation frames share a 70-world-unit height and a bottom-center anchor.
inline SDL_Rect destination(float centerX, float bottomY, const SDL_Rect& source, bool rotated) {
    const int sourceWidth = rotated ? source.h : source.w;
    const int sourceHeight = rotated ? source.w : source.h;
    const int width = std::max(18, (int)std::lround(sourceWidth * 70.0 / std::max(1, sourceHeight)));
    return {(int)std::lround(centerX - width * 0.5f), (int)std::lround(bottomY - 70), width, 70};
}

// Enumerate only intersecting copies, including diagonal wraps and maps smaller
// than the viewport. A sprite touching an edge without entering it is culled.
template<class Draw>
inline void forEachVisible(const SDL_Rect& rect, int viewWidth, int viewHeight,
    int wrapWidth, int wrapHeight, Draw draw) {
    if (rect.w <= 0 || rect.h <= 0 || viewWidth <= 0 || viewHeight <= 0) return;
    const int firstX = wrapWidth > 0 ? (int)std::floor((-(double)rect.x - rect.w) / wrapWidth) + 1 : 0;
    const int lastX = wrapWidth > 0 ? (int)std::ceil(((double)viewWidth - rect.x) / wrapWidth) - 1 : 0;
    const int firstY = wrapHeight > 0 ? (int)std::floor((-(double)rect.y - rect.h) / wrapHeight) + 1 : 0;
    const int lastY = wrapHeight > 0 ? (int)std::ceil(((double)viewHeight - rect.y) / wrapHeight) - 1 : 0;
    for (int y = firstY; y <= lastY; ++y) for (int x = firstX; x <= lastX; ++x) {
        const SDL_Rect copy{rect.x + x * wrapWidth, rect.y + y * wrapHeight, rect.w, rect.h};
        if ((double)copy.x + copy.w <= 0 || (double)copy.y + copy.h <= 0 ||
            copy.x >= viewWidth || copy.y >= viewHeight) continue;
        draw(copy);
    }
}

// Test the crossing against the same visible wrapped copy that is drawn.
template<class Activate>
inline void forEachActivation(const SDL_Rect& rect, int viewWidth, int viewHeight,
    int wrapWidth, int wrapHeight, float signX, float previousPlayerX, float playerX, Activate activate) {
    forEachVisible(rect, viewWidth, viewHeight, wrapWidth, wrapHeight, [&](const SDL_Rect& copy) {
        const float copyX = signX + (copy.x - rect.x);
        if (previousPlayerX < copyX && playerX >= copyX) activate(copy, copyX);
    });
}
} // namespace SignpostRendering
