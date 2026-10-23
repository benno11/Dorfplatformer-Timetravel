#pragma once

#include "CharacterAppearance.h"
#include "CharacterHairPhysics.h"

namespace CharacterCreator {
inline HairPhysics::CollisionScene hairBodyCollisions(const Appearance& appearance, int facing, bool sliding) {
    Appearance a = appearance;
    a.normalize();
    HairPhysics::CollisionScene scene;
    const float scale = a.build == 0 ? 0.88f : (a.build == 2 ? 1.08f : 1.0f);
    const float direction = facing < 0 ? -1.0f : 1.0f;
    scene.bodySide = facing < 0 ? 1 : -1;
    const float width = a.hairStyle == customPartsHairStyle ? (float)a.hairShape[3] : (a.hairStyle == 2 ? 3.0f : 2.0f);
    const float center = 8 + (3 + width * 0.5f - 8) * scale;
    scene.root = {(facing < 0 ? 16 - center : center) + (sliding ? 2 * direction * scale : 0), 6};
    scene.radius = std::max(0.5f, width * scale * 0.5f);
    // Leave the hair attachment free, while protecting the face and torso below it.
    scene.boxes.push_back({facing < 0 ? 2.0f : 6.0f, 7, 8, 5, true});
    scene.boxes.push_back({4, 13, 8, 14, true});
    return scene;
}

inline void addHairWorldBox(HairPhysics::CollisionScene& scene, const HairPhysics::Box& box,
    const HairPhysics::Box& player, bool oneWay = false) {
    if (player.w <= 0 || player.h <= 0) return;
    scene.worldOrigin = {player.x, player.y};
    scene.worldScale = {player.w / 16, player.h / 28};
    scene.hasWorldTransform = true;
    scene.boxes.push_back({(box.x - player.x) * 16 / player.w, (box.y - player.y) * 28 / player.h,
        box.w * 16 / player.w, box.h * 28 / player.h, false, oneWay});
}

// Read the current tile flags, including wrapped neighbors, rather than caching level geometry.
template<class Map>
inline void addHairTileCollisions(HairPhysics::CollisionScene& scene, const Map& map,
    const HairPhysics::Box& player, float visibleHeight, bool wrapX, bool wrapY, float visibleWidth = 0) {
    if (map.tileSize <= 0 || map.w <= 0 || map.h <= 0 || player.w <= 0 || player.h <= 0) return;
    scene.worldOrigin = {player.x, player.y};
    scene.worldScale = {player.w / 16, player.h / 28};
    scene.hasWorldTransform = true;
    const float tile = (float)map.tileSize;
    const float horizontalReach = std::max(player.w, visibleWidth);
    const int left = (int)std::floor((player.x - horizontalReach) / tile);
    const int right = (int)std::floor((player.x + player.w + horizontalReach) / tile);
    const int top = (int)std::floor((player.y - player.h) / tile);
    const int bottom = (int)std::floor((player.y + std::max(player.h * 3, visibleHeight + player.h)) / tile);
    auto wrap = [](int value, int size) { return (value % size + size) % size; };
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const int tx = wrapX ? wrap(x, map.w) : x;
        const int ty = wrapY ? wrap(y, map.h) : std::clamp(y, 0, map.h - 1);
        bool solid = tx < 0 || tx >= map.w;
        bool semi = false;
        if (!solid) {
            const size_t index = (size_t)ty * map.w + tx;
            solid = index < map.solid.size() && map.solid[index] != 0;
            semi = index < map.semisolid.size() && map.semisolid[index] != 0;
        }
        if (solid || semi) addHairWorldBox(scene, {x * tile, y * tile, tile, solid ? tile : 0.5f}, player, !solid);
    }
}
} // namespace CharacterCreator
