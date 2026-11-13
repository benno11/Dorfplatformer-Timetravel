#pragma once
#include <cmath>
#include <vector>

namespace WrappedView {
struct Rect { float x, y, w, h; };
// Visible screen-space copies of an actor, including seam-spanning sprites and
// cameras which follow a continuous image outside the canonical world range.
inline std::vector<Rect> copies(float x, float y, float w, float h,
                                float camX, float camY, float viewW, float viewH,
                                float wrapW = 0, float wrapH = 0) {
    std::vector<Rect> result;
    if (!(w > 0 && h > 0 && viewW > 0 && viewH > 0) || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(camX) || !std::isfinite(camY)) return result;
    if (!std::isfinite(wrapW) || wrapW < 0) wrapW = 0;
    if (!std::isfinite(wrapH) || wrapH < 0) wrapH = 0;
    const int firstX = wrapW > 0 ? static_cast<int>(std::floor((camX - x - w) / wrapW)) : 0;
    const int lastX = wrapW > 0 ? static_cast<int>(std::ceil((camX + viewW - x) / wrapW)) : 0;
    const int firstY = wrapH > 0 ? static_cast<int>(std::floor((camY - y - h) / wrapH)) : 0;
    const int lastY = wrapH > 0 ? static_cast<int>(std::ceil((camY + viewH - y) / wrapH)) : 0;
    for (int iy = firstY; iy <= lastY; ++iy) for (int ix = firstX; ix <= lastX; ++ix) {
        const float sx = x + ix * wrapW - camX, sy = y + iy * wrapH - camY;
        if (sx + w > 0 && sy + h > 0 && sx < viewW && sy < viewH) result.push_back({sx, sy, w, h});
    }
    return result;
}
}
