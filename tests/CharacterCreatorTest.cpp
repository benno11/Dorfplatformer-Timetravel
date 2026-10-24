#include "CharacterRenderer.h"
#include "CharacterAttributes.h"
#include "CharacterFile.h"
#include "CharacterSpriteImport.h"
#include "CharacterHairCollision.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <limits>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    using namespace CharacterCreator;
    Appearance a;
    assert(!a.enabled);
    for (const char* part : {"enabled", "skin", "hair_color", "hair_style", "shirt", "pants", "hat", "accent"})
        assert(change(a, part));
    assert(a.enabled);
    const auto document = save(a);
    assert(document["experimental"] == true && document["version"] == 1);
    assert(save(load(nlohmann::json::parse(document.dump()))) == document);
    assert(!change(a, "unknown"));
    assert(save(a) == document);
    assert(change(a, "reset") && !a.enabled);
    assert(save(a) == save(Appearance{}));
    for (int i = 0; i < (int)skinColors.size(); ++i) change(a, "skin");
    assert(a.skin == 0);
    for (int i = 0; i < (int)hats.size(); ++i) change(a, "hat");
    assert(a.hat == 0);
    const auto malformed = load({{"skin", -200}, {"hair_style", "bad"}, {"hat", 999999},
        {"shirt", std::numeric_limits<uint64_t>::max()}, {"enabled", "true"}});
    assert(malformed.skin == 0 && malformed.hairStyle == 0 && malformed.hat == 3);
    assert(malformed.shirt == 9 && !malformed.enabled);
    assert(!load(nullptr).enabled && !load({{"version", 2}, {"enabled", true}}).enabled);
    for (const auto& preset : placeholderCharacters) {
        assert(change(a, std::string("preset:") + preset.id));
        assert(a.enabled && save(a) == save(preset.appearance));
        assert(save(load(save(a))) == save(a));
        const auto original = save(a);
        assert(change(a, "shirt") && save(a) != original);
    }
    const auto beforeUnknownPreset = save(a);
    assert(!change(a, "preset:missing") && save(a) == beforeUnknownPreset);
    a = Appearance{};
    a.enabled = true;
    a.speed = 4;
    a.acceleration = 0;
    a.jump = 3;
    a.gravity = 1;
    a.springiness = 4;
    a.build = 2;
    const MovementConfig base;
    const auto customMovement = movementFor(a, base, true);
    assert(std::fabs(customMovement.maxSpeedGround - base.maxSpeedGround * 1.25f) < 0.001f);
    assert(std::fabs(customMovement.accelGround - base.accelGround * 0.75f) < 0.001f);
    assert(std::fabs(customMovement.jumpSpeed - base.jumpSpeed * 1.1f) < 0.001f);
    assert(std::fabs(customMovement.gravityGround - base.gravityGround * 0.9f) < 0.001f);
    assert(movementFor(a, base, false).maxSpeedGround == base.maxSpeedGround);
    a.enabled = false;
    assert(movementFor(a, base, true).jumpSpeed == base.jumpSpeed);
    assert(save(load(save(a))) == save(a));
    assert(change(a, "reset_attributes") && a.build == 1 && a.speed == 2 && a.springiness == 2);
    assert(!change(a, "texture:missing"));
    for (const char* part : partIds) assert(change(a, std::string("texture:") + part));
    assert(save(load(save(a))) == save(a));
    const auto invalidTexture = load({{"textures", {6, -1, 100, "bad"}},
        {"texture_pixels", {"invalid", nullptr}}, {"speed", 9999}, {"gravity", -1}});
    assert(invalidTexture.textures[0] == 0 && invalidTexture.textures[1] == 0 &&
        invalidTexture.textures[2] == 0 && invalidTexture.speed == 4 && invalidTexture.gravity == 0);

    PartPhysics parts;
    parts.update(1.0f / 60, 0, 0, true, false, 1);
    parts.update(1.0f / 60, 700, 0, true, false, 1);
    for (int frame = 0; frame < 30; ++frame) parts.update(1.0f / 60, 700, 0, true, false, 1);
    assert(parts.headX.position < -0.1f && parts.hair.position < -0.5f);
    const auto frozen = parts;
    parts.update(0, -700, 1000, false, true, -1);
    assert(parts.hair.position == frozen.hair.position && parts.hatY.velocity == frozen.hatY.velocity);
    for (int frame = 0; frame < 360; ++frame) parts.update(1.0f / 60, 0, 0, true, false, 1);
    assert(std::fabs(parts.hair.position) < 0.001f && std::fabs(parts.arms.position) < 0.001f);
    PartPhysics landing;
    landing.update(1.0f / 60, 0, 1200, false, false, 1);
    landing.update(1.0f / 60, 0, 0, true, false, 1);
    assert(landing.headY.velocity > 0 && landing.hatY.velocity < 0);
    PartPhysics reference;
    for (int fps : {30, 60, 120, 240}) {
        PartPhysics simulation;
        simulation.update(1.0f / fps, 0, 0, true, false, 1);
        for (int frame = 1; frame <= fps * 2; ++frame) {
            const float speed = std::min(500.0f, frame * 1000.0f / fps);
            simulation.update(1.0f / fps, speed, 0, true, false, 1);
        }
        if (fps == 30) reference = simulation;
        assert(std::fabs(simulation.headX.position - reference.headX.position) < 0.04f);
        assert(std::fabs(simulation.hair.position - reference.hair.position) < 0.08f);
    }
    for (int frame = 0; frame < 2000; ++frame) {
        parts.update(frame % 4 ? 1.0f / 240 : 5.0f, frame % 2 ? 100000 : -100000,
            frame % 3 ? 2000 : -2000, frame % 7 == 0, frame % 5 == 0, frame % 2 ? 1 : -1);
        assert(std::isfinite(parts.hair.position) && std::fabs(parts.hair.position) <= 2.2f);
        assert(std::isfinite(parts.arms.velocity) && std::fabs(parts.arms.position) <= 0.85f);
        assert(std::isfinite(parts.hatY.position) && std::fabs(parts.hatY.position) <= 1.5f);
    }
    parts.update(std::numeric_limits<float>::quiet_NaN(), 0, 0, true, false, 1);
    assert(!parts.initialized && parts.hair.position == 0);
    assert(!parts.builtInHair.initialized);

    HairPhysics strand;
    strand.update(1.0f / 60, 0, 0);
    for (int frame = 0; frame < 30; ++frame) strand.update(1.0f / 60, 700, 0);
    assert(strand.joints[0].x == 0 && strand.joints[0].y == 0);
    assert(strand.offset(10).x < -0.1f);
    assert(std::fabs(strand.offset(2).x - strand.offset(10).x) > 0.1f);
    const auto pausedStrand = strand;
    strand.update(0, -700, -1000);
    for (int i = 0; i < HairPhysics::jointCount; ++i) {
        assert(strand.joints[i].x == pausedStrand.joints[i].x && strand.joints[i].y == pausedStrand.joints[i].y);
    }
    // Facing changes must not reverse the strand's world-space inertia.
    PartPhysics facingRight, facingLeft;
    for (int frame = 0; frame < 60; ++frame) {
        facingRight.update(1.0f / 60, 500, 0, true, false, 1);
        facingLeft.update(1.0f / 60, 500, 0, true, false, -1);
    }
    assert(facingRight.builtInHair.offset(10).x == facingLeft.builtInHair.offset(10).x);
    // Sudden stops retain momentum before the strand settles under gravity.
    const float movingTip = strand.offset(10).x;
    strand.update(1.0f / 60, 0, 0);
    assert(std::fabs(strand.offset(10).x) > 0.01f && strand.offset(10).x != movingTip);
    for (int frame = 0; frame < 1200; ++frame) strand.update(1.0f / 60, 0, 0);
    assert(std::fabs(strand.offset(10).x) < 0.01f);
    HairPhysics jumpingStrand = pausedStrand, groundedStrand = pausedStrand;
    for (int frame = 0; frame < 30; ++frame) {
        jumpingStrand.update(1.0f / 60, 700, -900);
        groundedStrand.update(1.0f / 60, 700, 0);
    }
    assert(std::fabs(jumpingStrand.offset(10).y - groundedStrand.offset(10).y) > 0.001f);
    HairPhysics strandReference;
    for (int fps : {30, 60, 120, 240}) {
        HairPhysics simulation;
        simulation.update(1.0f / fps, 0, 0);
        for (int frame = 1; frame <= fps * 2; ++frame)
            simulation.update(1.0f / fps, std::min(500.0f, frame * 1000.0f / fps), 0);
        if (fps == 30) strandReference = simulation;
        assert(std::fabs(simulation.offset(10).x - strandReference.offset(10).x) < 0.15f);
    }
    for (int frame = 0; frame < 1000; ++frame) {
        strand.update(frame % 3 ? 1.0f / 240 : 5, frame % 2 ? 100000 : -100000,
            frame % 3 ? 100000 : -100000);
        for (const auto& joint : strand.joints) {
            assert(std::isfinite(joint.x) && std::fabs(joint.x) <= 2.8f);
            assert(std::isfinite(joint.y) && std::fabs(joint.y) <= HairPhysics::reach + 3.75f);
        }
    }
    strand.update(std::numeric_limits<float>::quiet_NaN(), 0, 0);
    assert(!strand.initialized && strand.offset(10).x == 0);
    // Purely vertical movement stretches and compresses a straight strand.
    strand.update(1.0f / 60, 0, 0);
    for (int frame = 0; frame < 20; ++frame) strand.update(1.0f / 60, 0, -900);
    const float risingHeight = strand.offset(10).y;
    for (int frame = 0; frame < 20; ++frame) strand.update(1.0f / 60, 0, 900);
    assert(std::fabs(strand.offset(10).y - risingHeight) > 0.1f);

    HairPhysics::CollisionScene floorScene;
    floorScene.radius = 0.5f;
    floorScene.boxes.push_back({-100, 10, 200, 4});
    assert(floorScene.resolve({0, 0}, {0, 30}).y < 9.5f);
    HairPhysics::CollisionScene wallScene;
    wallScene.radius = 0.5f;
    wallScene.boxes.push_back({2, -100, 4, 200});
    const auto slide = wallScene.resolve({0, 0}, {10, 5});
    assert(slide.x < 1.5f && std::fabs(slide.y - 5) < 0.01f);
    wallScene.boxes.push_back({-100, 3, 200, 4});
    const auto corner = wallScene.resolve({0, 0}, {10, 10});
    assert(corner.x < 1.5f && corner.y < 2.5f);
    HairPhysics::CollisionScene ceilingScene;
    ceilingScene.radius = 0.5f;
    ceilingScene.boxes.push_back({-100, -5, 200, 1});
    assert(ceilingScene.resolve({0, 0}, {0, -20}).y > -3.5f);
    HairPhysics::CollisionScene platformScene;
    platformScene.radius = 0.5f;
    platformScene.boxes.push_back({-100, 5, 200, 0.5f, false, true});
    assert(platformScene.resolve({0, 0}, {0, 10}).y < 4.5f);
    assert(platformScene.resolve({0, 10}, {0, 0}).y == 0);
    HairPhysics::CollisionScene embeddedScene;
    embeddedScene.radius = 0.5f;
    embeddedScene.boxes.push_back({0, 0, 10, 10});
    const auto ejected = embeddedScene.resolve({3, 3}, {3, 3});
    assert(ejected.x < -0.5f || ejected.y < -0.5f);
    // Overlapping expanded tiles must eject sideways from a wall, never up a seam.
    for (int facing : {1, -1}) for (bool reverse : {false, true}) {
        HairPhysics::CollisionScene tiledWall;
        tiledWall.radius = 1;
        for (int row = -4; row <= 4; ++row)
            tiledWall.boxes.push_back({facing == 1 ? 2.0f : -6.0f, row * 4.0f, 4, 4});
        if (reverse) std::reverse(tiledWall.boxes.begin(), tiledWall.boxes.end());
        for (float y : {0.0f, 0.25f, 3.75f, 4.0f, 4.25f}) {
            const HairPhysics::Point inside{facing * 2.5f, y};
            const auto recovered = tiledWall.resolve(inside, inside);
            assert(recovered.x * facing < 1.0f);
            assert(std::fabs(recovered.y - y) < 0.001f);
        }
    }
    HairPhysics collidingStrand;
    for (int frame = 0; frame < 120; ++frame) collidingStrand.update(1.0f / 60, 0, 0, 1, &floorScene);
    for (const auto& joint : collidingStrand.joints) assert(joint.y <= 9.5f && std::isfinite(joint.x));
    const auto bodyScene = hairBodyCollisions(Appearance{}, 1, false);
    assert(!bodyScene.boxes.empty() && !bodyScene.blocksWorld(6, 15, 1, 1));
    assert(bodyScene.resolve({0, 0}, {3, 12}).x != 3);
    // A root just above the inflated head must escape sideways, not catch on its top.
    Appearance snagHair;
    snagHair.hairStyle = customPartsHairStyle;
    snagHair.hairShape[3] = 4;
    for (int facing : {1, -1}) {
        auto snagScene = hairBodyCollisions(snagHair, facing, true);
        snagScene.root.y = 4.5f;
        const auto released = snagScene.resolve({0, 0}, {0, 2});
        assert(std::fabs(released.y - 2) < 0.001f);
        assert(facing == 1 ? released.x < 0 : released.x > 0);
    }
    // Recovery must work across builds, hair widths, turns, and slide/jump transitions.
    for (int build = 0; build < 3; ++build) for (int width = 1; width <= 4; ++width)
        for (int facing : {1, -1}) for (bool sliding : {false, true}) {
            snagHair.build = build;
            snagHair.hairShape[3] = width;
            PartPhysics recovery;
            for (int frame = 0; frame < 240; ++frame) {
                const bool airborne = frame >= 30 && frame < 90;
                const float vy = airborne ? (frame < 60 ? -900.0f : 900.0f) : 0;
                const int direction = frame < 120 ? facing : -facing;
                const auto scene = hairBodyCollisions(snagHair, direction, sliding && frame < 120);
                recovery.update(1.0f / 60, 0, vy, !airborne, sliding && frame < 120, direction, 1, &scene);
            }
            assert(recovery.builtInHair.joints[0].x == 0 && recovery.builtInHair.joints[0].y == 0);
            assert(recovery.builtInHair.joints.back().y > HairPhysics::reach * 0.65f);
            assert(std::isfinite(recovery.builtInHair.joints.back().x));
        }

    struct HairTestMap {
        int tileSize = 32, w = 3, h = 3;
        std::vector<unsigned char> solid = std::vector<unsigned char>(9, 0);
        std::vector<unsigned char> semisolid = std::vector<unsigned char>(9, 0);
    } hairMap;
    hairMap.solid[4] = 1;
    HairPhysics::CollisionScene tileScene;
    addHairTileCollisions(tileScene, hairMap, {32, 0, 16, 28}, 64, false, false);
    assert(tileScene.blocksWorld(1, 33, 1, 1));
    hairMap.solid[4] = 0;
    tileScene = {};
    addHairTileCollisions(tileScene, hairMap, {32, 0, 16, 28}, 64, false, false);
    assert(!tileScene.blocksWorld(1, 33, 1, 1));
    tileScene = {};
    addHairTileCollisions(tileScene, hairMap, {-16, 0, 16, 28}, 64, true, false);
    assert(!tileScene.blocksWorld(1, 1, 1, 1));
    hairMap.solid[2] = 1;
    tileScene = {};
    addHairTileCollisions(tileScene, hairMap, {-16, 0, 16, 28}, 64, true, false);
    assert(tileScene.blocksWorld(1, 1, 1, 1));

    Appearance extentHair;
    extentHair.hairStyle = customPartsHairStyle;
    extentHair.uncapHairLength = true;
    extentHair.hairShape = {{3, 0, 100, 2, 0}};
    const SDL_FRect viewport{0, 0, 640, 224};
    const SDL_FRect bodyAbove{8, -60, 16, 28};
    assert(!SDL_HasRectIntersectionFloat(&bodyAbove, &viewport));
    auto hairBounds = visualBounds(extentHair, bodyAbove);
    assert(SDL_HasRectIntersectionFloat(&hairBounds, &viewport));
    PartPhysics stretchedHair;
    stretchedHair.builtInHair.joints.back().y += 3;
    assert(visualBounds(extentHair, bodyAbove, Pose::Idle, 0, &stretchedHair).h > hairBounds.h);
    for (const SDL_FRect outside : {SDL_FRect{-17, -60, 16, 28}, SDL_FRect{641, -60, 16, 28},
        SDL_FRect{8, 225, 16, 28}, SDL_FRect{8, -200, 16, 28}}) {
        hairBounds = visualBounds(extentHair, outside);
        assert(!SDL_HasRectIntersectionFloat(&hairBounds, &viewport));
    }
    hairBounds = visualBounds(extentHair, bodyAbove, Pose::Death);
    assert(hairBounds.h == bodyAbove.h);
    extentHair.uncapHairLength = false;
    assert(visualBounds(extentHair, bodyAbove).h == bodyAbove.h);
    extentHair.uncapHairLength = true;
    extentHair.hairShape[1] = std::numeric_limits<int>::max();
    hairBounds = visualBounds(extentHair, bodyAbove);
    assert(std::isfinite(hairBounds.h) && hairBounds.h > 10000 && hairBounds.h < 10100 && hairBounds.y + hairBounds.h > viewport.y);
    extentHair.hairShape[1] = 100;
    extentHair.hairShape[2] = 0; // Fringe alone also extends the visible bounds.
    hairBounds = visualBounds(extentHair, bodyAbove);
    assert(SDL_HasRectIntersectionFloat(&hairBounds, &viewport));

    assert(SDL_Init(0));
    SDL_Surface* tile = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA8888);
    assert(tile);
    assert(SDL_FillSurfaceRect(tile, nullptr, SDL_MapSurfaceRGBA(tile, 200, 50, 100, 128)));
    const std::string imported = textureFromSurface(tile);
    assert(validTexturePixels(imported));
    a.texturePixels[(int)Part::Hat] = imported;
    a.textures[(int)Part::Hat] = 6;
    const auto hatPixel = texturePixel(a, Part::Hat, colors[0], 0, 0);
    assert(hatPixel.r == 200 && hatPixel.g == 50 && hatPixel.b == 100 && hatPixel.a == 128);
    assert(a.textures[(int)Part::Shirt] == 1); // Importing a hat leaves the shirt alone.
    assert(save(load(save(a))) == save(a));
    assert(textureFromSurface(nullptr).empty());
    // Hair carries a silhouette and alpha independently of tiled part textures.
    a.hairPixels = pixelsFromSurface(tile, 16, 16);
    assert(validHairPixels(a.hairPixels));
    a.hairStyle = customHairStyle;
    assert(save(load(save(a))) == save(a));
    auto hairCopy = a;
    assert(change(hairCopy, "hair_style") && hairCopy.hairStyle == customPartsHairStyle);
    assert(change(hairCopy, "hair_style") && hairCopy.hairStyle == 0);
    for (int i = 0; i < customHairStyle; ++i) assert(change(hairCopy, "hair_style"));
    assert(hairCopy.hairStyle == customHairStyle);
    assert(change(hairCopy, "clear_hair") && hairCopy.hairPixels.empty() && hairCopy.hairStyle == 0);
    for (int i = 0; i < customHairStyle; ++i) assert(change(hairCopy, "hair_style"));
    assert(hairCopy.hairStyle == customPartsHairStyle);
    assert(change(hairCopy, "hair_style") && hairCopy.hairStyle == 0);
    assert(load({{"hair_style", customHairStyle}, {"hair_pixels", "invalid"}}).hairStyle == 0);
    auto legacyHair = save(hairCopy);
    legacyHair.erase("hair_pixels");
    assert(save(load(legacyHair)) == save(hairCopy));
    legacyHair.erase("hair_shape");
    assert(load(legacyHair).hairShape == defaultHairShape);
    assert(change(hairCopy, "use_hair_parts") && hairCopy.hairStyle == customPartsHairStyle);
    for (int i = 0; i < (int)hairShapeIds.size(); ++i) {
        const int start = hairCopy.hairShape[i];
        const int count = hairShapeMaximum[i] + (i == 3 ? 0 : 1);
        for (int step = 0; step < count; ++step)
            assert(change(hairCopy, std::string("hair_shape:") + hairShapeIds[i]));
        assert(hairCopy.hairShape[i] == start);
    }
    assert(!change(hairCopy, "hair_shape:missing"));
    hairCopy.hairShape = {{5, 6, 10, 4, 3}};
    Appearance partsTransferred;
    assert(importCharacter(exportCharacter(hairCopy), partsTransferred) && save(partsTransferred) == save(hairCopy));
    const auto beforeInvalidShape = save(partsTransferred);
    for (const auto& invalid : {nlohmann::json(42), nlohmann::json::array({1, 2}),
        nlohmann::json::array({3, 3, 0, 0, 0}), nlohmann::json::array({3, 3, 21, 2, 0})}) {
        auto badShape = nlohmann::json::parse(exportCharacter(hairCopy));
        badShape["character"]["hair_shape"] = invalid;
        assert(!importCharacter(badShape.dump(), partsTransferred) && save(partsTransferred) == beforeInvalidShape);
    }
    assert((load({{"hair_shape", {-10, 99, 99, 0, 99}}}).hairShape == std::array<int, 5>{{0, 20, 20, 1, 3}}));
    assert(change(hairCopy, "reset_hair_parts") && hairCopy.hairShape == defaultHairShape);
    assert(change(hairCopy, "uncap_hair_length") && hairCopy.uncapHairLength);
    hairCopy.hairShape[1] = 20;
    hairCopy.hairShape[2] = 20;
    assert(change(hairCopy, "hair_shape:fringe") && hairCopy.hairShape[1] == 21);
    assert(change(hairCopy, "hair_shape:length") && hairCopy.hairShape[2] == 21);
    hairCopy.hairShape[1] = 500;
    hairCopy.hairShape[2] = std::numeric_limits<int>::max();
    assert(change(hairCopy, "hair_shape:length") && hairCopy.hairShape[2] == 10000);
    assert(importCharacter(exportCharacter(hairCopy), partsTransferred) && save(partsTransferred) == save(hairCopy));
    assert(change(hairCopy, "hair_shorten:length") && hairCopy.hairShape[2] == 9999);
    auto oversizedHair = nlohmann::json::parse(exportCharacter(hairCopy));
    oversizedHair["character"]["hair_shape"][2] = 10001;
    const auto beforeOversizedHair = save(partsTransferred);
    assert(!importCharacter(oversizedHair.dump(), partsTransferred) && save(partsTransferred) == beforeOversizedHair);
    const auto clampedHair = load({{"uncap_hair_length", true}, {"hair_shape", {3, 10001, 999999, 2, 0}}});
    assert(clampedHair.hairShape[1] == 10000 && clampedHair.hairShape[2] == 10000);
    hairCopy.hairShape[1] = 0;
    assert(change(hairCopy, "hair_shorten:fringe") && hairCopy.hairShape[1] == 0);
    hairCopy.hairShape[1] = 500;
    assert(change(hairCopy, "uncap_hair_length") && !hairCopy.uncapHairLength);
    assert(hairCopy.hairShape[1] == 20 && hairCopy.hairShape[2] == 20);
    assert(change(hairCopy, "uncap_hair_length"));
    assert(change(hairCopy, "reset_hair_parts") && !hairCopy.uncapHairLength && hairCopy.hairShape == defaultHairShape);
    auto invalidUncap = nlohmann::json::parse(exportCharacter(hairCopy));
    invalidUncap["character"]["uncap_hair_length"] = "true";
    const auto beforeInvalidUncap = save(partsTransferred);
    assert(!importCharacter(invalidUncap.dump(), partsTransferred) && save(partsTransferred) == beforeInvalidUncap);
    legacyHair.erase("uncap_hair_length");
    assert(!load(legacyHair).uncapHairLength);
    a.speed = 4;
    a.jump = 0;
    Appearance transferred;
    assert(importCharacter(exportCharacter(a), transferred) && save(transferred) == save(a));
    const auto beforeBadImport = save(transferred);
    for (const auto& invalid : {nlohmann::json("bad"), nlohmann::json(42), nlohmann::json("")}) {
        auto badHair = nlohmann::json::parse(exportCharacter(a));
        badHair["character"]["hair_pixels"] = invalid;
        assert(!importCharacter(badHair.dump(), transferred) && save(transferred) == beforeBadImport);
    }
    for (const std::string& bad : {std::string("not json"), std::string("{}"),
        std::string(262145, 'x'), std::string("{\"format\":\"df-character\",\"version\":2}")}) {
        assert(!importCharacter(bad, transferred) && save(transferred) == beforeBadImport);
    }
    auto corrupted = nlohmann::json::parse(exportCharacter(a));
    corrupted["character"]["texture_pixels"][4] = "broken pixels";
    assert(!importCharacter(corrupted.dump(), transferred) && save(transferred) == beforeBadImport);
    const auto filePath = std::filesystem::temp_directory_path() /
        ("df-character-test-" + std::to_string(SDL_GetTicksNS()) + ".dfcharacter");
    assert(writeCharacterFile(filePath.u8string(), a));
    assert(readCharacterFile(filePath.u8string(), transferred) && save(transferred) == save(a));
    assert(std::filesystem::remove(filePath));
    assert(!readCharacterFile(filePath.u8string(), transferred) && save(transferred) == save(a));
    const auto packPath = filePath.string() + ".pack.json";
    const auto layerPath = filePath.string() + ".bmp";
    SDL_Surface* atlas = SDL_CreateSurface(176, 28, SDL_PIXELFORMAT_RGBA8888);
    assert(atlas);
    for (int frame = 0; frame < 11; ++frame) {
        const SDL_Rect rect{frame * 16, 0, 16, 28};
        assert(SDL_FillSurfaceRect(atlas, &rect, SDL_MapSurfaceRGBA(atlas, (Uint8)(frame * 20), 80, 120, 255)));
    }
    assert(SDL_SaveBMP(atlas, layerPath.c_str()));
    SDL_DestroySurface(atlas);
    std::vector<std::string> order;
    for (auto name : spriteFrameNames) order.push_back(name);
    std::swap(order[0], order[1]);
    const std::string layerName = std::filesystem::path(layerPath).filename().u8string();
    nlohmann::json pack{{"characters", {{"example", {{"frame_width", 16}, {"frame_height", 28},
        {"order", order}, {"hair_base", layerName}, {"bangs_overlay", layerName}}}}}};
    auto writePack = [&]() { std::ofstream out(packPath); out << pack.dump(); };
    writePack();
    std::vector<Appearance> packCharacters;
    const auto bitmapLoader = [](const char* path) { return SDL_LoadBMP(path); };
    assert(readSpritePack(packPath, packCharacters, bitmapLoader) && packCharacters.size() == 1);
    assert(packCharacters[0].sprites.valid() && packCharacters[0].enabled);
    // Non-canonical manifest ordering must map back to the correct animation frames.
    assert(rgbaPixel(packCharacters[0].sprites.base, 0).r == 20);
    assert(rgbaPixel(packCharacters[0].sprites.base, 16).r == 0);
    const auto beforeBadPack = save(packCharacters[0]);
    pack["characters"]["example"]["hair_base"] = "../outside.png";
    writePack();
    assert(!readSpritePack(packPath, packCharacters, bitmapLoader));
    assert(packCharacters.size() == 1 && save(packCharacters[0]) == beforeBadPack);
    pack["characters"]["example"]["hair_base"] = layerName;
    pack["characters"]["example"]["order"][1] = pack["characters"]["example"]["order"][0];
    writePack();
    assert(!readSpritePack(packPath, packCharacters, bitmapLoader));
    assert(std::filesystem::remove(packPath) && std::filesystem::remove(layerPath));
    Appearance spriteExample;
    if (argc > 2) {
        assert(readCharacterFile(argv[2], spriteExample) && spriteExample.sprites.valid());
        assert(importCharacter(exportCharacter(spriteExample), transferred));
        assert(save(spriteExample) == save(transferred));
    }
    SDL_DestroySurface(tile);
    SDL_Surface* surface = SDL_CreateSurface(640, 224, SDL_PIXELFORMAT_RGBA8888);
    assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    // Every source pixel is visible at 1.5 output pixels, even though the
    // character is only half-size in world coordinates before gameplay zoom.
    Appearance fineSprite;
    fineSprite.build = 1;
    fineSprite.sprites.bangs = std::string(176 * 28 * 8, '0');
    for (int y = 0; y < 28; ++y) for (int x = 0; x < 176; ++x)
        fineSprite.sprites.base += (x + y) % 2 ? "ff0000ff" : "00ff00ff";
    for (int facing : {1, -1}) {
        assert(SDL_SetRenderScale(renderer, 3, 3));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        assert(SDL_RenderClear(renderer));
        render(renderer, fineSprite, SDL_FRect{0, 0, 8, 14}, Pose::Idle, 0, facing);
        float scaleX, scaleY;
        assert(SDL_GetRenderScale(renderer, &scaleX, &scaleY) && scaleX == 3 && scaleY == 3);
        SDL_Surface* detail = SDL_RenderReadPixels(renderer, nullptr);
        assert(detail);
        for (int y = 0; y < 28; ++y) for (int x = 0; x < 16; ++x) {
            Uint8 r, g, b, alpha;
            const int screenX = facing > 0 ? x : 15 - x;
            assert(SDL_ReadSurfacePixel(detail, (int)((screenX + 0.5f) * 1.5f),
                (int)((y + 0.5f) * 1.5f), &r, &g, &b, &alpha));
            assert(r == ((x + y) % 2 ? 255 : 0));
            assert(g == ((x + y) % 2 ? 0 : 255) && b == 0 && alpha == 255);
        }
        SDL_DestroySurface(detail);
    }
    assert(SDL_SetRenderScale(renderer, 1, 1));
    // Gameplay/UI helpers can leave an explicit viewport. Custom characters
    // must remain visible near its right edge with zoom and clipping enabled.
    for (const Appearance& appearance : {Appearance{}, fineSprite}) {
        assert(SDL_SetRenderScale(renderer, 3, 3));
        const SDL_Rect view{0, 0, 200, 70};
        const SDL_Rect clip{140, 10, 40, 40};
        assert(SDL_SetRenderViewport(renderer, &view));
        assert(SDL_SetRenderClipRect(renderer, &clip));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        assert(SDL_RenderClear(renderer));
        render(renderer, appearance, SDL_FRect{150, 20, 8, 14});
        SDL_Rect actualView, actualClip;
        float scaleX, scaleY;
        assert(SDL_GetRenderScale(renderer, &scaleX, &scaleY) && scaleX == 3 && scaleY == 3);
        assert(SDL_GetRenderViewport(renderer, &actualView) && SDL_RectsEqual(&view, &actualView));
        assert(SDL_RenderClipEnabled(renderer));
        assert(SDL_GetRenderClipRect(renderer, &actualClip) && SDL_RectsEqual(&clip, &actualClip));
        SDL_Surface* visible = SDL_RenderReadPixels(renderer, nullptr);
        assert(visible);
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(visible, 462, 87, &r, &g, &b, &alpha));
        assert(r || g || b);
        SDL_DestroySurface(visible);
        assert(SDL_SetRenderClipRect(renderer, nullptr));
        assert(SDL_SetRenderViewport(renderer, nullptr));
        assert(SDL_SetRenderScale(renderer, 1, 1));
    }
    Appearance hairTest;
    hairTest.hairPixels = std::string(16 * 16 * 8, '0');
    hairTest.hairPixels.replace((1 * 16 + 1) * 8, 8, "c8326480");
    hairTest.hairStyle = customHairStyle;
    for (int facing : {1, -1}) {
        assert(SDL_SetRenderScale(renderer, 3, 3));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        assert(SDL_RenderClear(renderer));
        render(renderer, hairTest, SDL_FRect{0, 0, 8, 14}, Pose::Idle, 0, facing);
        float scaleX, scaleY;
        assert(SDL_GetRenderScale(renderer, &scaleX, &scaleY) && scaleX == 3 && scaleY == 3);
        SDL_Surface* detail = SDL_RenderReadPixels(renderer, nullptr);
        assert(detail);
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(detail, facing == 1 ? 2 : 21, 2, &r, &g, &b, &alpha));
        assert(r >= 99 && r <= 101 && g >= 24 && g <= 26 && b >= 49 && b <= 51);
        SDL_DestroySurface(detail);
    }
    assert(SDL_SetRenderScale(renderer, 1, 1));
    for (int facing : {1, -1}) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        render(renderer, hairTest, SDL_FRect{0, 0, 16, 28}, Pose::Idle, 0, facing);
        SDL_Surface* hairOutput = SDL_RenderReadPixels(renderer, nullptr);
        assert(hairOutput);
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(hairOutput, facing == 1 ? 1 : 14, 1, &r, &g, &b, &alpha));
        assert(r >= 99 && r <= 101 && g >= 24 && g <= 26 && b >= 49 && b <= 51);
        assert(SDL_ReadSurfacePixel(hairOutput, facing == 1 ? 0 : 15, 1, &r, &g, &b, &alpha));
        assert(r == 0 && g == 0 && b == 0);
        SDL_DestroySurface(hairOutput);
    }
    // Built-in custom spikes use the selected hair color and part texture.
    hairTest = Appearance{};
    hairTest.hairStyle = customPartsHairStyle;
    hairTest.hairShape = {{3, 3, 8, 3, 2}};
    hairTest.hairColor = 2;
    for (int texture : {0, 1}) for (int facing : {1, -1}) {
        hairTest.textures[(int)Part::Hair] = texture;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        render(renderer, hairTest, SDL_FRect{0, 0, 16, 28}, Pose::Idle, 0, facing);
        SDL_Surface* hairOutput = SDL_RenderReadPixels(renderer, nullptr);
        assert(hairOutput);
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(hairOutput, facing == 1 ? 4 : 11, 1, &r, &g, &b, &alpha));
        const auto expected = texturePixel(hairTest, Part::Hair, colors[2], facing == 1 ? 0 : 1, 0);
        assert(r == expected.r && g == expected.g && b == expected.b);
        SDL_DestroySurface(hairOutput);
    }
    // Very large uncapped lengths render beyond the sprite without iterating the saved length.
    hairTest.uncapHairLength = true;
    hairTest.hairShape[1] = std::numeric_limits<int>::max();
    hairTest.hairShape[2] = std::numeric_limits<int>::max();
    for (int texture : {0, 1}) for (int facing : {1, -1}) {
        hairTest.textures[(int)Part::Hair] = texture;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        render(renderer, hairTest, SDL_FRect{0, 0, 16, 28}, Pose::Idle, 0, facing);
        SDL_Surface* hairOutput = SDL_RenderReadPixels(renderer, nullptr);
        assert(hairOutput);
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(hairOutput, facing == 1 ? 3 : 12, 100, &r, &g, &b, &alpha));
        assert(r > 0 && g > 0 && b > 0);
        SDL_DestroySurface(hairOutput);
    }
    for (int facing : {1, -1}) {
        PartPhysics floorContact;
        floorContact.hairCollisions.boxes.push_back({0, 40, 16, 10});
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        render(renderer, hairTest, SDL_FRect{0, 0, 16, 28}, Pose::Idle, 0, facing, 255, &floorContact);
        SDL_Surface* hairOutput = SDL_RenderReadPixels(renderer, nullptr);
        assert(hairOutput);
        Uint8 r, g, b, alpha;
        const int x = facing == 1 ? 3 : 12;
        assert(SDL_ReadSurfacePixel(hairOutput, x, 35, &r, &g, &b, &alpha));
        assert(r > 0);
        for (int y : {45, 75}) {
            assert(SDL_ReadSurfacePixel(hairOutput, x, y, &r, &g, &b, &alpha));
            assert(r == 0 && g == 0 && b == 0);
        }
        SDL_DestroySurface(hairOutput);
    }
    // Exercise the same bounds check as gameplay with the entire body above the screen.
    for (int facing : {1, -1}) for (Pose pose : {Pose::Idle, Pose::Slide}) {
        const SDL_FRect destination{8, -60, 16, pose == Pose::Slide ? 18.0f : 28.0f};
        const auto bounds = visualBounds(extentHair, destination, pose);
        assert(!SDL_HasRectIntersectionFloat(&destination, &viewport));
        assert(SDL_HasRectIntersectionFloat(&bounds, &viewport));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (SDL_HasRectIntersectionFloat(&bounds, &viewport)) render(renderer, extentHair, destination, pose, 0, facing);
        SDL_Surface* visibleHair = SDL_RenderReadPixels(renderer, nullptr);
        assert(visibleHair);
        int visiblePixels = 0;
        for (int y = 0; y < 10; ++y) for (int x = 8; x < 24; ++x) {
            Uint8 r, g, b, alpha;
            assert(SDL_ReadSurfacePixel(visibleHair, x, y, &r, &g, &b, &alpha));
            if (r || g || b) ++visiblePixels;
        }
        assert(visiblePixels > 0);
        SDL_DestroySurface(visibleHair);
    }
    SDL_SetRenderDrawColor(renderer, 17, 25, 38, 255);
    SDL_RenderClear(renderer);
    const Pose poses[]{Pose::Idle, Pose::Run, Pose::Jump, Pose::Slide, Pose::Fall, Pose::Hurt, Pose::Death};
    for (int i = 0; i < 16; ++i) {
        a.skin = i % skinColors.size();
        a.hairStyle = i % hairStyles.size();
        a.hairColor = i % colors.size();
        a.shirt = (i + 2) % colors.size();
        a.pants = i % colors.size();
        a.accent = (i + 4) % colors.size();
        a.hat = (i / 4) % hats.size();
        a.build = i % 3;
        for (int part = 0; part < 6; ++part) a.textures[part] = (i + part) % 6;
        if (i % 3 == 0) { a.textures[(int)Part::Hat] = 6; a.texturePixels[(int)Part::Hat] = imported; }
        if (argc > 2 && i < 8) a = spriteExample;
        else a.sprites = {};
        SDL_FRect dst{(float)((i % 8) * 80 + 8), (float)((i / 8) * 112 + 4), 64, 104};
        if (poses[i % 7] == Pose::Slide) { dst.y += 36; dst.h -= 36; }
        SDL_SetRenderDrawColor(renderer, 11, 22, 33, 44);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        PartPhysics motion;
        for (int frame = 0; frame < 30; ++frame)
            motion.update(1.0f / 60, i % 2 ? -500.0f : 500.0f, 0, true, poses[i % 7] == Pose::Slide, i % 2 ? -1 : 1);
        motion.update(1.0f / 60, 0, 1000, false, false, 1);
        motion.update(1.0f / 60, 0, 0, true, false, 1);
        render(renderer, a, dst, poses[i % 7], 0.12f, i % 2 ? -1 : 1, 220, &motion);
        Uint8 r, g, b, alpha;
        SDL_BlendMode blend;
        SDL_GetRenderDrawColor(renderer, &r, &g, &b, &alpha);
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        assert(r == 11 && g == 22 && b == 33 && alpha == 44 && blend == SDL_BLENDMODE_NONE);
    }
    SDL_Surface* output = SDL_RenderReadPixels(renderer, nullptr);
    assert(output);
    int colored = 0;
    for (int y = 0; y < output->h; ++y) for (int x = 0; x < output->w; ++x) {
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(output, x, y, &r, &g, &b, &alpha));
        if (r != 17 || g != 25 || b != 38) {
            ++colored;
            // Every sprite must remain inside its allocated preview cell.
            assert(x % 80 >= 8 && x % 80 < 72 && y % 112 >= 4 && y % 112 < 108);
        }
    }
    assert(colored > 15000);
    if (argc > 1) assert(SDL_SaveBMP(output, argv[1]));
    SDL_DestroySurface(output);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    SDL_Quit();
}
