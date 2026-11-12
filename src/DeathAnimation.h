#pragma once
#include <algorithm>

namespace DeathAnimation {
struct Flight {
    static constexpr float launchSpeed = 600;
    static constexpr float gravity = 1800;
    float x = 0, startY = 0;
    double elapsed = 0;
    void start(float screenX, float screenY) { x = screenX; startY = screenY; elapsed = 0; }
    void advance(double seconds) { elapsed += std::max(0.0, seconds); }
    float y() const { return startY - launchSpeed * elapsed + 0.5 * gravity * elapsed * elapsed; }
    bool finished(float screenHeight, float spriteHeight) const {
        return elapsed > launchSpeed / gravity && y() >= screenHeight + spriteHeight;
    }
};
}
