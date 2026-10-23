#pragma once

#include <SDL3/SDL.h>
#include <cmath>

// Atlas textures are shared by large and small sprites. Filter only this draw
// when it shrinks the source, accounting for the renderer's gameplay zoom.
class ScopedDownsampleFilter {
public:
    ScopedDownsampleFilter(SDL_Renderer* renderer, SDL_Texture* texture,
        float sourceWidth, float sourceHeight, float drawWidth, float drawHeight)
        : texture_(texture) {
        if (!renderer || !texture || sourceWidth <= 0 || sourceHeight <= 0 || drawWidth <= 0 || drawHeight <= 0) return;
        float scaleX = 1, scaleY = 1;
        if (!SDL_GetRenderScale(renderer, &scaleX, &scaleY)) return;
        if (drawWidth * std::fabs(scaleX) >= sourceWidth && drawHeight * std::fabs(scaleY) >= sourceHeight) return;
        if (!SDL_GetTextureScaleMode(texture, &previous_) || previous_ == SDL_SCALEMODE_LINEAR) return;
        changed_ = SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }

    ~ScopedDownsampleFilter() {
        if (changed_) SDL_SetTextureScaleMode(texture_, previous_);
    }

    ScopedDownsampleFilter(const ScopedDownsampleFilter&) = delete;
    ScopedDownsampleFilter& operator=(const ScopedDownsampleFilter&) = delete;

private:
    SDL_Texture* texture_ = nullptr;
    SDL_ScaleMode previous_ = SDL_SCALEMODE_NEAREST;
    bool changed_ = false;
};
