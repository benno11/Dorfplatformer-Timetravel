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

bool RectHitsSolid(const TileMap& map, float x, float y, int w, int h);
void SetHorizontalWrapCollision(bool enabled);

PlayerUpdateResult UpdatePlayerMovement(
    Player& player,
    const TileMap& map,
    float dt,
    float jumpBufferMax,
    const MovementConfig& movement,
    float touchMove,
    bool touchDown,
    bool touchJump,
    float gamepadMove,
    bool gamepadDown,
    bool gamepadJump,
    bool gamepadFreeMove,
    float& inputMove,
    bool& inputDown
);
