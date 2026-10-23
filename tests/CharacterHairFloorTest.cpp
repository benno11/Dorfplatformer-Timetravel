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
            for (const auto& strand : motion.floorHair) {
                assert(strand.initialized);
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
