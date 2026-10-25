#include "CharacterPartPhysics.h"
#include "CharacterHairCollision.h"
#include "CharacterHairInteraction.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <utility>

int main() {
    using namespace CharacterCreator;
    for (int fps : {30, 60, 120}) for (int facing : {1, -1}) for (int build : {0, 1, 2}) {
        Appearance a;
        a.hairStyle = customPartsHairStyle;
        a.hairShape = {{3, 0, 10, 2, 0}};
        a.build = build;
        PartPhysics parts;
        HairMouseInteraction mouse;
        const float dt = 1.0f / fps;
        for (int i = 0; i < fps; ++i) parts.update(dt, 0, 0, true, false, facing, 1, nullptr, &a);
        const float scale = build == 0 ? 0.88f : (build == 2 ? 1.08f : 1.0f);
        float rootX = 8 + (5 - 8) * scale;
        if (facing < 0) rootX = 16 - rootX;
        const auto p = parts.builtInHair.joints[9];
        HairPhysics::Point pointer{rootX + p.x * scale, 6.5f + p.y};
        // Holding a click that began elsewhere cannot grab a lock accidentally.
        mouse.update(parts, a, Pose::Idle, facing, {-50, -50}, true);
        mouse.update(parts, a, Pose::Idle, facing, pointer, true);
        assert(!mouse.dragging());
        mouse.update(parts, a, Pose::Idle, facing, pointer, false);
        mouse.update(parts, a, Pose::Idle, facing, pointer, true);
        assert(mouse.dragging() && mouse.strand == 1 && !mouse.floor);
        const int grabbed = mouse.joint;
        pointer.x -= facing * 6;
        pointer.y -= 2;
        for (int i = 0; i < fps; ++i) {
            mouse.update(parts, a, Pose::Idle, facing, pointer, true);
            parts.update(dt, 0, 0, true, false, facing, 1, nullptr, &a);
        }
        assert(parts.builtInHair.joints[grabbed].x * facing < -1);
        const auto held = parts.builtInHair.joints[grabbed];
        mouse.update(parts, a, Pose::Idle, facing, pointer, false);
        assert(!mouse.dragging() && parts.builtInHair.mouse.joint == -1);
        assert(parts.builtInHair.joints[grabbed].x == held.x); // Release preserves position/momentum.
        for (int i = 0; i < fps * 3; ++i) parts.update(dt, 0, 0, true, false, facing, 1, nullptr, &a);
        assert(std::fabs(parts.builtInHair.joints[grabbed].x) < std::fabs(held.x));
        assert(parts.builtInHair.joints[0].x == 0 && parts.builtInHair.joints[0].y == 0);
        // Focus/pause cancellation requires a fresh press on return.
        const auto tip = parts.builtInHair.joints[9];
        pointer = {rootX + tip.x * scale, 6.5f + tip.y};
        mouse.update(parts, a, Pose::Idle, facing, pointer, true);
        assert(mouse.dragging());
        mouse.update(parts, a, Pose::Idle, facing, pointer, true, false);
        assert(!mouse.dragging());
        mouse.update(parts, a, Pose::Idle, facing, pointer, true);
        assert(!mouse.dragging());
    }
    // A floor-length lock remains constrained by terrain while being lifted.
    {
        Appearance a;
        a.hairStyle = customPartsHairStyle;
        a.uncapHairLength = true;
        a.hairShape = {{3, 0, 80, 2, 0}};
        PartPhysics parts;
        HairMouseInteraction mouse;
        auto scene = hairBodyCollisions(a, 1, false);
        scene.boxes.push_back({-200, 28, 400, 16});
        for (int i = 0; i < 120; ++i) parts.update(1.0f / 60, 0, 0, true, false, 1, 1, &scene, &a);
        auto pointer = parts.floorHair[1].points[25];
        mouse.update(parts, a, Pose::Idle, 1, pointer, true);
        assert(mouse.dragging() && mouse.floor && mouse.strand == 1);
        const int index = mouse.joint;
        const float originalY = parts.floorHair[1].points[index].y;
        pointer.y -= 12;
        for (int i = 0; i < 120; ++i) {
            mouse.update(parts, a, Pose::Idle, 1, pointer, true);
            parts.update(1.0f / 60, 0, 0, true, false, 1, 1, &scene, &a);
            const auto& rope = parts.floorHair[1];
            for (int j = 1; j < rope.count; ++j) {
                assert(std::isfinite(rope.points[j].x) && std::isfinite(rope.points[j].y));
                assert(rope.points[j].y <= 27.501f);
            }
        }
        assert(parts.floorHair[1].points[index].y < originalY - 1);
        a.hairShape[2] = 0;
        mouse.update(parts, a, Pose::Idle, 1, pointer, true);
        assert(!mouse.dragging() && parts.floorHair[1].mouse.joint == -1);
    }
    // Side hair has an independent solver and floor rope, so changing its
    // length does not inherit or truncate the back-hair simulation.
    {
        Appearance a;
        a.hairStyle = customPartsHairStyle;
        a.uncapHairLength = true;
        a.hairShape = {{3, 0, 40, 2, 0, 60}};
        PartPhysics parts;
        for (int frame = 0; frame < 60; ++frame)
            parts.update(1.0f / 60, 300, 0, true, false, 1, 1, nullptr, &a);
        assert(parts.floorHair[1].initialized && parts.floorHair[1].length == 40);
        assert(parts.floorHair[2].initialized && parts.floorHair[2].length == 60);
        a.uncapHairLength = false;
        a.hairShape[2] = 10;
        a.hairShape[5] = 5;
        parts.update(1.0f / 60, 300, 0, true, false, 1, 1, nullptr, &a);
        assert(parts.sideHair.restSpacing != parts.builtInHair.restSpacing);
    }
    // Facing changes must not reverse world-space spring momentum or mirror
    // the particle chains. Compare a turn with continuing at the same speed.
    for (int initialFacing : {1, -1}) {
        Appearance turnAppearance;
        turnAppearance.hairStyle = customPartsHairStyle;
        turnAppearance.hairShape = {{3, 4, 10, 3, 0}};
        PartPhysics moving;
        const auto originalScene = hairBodyCollisions(turnAppearance, initialFacing, false);
        for (int frame = 0; frame < 120; ++frame)
            moving.update(1.0f / 60, initialFacing * 500.0f, 0, true, false,
                initialFacing, 1, &originalScene, &turnAppearance);
        PartPhysics continuing = moving, turning = moving;
        const auto turnedScene = hairBodyCollisions(turnAppearance, -initialFacing, false);
        continuing.update(1.0f / 240, initialFacing * 500.0f, 0, true, false,
            initialFacing, 1, &originalScene, &turnAppearance);
        turning.update(1.0f / 240, initialFacing * 500.0f, 0, true, false,
            -initialFacing, 1, &turnedScene, &turnAppearance);
        for (auto pair : {std::pair<PartSpring, PartSpring>{continuing.headX, turning.headX},
            {continuing.hair, turning.hair}, {continuing.hatX, turning.hatX}}) {
            assert(std::fabs(pair.first.position + pair.second.position) < 0.00001f);
            assert(std::fabs(pair.first.velocity + pair.second.velocity) < 0.00001f);
        }
        for (int i = 0; i < HairPhysics::jointCount; ++i) {
            assert(std::fabs(continuing.builtInHair.joints[i].x - turning.builtInHair.joints[i].x) < 0.15f);
            assert(std::fabs(continuing.fringeHair.joints[i].x - turning.fringeHair.joints[i].x) < 0.15f);
        }
        assert(turning.builtInHair.joints.back().x * initialFacing < 0);
        const auto pausedTurn = turning;
        turning.update(0, 0, 0, true, false, initialFacing, 1, &originalScene, &turnAppearance);
        assert(turning.previousFacing == pausedTurn.previousFacing);
        assert(turning.hairTurnTime == pausedTurn.hairTurnTime);
        assert(turning.hair.position == pausedTurn.hair.position && turning.hair.velocity == pausedTurn.hair.velocity);
        for (int frame = 0; frame < 30; ++frame)
            turning.update(1.0f / 60, 0, 0, true, false, -initialFacing, 1, &turnedScene, &turnAppearance);
        assert(turning.hairTurnTime == 0);
        assert(turning.builtInHair.joints.back().x * initialFacing > 0); // Settles on the new trailing side.
        for (const auto& joint : turning.builtInHair.joints)
            assert(std::isfinite(joint.x) && std::isfinite(joint.y));
    }
    HairPhysics::CollisionScene turnFloor;
    turnFloor.radius = 0.5f;
    turnFloor.boxes.push_back({-100, 5, 200, 8});
    HairPhysics turningAtFloor;
    HairPhysics::Profile floorProfile{10, 1, 1};
    for (int frame = 0; frame < 30; ++frame) {
        turningAtFloor.update(1.0f / 60, 500, 900, 1, &turnFloor, &floorProfile,
            900, std::max(0.0f, 0.15f - frame / 60.0f));
        for (const auto& joint : turningAtFloor.joints)
            assert(joint.y <= 4.5f); // Turn easing cannot soften world collisions.
    }
    // The shortest authored lock must not secretly simulate twice its length.
    HairPhysics singlePixel;
    HairPhysics::Profile singleProfile{1, 1, 1};
    singlePixel.update(1.0f / 60, 0, 0, 1, nullptr, &singleProfile);
    assert(std::fabs(singlePixel.restSpacing * (HairPhysics::jointCount - 1) - 1) < 0.0001f);
    // Ordinary-length locks must visibly trail at running speed. Previously the
    // fixed lateral cap and weak airflow left a 20-pixel lock moving by about
    // half a pixel, making every length below the floor-rope cutoff look rigid.
    for (float length : {1.0f, 4.0f, 10.0f, 20.0f}) {
        HairPhysics movingHair;
        HairPhysics::Profile profile{length, 1, 1};
        movingHair.update(1.0f / 60, 0, 0, 1, nullptr, &profile);
        for (int frame = 0; frame < 30; ++frame)
            movingHair.update(1.0f / 60, 500, 0, 1, nullptr, &profile);
        assert(movingHair.offset(length).x < -std::min(1.0f, length * 0.2f));
    }
    // Uncapping must not replace short-hair physics with the floor rope. Include
    // wide locks, jumps, turns, and shrinking a previously floor-length strand.
    for (int length : {0, 1, 2, 3, 4, 6, normalHairLengthLimit}) for (int width : {1, 4}) {
        Appearance capped;
        capped.hairStyle = customPartsHairStyle;
        capped.hairShape = {{3, length, length, width, 0}};
        auto uncapped = capped;
        uncapped.uncapHairLength = true;
        PartPhysics normal, overrideParts;
        for (int frame = 0; frame < 120; ++frame) {
            const int facing = frame < 60 ? 1 : -1;
            const float vx = facing * 300.0f;
            const float vy = frame < 40 ? -500.0f : (frame < 80 ? 500.0f : 0);
            const auto scene = hairBodyCollisions(capped, facing, false);
            normal.update(1.0f / 60, vx, vy, vy == 0, false, facing, 1, &scene, &capped);
            overrideParts.update(1.0f / 60, vx, vy, vy == 0, false, facing, 1, &scene, &uncapped);
            assert(!overrideParts.floorHair[0].initialized && !overrideParts.floorHair[1].initialized);
            for (int i = 0; i < HairPhysics::jointCount; ++i) {
                const auto a = normal.builtInHair.joints[i], b = overrideParts.builtInHair.joints[i];
                assert(std::isfinite(a.x) && std::isfinite(a.y));
                assert(a.x == b.x && a.y == b.y);
            }
        }
        uncapped.hairShape[2] = 80;
        overrideParts.update(1.0f / 60, 0, 0, true, false, 1, 1, nullptr, &uncapped);
        assert(overrideParts.floorHair[1].initialized);
        uncapped.hairShape[2] = length;
        overrideParts.update(1.0f / 60, 0, 0, true, false, 1, 1, nullptr, &uncapped);
        assert(!overrideParts.floorHair[1].initialized);
    }
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
