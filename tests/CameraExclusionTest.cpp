#include "CameraExclusion.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace CameraExclusion;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        const auto empty = resolve(10, 20, 100, 100, {});
        require(empty.resolved && empty.x == 10 && empty.y == 20, "No objects leave the camera unchanged");
        const std::vector<Rect> right{{90, 40, 32, 32}}, left{{-22, 40, 32, 32}},
            above{{40, -22, 32, 32}}, below{{40, 90, 32, 32}};
        const auto a = resolve(0, 0, 100, 100, right), b = resolve(0, 0, 100, 100, left),
            c = resolve(0, 0, 100, 100, above), d = resolve(0, 0, 100, 100, below);
        require(a.resolved && a.x < 0 && a.y == 0, "Move left to hide a region on the right");
        require(b.resolved && b.x > 0 && b.y == 0, "Move right to hide a region on the left");
        require(c.resolved && c.y > 0 && c.x == 0, "Move down to hide a region above");
        require(d.resolved && d.y < 0 && d.x == 0, "Move up to hide a region below");
        require(!overlap(a.x, a.y, 100, 100, right) && !overlap(b.x, b.y, 100, 100, left) &&
            !overlap(c.x, c.y, 100, 100, above) && !overlap(d.x, d.y, 100, 100, below), "Whole regions remain offscreen");
        const std::vector<Rect> neighbors{{90, 40, 32, 32}, {-30, 0, 20, 100}};
        auto result = resolve(0, 0, 100, 100, neighbors);
        require(result.resolved && !overlap(result.x, result.y, 100, 100, neighbors), "Multiple regions do not push into each other");
        const auto repeat = resolve(0, 0, 100, 100, neighbors);
        require(result.x == repeat.x && result.y == repeat.y, "Ties remain deterministic across frames");
        std::vector<Rect> dense;
        for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) dense.push_back({x * 32.0f, y * 32.0f, 32, 32});
        result = resolve(100, 100, 100, 100, dense);
        require(result.resolved && !overlap(result.x, result.y, 100, 100, dense), "Painted areas find a safe exterior");
        result = resolve(0, 0, 1280, 720, {{630, 350, 32, 32}});
        require(result.resolved && !overlap(result.x, result.y, 1280, 720, {{630, 350, 32, 32}}), "Use actual zoomed view dimensions");
        const std::vector<Rect> seamX{{390, 20, 32, 32}}, seamY{{20, 390, 32, 32}};
        result = resolve(0, 0, 100, 100, seamX, 400, 0);
        require(result.resolved && !overlap(result.x, result.y, 100, 100, seamX, 400, 0), "Wrapped copies at horizontal seams");
        result = resolve(0, 0, 100, 100, seamY, 0, 400);
        require(result.resolved && !overlap(result.x, result.y, 100, 100, seamY, 0, 400), "Wrapped copies at vertical seams");
        result = resolve(0, 0, 100, 100, {{20, 20, 32, 32}}, 100, 0);
        require(result.resolved && !overlap(result.x, result.y, 100, 100, {{20, 20, 32, 32}}, 100, 0), "Escape on a non-wrapped axis");
        result = resolve(0, 0, 100, 100, {{20, 20, 32, 32}}, 100, 100);
        require(!result.resolved && result.x == 0 && result.y == 0, "Impossible fully wrapped layouts terminate without drifting");
        result = resolve(0, 0, 100, 100, {{std::numeric_limits<float>::quiet_NaN(), 0, 32, 32}});
        require(result.resolved && result.x == 0 && result.y == 0, "Invalid objects do not poison the camera");
        // Frame sequences must keep the same side and slide through painted seams.
        std::vector<Rect> wall;
        for (int y = -8; y <= 24; ++y) wall.push_back({120, y * 32.0f, 32, 32});
        Controller moving;
        auto previous = moving.update(0, 0, 100, 100, wall, 0);
        for (int frame = 1; frame <= 120; ++frame) {
            const auto next = moving.update(200, frame * 2.0f, 100, 100, wall, 1.0f / 60);
            require(next.resolved && !overlap(next.x, next.y, 100, 100, wall), "Sliding stays outside the whole painted wall");
            require(next.x < 20 && next.y >= previous.y && next.y - previous.y < 2.1f,
                "No side flipping, seam sticking or jumps while sliding vertically");
            previous = next;
        }
        require(previous.y > 220 && previous.x > 18, "Camera follows tangent movement along the wall");
        std::vector<Rect> row;
        for (int x = -8; x <= 24; ++x) row.push_back({x * 32.0f, 120, 32, 32});
        Controller horizontal;
        horizontal.update(0, 0, 100, 100, row, 0);
        for (int frame = 1; frame <= 120; ++frame) {
            const auto next = horizontal.update(frame * 2.0f, 200, 100, 100, row, 1.0f / 60);
            require(next.y < 20 && !overlap(next.x, next.y, 100, 100, row), "Horizontal sliding stays above all objects");
            previous = next;
        }
        require(previous.x > 220 && previous.y > 18, "Camera follows horizontal edges");
        Controller descending, leftward;
        descending.update(170, 0, 100, 100, wall, 0);
        leftward.update(0, 170, 100, 100, row, 0);
        for (int frame = 1; frame <= 120; ++frame) {
            const auto vertical = descending.update(-100, -frame * 2.0f, 100, 100, wall, 1.0f / 60);
            const auto sideways = leftward.update(-frame * 2.0f, -100, 100, 100, row, 1.0f / 60);
            require(vertical.x > 152 && !overlap(vertical.x, vertical.y, 100, 100, wall), "Sliding upward stays on the right side");
            require(sideways.y > 152 && !overlap(sideways.x, sideways.y, 100, 100, row), "Sliding left stays below the objects");
            if (frame == 120) require(vertical.y < -220 && sideways.x < -220, "Negative-direction tangent movement continues");
        }
        for (float offset : {0.13f, 0.37f, 10000.17f}) {
            const std::vector<Rect> fractional{{offset + 120.3f, -100, 32.7f, 1000}};
            Controller precise; precise.update(offset, 0, 99.37f, 100.13f, fractional, 0);
            for (int frame = 1; frame <= 120; ++frame) {
                result = precise.update(offset + 200, frame * 0.83f, 99.37f, 100.13f, fractional, 1.0f / 60);
                require(!overlap(result.x, result.y, 99.37f, 100.13f, fractional), "Fractional geometry never leaks onscreen");
            }
            require(result.y > 90, "Floating-point contacts do not freeze tangent movement");
        }
        Controller fast;
        fast.update(0, 0, 100, 100, wall, 0);
        result = fast.update(2000, 0, 100, 100, wall, 1);
        require(result.x < 20 && !overlap(result.x, result.y, 100, 100, wall), "Swept motion cannot tunnel through obstacles");
        auto reversed = wall; std::reverse(reversed.begin(), reversed.end());
        Controller forwardOrder, reverseOrder;
        forwardOrder.update(0, 0, 100, 100, wall, 0); reverseOrder.update(0, 0, 100, 100, reversed, 0);
        for (int frame = 1; frame <= 120; ++frame) {
            const auto first = forwardOrder.update(200, frame * 2.0f, 100, 100, wall, 1.0f / 60);
            const auto second = reverseOrder.update(200, frame * 2.0f, 100, 100, reversed, 1.0f / 60);
            require(std::fabs(first.x - second.x) < 0.001f && std::fabs(first.y - second.y) < 0.001f,
                "Multiple object ordering cannot change sliding behavior");
        }
        float referenceY = 0;
        for (int fps : {30, 60, 120}) {
            Controller timed; timed.update(0, 0, 100, 100, wall, 0);
            for (int frame = 0; frame < fps; ++frame) result = timed.update(200, 100, 100, 100, wall, 1.0f / fps);
            if (fps == 30) referenceY = result.y;
            require(std::fabs(result.y - referenceY) < 0.02f && result.y > 99, "Sliding speed is frame-rate independent");
        }
        const std::vector<Rect> corner{{120, -200, 32, 400}, {120, 220, 300, 32}};
        Controller around; around.update(0, 0, 100, 100, corner, 0);
        for (int frame = 1; frame <= 220; ++frame) {
            result = around.update(200, frame * 2.0f, 100, 100, corner, 1.0f / 60);
            require(!overlap(result.x, result.y, 100, 100, corner), "Connected corners remain excluded");
        }
        require(result.x > 190 && result.y > 400, "Camera releases the edge smoothly after passing the objects");
        Controller seam;
        const std::vector<Rect> wrappedWall{{390, -200, 32, 600}};
        previous = seam.update(280, 0, 100, 100, wrappedWall, 0, 400, 0);
        for (int frame = 1; frame <= 60; ++frame) {
            result = seam.update(-40, frame * 2.0f, 100, 100, wrappedWall, 1.0f / 60, 400, 0);
            require(std::fabs(result.x - previous.x) < 25 && !overlap(result.x, result.y, 100, 100, wrappedWall, 400, 0),
                "World seams do not cause full-world camera jumps");
            previous = result;
        }
        moving.reset();
        result = moving.update(0, 0, 100, 100, {}, 1.0f / 60);
        require(result.x == 0 && result.y == 0, "Reloads and levels without exclusions keep normal camera behavior");
        std::cout << "Camera exclusion directions, multi-region, zoom and wrapping checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
