#pragma once
#include "CharacterPartPhysics.h"

namespace CharacterCreator {
// Input is in the renderer's 16 x 28 sprite coordinates, with world-facing X.
// Picking only on a new press prevents dragging a menu click onto the hair.
struct HairMouseInteraction {
    using Point = HairPhysics::Point;
    int strand = -1, joint = -1;
    bool wasDown = false, floor = false;
    std::array<int, 9> shape{};

    bool dragging() const { return strand >= 0; }
    void release(PartPhysics& physics) {
        strand = joint = -1;
        physics.builtInHair.mouse = {};
        physics.fringeHair.mouse = {};
        physics.sideHair.mouse = {};
        for (auto& rope : physics.floorHair) rope.mouse = {};
    }

    void update(PartPhysics& physics, Appearance appearance, Pose pose, int facing,
        Point pointer, bool down, bool enabled = true) {
        appearance.normalize();
        const std::array<int, 9> currentShape{{appearance.hairStyle, appearance.build, (int)appearance.uncapHairLength,
            appearance.hairShape[0], appearance.hairShape[1], appearance.hairShape[2], appearance.hairShape[3],
            appearance.hairShape[4], appearance.hairShape[5]}};
        const bool pressed = down && !wasDown;
        wasDown = down;
        const bool changed = currentShape != shape;
        shape = currentShape;
        if (changed) release(physics);
        if (!enabled || !down || !std::isfinite(pointer.x) || !std::isfinite(pointer.y) ||
            pose == Pose::Death || appearance.hairStyle == 3 || appearance.hairStyle == customHairStyle ||
            !appearance.sprites.base.empty()) {
            release(physics);
            return;
        }
        const float scale = appearance.build == 0 ? 0.88f : (appearance.build == 2 ? 1.08f : 1.0f);
        const float lean = (pose == Pose::Slide ? 2.0f : 0) + physics.headX.position;
        const float headY = 3 + physics.headY.position * 1.3f;
        const bool custom = appearance.hairStyle == customPartsHairStyle;
        const float width = custom ? (float)appearance.hairShape[3] : (appearance.hairStyle == 2 ? 3.0f : 2.0f);
        auto origin = [&](int lock) {
            const float left = lock == 0 ? 3.0f : (lock == 1 ? 4.0f : 13.0f - width);
            float x = 8 + (left + lean + width * 0.5f - 8) * scale;
            if (facing < 0) x = 16 - x;
            return Point{x, headY + (lock == 0 ? (custom ? appearance.hairShape[0] : 3) : (custom ? 3 : 5)) + 0.5f};
        };
        auto& back = physics.builtInHair;
        if (pressed) {
            release(physics);
            float best = (width * scale * 0.5f + 1.5f) * (width * scale * 0.5f + 1.5f);
            const int lockCount = custom ? 3 : 2;
            for (int lock = 0; lock < lockCount; ++lock) {
                const int length = custom ? appearance.hairShape[lock == 2 ? 5 : lock + 1] :
                    (lock == 0 ? 3 : (appearance.hairStyle == 2 ? 7 : 0));
                if (length <= 0 || width <= 0) continue;
                const auto& rope = physics.floorHair[lock];
                const bool persistent = rope.initialized;
                const auto& hair = lock == 0 ? physics.fringeHair : (lock == 1 ? back : physics.sideHair);
                const auto root = origin(lock);
                const float start = !custom && lock == 1 ? 2.0f : 0.0f;
                const int count = persistent ? rope.count : HairPhysics::jointCount;
                for (int i = 1; i < count; ++i) {
                    const float distance = i * hair.restSpacing;
                    if (!persistent && (distance < start || distance > start + length)) continue;
                    const Point point = persistent ? rope.points[i] : Point{
                        root.x + hair.joints[i].x * scale,
                        root.y + hair.joints[i].y - start};
                    const float dx = point.x - pointer.x, dy = point.y - pointer.y;
                    if (dx * dx + dy * dy >= best) continue;
                    best = dx * dx + dy * dy;
                    strand = lock; joint = i; floor = persistent;
                }
            }
        }
        if (!dragging()) return;
        auto& hair = strand == 0 ? physics.fringeHair : (strand == 1 ? back : physics.sideHair);
        auto& rope = physics.floorHair[strand];
        if (floor != rope.initialized || (floor && joint >= rope.count)) { release(physics); return; }
        if (floor) rope.mouse = {joint, pointer};
        else {
            const auto root = origin(strand);
            const float start = !custom && strand == 1 ? 2.0f : 0.0f;
            hair.mouse = {joint, {(pointer.x - root.x) / scale, pointer.y - root.y + start}};
        }
    }
};
} // namespace CharacterCreator
