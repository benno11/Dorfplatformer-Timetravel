#pragma once
#include <SDL3/SDL.h>
#include <algorithm>

inline bool DrawSliderTextureF(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect& rect) {
    if (!texture || rect.w <= 0 || rect.h <= 0) return false;

    return SDL_RenderTexture9Grid(renderer, texture, nullptr, 3, 3, 3, 3,
        std::min(1.0f, std::min(rect.w, rect.h) / 6.0f), &rect);
}

inline bool DrawSliderTexture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect& rect) {
    return DrawSliderTextureF(renderer,texture,SDL_FRect{(float)rect.x,(float)rect.y,(float)rect.w,(float)rect.h});
}
