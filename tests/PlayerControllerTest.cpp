#include "PlayerController.h"
#include "LevelWrap.h"
#include "SimulationTiming.h"

#include <SDL3/SDL.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>

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

void step(Player& player, const TileMap& map, float dt, MovementConfig movement = {}, float move = 0.0f, bool jump = false) {
    KeyboardBindings bindings;
    float inputMove = 0.0f;
    bool inputDown = false;
    const PlayerUpdateResult result = UpdatePlayerMovement(
        player, map, dt, movement,
        move, false, jump,
        0.0f, false, false, false, false,
        bindings, inputMove, inputDown
    );
    assert(result == PlayerUpdateResult::Normal);
}

void update(Player& player, const TileMap& map) {
    MovementConfig movement;
    movement.gravityGround = 0.0f;
    for (int i = 0; i < 6; ++i) step(player, map, 1.0f / 30.0f, movement);
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
    // Non-wrapping horizontal map bounds are solid walls.
    assert(RectHitsSolid(wrapMap, -32.0f, 64.0f, 30, 30));
    SetHorizontalWrapCollision(true);
    assert(RectHitsSolid(wrapMap, -32.0f, 64.0f, 30, 30));

    wrapMap.setSolid(1, 2, 1);
    SetVerticalWrapCollision(false);
    assert(!RectHitsSolid(wrapMap, 64.0f, -32.0f, 30, 30));
    SetVerticalWrapCollision(true);
    assert(RectHitsSolid(wrapMap, 64.0f, -32.0f, 30, 30));
    SetHorizontalWrapCollision(false);
    SetVerticalWrapCollision(false);

    // Each face must eject to the closest clear side without held input.
    TileMap ejectMap;
    ejectMap.tileSize = 64;
    ejectMap.resize(8, 8);
    ejectMap.setSolid(3, 3, 1);
    const float positions[][2] = {{170, 205}, {250, 205}, {205, 170}, {205, 250}};
    for (int face = 0; face < 4; ++face) {
        Player embedded;
        embedded.w = embedded.h = 30;
        embedded.x = positions[face][0];
        embedded.y = positions[face][1];
        embedded.vx = face == 0 ? 100.0f : (face == 1 ? -100.0f : 0.0f);
        embedded.vy = face == 2 ? 100.0f : (face == 3 ? -100.0f : 0.0f);
        assert(RectHitsSolid(ejectMap, embedded.x, embedded.y, embedded.w, embedded.h));
        step(embedded, ejectMap, 0.0f);
        assert(!RectHitsSolid(ejectMap, embedded.x, embedded.y, embedded.w, embedded.h));
        assert(embedded.vx == 0.0f && embedded.vy == 0.0f);
        if (face == 0) assert(embedded.x < positions[face][0]);
        if (face == 1) assert(embedded.x > positions[face][0]);
        if (face == 2) assert(embedded.y < positions[face][1] && embedded.onGround);
        if (face == 3) assert(embedded.y > positions[face][1] && !embedded.onGround);
    }

    TileMap sealedMap;
    sealedMap.resize(16, 16);
    for (int y = 0; y < sealedMap.h; ++y)
        for (int x = 0; x < sealedMap.w; ++x) sealedMap.setSolid(x, y, 1);
    Player trapped;
    trapped.x = trapped.y = 400.0f;
    trapped.vx = trapped.vy = 100.0f;
    step(trapped, sealedMap, 1.0f);
    assert(trapped.x == 400.0f && trapped.y == 400.0f);
    assert(trapped.vx == 0.0f && trapped.vy == 0.0f);

    TileMap emptyMap;
    emptyMap.resize(32, 32);
    MovementConfig limits;
    for (float velocity : {-10000.0f, 10000.0f}) {
        Player fast;
        fast.x = fast.y = 500.0f;
        fast.vy = velocity;
        step(fast, emptyMap, 1.0f / 60.0f, limits);
        assert(fast.vy >= -limits.maxRiseSpeed && fast.vy <= limits.maxFallSpeed);
    }
    TileMap waterMap = emptyMap;
    waterMap.setWater(14, 14, 1);
    Player swimmer;
    swimmer.x = swimmer.y = 450.0f;
    swimmer.vy = 1000.0f;
    step(swimmer, waterMap, 1.0f / 60.0f, limits);
    assert(swimmer.inWater && swimmer.vy == limits.maxFallSpeedInWater);

    // Push tiles move in both directions, leave decaying momentum, and hand
    // control back to the player when matching directional input is pressed.
    MovementConfig pushMovement;
    pushMovement.gravityGround = 0.0f;
    for (int direction : {-1, 1}) {
        TileMap pushMap = emptyMap;
        pushMap.tileIds[6 * pushMap.w + 6] = direction > 0 ? 83 : 86;
        Player pushed;
        pushed.x = pushed.y = 200.0f;
        pushed.w = pushed.h = 30;
        step(pushed, pushMap, 1.0f / 60.0f, pushMovement);
        assert((pushed.x - 200.0f) * direction > 0.0f);
        assert(pushed.vx * direction > 0.0f);
        assert(pushed.tilePushDirection == direction);
        const float pushSpeed = std::fabs(pushed.vx);
        // Continuous contact is bounded, rather than stacking impulses.
        step(pushed, pushMap, 1.0f / 60.0f, pushMovement);
        assert(std::fabs(pushed.vx) == pushSpeed);
        pushMap.tileIds[6 * pushMap.w + 6] = 0;
        Player matchingInput = pushed;
        Player opposingInput = pushed;
        const float previousX = pushed.x;
        step(pushed, pushMap, 1.0f / 60.0f, pushMovement);
        assert((pushed.x - previousX) * direction > 0.0f);
        assert(pushed.vx * direction > 0.0f && std::fabs(pushed.vx) < pushSpeed);
        step(matchingInput, pushMap, 1.0f / 60.0f, pushMovement, (float)direction);
        assert(matchingInput.tilePushDirection == 0);
        assert(std::fabs(matchingInput.vx) < std::fabs(pushed.vx));
        assert(matchingInput.vx * direction > 0.0f);
        step(opposingInput, pushMap, 1.0f / 60.0f, pushMovement, (float)-direction);
        assert(opposingInput.vx * direction > 0.0f);
        assert(std::fabs(opposingInput.vx) < std::fabs(pushed.vx));
        for (int frame = 0; frame < 120; ++frame) step(pushed, pushMap, 1.0f / 60.0f, pushMovement);
        assert(pushed.vx == 0.0f && pushed.tilePushDirection == 0);

        // A wall stops both the player and the leftover tile momentum.
        pushMap.tileIds[6 * pushMap.w + 6] = direction > 0 ? 83 : 86;
        pushMap.setSolid(direction > 0 ? 7 : 5, 6, 1);
        Player blocked;
        blocked.x = direction > 0 ? 194.0f : 192.0f;
        blocked.y = 200.0f;
        blocked.w = blocked.h = 30;
        step(blocked, pushMap, 1.0f / 60.0f, pushMovement);
        assert(blocked.vx == 0.0f && blocked.tilePushDirection == 0);
        assert(!RectHitsSolid(pushMap, blocked.x, blocked.y, blocked.w, blocked.h));
    }

    TileMap pushFloor = emptyMap;
    pushFloor.tileIds[7 * pushFloor.w + 6] = 83;
    pushFloor.setSolid(6, 7, 1);
    Player onPushFloor;
    onPushFloor.x = 200.0f;
    onPushFloor.y = 194.0f;
    onPushFloor.w = onPushFloor.h = 30;
    step(onPushFloor, pushFloor, 1.0f / 60.0f, pushMovement);
    assert(onPushFloor.x > 200.0f && onPushFloor.tilePushDirection == 1);

    TileMap wrappedPushMap = emptyMap;
    wrappedPushMap.tileIds[6 * wrappedPushMap.w + wrappedPushMap.w - 1] = 83;
    Player wrappedPush;
    wrappedPush.x = -8.0f;
    wrappedPush.y = 200.0f;
    wrappedPush.w = wrappedPush.h = 30;
    SetHorizontalWrapCollision(true);
    step(wrappedPush, wrappedPushMap, 1.0f / 60.0f, pushMovement);
    assert(wrappedPush.vx > 0.0f && wrappedPush.tilePushDirection == 1);
    SetHorizontalWrapCollision(false);

    TileMap bounceMap = emptyMap;
    bounceMap.bouncyTile12 = true;
    for (int x = 0; x < bounceMap.w; ++x) {
        bounceMap.setSolid(x, 7, 1);
        bounceMap.tileIds[7 * bounceMap.w + x] = 12;
    }
    Player landing;
    landing.x = 200.0f;
    landing.y = 193.0f;
    landing.w = landing.h = 30;
    landing.vy = 1000.0f;
    Player bouncing = landing;
    step(bouncing, bounceMap, 1.0f / 120.0f);
    const float bounceSpeed = std::sqrt(2.0f * limits.gravityGround * bounceMap.tileSize / 5.0f);
    assert(std::fabs(bouncing.vy + bounceSpeed) < 0.001f);
    assert(bouncing.tile12BounceActive && !bouncing.onGround);
    const float floorY = bouncing.y;
    step(bouncing, bounceMap, 1.0f / 240.0f);
    assert(std::fabs(bouncing.vy - (-bounceSpeed + limits.gravityGround / 240.0f)) < 0.001f);
    Player canceledBounce = bouncing;
    step(canceledBounce, bounceMap, 1.0f / 240.0f, {}, 0, true);
    assert(!canceledBounce.tile12BounceActive && canceledBounce.vy >= 0.0f);
    assert(canceledBounce.jumpBufferTime > 0.0f);
    // The canceled bounce's buffered input becomes a normal jump on touchdown.
    for (int frame = 0; frame < 30 && canceledBounce.vy >= 0.0f; ++frame)
        step(canceledBounce, bounceMap, 1.0f / 240.0f, {}, 0, true);
    assert(canceledBounce.vy < -bounceSpeed && !canceledBounce.tile12BounceActive);
    float highestY = bouncing.y;
    int relaunches = 0;
    for (int frame = 0; frame < 240; ++frame) {
        const float previousVy = bouncing.vy;
        step(bouncing, bounceMap, 1.0f / 240.0f);
        highestY = std::min(highestY, bouncing.y);
        if (previousVy > 0 && bouncing.vy < 0) {
            ++relaunches;
            assert(std::fabs(bouncing.vy + bounceSpeed) < 0.001f);
        }
    }
    assert(relaunches >= 3);
    assert(std::fabs((floorY - highestY) - bounceMap.tileSize / 5.0f) < 0.8f);
    Player jumpPriority = landing;
    step(jumpPriority, bounceMap, 1.0f / 120.0f, {}, 0, true);
    assert(jumpPriority.vy == -limits.jumpSpeed && !jumpPriority.tile12BounceActive);
    Player bufferedTap = landing;
    bufferedTap.jumpBufferTime = 0.1f;
    step(bufferedTap, bounceMap, 1.0f / 120.0f);
    assert(bufferedTap.vy == -limits.jumpCutSpeed && !bufferedTap.tile12BounceActive);
    Player expiredBuffer = landing;
    expiredBuffer.jumpBufferTime = 0.001f;
    step(expiredBuffer, bounceMap, 1.0f / 120.0f);
    assert(expiredBuffer.tile12BounceActive);
    TileMap normalTile12 = bounceMap;
    normalTile12.bouncyTile12 = false;
    Player normalLanding = landing;
    step(normalLanding, normalTile12, 1.0f / 120.0f);
    assert(normalLanding.onGround && normalLanding.vy == 0 && !normalLanding.tile12BounceActive);
    TileMap otherTile = bounceMap;
    for (auto& id : otherTile.tileIds) if (id == 12) id = 1;
    Player otherLanding = landing;
    step(otherLanding, otherTile, 1.0f / 120.0f);
    assert(otherLanding.onGround && otherLanding.vy == 0);

    // A stall advances one bounded gameplay frame, with no catch-up next frame.
    Player stalled;
    stalled.x = stalled.y = 500.0f;
    stalled.vx = 600.0f;
    stalled.vy = 1000.0f;
    Player normal = stalled;
    step(stalled, emptyMap, 5.0f);
    step(normal, emptyMap, GameplayDeltaTime(5.0));
    assert(stalled.x == normal.x && stalled.y == normal.y);
    step(stalled, emptyMap, 1.0f / 60.0f);
    step(normal, emptyMap, 1.0f / 60.0f);
    assert(stalled.x == normal.x && stalled.y == normal.y);
    assert(GameplayDeltaTime(-1.0) == 0.0f);
    assert(GameplayDeltaTime(std::numeric_limits<double>::infinity()) == 0.0f);
    assert(GameplayDeltaTime(std::numeric_limits<double>::quiet_NaN()) == 0.0f);
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
    assert(LevelHasWrapTrigger(wrapTriggerIds, 62));
    assert(!LevelHasWrapTrigger(wrapTriggerIds, 69));
    assert(LevelHasWrapControlTrigger(wrapTriggerIds, 62, 69));
    bool wrapXActive = false;
    wrapXActive = UpdateLevelWrapTriggerState(triggerPlayer, wrapTriggers, wrapTriggerIds, 62, 69, wrapXActive);
    assert(wrapXActive);
    triggerPlayer.x = 120.0f;
    assert(!PlayerTouchesLevelWrapTrigger(triggerPlayer, wrapTriggers, wrapTriggerIds, 62));
    wrapXActive = UpdateLevelWrapTriggerState(triggerPlayer, wrapTriggers, wrapTriggerIds, 62, 69, wrapXActive);
    assert(wrapXActive);
    wrapTriggers.push_back(ObjectInstance{"69", 96.0f, 32.0f});
    std::vector<int> wrapDisableIds{62, 63, 69};
    triggerPlayer.x = 80.0f;
    wrapXActive = UpdateLevelWrapTriggerState(triggerPlayer, wrapTriggers, wrapDisableIds, 62, 69, wrapXActive);
    assert(!wrapXActive);

    SDL_Quit();
    return 0;
}
