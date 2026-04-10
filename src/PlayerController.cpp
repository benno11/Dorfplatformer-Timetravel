#include "PlayerController.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace {
bool g_horizontalWrapCollision = false;
bool g_verticalWrapCollision = false;

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
    bool& inputDown
) {
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
        player.vy = 0.0f;
        player.onGround = false;
        player.jumpHeld = false;
        player.jumpHoldTime = 0.0f;
        player.jumpWasDown = false;
        return PlayerUpdateResult::RenderOnly;
    }

    float move = touchMove + gamepadMove;
    if (keyHeld(keys, keybinds.moveLeft) || keys[SDL_SCANCODE_LEFT]) move -= 1.0f;
    if (keyHeld(keys, keybinds.moveRight) || keys[SDL_SCANCODE_RIGHT]) move += 1.0f;
    move = std::clamp(move, -1.0f, 1.0f);
    bool downHeld = touchDown || gamepadDown || keyHeld(keys, keybinds.moveDown) || keys[SDL_SCANCODE_DOWN];
    inputMove = move;
    inputDown = downHeld;
    const bool insideSolid = RectHitsSolid(map, player.x, player.y, player.w, player.h);

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
    if (!inWater && !player.onGround) {
        // Air control should be softer than ground control.
        controlAccel *= 0.72f;
        controlFriction *= 0.18f;
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

    bool jumpDown = touchJump || gamepadJump || keyHeld(keys, keybinds.jump) || keys[SDL_SCANCODE_UP];
    bool jumpPressed = jumpDown && !player.jumpWasDown;
    bool jumpReleased = !jumpDown && player.jumpWasDown;

    if (insideSolid) {
        // Escape-mode controls while embedded in solid tiles: ignore collision locks.
        player.vx = move * movement.maxSpeedGround;
        player.vy = 0.0f;
        player.x += player.vx * dt;
        if (jumpDown) {
            player.y -= movement.jumpSpeed * dt;
        }
        player.onGround = false;
        player.jumpHeld = false;
        player.jumpHoldTime = 0.0f;
        player.jumpWasDown = jumpDown;
        player.inWater = false;
        return PlayerUpdateResult::Normal;
    }

    if ((player.onGround || inWater) && jumpPressed) {
        player.vy = -jumpSpeed;
        if (player.onGround) player.onGround = false;
        player.jumpHeld = false;
        player.jumpHoldTime = 0.0f;
    }

    player.jumpHeld = false;

    if (jumpReleased && player.vy < -jumpCutSpeed) {
        player.vy = -jumpCutSpeed;
    }
    if (!inWater && !jumpDown && player.vy < 0.0f) {
        // Early release should create a noticeably shorter hop.
        gravity *= 1.35f;
    }

    if (inWater) {
        if (jumpDown) {
            // Preserve stronger jump impulses so the player can break out of water.
            // Swim rise only assists up to the normal swim-up cap.
            if (player.vy > -swimUpSpeed) {
                player.vy -= swimRise * dt;
                if (player.vy < -swimUpSpeed) player.vy = -swimUpSpeed;
            }
        }
        if (player.vy > 420.0f) player.vy = 420.0f;
    }

    player.vy += gravity * dt;
    player.jumpWasDown = jumpDown;

    if (inWater) {
        player.drownTimer += dt;
        if (player.drownTimer >= 45.0f) {
            player.drownTimer = 0.0f;
            return PlayerUpdateResult::Reloaded;
        }
    } else {
        player.drownTimer = 0.0f;
    }

    float newX = player.x + player.vx * dt;
    if (!RectHitsSolid(map, newX, player.y, player.w, player.h)) {
        player.x = newX;
    } else {
        int dir = (player.vx > 0) ? 1 : -1;
        while (!RectHitsSolid(map, player.x + dir, player.y, player.w, player.h)) {
            player.x += dir;
        }
        player.vx = 0.0f;
    }

    float newY = player.y + player.vy * dt;
    if (!RectHitsSolid(map, player.x, newY, player.w, player.h)) {
        if (player.vy > 0 && rectHitsSemiSolidDown(map, player.y, newY, player.x, player.w, player.h)) {
            int t = map.tileSize;
            int bottomTile = (int)std::floor((newY + player.h - 1) / t);
            player.y = bottomTile * t - player.h + 1;
            player.onGround = true;
            player.vy = 0.0f;
        } else {
            player.y = newY;
            player.onGround = false;
        }
    } else {
        int dir = (player.vy > 0) ? 1 : -1;
        while (!RectHitsSolid(map, player.x, player.y + dir, player.w, player.h)) {
            player.y += dir;
        }
        if (dir > 0) player.onGround = true;
        player.vy = 0.0f;
    }

    const float resetY = (float)((map.h + 7) * map.tileSize);
    if (player.vy >= 0.0f) {
        // High-FPS stability: explicit probe below feet prevents grounded flicker
        // when vy ~= 0 and no actual Y movement occurs this frame.
        player.onGround = rectHasGroundBelow(map, player.x, player.y, player.w, player.h);
    }
    if (player.y > resetY) {
        return PlayerUpdateResult::Reloaded;
    }

    return PlayerUpdateResult::Normal;
}

