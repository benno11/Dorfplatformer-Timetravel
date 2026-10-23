#pragma once

#include <SDL3/SDL.h>
#include <algorithm>

// Average every supersampled pixel, weighting color by alpha so transparent
// borders do not darken the antialiased edges. The input uses RGBA byte order.
inline SDL_Surface* DownsampleTextSurface(SDL_Surface* source, int factor) {
    if (!source || source->format != SDL_PIXELFORMAT_RGBA32 || factor < 1) return nullptr;
    SDL_Surface* result = SDL_CreateSurface((source->w - 1) / factor + 1,
        (source->h - 1) / factor + 1, SDL_PIXELFORMAT_RGBA32);
    if (!result) return nullptr;
    if (!SDL_LockSurface(source)) {
        SDL_DestroySurface(result);
        return nullptr;
    }
    if (!SDL_LockSurface(result)) {
        SDL_UnlockSurface(source);
        SDL_DestroySurface(result);
        return nullptr;
    }
    for (int y = 0; y < result->h; ++y) for (int x = 0; x < result->w; ++x) {
        Uint64 red = 0, green = 0, blue = 0, alpha = 0;
        const int right = std::min(source->w, (x + 1) * factor);
        const int bottom = std::min(source->h, (y + 1) * factor);
        for (int sy = y * factor; sy < bottom; ++sy) {
            const auto* row = static_cast<const Uint8*>(source->pixels) + sy * source->pitch;
            for (int sx = x * factor; sx < right; ++sx) {
                const auto* pixel = row + sx * 4;
                red += (Uint64)pixel[0] * pixel[3];
                green += (Uint64)pixel[1] * pixel[3];
                blue += (Uint64)pixel[2] * pixel[3];
                alpha += pixel[3];
            }
        }
        const Uint64 count = (Uint64)(right - x * factor) * (bottom - y * factor);
        auto* pixel = static_cast<Uint8*>(result->pixels) + y * result->pitch + x * 4;
        pixel[0] = alpha ? (Uint8)((red + alpha / 2) / alpha) : 0;
        pixel[1] = alpha ? (Uint8)((green + alpha / 2) / alpha) : 0;
        pixel[2] = alpha ? (Uint8)((blue + alpha / 2) / alpha) : 0;
        pixel[3] = (Uint8)((alpha + count / 2) / count);
    }
    SDL_UnlockSurface(result);
    SDL_UnlockSurface(source);
    return result;
}
