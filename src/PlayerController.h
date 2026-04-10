#pragma once

#include "TileMap.h"
#include "Player.h"

enum class PlayerUpdateResult {
    Normal,
    RenderOnly,
    Reloaded
};

struct MovementConfig {
    float accelInWater = 700.0f;
    float accelGround = 1200.0f;
    float maxSpeedInWater = 400.0f;
    float maxSpeedGround = 700.0f;
    float frictionInWater = 200.0f;
    float frictionGround = 400.0f;
    float gravityInWater = 900.0f;
    float gravityGround = 2000.0f;
    float jumpSpeed = 900.0f;
    float jumpHoldGravity = 300.0f;
    float jumpHoldMax = 0.42f;
    float jumpCutSpeed = 220.0f;
    float swimUpSpeed = 380.0f;
    float swimRise = 900.0f;
};

struct KeyboardBindings {
    SDL_Scancode moveLeft = SDL_SCANCODE_A;
    SDL_Scancode moveRight = SDL_SCANCODE_D;
    SDL_Scancode moveDown = SDL_SCANCODE_S;
    SDL_Scancode jump = SDL_SCANCODE_SPACE;
    SDL_Scancode pause = SDL_SCANCODE_ESCAPE;
};

bool RectHitsSolid(const TileMap& map, float x, float y, int w, int h);
void SetHorizontalWrapCollision(bool enabled);
void SetVerticalWrapCollision(bool enabled);

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
);
