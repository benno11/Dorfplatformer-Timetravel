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
        require(group[0].started && group[2].started && !group[1].started, "Touching one platform must activate every platform");
        const SDL_FRect contactPlatform{100, 100, 32, 32};
        Player contact;
        for (const SDL_FPoint position : {SDL_FPoint{101, 100.0f - contact.h}, SDL_FPoint{101, 132},
            SDL_FPoint{100.0f - contact.w, 101}, SDL_FPoint{132, 101}}) {
            contact.x = position.x;
            contact.y = position.y;
            require(MovingPlatform::touching(contact, contactPlatform), "Every platform face must register contact");
            std::vector<MovingPlatform::State> contactedGroup(3);
            MovingPlatform::synchronize(contactedGroup, ids, MovingPlatform::touching(contact, contactPlatform));
            require(contactedGroup[0].started && contactedGroup[2].started && !contactedGroup[1].started,
                "Contact from any side must start the synchronized platform group");
            if (position.y >= 100)
                require(!MovingPlatform::standingOn(contact, contactPlatform), "Side and underside contact must not make the player a rider");
        }
        for (const SDL_FPoint position : {SDL_FPoint{101, 98.0f - contact.h}, SDL_FPoint{101, 134},
            SDL_FPoint{98.0f - contact.w, 101}, SDL_FPoint{134, 101}, SDL_FPoint{100.0f - contact.w, 100.0f - contact.h}}) {
            contact.x = position.x;
            contact.y = position.y;
            require(!MovingPlatform::touching(contact, contactPlatform), "Gaps and diagonal corner proximity must not activate platforms");
        }
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

        std::vector<ObjectInstance> cargo{
            {"95", 1024, 224}, {"96", 1024, 256}, {"1001", 1040, 224},
            {"1002", 1024, 288}, {"97", 1056, 224}, {"9999", 1024, 224},
            {"96", 1024, 256}
        };
        const std::vector<int> cargoIds{95, 96, 1001, 1002, 97, 9999, 96};
        const auto originalCargo = cargo;
        std::vector<MovingPlatform::State> cargoStates(cargo.size());
        MovingPlatform::advanceObjects(cargo, cargoStates, cargoIds, 1, 32);
        require(cargo[0].x == 1024 && cargo[0].y == 224, "Idle platforms must leave their cargo still");
        MovingPlatform::synchronize(cargoStates, cargoIds, true);
        for (int frame = 0; frame < 420; ++frame)
            MovingPlatform::advanceObjects(cargo, cargoStates, cargoIds, 1.0f / 60, 32);
        for (size_t index : {0u, 1u, 2u, 5u, 6u}) {
            require(close(cargo[index].x, originalCargo[index].x - 352) &&
                close(cargo[index].y, originalCargo[index].y + 352),
                "Every supported object must move exactly once with its platform regardless of ID or ordering");
        }
        require(cargo[3].x == originalCargo[3].x && cargo[3].y == originalCargo[3].y &&
            cargo[4].x == originalCargo[4].x && cargo[4].y == originalCargo[4].y,
            "Objects below or beside a platform must remain in place");
        std::vector<ObjectInstance> wrappedCargo{{"96", 504, 8}, {"95", 8, 488}};
        const std::vector<int> wrappedIds{96, 95};
        std::vector<MovingPlatform::State> wrappedStates(2);
        wrappedStates[0].started = true;
        const auto wrappedDeltas = MovingPlatform::advanceObjects(wrappedCargo, wrappedStates, wrappedIds,
            3.35f, 32, 512, 512);
        require(close(wrappedCargo[1].x, 8 - 176) && close(wrappedCargo[1].y, 488 + 176) &&
            wrappedDeltas[0].x == wrappedDeltas[1].x && wrappedDeltas[0].y == wrappedDeltas[1].y,
            "Platforms must carry objects across horizontal and vertical wrap seams");

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
            require(MovingPlatform::settleRider(player, map, bounds), "A carried rider must settle on the surface");
            require(MovingPlatform::standingOn(player, bounds), "Solid collision must keep the rider on top");
            require(close(player.y + player.h, bounds.y) && player.vy == 0 && player.onGround,
                "The rider's feet must stay fixed to the surface without gravity wobble");
        }
        require(close(cx, 672) && close(cy, 608), "Rider must arrive at the final platform position");
        require(close(player.x + player.w * 0.5f, cx), "Platform must carry horizontal position without drift");
        player.onGround = true;
        auto bounds = MovingPlatform::nearPlayer(cx, cy, 32, player);
        std::vector<SDL_FRect> solids{bounds};
        for (int frame = 0; frame < 240; ++frame) {
            UpdatePlayerMovement(player, map, frame % 2 ? 1.0f / 30 : 1.0f / 144, movement, 0, false, false,
                0, false, false, false, false, keys, inputMove, inputDown, &solids);
            require(MovingPlatform::settleRider(player, map, bounds) && close(player.y + player.h, bounds.y) &&
                player.vy == 0 && player.onGround, "A stopped platform must remain stable at varying frame rates");
        }
        UpdatePlayerMovement(player, map, 1.0f / 60, movement, 0, false, true,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(!MovingPlatform::settleRider(player, map, bounds), "Settling must not cancel jumping off a platform");
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
        require(!MovingPlatform::settleRider(player, map, solids[0]), "Settling must preserve buffered jumps");
        require(MovingPlatform::touchingTop(player, solids[0]), "Buffered jump landings must still trigger the platform group");
        player = Player{};
        player.x = 625;
        player.y = 675;
        player.vy = -300;
        UpdatePlayerMovement(player, map, 1.0f / 30, movement, 0, false, false,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.y >= 672 && player.vy == 0,
            "Solid platforms must stop rising players at their underside");
        require(MovingPlatform::touching(player, solids[0]), "Underside collision must activate the platform");
        player = Player{};
        player.x = 590;
        player.y = 644;
        player.vx = 300;
        UpdatePlayerMovement(player, map, 1.0f / 30, movement, 1, false, false,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.x + player.w <= 624 && player.vx == 0, "Platforms must block their left side");
        require(MovingPlatform::touching(player, solids[0]), "Left-side collision must activate the platform");
        player = Player{};
        player.x = 660;
        player.y = 644;
        player.vx = -300;
        UpdatePlayerMovement(player, map, 1.0f / 30, movement, -1, false, false,
            0, false, false, false, false, keys, inputMove, inputDown, &solids);
        require(player.x >= 656 && player.vx == 0, "Platforms must block their right side");
        require(MovingPlatform::touching(player, solids[0]), "Right-side collision must activate the platform");
        require(!RectHitsSolid(map, 630, 650, 10, 10), "Dynamic colliders must not leak outside the player update");
        // A platform must occupy exactly the same collision pixels as a solid tile,
        // including fractional positions just inside each edge.
        TileMap tiledMap;
        tiledMap.resize(64, 64);
        tiledMap.setSolid(20, 20, 1);
        solids = {{640, 640, 32, 32}};
        const Player defaultPlayer;
        const SDL_FPoint edgePositions[] = {
            {640.0f - defaultPlayer.w, 644}, {640.0f - defaultPlayer.w + 0.5f, 644},
            {672, 644}, {672 - 0.5f, 644},
            {644, 640.0f - defaultPlayer.h}, {644, 640.0f - defaultPlayer.h + 0.5f},
            {644, 672}, {644, 672 - 0.5f}
        };
        for (const auto& position : edgePositions) {
            Player onPlatform, onTile;
            onPlatform.x = onTile.x = position.x;
            onPlatform.y = onTile.y = position.y;
            const auto platformResult = UpdatePlayerMovement(onPlatform, map, 1.0f / 60, movement, 0, false, false,
                0, false, false, false, false, keys, inputMove, inputDown, &solids);
            const auto tileResult = UpdatePlayerMovement(onTile, tiledMap, 1.0f / 60, movement, 0, false, false,
                0, false, false, false, false, keys, inputMove, inputDown);
            require(platformResult == tileResult && close(onPlatform.x, onTile.x) && close(onPlatform.y, onTile.y) &&
                close(onPlatform.vy, onTile.vy) && onPlatform.onGround == onTile.onGround,
                "Platform edges must match solid tile edges without an extra collision pixel");
        }
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
