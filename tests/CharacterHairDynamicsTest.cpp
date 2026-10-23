#include "CharacterPartPhysics.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>

int main() {
    using namespace CharacterCreator;
    HairPhysics::CollisionScene torso;
    torso.bodySide = -1;
    torso.boxes.push_back({0, 0, 10, 20, true});
    assert(torso.resolve({12, 10}, {9, 10}).x > 10); // A swinging lock stays on its incoming side.
    assert(torso.resolve({-2, 10}, {1, 10}).x < 0);
    HairPhysics light, heavy, flexible, stiff;
    HairPhysics::Profile lightProfile{12, 1, 0.5f}, heavyProfile{12, 1, 2};
    HairPhysics::Profile flexibleProfile{12, 0.5f, 1}, stiffProfile{12, 2, 1};
    for (int frame = 0; frame < 180; ++frame) {
        light.update(1.0f / 60, 300, 0, 1, nullptr, &lightProfile);
        heavy.update(1.0f / 60, 300, 0, 1, nullptr, &heavyProfile);
        flexible.update(1.0f / 60, 300, 0, 1, nullptr, &flexibleProfile);
        stiff.update(1.0f / 60, 300, 0, 1, nullptr, &stiffProfile);
    }
    assert(std::fabs(light.offset(12).x) > std::fabs(heavy.offset(12).x) + 0.02f);
    assert(std::fabs(flexible.offset(12).x) > std::fabs(stiff.offset(12).x) + 0.02f);
    assert(std::fabs(flexible.offset(12).x) > std::fabs(flexible.offset(1).x));
    const float movingTip = flexible.offset(12).x;
    flexible.update(1.0f / 60, 0, 0, 1, nullptr, &flexibleProfile);
    assert(std::fabs(flexible.offset(12).x) > 0.01f && flexible.offset(12).x != movingTip);
    for (int frame = 0; frame < 600; ++frame)
        flexible.update(1.0f / 60, 0, 0, 1, nullptr, &flexibleProfile);
    assert(std::fabs(flexible.offset(12).x) < 0.01f);

    Appearance appearance;
    appearance.hairStyle = customPartsHairStyle;
    appearance.hairShape = {{3, 4, 10, 3, 0}};
    for (bool overrideLength : {false, true}) {
        appearance.uncapHairLength = overrideLength;
        PartPhysics ascending;
        ascending.update(1.0f / 60, 0, 0, true, false, 1, 1, nullptr, &appearance);
        ascending.headY.velocity = 30; // A downward head bounce while the body still rises.
        ascending.update(1.0f / 60, 0, -20, false, false, 1, 1, nullptr, &appearance);
        assert(ascending.builtInHair.stretch >= 0 && ascending.fringeHair.stretch >= 0);
        ascending.update(1.0f / 60, 0, -1, false, false, 1, 1, nullptr, &appearance);
        assert(ascending.builtInHair.stretch >= 0 && ascending.fringeHair.stretch >= 0);
    }
    appearance.uncapHairLength = false;
    PartPhysics parts;
    for (int frame = 0; frame < 120; ++frame)
        parts.update(1.0f / 60, 300, 0, true, false, 1, 1, nullptr, &appearance);
    assert(parts.fringeHair.restSpacing < parts.builtInHair.restSpacing);
    assert(std::fabs(parts.fringeHair.offset(4).x - parts.builtInHair.offset(4).x) > 0.02f);
    const auto paused = parts;
    parts.update(0, -500, -900, false, false, -1, 1, nullptr, &appearance);
    for (int i = 0; i < HairPhysics::jointCount; ++i) {
        assert(parts.fringeHair.joints[i].x == paused.fringeHair.joints[i].x);
        assert(parts.builtInHair.joints[i].y == paused.builtInHair.joints[i].y);
    }
    HairPhysics::Profile invalid{std::numeric_limits<float>::quiet_NaN(), -100, std::numeric_limits<float>::infinity()};
    flexible.update(1.0f / 60, 1000, -1000, 1, nullptr, &invalid);
    for (const auto& point : flexible.joints) assert(std::isfinite(point.x) && std::isfinite(point.y));
    assert(flexible.joints[0].x == 0 && flexible.joints[0].y == 0);
    HairPhysics rising, falling, stationary;
    HairPhysics::Profile verticalProfile{10, 1, 1};
    HairPhysics slowingAscent;
    slowingAscent.update(1.0f / 60, 0, -100, 1, nullptr, &verticalProfile);
    slowingAscent.update(1.0f / 60, 0, -1, 1, nullptr, &verticalProfile);
    assert(slowingAscent.stretch >= 0); // Slowing upward motion is not a fall.
    for (int frame = 0; frame < 30; ++frame) {
        rising.update(1.0f / 60, 0, -900, 1, nullptr, &verticalProfile);
        falling.update(1.0f / 60, 0, 900, 1, nullptr, &verticalProfile);
        stationary.update(1.0f / 60, 0, 0, 1, nullptr, &verticalProfile);
    }
    const float rise = rising.offset(10).y - stationary.offset(10).y;
    const float fall = stationary.offset(10).y - falling.offset(10).y;
    assert(rise > 0.1f && rise < 0.5f);
    assert(fall > 0.1f && fall < 0.6f);
    const float fallingTip = falling.offset(10).y;
    for (int frame = 0; frame < 12; ++frame) falling.update(1.0f / 60, 0, 0, 1, nullptr, &verticalProfile);
    assert(falling.offset(10).y > fallingTip + 0.1f); // Landing eases back toward rest.
    for (int frame = 0; frame < 240; ++frame) falling.update(1.0f / 60, 0, 0, 1, nullptr, &verticalProfile);
    assert(std::fabs(falling.offset(10).y - stationary.offset(10).y) < 0.05f);

    appearance.uncapHairLength = true;
    appearance.hairShape[2] = 60;
    PartPhysics risingLong, fallingLong;
    for (int frame = 0; frame < 60; ++frame) {
        risingLong.update(1.0f / 60, 0, -900, false, false, 1, 1, nullptr, &appearance);
        fallingLong.update(1.0f / 60, 0, 900, false, false, 1, 1, nullptr, &appearance);
    }
    assert(risingLong.floorHair[1].at(60).y > fallingLong.floorHair[1].at(60).y + 2);
    assert(risingLong.floorHair[1].at(60).y < fallingLong.floorHair[1].at(60).y + 6);
    HairPhysics reference;
    for (int fps : {30, 60, 120, 240}) {
        HairPhysics simulation;
        for (int frame = 0; frame < fps * 2; ++frame)
            simulation.update(1.0f / fps, 300, -200, 1, nullptr, &stiffProfile);
        if (fps == 30) reference = simulation;
        assert(std::fabs(simulation.offset(12).x - reference.offset(12).x) < 0.05f);
        assert(std::fabs(simulation.offset(12).y - reference.offset(12).y) < 0.05f);
    }
}
