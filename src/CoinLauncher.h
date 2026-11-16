#pragma once
#include "WrappedView.h"
#include <algorithm>
#include <cmath>

namespace CoinLauncher {
constexpr float radius = 8.0f;
// Coin and object positions are centers. Sweep the leading edge so a fast
// coin cannot skip a launcher between frames. Return the launch direction.
inline int bounce(int objectId, float ox, float oy, float oldX, float oldY,
                  float x, float& y, float& vy, float wrapW = 0, float wrapH = 0) {
    if (objectId != 31 && objectId != 46) return 0;
    const float half = objectId == 31 ? 16.0f : 14.0f;
    const float left = std::min(oldX, x) - radius;
    const float top = std::min(oldY, y) - radius;
    const auto copies = WrappedView::copies(ox-half, oy-half, half*2, half*2,
        left, top, std::fabs(x-oldX)+radius*2, std::fabs(y-oldY)+radius*2,
        wrapW, wrapH);
    float earliest = 2.0f, contact = y;
    int direction = 0;
    for (const auto& copy : copies) {
        const float bx = copy.x + left, by = copy.y + top;
        const int dir = vy >= 0 ? -1 : 1;
        if (objectId == 31 && dir > 0) continue;
        const float face = dir < 0 ? by-radius : by+half*2+radius;
        const float delta = y-oldY;
        if (delta == 0) continue;
        const float t = (face-oldY)/delta;
        if (t < 0 || t > 1 || t >= earliest) continue;
        const float hitX = oldX+(x-oldX)*t;
        if (hitX+radius <= bx || hitX-radius >= bx+half*2) continue;
        earliest = t;
        contact = face;
        direction = dir;
    }
    if (direction) {
        y = contact;
        vy = direction * (objectId == 31 ? 1800.0f : 1200.0f);
    }
    return direction;
}
}
