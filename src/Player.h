#pragma once

struct Player {
    static constexpr float kUnderwaterAirSeconds = 45.0f;
    static constexpr float kAirRefillPerSecond = 3.0f;
    float x = 64.0f;
    float y = 64.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    int tilePushDirection = 0; // Direction of momentum remaining from tiles 83/86.
    bool tile12BounceActive = false;
    bool hasShield = false;
    int w = 30;
    int h = 56;
    bool onGround = false;
    bool jumpHeld = false;
    bool jumpWasDown = false;
    float jumpHoldTime = 0.0f;
    float jumpBufferTime = 0.0f;
    bool inWater = false;
    float drownTimer = 0.0f;
    bool freeMove = false;
    int facing = 1; // 1 = right, -1 = left
    int anim = 0;
    float animTime = 0.0f;
};
