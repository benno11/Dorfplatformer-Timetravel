#include "CharacterRenderer.h"
#include "CharacterHairCollision.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>

int main() {
    using namespace CharacterCreator;
    for (bool oneWay : {false, true}) for (float direction : {-1.0f, 1.0f}) {
        HairPhysics::CollisionScene floor;
        floor.root = {5, 6};
        floor.boxes.push_back({-100, 28, 200, 8, false, oneWay});
        HairPhysics::Point point{5, 6.5f};
        for (int row = 0; row < 60; ++row) {
            point = floor.drape(point, {point.x, point.y + 1}, 4, direction);
            assert(point.y <= 27.5f);
            assert(!floor.blocksWorld(point.x - 2, point.y - 0.5f, 4, 1));
        }
        assert((point.x - 5) * direction > 30);
        assert(point.y > 27.4f);
        // A wall stops the tail rather than allowing the surplus to tunnel.
        floor.boxes.push_back({direction < 0 ? -12.0f : 20.0f, 0, 2, 28});
        point = {5, 6.5f};
        for (int row = 0; row < 100; ++row) {
            point = floor.drape(point, {point.x, point.y + 1}, 4, direction);
            assert(!floor.blocksWorld(point.x - 2, point.y - 0.5f, 4, 1));
        }
        assert(direction < 0 ? point.x >= -8 : point.x <= 18);
        floor.boxes.clear();
        const auto falling = floor.drape(point, {point.x, point.y + 5}, 4, direction);
        assert(falling.y > point.y + 4.9f);
    }
    HairPhysics::CollisionScene ledge;
    ledge.boxes.push_back({0, 28, 10, 8});
    HairPhysics::Point point{5, 6.5f};
    for (int row = 0; row < 60; ++row) point = ledge.drape(point, {point.x, point.y + 1}, 2, -1);
    assert(point.x < -1 && point.y > 35); // Falls once the tail passes the edge.
    HairPhysics::CollisionScene platform;
    platform.boxes.push_back({-100, 28, 200, 0.5f, false, true});
    assert(platform.drape({5, 35}, {5, 20}, 2, -1).y == 20); // Rising through a platform.

    struct TestMap {
        int tileSize = 16, w = 20, h = 3;
        std::vector<unsigned char> solid = std::vector<unsigned char>(60, 0);
        std::vector<unsigned char> semisolid = std::vector<unsigned char>(60, 0);
    } map;
    map.solid[20] = 1;
    HairPhysics::CollisionScene nearby;
    addHairTileCollisions(nearby, map, {96, 0, 16, 28}, 96, false, false, 160);
    assert(nearby.blocksWorld(-96, 16, 16, 1)); // Include walls beside a floor tail.

    HairPhysics::CollisionScene restingFloor;
    restingFloor.boxes.push_back({-200, 28, 400, 16});
    restingFloor.hasWorldTransform = true;
    HairFloorPhysics resting;
    for (int frame = 0; frame < 120; ++frame)
        resting.update(1.0f / 60, {5, 6.5f}, 80, 2, -1, restingFloor);
    const auto restingTip = resting.at(80);
    assert(restingTip.x < -20 && restingTip.y <= 27.5f);
    // Merely turning cannot mirror the grounded tail to the other side.
    resting.update(1.0f / 60, {5, 6.5f}, 80, 2, 1, restingFloor);
    assert(std::fabs(resting.at(80).x - restingTip.x) < 2);
    Appearance turningHair;
    turningHair.hairStyle = customPartsHairStyle;
    turningHair.uncapHairLength = true;
    turningHair.hairShape = {{3, 0, 80, 2, 0}};
    PartPhysics turningParts;
    auto rightScene = hairBodyCollisions(turningHair, 1, false);
    addHairWorldBox(rightScene, {-200, 28, 400, 16}, {0, 0, 16, 28});
    for (int frame = 0; frame < 120; ++frame)
        turningParts.update(1.0f / 60, 0, 0, true, false, 1, 1, &rightScene, &turningHair);
    const auto beforeTurn = turningParts.floorHair[1].at(80);
    const float oldTrailDirection = turningParts.floorTrailDirection;
    auto leftScene = hairBodyCollisions(turningHair, -1, false);
    addHairWorldBox(leftScene, {-200, 28, 400, 16}, {0, 0, 16, 28});
    turningParts.update(1.0f / 60, 0, 0, true, false, -1, 1, &leftScene, &turningHair);
    const auto afterTurn = turningParts.floorHair[1].at(80);
    assert(std::fabs(afterTurn.x - beforeTurn.x) < 2);
    assert(std::fabs(afterTurn.y - beforeTurn.y) < 2);
    assert(turningParts.floorTrailDirection == oldTrailDirection);
    auto lastTurnTip = afterTurn;
    for (int frame = 0; frame < 30; ++frame) {
        turningParts.update(1.0f / 60, 0, 0, true, false, -1, 1, &leftScene, &turningHair);
        const auto tip = turningParts.floorHair[1].at(80);
        assert(std::fabs(tip.x - lastTurnTip.x) < 2);
        assert(std::fabs(tip.y - lastTurnTip.y) < 2);
        assert(tip.x < 0); // No delayed reflection when the turn transition ends.
        lastTurnTip = tip;
    }
    // Walk toward the resting tail: its world position sticks while slack builds.
    for (int frame = 1; frame <= 20; ++frame) {
        restingFloor.worldOrigin.x = -frame * 0.5f;
        resting.update(1.0f / 60, {5, 6.5f}, 80, 2, 1, restingFloor);
    }
    assert(std::fabs(resting.at(80).x + restingFloor.worldOrigin.x - restingTip.x) < 8);
    for (int i = 1; i < resting.count; ++i) {
        assert(std::isfinite(resting.points[i].x) && std::isfinite(resting.points[i].y));
        assert(!restingFloor.blocksWorld(resting.points[i].x - 1, resting.points[i].y - 0.5f, 2, 1));
    }
    const auto pausedTip = resting.at(80);
    resting.update(0, {100, 0}, 80, 2, -1, restingFloor);
    assert(resting.at(80).x == pausedTip.x && resting.at(80).y == pausedTip.y);
    // A respawn resets locally instead of pulling the old tail across the map.
    restingFloor.worldOrigin.x = 10000;
    resting.update(1.0f / 60, {5, 6.5f}, 80, 2, -1, restingFloor);
    assert(std::fabs(resting.at(80).x) < 100);
    restingFloor.boxes.clear();
    const float supportedY = resting.at(80).y;
    for (int frame = 0; frame < 30; ++frame)
        resting.update(1.0f / 60, {5, 6.5f}, 80, 2, -1, restingFloor);
    assert(resting.at(80).y > supportedY + 2);

    HairPhysics::CollisionScene cornerFloor;
    cornerFloor.boxes = {{-100, 28, 200, 16}, {-14, -30, 2, 58}};
    HairFloorPhysics bunched;
    for (int frame = 0; frame < 120; ++frame)
        bunched.update(1.0f / 60, {5, 6.5f}, 80, 2, -1, cornerFloor);
    float minY = 28, maxY = -100;
    for (int i = 10; i < bunched.count; ++i) {
        const auto p = bunched.points[i];
        assert(std::isfinite(p.x) && std::isfinite(p.y));
        assert(!cornerFloor.blocksWorld(p.x - 1, p.y - 0.5f, 2, 1));
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    assert(maxY - minY > 2); // Surplus forms folds rather than overlapping at the wall.
    HairFloorPhysics enormous;
    enormous.update(1.0f / 60, {5, 6.5f}, std::numeric_limits<int>::max(), 4, -1, cornerFloor);
    assert(enormous.count <= HairFloorPhysics::capacity && enormous.length == HairFloorPhysics::maxLength);
    enormous.update(1.0f / 60, {5, 6.5f}, 0, 4, -1, cornerFloor);
    assert(!enormous.initialized);

    // A real jump moves the collision scene relative to world-space resting hair.
    // Include an airborne turn: body ejection must not tear neighboring links apart.
    for (int fps : {30, 60, 120, 240}) for (int width : {2, 4}) {
        Appearance jumper;
        jumper.hairStyle = customPartsHairStyle;
        jumper.uncapHairLength = true;
        jumper.hairShape = {{3, 4, 80, width, 0}};
        jumper.build = width == 4 ? 0 : 1;
        PartPhysics motion;
        const float dt = 1.0f / fps;
        float y = 300, vy = 0;
        for (int frame = 0; frame < fps * 4; ++frame) {
            bool grounded = frame < fps;
            if (frame == fps) vy = -900;
            if (frame >= fps) {
                vy += 1800 * dt;
                y += vy * dt;
                if (y >= 300) { y = 300; vy = 0; grounded = true; }
            }
            const int facing = frame < fps * 3 / 2 ? 1 : -1;
            auto scene = hairBodyCollisions(jumper, facing, false);
            addHairWorldBox(scene, {-500, 356, 1000, 64}, {0, y, 32, 56});
            motion.update(dt, 0, vy, grounded, false, facing, 1, &scene, &jumper);
            assert(!motion.floorHair[0].initialized); // The short fringe uses regular strand physics.
            assert(motion.floorHair[1].initialized);
            for (const auto& strand : motion.floorHair) {
                if (!strand.initialized) continue;
                for (int i = 1; i < strand.count; ++i) {
                    const float dx = strand.points[i].x - strand.points[i - 1].x;
                    const float dy = strand.points[i].y - strand.points[i - 1].y;
                    assert(std::isfinite(dx) && std::isfinite(dy));
                    assert(std::sqrt(dx * dx + dy * dy) <= strand.linkLength * 1.25f);
                    assert(!scene.blocksWorld(strand.points[i].x - strand.width * 0.5f,
                        strand.points[i].y - 0.5f, strand.width, 1));
                }
            }
        }
    }

    assert(SDL_Init(0));
    SDL_Surface* surface = SDL_CreateSurface(160, 96, SDL_PIXELFORMAT_RGBA8888);
    assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    // The game/preview target can be larger than the physical window. Uncapped
    // hair must use that target's bounds, even when the body is beyond the window.
    SDL_Texture* previewTarget = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, 320, 192);
    assert(previewTarget);
    assert(SDL_SetRenderTarget(renderer, previewTarget));
    Appearance targetHair;
    targetHair.hairStyle = customPartsHairStyle;
    targetHair.hairColor = 2;
    targetHair.hairShape = {{3, 0, 60, 2, 0}};
    for (bool uncapped : {false, true}) for (int facing : {1, -1}) {
        targetHair.uncapHairLength = uncapped;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        assert(SDL_RenderClear(renderer));
        render(renderer, targetHair, {240, 100, 16, 28}, Pose::Idle, 0, facing);
        SDL_Surface* output = SDL_RenderReadPixels(renderer, nullptr);
        assert(output && output->w == 320 && output->h == 192);
        Uint8 r, g, b, alpha;
        assert(SDL_ReadSurfacePixel(output, 245, 104, &r, &g, &b, &alpha));
        assert(r == colors[2].r && g == colors[2].g && b == colors[2].b);
        if (uncapped) {
            assert(SDL_ReadSurfacePixel(output, facing > 0 ? 244 : 250, 150, &r, &g, &b, &alpha));
            assert(r == colors[2].r && g == colors[2].g && b == colors[2].b);
        }
        SDL_DestroySurface(output);
    }
    assert(SDL_SetRenderTarget(renderer, nullptr));
    SDL_DestroyTexture(previewTarget);
    // Back hair keeps its volume above the tip in both preview and physics
    // rendering. Even the thinnest lock must remain visible in either direction.
    for (int width : {1, 2, 4}) for (int facing : {1, -1}) for (bool moving : {false, true}) {
        Appearance tapered;
        tapered.hairStyle = customPartsHairStyle;
        tapered.hairColor = 2;
        tapered.build = 1;
        tapered.uncapHairLength = true;
        tapered.hairShape = {{3, 0, 40, width, 0}};
        PartPhysics motion;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        assert(SDL_RenderClear(renderer));
        render(renderer, tapered, {48, 0, 32, 56}, Pose::Idle, 0, facing, 255, moving ? &motion : nullptr);
        SDL_Surface* output = SDL_RenderReadPixels(renderer, nullptr);
        assert(output);
        // The inward edge covers the side of the head, beyond the former
        // attachment, and mirrors with the character.
        const int sideX = 48 + 2 * (facing > 0 ? 4 + width - 1 : 16 - 4 - width);
        Uint8 sideR, sideG, sideB, sideAlpha;
        assert(SDL_ReadSurfacePixel(output, sideX, 14, &sideR, &sideG, &sideB, &sideAlpha));
        assert(sideR == colors[2].r && sideG == colors[2].g && sideB == colors[2].b);
        auto hairPixelsInRow = [&](int y) {
            int count = 0;
            for (int x = 48; x < 80; ++x) {
                Uint8 r, g, b, alpha;
                assert(SDL_ReadSurfacePixel(output, x, y, &r, &g, &b, &alpha));
                if (r == colors[2].r && g == colors[2].g && b == colors[2].b) ++count;
            }
            return count;
        };
        const int upperWidth = hairPixelsInRow(58), tipWidth = hairPixelsInRow(90);
        assert(tipWidth >= 2);
        if (width > 1) assert(upperWidth > tipWidth);
        else assert(upperWidth == tipWidth);
        SDL_DestroySurface(output);
    }
    Appearance hair;
    hair.hairStyle = customPartsHairStyle;
    hair.uncapHairLength = true;
    hair.hairShape = {{3, 0, 100, 2, 0}};
    for (int facing : {1, -1}) for (int build : {0, 1, 2}) for (int texture : {0, 1}) for (bool persistent : {false, true}) {
        hair.build = build;
        hair.textures[(int)Part::Hair] = texture;
        PartPhysics physics;
        physics.hairCollisions.boxes.push_back({-200, 28, 400, 16});
        if (persistent) {
            const auto scene = physics.hairCollisions;
            for (int frame = 0; frame < 60; ++frame)
                physics.update(1.0f / 60, 0, 0, true, false, facing, 1, &scene, &hair);
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        render(renderer, hair, {64, 0, 16, 28}, Pose::Idle, 0, facing, 255, &physics);
        SDL_Surface* output = SDL_RenderReadPixels(renderer, nullptr);
        assert(output);
        Uint8 r, g, b, a;
        const int x = facing > 0 ? 50 : 94;
        assert(SDL_ReadSurfacePixel(output, x, 27, &r, &g, &b, &a));
        assert(r || g || b); // The tail continues along the floor beyond the sprite.
        for (int y : {28, 35, 50}) {
            assert(SDL_ReadSurfacePixel(output, x, y, &r, &g, &b, &a));
            assert(r == 0 && g == 0 && b == 0);
        }
        SDL_DestroySurface(output);
        const auto bounds = visualBounds(hair, {64, 0, 16, 28}, Pose::Idle, 0, &physics);
        assert(bounds.x < 50 && bounds.x + bounds.w > 94);
    }
    // Contact compression can create a zero-length link inside a valid strand.
    hair.build = 1;
    hair.textures[(int)Part::Hair] = 0;
    hair.hairShape = {{3, 0, 40, 2, 0}};
    PartPhysics folded;
    auto& foldedStrand = folded.floorHair[1];
    foldedStrand.update(1.0f / 60, {4, 6.5f}, 40, 2, -1, folded.hairCollisions);
    for (int i = 0; i < foldedStrand.count; ++i)
        foldedStrand.points[i] = {4, 6.5f + std::max(0, i - 1) * foldedStrand.linkLength};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    render(renderer, hair, {64, 0, 16, 28}, Pose::Idle, 0, 1, 255, &folded);
    SDL_Surface* foldedPixels = SDL_RenderReadPixels(renderer, nullptr);
    assert(foldedPixels);
    Uint8 red, green, blue, alpha;
    assert(SDL_ReadSurfacePixel(foldedPixels, 68, 32, &red, &green, &blue, &alpha));
    assert(red || green || blue); // A collapsed first link cannot hide the remaining tail.
    SDL_DestroySurface(foldedPixels);
    // Saved lengths may be enormous; rendering work remains bounded by the view.
    hair.hairShape[2] = std::numeric_limits<int>::max();
    PartPhysics longHair;
    longHair.hairCollisions.boxes.push_back({-200, 28, 400, 16});
    const auto longScene = longHair.hairCollisions;
    longHair.update(1.0f / 60, 0, 0, true, false, 1, 1, &longScene, &hair);
    render(renderer, hair, {64, 0, 16, 28}, Pose::Idle, 0, 1, 255, &longHair);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    SDL_Quit();
}
