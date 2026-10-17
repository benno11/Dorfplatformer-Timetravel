#include "MovingPlatform.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static bool close(float a, float b) { return std::fabs(a - b) < 0.03f; }
int main() {
    try {
        MovingPlatform::State idle;
        auto delta = MovingPlatform::advance(idle, 1, 32);
        require(delta.x == 0 && delta.y == 0 && idle.elapsed == 0, "Untriggered platforms must remain stationary");
        std::vector<MovingPlatform::State> group(3);
        const std::vector<int> ids{ObjectIds::MovingPlatform, ObjectIds::Shield, ObjectIds::MovingPlatform};
        MovingPlatform::synchronize(group, ids, false);
        require(!group[0].started && !group[2].started, "Platform groups must wait for player contact");
        MovingPlatform::synchronize(group, ids, true);
        require(group[0].started && group[2].started && !group[1].started, "Standing on one platform must activate every platform");
        for (int frame = 0; frame < 420; ++frame) {
            const auto first = MovingPlatform::advance(group[0], 1.0f / 60, 32);
            const auto second = MovingPlatform::advance(group[2], 1.0f / 60, 32);
            require(first.x == second.x && first.y == second.y && group[0].elapsed == group[2].elapsed,
                "All platforms must move and stop together");
        }
        for (int fps : {30, 60, 144, 1000}) {
            MovingPlatform::State state{0, true};
            float x = 0, y = 0;
            for (int frame = 0; frame < fps * 7; ++frame) {
                delta = MovingPlatform::advance(state, 1.0f / fps, 32);
                x += delta.x; y += delta.y;
            }
            require(close(x, -352) && close(y, 352), "Platform must move exactly eleven blocks along both axes");
            require(state.elapsed == MovingPlatform::kDuration, "Platform must stop at 6.7 seconds");
            delta = MovingPlatform::advance(state, 10, 32);
            require(delta.x == 0 && delta.y == 0, "Completed platforms must stay at their destination");
        }
        MovingPlatform::State half{0, true};
        delta = MovingPlatform::advance(half, 3.35f, 32);
        require(close(delta.x, -176) && close(delta.y, 176), "Half the travel time must cover half the distance");
        require(MovingPlatform::advance(half, 0, 32).x == 0, "Pause must not move a platform");
        require(MovingPlatform::advance(half, std::numeric_limits<float>::quiet_NaN(), 32).x == 0,
            "Invalid frame times must not corrupt platform motion");
        MovingPlatform::State resumed{half.elapsed, half.started};
        delta = MovingPlatform::advance(resumed, 10, 32);
        require(close(delta.x, -176) && close(delta.y, 176), "Resuming a saved half-finished trip must move only the remaining distance");

        require(SDL_Init(0), "SDL init");
        TileMap map;
        map.resize(64, 64);
        Player player;
        float cx = 1024, cy = 256;
        player.x = cx - player.w * 0.5f;
        player.y = cy - 16 - player.h;
        player.vy = 0;
        MovingPlatform::State state;
        MovementConfig movement;
        KeyboardBindings keys;
        float inputMove = 0;
        bool inputDown = false;
        for (int frame = 0; frame < 420; ++frame) {
            auto bounds = MovingPlatform::nearPlayer(cx, cy, 32, player);
            require(MovingPlatform::standingOn(player, bounds), "Player must remain standing on a descending platform");
            state.started = true;
            delta = MovingPlatform::advance(state, 1.0f / 60, 32);
            cx += delta.x; cy += delta.y;
            MovingPlatform::carry(player, map, delta);
            bounds = MovingPlatform::nearPlayer(cx, cy, 32, player);
            player.onGround = MovingPlatform::standingOn(player, bounds);
            const std::vector<SDL_FRect> solids{bounds};
            require(UpdatePlayerMovement(player, map, 1.0f / 60, movement, 0, false, false,
                0, false, false, false, false, keys, inputMove, inputDown, &solids) == PlayerUpdateResult::Normal,
                "Rider physics must remain active");
            require(MovingPlatform::standingOn(player, bounds), "Solid collision must keep the rider on top");
            require(player.y + player.h <= bounds.y && bounds.y - player.y - player.h <= 1.5f,
                "Player must stay on the platform's top surface within collision resolution tolerance");
        }
        require(close(cx, 672) && close(cy, 608), "Rider must arrive at the final platform position");
        require(close(player.x + player.w * 0.5f, cx), "Platform must carry horizontal position without drift");
        player.onGround = true;
        auto bounds = MovingPlatform::nearPlayer(cx, cy, 32, player);
        std::vector<SDL_FRect> solids{bounds};
        UpdatePlayerMovement(player, map, 1.0f / 60, movement, 0, false, true,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.vy < 0 && !MovingPlatform::standingOn(player, bounds),
            "Jumping must detach the rider from the platform");
        solids = {{624, 640, 32, 32}};
        player = Player{};
        player.x = 625;
        player.y = 580;
        player.vy = 240;
        player.jumpBufferTime = 0.1f;
        UpdatePlayerMovement(player, map, 1.0f / 30, movement, 0, false, false,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.vy < 0 && !player.onGround && player.jumpBufferTime == 0,
            "Buffered jumps must fire on solid platform landing");
        require(MovingPlatform::touchingTop(player, solids[0]), "Buffered jump landings must still trigger the platform group");
        player = Player{};
        player.x = 625;
        player.y = 675;
        player.vy = -300;
        UpdatePlayerMovement(player, map, 1.0f / 30, movement, 0, false, false,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.y >= 672 && player.vy == 0,
            "Solid platforms must stop rising players at their underside");
        player = Player{};
        player.x = 590;
        player.y = 644;
        player.vx = 300;
        UpdatePlayerMovement(player, map, 1.0f / 30, movement, 1, false, false,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.x + player.w <= 624 && player.vx == 0, "Platforms must block their left side");
        player = Player{};
        player.x = 660;
        player.y = 644;
        player.vx = -300;
        UpdatePlayerMovement(player, map, 1.0f / 30, movement, -1, false, false,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.x >= 656 && player.vx == 0, "Platforms must block their right side");
        require(!RectHitsSolid(map, 630, 650, 10, 10), "Dynamic colliders must not leak outside the player update");
        player = Player{};
        player.x = 64; player.y = 64;
        map.setSolid(1, 2, 1); map.setSolid(1, 3, 1);
        MovingPlatform::carry(player, map, {-40, 0});
        require(player.x == 64 && !RectHitsSolid(map, player.x, player.y, player.w, player.h),
            "Carrying must not push players through map solids");
        player.x = 500;
        bounds = MovingPlatform::nearPlayer(8, 64, 32, player, 512, 0);
        require(bounds.x == 504, "Wrapped platforms must support the nearby player copy");
        SDL_Quit();
        std::cout << "Moving platform tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << ": " << SDL_GetError() << '\n';
        return 1;
    }
}
