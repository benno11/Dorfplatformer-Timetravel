#pragma once

struct Player {
    float x = 64.0f;
    float y = 64.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    int w = 40;
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
