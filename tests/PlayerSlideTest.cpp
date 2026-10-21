#include "PlayerController.h"
#include "LevelManager.h"
#include "AssetPath.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

// Exercise real box rewards with local assets and no network/save dependencies.
std::string GetAppSaveRootPath() { return "."; }
std::string ReadTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
bool FileExists(const std::string& path) { return std::filesystem::exists(path); }

int main() {
    assert(SDL_Init(0));
    TileMap map;
    map.resize(100, 8);
    for (int x = 0; x < map.w; ++x) map.setSolid(x, 5, 1);
    auto grounded = [&]() {
        Player p;
        p.x = 1000;
        p.y = 5 * map.tileSize - p.h;
        p.onGround = true;
        return p;
    };
    LevelManager manager;
    std::vector<ObjectInstance> objects;
    auto step = [&](Player& p, float dt, bool down, float move = 0.0f) {
        float inputMove = 0;
        bool inputDown = false;
        const auto result = UpdatePlayerMovement(p, map, dt, {}, move, down, false,
            0, false, false, false, false, {}, inputMove, inputDown, nullptr,
            [&](const SDL_FRect& rect) { manager.openCoinBoxesInRect(map, rect, objects, p); });
        assert(result == PlayerUpdateResult::Normal);
    };

    // One press grants 50 frames at 60 Hz, independently of rendering rate.
    for (int fps : {30, 60, 120, 240}) {
        Player p = grounded();
        step(p, 0.0f, true);
        assert(p.slideTime == Player::kSlideDuration);
        const float startX = p.x;
        const int frames = (int)std::lround(Player::kSlideDuration * fps);
        for (int frame = 0; frame < frames; ++frame) {
            assert(p.slideTime > 0);
            step(p, 1.0f / fps, true, -1);
            assert(p.facing == 1); // Opposing input cannot reverse a slide.
        }
        assert(p.slideTime == 0);
        assert(p.x > startX + 500);
        step(p, 1.0f / fps, true);
        assert(p.slideTime == 0); // Holding Down cannot chain invulnerability.
        step(p, 0.0f, false);
        step(p, 0.0f, true);
        assert(p.slideTime == Player::kSlideDuration);
    }

    Player left = grounded();
    left.facing = -1;
    step(left, 1.0f / 60, true, 1);
    assert(left.x < 1000 && left.vx < 0 && left.facing == -1);

    Player airborne = grounded();
    airborne.onGround = false;
    airborne.y -= 50;
    step(airborne, 1.0f / 60, true);
    assert(airborne.slideTime == 0);

    // Side impacts open boxes once and retain the normal empty-box behavior.
    const int boxX = 33, boxY = 4, boxIndex = boxY * map.w + boxX;
    map.tileIds[boxIndex] = 20;
    map.setSolid(boxX, boxY, 1);
    Player boxed = grounded();
    boxed.x = boxX * map.tileSize - boxed.w - 4;
    for (int frame = 0; frame < 4; ++frame) step(boxed, 1.0f / 30, true);
    assert(map.tileIds[boxIndex] == 21 && manager.coinCount() == 5);
    assert(!RectHitsSolid(map, boxed.x, boxed.y, boxed.w, boxed.h));

    // Ordinary walls stop movement without canceling the promised immunity.
    const int wallX = 38;
    map.setSolid(wallX, boxY, 1);
    boxed = grounded();
    boxed.x = wallX * map.tileSize - boxed.w - 4;
    step(boxed, 1.0f / 30, true);
    assert(boxed.x + boxed.w <= wallX * map.tileSize + 1);
    assert(boxed.vx == 0 && !RectHitsSolid(map, boxed.x, boxed.y, boxed.w, boxed.h));
    assert(boxed.slideTime > 0); // Blocking movement does not cancel the promised immunity.

    // Shield boxes retain the regular reward behavior during a slide.
    map.tileIds[boxIndex] = 20;
    objects.push_back({"97", (boxX + 0.5f) * map.tileSize, (boxY + 0.5f) * map.tileSize});
    boxed = grounded();
    boxed.x = boxX * map.tileSize - boxed.w - 4;
    step(boxed, 1.0f / 30, true);
    assert(boxed.hasShield && manager.coinCount() == 5 && map.tileIds[boxIndex] == 21);

    // Wrapped contacts open the matching box on the opposite edge.
    const int wrappedIndex = boxY * map.w + map.w - 1;
    map.tileIds[wrappedIndex] = 20;
    Player wrapped = grounded();
    assert(manager.openCoinBoxesInRect(map, {-16, boxY * (float)map.tileSize, 16, 20},
        objects, wrapped, true, false) == 1);
    assert(map.tileIds[wrappedIndex] == 21 && manager.coinCount() == 10);
    assert(manager.openCoinBoxesInRect(map, {-16, boxY * (float)map.tileSize, 16, 20},
        objects, wrapped, true, false) == 0);
    SDL_Quit();
}
