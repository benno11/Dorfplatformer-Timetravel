#pragma once

#include <algorithm>
#include <cmath>

// Discard stall time instead of advancing gameplay past interactions in one frame.
// Do not accumulate the discarded time for later catch-up frames.
inline float GameplayDeltaTime(double elapsedSeconds) {
    if (!std::isfinite(elapsedSeconds)) return 0.0f;
    return static_cast<float>(std::clamp(elapsedSeconds, 0.0, 1.0 / 30.0));
}
