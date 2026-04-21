#include "World3PatternBackground.h"

#include <algorithm>
#include <cmath>

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
) {
    if (!ren || !blocksTex || !world3PatternFrames) return false;

    const Frame* fallbackFrame = nullptr;
    for (int i = 0; i < 10; ++i) {
        const Frame* f = world3PatternFrames[i];
        if (f) {
            fallbackFrame = f;
            break;
        }
    }
    if (!fallbackFrame) return false;

    const int blockW = std::max(16, mapTileSize * 2);
    const int blockH = std::max(16, mapTileSize * 2);
    // Keep the pattern anchored to level space so it repeats consistently
    // across the whole World 3 map instead of drifting as a parallax layer.
    const float bgCamX = camX;
    const float bgCamY = camY;
    const int bgOffsetX = (int)std::floor(bgCamX) % blockW;
    const int bgOffsetY = (int)std::floor(bgCamY) % blockH;
    const int worldBaseGX = (int)std::floor(bgCamX / (float)blockW);
    const int worldBaseGY = (int)std::floor(bgCamY / (float)blockH);

    SDL_SetTextureColorMod(blocksTex, 80, 80, 80);
    for (int y = -bgOffsetY - blockH, gy = worldBaseGY - 1; y < worldViewH + blockH; y += blockH, ++gy) {
        for (int x = -bgOffsetX - blockW, gx = worldBaseGX - 1; x < worldViewW + blockW; x += blockW, ++gx) {
            unsigned int hash = (unsigned int)(gx * 73856093u) ^ (unsigned int)(gy * 19349663u) ^ 0x9e3779b9u;
            hash ^= (unsigned int)(currentLevelId * 83492791u);
            const int frameIndex = (int)(hash % 10u);
            const Frame* frame = world3PatternFrames[frameIndex];
            if (!frame) frame = fallbackFrame;

            if (x + blockW <= 0 || y + blockH <= 0 || x >= worldViewW || y >= worldViewH) {
                continue;
            }

            SDL_Rect dst{x, y, blockW, blockH};
            renderFrame(ren, blocksTex, *frame, dst);
        }
    }
    SDL_SetTextureColorMod(blocksTex, 255, 255, 255);
    return true;
}
