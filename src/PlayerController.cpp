#include "PlayerController.h"
#include "SimulationTiming.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace {
bool g_horizontalWrapCollision = false;
bool g_verticalWrapCollision = false;
bool swimheld = false;
const std::vector<SDL_FRect>* g_dynamicSolids = nullptr;

bool hitsDynamicSolid(float x, float y, int w, int h) {
    if (!g_dynamicSolids) return false;
    // Match the inclusive pixel bounds used by the tile collision checks below.
    // Using x + w / y + h here makes platforms block one pixel too early.
    for (const auto& rect : *g_dynamicSolids)
        if (x + w - 1 >= rect.x && x < rect.x + rect.w &&
            y + h - 1 >= rect.y && y < rect.y + rect.h) return true;
    return false;
}

inline int wrapTileX(int x, int w) {
    if (w <= 0) return x;
    int v = x % w;
    if (v < 0) v += w;
    return v;
}

inline int wrapTileY(int y, int h) {
    if (h <= 0) return y;
    int v = y % h;
    if (v < 0) v += h;
    return v;
}
}

void SetHorizontalWrapCollision(bool enabled) {
    g_horizontalWrapCollision = enabled;
}

void SetVerticalWrapCollision(bool enabled) {
    g_verticalWrapCollision = enabled;
}

bool RectHitsSolid(const TileMap& map, float x, float y, int w, int h) {
    if (hitsDynamicSolid(x, y, w, h)) return true;
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int top = (int)std::floor(y / t);
    int bottom = (int)std::floor((y + h - 1) / t);
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            const int qx = g_horizontalWrapCollision ? wrapTileX(tx, map.w) : tx;
            const int qy = g_verticalWrapCollision ? wrapTileY(ty, map.h) : ty;
            if (map.getSolid(qx, qy)) return true;
        }
    }
    return false;
}

static bool rectHitsSemiSolidDown(const TileMap& map, float oldY, float newY, float x, int w, int h) {
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int oldBottom = (int)std::floor((oldY + h - 1) / t);
    int newBottom = (int)std::floor((newY + h - 1) / t);
    if (newBottom < oldBottom) return false;

    int ty = newBottom;
    for (int tx = left; tx <= right; ++tx) {
        const int qx = g_horizontalWrapCollision ? wrapTileX(tx, map.w) : tx;
        const int qy = g_verticalWrapCollision ? wrapTileY(ty, map.h) : ty;
        if (!map.getSemiSolid(qx, qy)) continue;
        float tileTop = qy * t;
        float oldBottomY = oldY + h - 1;
        float newBottomY = newY + h - 1;
        if (oldBottomY <= tileTop && newBottomY >= tileTop) return true;
    }
    return false;
}

static bool rectHitsWater(const TileMap& map, float x, float y, int w, int h) {
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int top = (int)std::floor(y / t);
    int bottom = (int)std::floor((y + h - 1) / t);
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            const int qx = g_horizontalWrapCollision ? wrapTileX(tx, map.w) : tx;
            const int qy = g_verticalWrapCollision ? wrapTileY(ty, map.h) : ty;
            if (map.getWater(qx, qy)) return true;
        }
    }
    return false;
}

static bool rectHasGroundBelow(const TileMap& map, float x, float y, int w, int h) {
    if (hitsDynamicSolid(x, y + 1.0f, w, h)) return true;
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int footTile = (int)std::floor((y + h) / t);
    for (int tx = left; tx <= right; ++tx) {
        const int qx = g_horizontalWrapCollision ? wrapTileX(tx, map.w) : tx;
        const int qy = g_verticalWrapCollision ? wrapTileY(footTile, map.h) : footTile;
        if (map.getSolid(qx, qy) || map.getSemiSolid(qx, qy)) return true;
    }
    return false;
}

static bool keyHeld(const bool* keys, SDL_Scancode sc) {
    if (!keys) return false;
    if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT) return false;
    return keys[sc];
}

static bool standingOnTile12(const TileMap& map, const Player& player) {
    if (map.tileSize <= 0 || map.w <= 0 || map.h <= 0) return false;
    int ty = (int)std::floor((player.y + player.h) / map.tileSize);
    if (g_verticalWrapCollision) ty = wrapTileY(ty, map.h);
    if (ty < 0 || ty >= map.h) return false;
    const int left = (int)std::floor(player.x / map.tileSize);
    const int right = (int)std::floor((player.x + player.w - 1) / map.tileSize);
    for (int tx = left; tx <= right; ++tx) {
        const int qx = g_horizontalWrapCollision ? wrapTileX(tx, map.w) : tx;
        if (qx < 0 || qx >= map.w) continue;
        const size_t index = (size_t)ty * map.w + qx;
        if (index < map.tileIds.size() && map.tileIds[index] == 12) return true;
    }
    return false;
}

