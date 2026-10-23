#include "TextSmoothing.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main() {
    assert(SDL_Init(0));
    SDL_Surface* source = SDL_CreateSurface(10, 5, SDL_PIXELFORMAT_RGBA32);
    assert(source);
    assert(SDL_FillSurfaceRect(source, nullptr, SDL_MapSurfaceRGBA(source, 0, 0, 0, 0)));
    // A thin stroke outside the bilinear sample locations must still contribute.
    const SDL_Rect stroke{0, 0, 1, 5};
    assert(SDL_FillSurfaceRect(source, &stroke, SDL_MapSurfaceRGBA(source, 255, 100, 50, 255)));
    SDL_Surface* result = DownsampleTextSurface(source, 5);
    assert(result && result->w == 2 && result->h == 1);
    Uint8 r, g, b, a;
    assert(SDL_ReadSurfacePixel(result, 0, 0, &r, &g, &b, &a));
    assert(r == 255 && g == 100 && b == 50 && a == 51);
    assert(SDL_ReadSurfacePixel(result, 1, 0, &r, &g, &b, &a));
    assert(r == 0 && g == 0 && b == 0 && a == 0);
    SDL_DestroySurface(result);
    SDL_DestroySurface(source);

    // Partial edge blocks retain coverage without an artificial transparent rim.
    source = SDL_CreateSurface(7, 6, SDL_PIXELFORMAT_RGBA32);
    assert(source);
    assert(SDL_FillSurfaceRect(source, nullptr, SDL_MapSurfaceRGBA(source, 30, 60, 90, 128)));
    result = DownsampleTextSurface(source, 5);
    assert(result && result->w == 2 && result->h == 2);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        assert(SDL_ReadSurfacePixel(result, x, y, &r, &g, &b, &a));
        assert(r == 30 && g == 60 && b == 90 && a == 128);
    }
    assert(!DownsampleTextSurface(source, 0));
    assert(!DownsampleTextSurface(nullptr, 5));
    SDL_DestroySurface(result);
    SDL_DestroySurface(source);
    SDL_Quit();
}
