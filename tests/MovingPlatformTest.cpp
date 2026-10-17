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
        for (int fps : {30, 60, 144, 1000}) {
            MovingPlatform::State state{0, true};
            float x = 0, y = 0;
            for (int frame = 0; frame < fps * 7; ++frame) {
                delta = MovingPlatform::advance(state, 1.0f / fps, 32);
                x += delta.x; y += delta.y;
            }
            require(close(x, -384) && close(y, 384), "Platform must move exactly twelve blocks along both axes");
            require(state.elapsed == MovingPlatform::kDuration, "Platform must stop at 6.7 seconds");
            delta = MovingPlatform::advance(state, 10, 32);
            require(delta.x == 0 && delta.y == 0, "Completed platforms must stay at their destination");
        }
        MovingPlatform::State half{0, true};
        delta = MovingPlatform::advance(half, 3.35f, 32);
        require(close(delta.x, -192) && close(delta.y, 192), "Half the travel time must cover half the distance");
        require(MovingPlatform::advance(half, 0, 32).x == 0, "Pause must not move a platform");
        require(MovingPlatform::advance(half, std::numeric_limits<float>::quiet_NaN(), 32).x == 0,
            "Invalid frame times must not corrupt platform motion");
        MovingPlatform::State resumed{half.elapsed, half.started};
        delta = MovingPlatform::advance(resumed, 10, 32);
        require(close(delta.x, -192) && close(delta.y, 192), "Resuming a saved half-finished trip must move only the remaining distance");

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
            const float beforeY = player.y;
            require(UpdatePlayerMovement(player, map, 1.0f / 60, movement, 0, false, false,
                0, false, false, false, false, keys, inputMove, inputDown) == PlayerUpdateResult::Normal,
                "Rider physics must remain active");
            require(MovingPlatform::land(player, map, beforeY, bounds, movement), "Player must land back on the platform after gravity");
            require(close(player.y + player.h, bounds.y), "Player must stay on the platform's top surface");
        }
        require(close(cx, 640) && close(cy, 640), "Rider must arrive at the final platform position");
        require(close(player.x + player.w * 0.5f, cx), "Platform must carry horizontal position without drift");
        player.onGround = true;
        const float oldY = player.y;
        UpdatePlayerMovement(player, map, 1.0f / 60, movement, 0, false, true,
            0, false, false, false, false, keys, inputMove, inputDown);
        auto bounds = MovingPlatform::nearPlayer(cx, cy, 32, player);
        require(player.vy < 0 && !MovingPlatform::land(player, map, oldY, bounds, movement),
            "Jumping must detach the rider from the platform");
        player = Player{};
        player.x = 640;
        player.y = 588;
        player.vy = 200;
        player.jumpBufferTime = 0.1f;
        require(MovingPlatform::land(player, map, 580, {624, 640, 32, 32}, movement), "Falling player must land on the platform");
        require(player.vy < 0 && !player.onGround && player.jumpBufferTime == 0,
            "Buffered jumps must fire on platform landing");
        player.jumpBufferTime = 0;
        player.vy = -200;
        require(!MovingPlatform::land(player, map, 650, {624, 640, 32, 32}, movement),
            "Rising players must pass through the platform from below");
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
