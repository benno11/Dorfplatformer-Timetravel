#pragma once

#include <SDL3/SDL.h>

#include "GameSupport.h"

bool RenderWorld3PatternBackground(
    SDL_Renderer* ren,
    SDL_Texture* blocksTex,
    const Frame* const world3PatternFrames[10],
    int currentLevelId,
    int mapTileSize,
    int worldViewW,
    int worldViewH,
    float camX,
    float camY
);

