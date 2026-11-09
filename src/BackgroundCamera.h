#pragma once

#include <cmath>

// Follow normal camera movement without inheriting jumps across level edges.
struct BackgroundCamera {
    float x = 0;
    float y = 0;

    void reset() { *this = {}; }

    void update(float cameraX, float cameraY, float width, float height, bool wrapX, bool wrapY) {
        if (!initialized) {
            x = cameraX;
            y = cameraY;
            initialized = true;
        } else {
            x += delta(cameraX - previousX, width, wrapX);
            y += delta(cameraY - previousY, height, wrapY);
        }
        previousX = cameraX;
        previousY = cameraY;
    }

private:
    bool initialized = false;
    float previousX = 0;
    float previousY = 0;

    static float delta(float movement, float span, bool wraps) {
        return wraps && span > 0 ? std::remainder(movement, span) : movement;
    }
};
