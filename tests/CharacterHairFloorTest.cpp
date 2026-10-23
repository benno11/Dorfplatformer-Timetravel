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

    assert(SDL_Init(0));
    SDL_Surface* surface = SDL_CreateSurface(160, 96, SDL_PIXELFORMAT_RGBA8888);
    assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    Appearance hair;
    hair.hairStyle = customPartsHairStyle;
    hair.uncapHairLength = true;
    hair.hairShape = {{3, 0, 100, 2, 0}};
    for (int facing : {1, -1}) for (int build : {0, 1, 2}) for (int texture : {0, 1}) {
        hair.build = build;
        hair.textures[(int)Part::Hair] = texture;
        PartPhysics physics;
        physics.hairCollisions.boxes.push_back({-200, 28, 400, 16});
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
    // Saved lengths may be enormous; rendering work remains bounded by the view.
    hair.hairShape[2] = std::numeric_limits<int>::max();
    PartPhysics longHair;
    longHair.hairCollisions.boxes.push_back({-200, 28, 400, 16});
    render(renderer, hair, {64, 0, 16, 28}, Pose::Idle, 0, 1, 255, &longHair);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    SDL_Quit();
}
