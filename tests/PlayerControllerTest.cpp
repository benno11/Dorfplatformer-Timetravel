#include "PlayerController.h"
#include "LevelWrap.h"

#include <SDL3/SDL.h>
#include <cassert>
#include <cmath>

namespace {
Player makeFallingPlayer() {
    Player player;
    player.x = 70.0f;
    player.y = 0.0f;
    player.vy = 1000.0f;
    player.w = 30;
    player.h = 30;
    return player;
}

void update(Player& player, const TileMap& map) {
    MovementConfig movement;
    movement.gravityGround = 0.0f;
    KeyboardBindings bindings;
    float inputMove = 0.0f;
    bool inputDown = false;
    const PlayerUpdateResult result = UpdatePlayerMovement(
        player, map, 0.2f, movement,
        0.0f, false, false,
        0.0f, false, false, false, false,
        bindings, inputMove, inputDown
    );
    assert(result == PlayerUpdateResult::Normal);
}
}

int main() {
    assert(SDL_Init(SDL_INIT_EVENTS));

    TileMap solidMap;
    solidMap.tileSize = 64;
    solidMap.resize(4, 8);
    solidMap.setSolid(1, 2, 1);
    Player solidPlayer = makeFallingPlayer();
    update(solidPlayer, solidMap);
    assert(solidPlayer.onGround);
    assert(solidPlayer.vy == 0.0f);
    assert(solidPlayer.y < 128.0f);
    assert(!RectHitsSolid(solidMap, solidPlayer.x, solidPlayer.y, solidPlayer.w, solidPlayer.h));

    TileMap semisolidMap;
    semisolidMap.tileSize = 64;
    semisolidMap.resize(4, 8);
    semisolidMap.setSemiSolid(1, 2, 1);
    Player semisolidPlayer = makeFallingPlayer();
    update(semisolidPlayer, semisolidMap);
    assert(semisolidPlayer.onGround);
    assert(semisolidPlayer.vy == 0.0f);
    assert(std::fabs(semisolidPlayer.y - 99.0f) < 0.001f);

    TileMap wrapMap;
    wrapMap.tileSize = 64;
    wrapMap.resize(3, 3);
    wrapMap.setSolid(2, 1, 1);
    SetHorizontalWrapCollision(false);
    assert(!RectHitsSolid(wrapMap, -32.0f, 64.0f, 30, 30));
    SetHorizontalWrapCollision(true);
    assert(RectHitsSolid(wrapMap, -32.0f, 64.0f, 30, 30));

    wrapMap.setSolid(1, 2, 1);
    SetVerticalWrapCollision(false);
    assert(!RectHitsSolid(wrapMap, 64.0f, -32.0f, 30, 30));
    SetVerticalWrapCollision(true);
    assert(RectHitsSolid(wrapMap, 64.0f, -32.0f, 30, 30));

    SetHorizontalWrapCollision(false);
    SetVerticalWrapCollision(false);

    std::vector<ObjectInstance> wrapTriggers{
        ObjectInstance{"62", 32.0f, 32.0f},
        ObjectInstance{"63", 160.0f, 160.0f},
    };
    const std::vector<int> wrapTriggerIds{62, 63};
    Player triggerPlayer;
    triggerPlayer.x = 16.0f;
    triggerPlayer.y = 16.0f;
    triggerPlayer.w = 30;
    triggerPlayer.h = 30;
    assert(PlayerTouchesLevelWrapTrigger(triggerPlayer, wrapTriggers, wrapTriggerIds, 62));
    assert(!PlayerTouchesLevelWrapTrigger(triggerPlayer, wrapTriggers, wrapTriggerIds, 63));
    triggerPlayer.x = 64.0f;
    assert(!PlayerTouchesLevelWrapTrigger(triggerPlayer, wrapTriggers, wrapTriggerIds, 62));

    SDL_Quit();
    return 0;
}
