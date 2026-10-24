#include "SignpostRendering.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <set>
#include <utility>

int main() {
    using namespace SignpostRendering;
    const SDL_Rect source{0, 0, 40, 100};
    const auto idle = destination(100, 80, source, false);
    assert(idle.x == 86 && idle.y == 10 && idle.w == 28 && idle.h == 70);
    const SDL_Rect rotatedSource{0, 0, 100, 40};
    const auto rotated = destination(100, 80, rotatedSource, true);
    assert(rotated.x == idle.x && rotated.y == idle.y && rotated.w == idle.w && rotated.h == idle.h);
    const auto wide = destination(100, 80, SDL_Rect{0, 0, 100, 100}, false);
    assert(wide.x == 65 && wide.x + wide.w == 135 && wide.y + wide.h == 80);

    auto visibleCount = [](SDL_Rect rect) {
        int count = 0;
        forEachVisible(rect, 320, 180, 0, 0, [&](const SDL_Rect&) { ++count; });
        return count;
    };
    // Fully offscreen and exact edge contact are culled on every side.
    for (const SDL_Rect rect : {SDL_Rect{-28, 10, 28, 70}, SDL_Rect{320, 10, 28, 70},
        SDL_Rect{20, -70, 28, 70}, SDL_Rect{20, 180, 28, 70}}) assert(visibleCount(rect) == 0);
    for (const SDL_Rect rect : {SDL_Rect{-27, 10, 28, 70}, SDL_Rect{319, 10, 28, 70},
        SDL_Rect{20, -69, 28, 70}, SDL_Rect{20, 179, 28, 70}}) assert(visibleCount(rect) == 1);
    // Its object anchor can be below the screen while the tall sign is visible.
    assert(visibleCount(destination(100, 220, source, false)) == 1);
    assert(visibleCount(SDL_Rect{10, 10, 0, 70}) == 0);

    auto activationCount = [](const SDL_Rect& bounds, float signX, float before, float after,
        int wrapWidth = 0, int wrapHeight = 0) {
        int count = 0;
        forEachActivation(bounds, 320, 180, wrapWidth, wrapHeight, signX, before, after,
            [&](const SDL_Rect& copy, float copyX) {
                assert(copy.x < 320 && copy.y < 180 && copy.x + copy.w > 0 && copy.y + copy.h > 0);
                assert(before < copyX && after >= copyX);
                ++count;
            });
        return count;
    };
    assert(activationCount(idle, 100, 99, 100) == 1);
    assert(activationCount(idle, 100, 101, 99) == 0); // Wrong direction.
    assert(activationCount(idle, 100, 100, 101) == 0); // Already crossed.
    assert(activationCount(idle, 100, 90, 99) == 0); // Has not reached it.
    assert(activationCount(destination(100, -1, source, false), 100, 99, 101) == 0);
    assert(activationCount(destination(100, 250, source, false), 100, 99, 101) == 0);
    assert(activationCount(destination(400, 80, source, false), 400, 399, 401) == 0);
    assert(activationCount(destination(-50, 80, source, false), -50, -51, -49) == 0);
    // Only the visible copy may activate; retain partial sprites at the edges.
    assert(activationCount(destination(0, 80, source, false), 0, -1, 1) == 1);
    assert(activationCount(destination(650, 80, source, false), 650, 9, 11, 640) == 1);
    assert(activationCount(destination(650, 80, source, false), 650, 649, 651, 640) == 0);
    assert(activationCount(destination(650, 440, source, false), 650, 9, 11, 640, 360) == 1);
    assert(activationCount(destination(100, 440, source, false), 100, 99, 101, 0, 360) == 1);

    // Compare the culled copies against exhaustive intersection checks. Cover
    // both seams, diagonal copies, distant cameras, and small repeated maps.
    for (int wrapWidth : {0, 96, 640}) for (int wrapHeight : {0, 64, 360})
        for (int x = -800; x <= 800; x += 43) for (int y = -500; y <= 500; y += 37) {
            const SDL_Rect rect{x, y, 70, 70};
            std::set<std::pair<int, int>> actual, expected;
            forEachVisible(rect, 320, 180, wrapWidth, wrapHeight, [&](const SDL_Rect& copy) {
                assert(copy.w == rect.w && copy.h == rect.h);
                assert(actual.emplace(copy.x, copy.y).second); // No duplicate draws.
            });
            for (int iy = wrapHeight ? -20 : 0; iy <= (wrapHeight ? 20 : 0); ++iy)
                for (int ix = wrapWidth ? -20 : 0; ix <= (wrapWidth ? 20 : 0); ++ix) {
                    const int left = x + ix * wrapWidth, top = y + iy * wrapHeight;
                    if (left < 320 && top < 180 && left + 70 > 0 && top + 70 > 0)
                        expected.emplace(left, top);
                }
            assert(actual == expected);
        }
}
