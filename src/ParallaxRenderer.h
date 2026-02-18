#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "GameSupport.h"

struct ParallaxWorldAssets {
    SDL_Texture* texture = nullptr;
    const std::unordered_map<std::string, Frame>* framesByName = nullptr;
    const std::vector<Frame>* frameList = nullptr;
};

void RenderParallaxBackground(
    SDL_Renderer* ren,
    int currentWorldId,
    float camX,
    float camY,
    int mapW,
    int mapH,
    int tileSize,
    int worldViewW,
    int worldViewH,
    const std::array<float, 3>& parallaxLayerScales,
    const ParallaxWorldAssets& world1,
    const ParallaxWorldAssets& world2,
    const ParallaxWorldAssets& world4,
    const ParallaxWorldAssets& world5
);


