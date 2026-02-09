#include "PlayerController.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

bool RectHitsSolid(const TileMap& map, float x, float y, int w, int h) {
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int top = (int)std::floor(y / t);
    int bottom = (int)std::floor((y + h - 1) / t);
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            if (map.getSolid(tx, ty)) return true;
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
        if (!map.getSemiSolid(tx, ty)) continue;
        float tileTop = ty * t;
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
            if (map.getWater(tx, ty)) return true;
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
        if (map.getSolid(tx, footTile) || map.getSemiSolid(tx, footTile)) return true;
    }
    return false;
}

PlayerUpdateResult UpdatePlayerMovement(
    Player& player,
    const TileMap& map,
    float dt,
    float jumpBufferMax,
    const MovementConfig& movement,
    float touchMove,
    bool touchDown,
    bool touchJump,
    float& inputMove,
    bool& inputDown
) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    bool shiftDown = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    player.freeMove = shiftDown;

    if (player.freeMove) {
        float freeSpeed = 1200.0f;
        float dx = 0.0f;
        float dy = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;
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

    float move = touchMove;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) move -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move += 1.0f;
    move = std::clamp(move, -1.0f, 1.0f);
    bool downHeld = touchDown || keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
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

    if (move != 0.0f) {
        player.vx += move * accel * dt;
        if (player.vx > maxSpeed) player.vx = maxSpeed;
        if (player.vx < -maxSpeed) player.vx = -maxSpeed;
    } else {
        if (player.vx > 0.0f) {
            player.vx = std::max(0.0f, player.vx - friction * dt);
        } else if (player.vx < 0.0f) {
            player.vx = std::min(0.0f, player.vx + friction * dt);
        }
    }

    bool jumpDown = touchJump || keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    bool jumpPressed = jumpDown && !player.jumpWasDown;
    bool jumpReleased = !jumpDown && player.jumpWasDown;
    if (jumpDown == false){
        player.jumpBufferTime = 0.0f;
    }

    if (jumpPressed) {
        player.jumpBufferTime = jumpBufferMax;
    } else {
        player.jumpBufferTime = std::max(0.0f, player.jumpBufferTime - dt);
    }

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

    if ((player.onGround || inWater) && player.jumpBufferTime > 0.0f) {
        player.vy = -jumpSpeed;
        if (player.onGround) player.onGround = false;
        player.jumpHeld = true;
        player.jumpHoldTime = 0.0f;
        player.jumpBufferTime = 0.0f;
    }

    if (!inWater && player.jumpHeld && jumpDown && player.jumpHoldTime < jumpHoldMax) {
        player.vy += jumpHoldGravity * dt;
        player.jumpHoldTime += dt;
    } else {
        player.jumpHeld = false;
    }

    if (jumpReleased && player.vy < -jumpCutSpeed) {
        player.vy = -jumpCutSpeed;
    }

    if (inWater) {
        if (jumpDown) {
            player.vy -= swimRise * dt;
            if (player.vy < -swimUpSpeed) player.vy = -swimUpSpeed;
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
