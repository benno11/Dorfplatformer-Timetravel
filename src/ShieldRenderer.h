#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

inline void RenderShieldField(SDL_Renderer* ren, const SDL_FRect& player, float time) {
    SDL_BlendMode previousBlend;
    SDL_GetRenderDrawBlendMode(ren, &previousBlend);
    Uint8 red, green, blue, alpha;
    SDL_GetRenderDrawColor(ren, &red, &green, &blue, &alpha);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    const float cx = player.x + player.w * 0.5f, cy = player.y + player.h * 0.5f;
    const float rx = player.w * 0.5f + 7.0f, ry = player.h * 0.5f + 7.0f;
    const float pulse = 0.5f + 0.5f * std::sin(time * 4.0f);
    SDL_SetRenderDrawColor(ren, 35, 120, 255, 22);
    for (float y = -ry; y <= ry; y += 1.0f) {
        const float ratio = y / ry;
        const float halfW = rx * std::sqrt(std::max(0.0f, 1.0f - ratio * ratio));
        SDL_RenderLine(ren, cx - halfW, cy + y, cx + halfW, cy + y);
    }
    for (int ring = 0; ring < 2; ++ring) {
        SDL_SetRenderDrawColor(ren, ring ? 180 : 60, ring ? 230 : 150, 255,
            (Uint8)((ring ? 150.0f : 70.0f) + pulse * 50.0f));
        float px = cx + rx + ring, py = cy;
        for (int step = 1; step <= 48; ++step) {
            const float angle = step * 6.2831853f / 48.0f;
            const float x = cx + std::cos(angle) * (rx + ring);
            const float y = cy + std::sin(angle) * (ry + ring);
            SDL_RenderLine(ren, px, py, x, y);
            px = x; py = y;
        }
    }
    SDL_SetRenderDrawBlendMode(ren, previousBlend);
    SDL_SetRenderDrawColor(ren, red, green, blue, alpha);
}