static int touchingPushTileDirection(const TileMap& map, const Player& player) {
    const int t = map.tileSize;
    if (t <= 0 || map.w <= 0 || map.h <= 0) return 0;
    const int left = (int)std::floor(player.x / t);
    const int right = (int)std::floor((player.x + player.w - 1) / t);
    const int top = (int)std::floor(player.y / t);
    // Include the surface directly under the feet for solid push tiles.
    const int bottom = (int)std::floor((player.y + player.h) / t);
    int direction = 0;
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            const int qx = g_horizontalWrapCollision ? wrapTileX(tx, map.w) : tx;
            const int qy = g_verticalWrapCollision ? wrapTileY(ty, map.h) : ty;
            if (qx < 0 || qx >= map.w || qy < 0 || qy >= map.h) continue;
            const size_t index = (size_t)qy * map.w + qx;
            if (index >= map.tileIds.size()) continue;
            if (map.tileIds[index] == 83) direction += 1;
            else if (map.tileIds[index] == 86) direction -= 1;
        }
    }
    return (direction > 0) - (direction < 0);
}

bool EjectPlayerFromSolid(Player& player, const TileMap& map) {
    if (!RectHitsSolid(map, player.x, player.y, player.w, player.h)) return true;

    // Find the nearest clear side, including ceilings and either wall. Bound the
    // search so a sealed room cannot hang the update or teleport across the map.
    const int maxDistance = map.tileSize * 4 + std::max(player.w, player.h);
    const int directions[][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    for (int distance = 1; distance <= maxDistance; ++distance) {
        for (const auto& direction : directions) {
            const float x = player.x + direction[0] * distance;
            const float y = player.y + direction[1] * distance;
            if (RectHitsSolid(map, x, y, player.w, player.h)) continue;
            player.x = x;
            player.y = y;
            // Only cancel velocity directed back into the surface.
            if (player.vx * direction[0] < 0.0f) {
                player.vx = 0.0f;
                player.tilePushDirection = 0;
            }
            if (player.vy * direction[1] < 0.0f) player.vy = 0.0f;
            player.onGround = rectHasGroundBelow(map, x, y, player.w, player.h);
            return true;
        }
    }
    player.vx = 0.0f;
    player.tilePushDirection = 0;
    player.vy = 0.0f;
    player.onGround = false;
    return false;
}

PlayerUpdateResult UpdatePlayerMovement(
    Player& player,
    const TileMap& map,
    float dt,
    const MovementConfig& movement,
    float touchMove,
    bool touchDown,
    bool touchJump,
    float gamepadMove,
    bool gamepadDown,
    bool gamepadJump,
    bool gamepadFreeMove,
    bool allowFreeMove,
    const KeyboardBindings& keybinds,
    float& inputMove,
    bool& inputDown,
    const std::vector<SDL_FRect>* dynamicSolids
) {
    struct SolidScope {
        const std::vector<SDL_FRect>* previous;
        ~SolidScope() { g_dynamicSolids = previous; }
    } solidScope{g_dynamicSolids};
    g_dynamicSolids = dynamicSolids;
    dt = GameplayDeltaTime(dt);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    bool shiftDown = allowFreeMove && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
    shiftDown = shiftDown || gamepadFreeMove;
    player.freeMove = shiftDown;

    if (player.freeMove) {
        float freeSpeed = 1200.0f;
        float dx = 0.0f;
        float dy = 0.0f;
        if (keyHeld(keys, keybinds.moveLeft) || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
        if (keyHeld(keys, keybinds.moveRight) || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keyHeld(keys, keybinds.jump) || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
        if (keyHeld(keys, keybinds.moveDown) || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.0f) { dx /= len; dy /= len; }
        player.x += dx * freeSpeed * dt;
        player.y += dy * freeSpeed * dt;
        player.vx = 0.0f;
        player.tilePushDirection = 0;
        player.vy = 0.0f;
        player.onGround = false;
        player.jumpHeld = false;
        player.jumpHoldTime = 0.0f;
        player.jumpWasDown = false;
        player.jumpBufferTime = 0.0f;
        player.tile12BounceActive = false;
        return PlayerUpdateResult::RenderOnly;
    }

    float move = touchMove + gamepadMove;
    if (keyHeld(keys, keybinds.moveLeft) || keys[SDL_SCANCODE_LEFT]) move -= 1.0f;
    if (keyHeld(keys, keybinds.moveRight) || keys[SDL_SCANCODE_RIGHT]) move += 1.0f;
    move = std::clamp(move, -1.0f, 1.0f);
    bool downHeld = touchDown || gamepadDown || keyHeld(keys, keybinds.moveDown) || keys[SDL_SCANCODE_DOWN];
    inputMove = move;
    inputDown = downHeld;
    if (!EjectPlayerFromSolid(player, map)) {
        return PlayerUpdateResult::Normal;
    }

    bool inWater = rectHitsWater(map, player.x, player.y, player.w, player.h);
    player.inWater = inWater;

    float accel = inWater ? movement.accelInWater : movement.accelGround;
    float maxSpeed = inWater ? movement.maxSpeedInWater : movement.maxSpeedGround;
    float friction = inWater ? movement.frictionInWater : movement.frictionGround;
    float gravity = inWater ? movement.gravityInWater : movement.gravityGround;
    float jumpSpeed = movement.jumpSpeed;
    float jumpHoldGravity = movement.jumpHoldGravity;
    float jumpHoldMax = movement.jumpHoldMax;
    float jumpCutSpeed = movement.jumpCutSpeed;
    float swimUpSpeed = movement.swimUpSpeed;
    float swimRise = movement.swimRise;

    float controlAccel = accel;
    float controlFriction = friction;
    const int pushDirection = touchingPushTileDirection(map, player);
    if (pushDirection == 0 && move * player.tilePushDirection > 0.0f) {
        // Matching input takes control again after leaving the push tile.
        player.vx = 0.0f;
        player.tilePushDirection = 0;
    }

    if (move != 0.0f) {
        const float targetVx = move * maxSpeed;
        const bool reversing = (player.vx * move) < 0.0f;
        if (reversing) {
            // Brake harder on direction flip so speed remains manageable.
            const float turnBrake = controlFriction * 1.6f * dt;
            if (player.vx > 0.0f) player.vx = std::max(0.0f, player.vx - turnBrake);
            else player.vx = std::min(0.0f, player.vx + turnBrake);
        }
        const float step = controlAccel * dt;
        if (player.vx < targetVx) player.vx = std::min(targetVx, player.vx + step);
        else if (player.vx > targetVx) player.vx = std::max(targetVx, player.vx - step);
    } else {
        const float step = controlFriction * dt;
        if (player.vx > 0.0f) player.vx = std::max(0.0f, player.vx - step);
        else if (player.vx < 0.0f) player.vx = std::min(0.0f, player.vx + step);
    }

    // Soft-cap overspeed instead of hard-clamping to avoid sudden snaps.
    const float speedSign = (player.vx >= 0.0f) ? 1.0f : -1.0f;
    const float speedAbs = std::fabs(player.vx);
    if (speedAbs > maxSpeed) {
        float excess = speedAbs - maxSpeed;
        const float recover = (!inWater && player.onGround) ? 14.0f : 7.0f;
        excess *= std::max(0.0f, 1.0f - recover * dt);
        player.vx = speedSign * (maxSpeed + excess);
    }
    if (pushDirection != 0) {
        constexpr float kTilePushSpeed = 360.0f;
        player.vx = (pushDirection * kTilePushSpeed * 4);
        player.tilePushDirection = pushDirection;
    } else if (player.vx * player.tilePushDirection <= 0.0f) {
        player.tilePushDirection = 0;
    }

    bool jumpDown = touchJump || gamepadJump || keyHeld(keys, keybinds.jump) || keys[SDL_SCANCODE_UP];
    bool jumpPressed = jumpDown && !player.jumpWasDown;
    bool jumpReleased = !jumpDown && player.jumpWasDown;
    player.jumpBufferTime = std::max(0.0f, player.jumpBufferTime - dt);
    if (!map.bouncyTile12) player.tile12BounceActive = false;
    if (jumpPressed) {
        player.jumpBufferTime = 0.15f;
        if (player.tile12BounceActive) {
            if (player.vy < 0.0f) player.vy = 0.0f;
            player.tile12BounceActive = false;
        }
    }

    bool bufferedJumpReady = player.jumpBufferTime > 0.0f;
    if ((player.onGround || inWater) && bufferedJumpReady) {
        player.vy = jumpDown ? -jumpSpeed : -std::min(jumpSpeed, jumpCutSpeed);
        if (player.onGround) player.onGround = false;
        player.jumpHeld = false;
        player.jumpHoldTime = 0.0f;
        player.jumpBufferTime = 0.0f;
        player.tile12BounceActive = false;
    }

    player.jumpHeld = false;

    if (jumpReleased && player.vy < -jumpCutSpeed) {
        player.vy = -jumpCutSpeed;
    }
    if (!inWater && !jumpDown && !player.tile12BounceActive && player.vy < 0.0f) {
        // Early release should create a noticeably shorter hop.
        gravity *= 1.35f;
    }

    if (inWater) {
        if (jumpDown && swimheld == false) {
            swimheld = true;
            // Preserve stronger jump impulses so the player can break out of water.
            // Swim rise only assists up to the normal swim-up cap.
            if (player.vy > -swimUpSpeed) {
                player.vy -= (swimRise * dt) * 0.25f;
                if (player.vy < -swimUpSpeed) player.vy = -swimUpSpeed;
            }
        }else{
            if (!jumpDown){
                swimheld = false;
            }
        }
    }

    player.vy += gravity * dt;
    player.vy = std::clamp(player.vy, -std::max(0.0f, movement.maxRiseSpeed),
        std::max(0.0f, inWater ? movement.maxFallSpeedInWater : movement.maxFallSpeed));
    player.jumpWasDown = jumpDown;

    if (inWater) {
        player.drownTimer += dt;
        if (player.drownTimer >= Player::kUnderwaterAirSeconds) {
            player.drownTimer = 0.0f;
            return PlayerUpdateResult::Reloaded;
        }
    } else {
        player.drownTimer = std::max(0.0f, player.drownTimer - Player::kAirRefillPerSecond * dt);
    }

    // Sweep long frame movements in tile-sized substeps. Checking only the final
    // position lets a fast fall (or a frame-time spike) jump completely across a
    // one-tile floor without ever overlapping it.
    const float maxCollisionStep = std::max(1.0f, map.tileSize * 0.5f);
    const float totalX = player.vx * dt;
    const int xSteps = std::max(1, (int)std::ceil(std::fabs(totalX) / maxCollisionStep));
    const float xStep = totalX / xSteps;
    for (int step = 0; step < xSteps; ++step) {
        const float newX = player.x + xStep;
        if (!RectHitsSolid(map, newX, player.y, player.w, player.h)) {
            player.x = newX;
            continue;
        }

        const int dir = (xStep > 0.0f) ? 1 : -1;
        while (!RectHitsSolid(map, player.x + dir, player.y, player.w, player.h)) {
            player.x += dir;
        }
        player.vx = 0.0f;
        player.tilePushDirection = 0;
        break;
    }

    const float totalY = player.vy * dt;
    const int ySteps = std::max(1, (int)std::ceil(std::fabs(totalY) / maxCollisionStep));
    const float yStep = totalY / ySteps;
    for (int step = 0; step < ySteps; ++step) {
        const float oldY = player.y;
        const float newY = oldY + yStep;
        if (!RectHitsSolid(map, player.x, newY, player.w, player.h)) {
            if (yStep > 0.0f && rectHitsSemiSolidDown(map, oldY, newY, player.x, player.w, player.h)) {
                const int bottomTile = (int)std::floor((newY + player.h - 1) / map.tileSize);
                player.y = bottomTile * map.tileSize - player.h + 1;
                player.onGround = true;
                player.vy = 0.0f;
                break;
            }
            player.y = newY;
            player.onGround = false;
            continue;
        }

        const int dir = (yStep > 0.0f) ? 1 : -1;
        while (!RectHitsSolid(map, player.x, player.y + dir, player.w, player.h)) {
            player.y += dir;
        }
        if (dir > 0) player.onGround = true;
        player.vy = 0.0f;
        break;
    }

    const float resetY = (float)((map.h + 7) * map.tileSize);
    if (player.vy >= 0.0f) {
        // High-FPS stability: explicit probe below feet prevents grounded flicker
        // when vy ~= 0 and no actual Y movement occurs this frame.
        player.onGround = rectHasGroundBelow(map, player.x, player.y, player.w, player.h);
    }
    if (player.onGround && player.jumpBufferTime > 0.0f) {
        // A jump queued before touchdown wins over the automatic tile impulse.
        player.vy = jumpDown ? -jumpSpeed : -std::min(jumpSpeed, jumpCutSpeed);
        player.onGround = false;
        player.jumpBufferTime = 0.0f;
        player.tile12BounceActive = false;
    } else if (player.onGround && !inWater && map.bouncyTile12 && standingOnTile12(map, player) && !jumpDown) {
        // v^2 = 2*g*h: start a 1/5-block hop, then use ordinary gravity.
        const float height = (1200f) // makes the player bounce higher than the tile itself, but not too high
        player.vy = -std::sqrt(2.0f * std::max(0.0f, movement.gravityGround) * height);
        player.onGround = player.vy == 0.0f;
        player.tile12BounceActive = player.vy < 0.0f;
    } else if (player.onGround) {
        player.tile12BounceActive = false;
    }
    if (player.y > resetY) {
        return PlayerUpdateResult::Reloaded;
    }

    return PlayerUpdateResult::Normal;
}
