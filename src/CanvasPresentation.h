#pragma once
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <cstdint>

namespace CanvasPresentation {
inline SDL_Rect destination(int width, int height) {
    // Fill every output pixel, independently of aspect ratio and UI scaling.
    return {0, 0, std::max(0, width), std::max(0, height)};
}
inline bool toCanvas(int x, int y, int width, int height, int canvasWidth, int canvasHeight,
                     int& canvasX, int& canvasY) {
    if (width <= 0 || height <= 0 || canvasWidth <= 0 || canvasHeight <= 0 ||
        x < 0 || y < 0 || x >= width || y >= height) return false;
    canvasX = static_cast<int>(static_cast<int64_t>(x) * canvasWidth / width);
    canvasY = static_cast<int>(static_cast<int64_t>(y) * canvasHeight / height);
    return true;
}
}
