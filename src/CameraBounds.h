#pragma once
#include <algorithm>
namespace CameraBounds {
// Wrapping and rendering switch together: never blend an unclamped camera into
// a frame which has already stopped drawing wrapped copies.
inline float target(float desired, float minimum, float maximum, bool wrap) {
    return wrap ? desired : std::clamp(desired, minimum, std::max(minimum, maximum));
}
}
