#include "BackgroundCamera.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void expect(float actual, float expected) {
    if (std::fabs(actual - expected) > 0.001f)
        throw std::runtime_error("Background camera jumped at a level edge");
}

int main() {
    BackgroundCamera camera;
    camera.update(990, 795, 1000, 800, true, true);
    camera.update(5, 4, 1000, 800, true, true);
    expect(camera.x, 1005);
    expect(camera.y, 804);
    camera.update(990, 795, 1000, 800, true, true);
    expect(camera.x, 990);
    expect(camera.y, 795);

    // Multiple laps must preserve movement rather than reset the background.
    for (int step = 1; step <= 300; ++step) {
        const float x = 990 + step * 17;
        const float y = 795 + step * 13;
        camera.update(std::fmod(x, 1000), std::fmod(y, 800), 1000, 800, true, true);
        expect(camera.x, x);
        expect(camera.y, y);
    }

    // A new level starts at its own camera position.
    camera.reset();
    camera.update(5, 4, 1000, 800, true, false);
    camera.update(990, 795, 1000, 800, true, false);
    expect(camera.x, -10);
    expect(camera.y, 795); // The non-wrapping axis follows the camera normally.
    camera.update(800, 600, 1000, 800, false, false);
    expect(camera.x, -200);
    expect(camera.y, 600);

    camera.reset();
    camera.update(0, 0, 0, 0, true, true);
    camera.update(20, -20, 0, 0, true, true);
    expect(camera.x, 20);
    expect(camera.y, -20);
    std::cout << "Background camera wrap tests passed\n";
}
