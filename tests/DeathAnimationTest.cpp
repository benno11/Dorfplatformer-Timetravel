#include "DeathAnimation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>

int main() {
    for (int fps : {30, 60, 120, 240}) {
        DeathAnimation::Flight flight;
        flight.start(100, 200);
        flight.advance(0.2);
        assert(flight.y() < 200); // Initial upward bounce.
        assert(!flight.finished(540, 56));
        flight.advance(0.5);
        assert(flight.y() > 200); // Falls back through the start height.
        flight.start(100, 200);
        for (int i = 0; i < fps; ++i) flight.advance(1.0 / fps);
        assert(std::abs(flight.y() - 500) < 0.01);
        assert(!flight.finished(540, 56));
        flight.advance(0.2);
        assert(flight.finished(540, 56));
        assert(flight.x == 100);
        const float stopped = flight.y();
        flight.advance(0);
        assert(flight.y() == stopped);
        flight.start(20, 10);
        assert(flight.elapsed == 0 && !flight.finished(540, 56));
    }
}
