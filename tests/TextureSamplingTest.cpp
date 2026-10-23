#include "TextureSampling.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <initializer_list>

static SDL_ScaleMode mode(SDL_Texture* texture) {
    SDL_ScaleMode result;
    assert(SDL_GetTextureScaleMode(texture, &result));
    return result;
}

int main() {
    assert(SDL_Init(0));
    SDL_Surface* output = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA8888);
    assert(output);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(output);
    assert(renderer);
    SDL_Surface* stripes = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA8888);
    assert(stripes);
    for (int x = 0; x < 4; ++x) {
        const Uint8 color = x % 2 ? 0 : 255;
        const SDL_Rect column{x, 0, 1, 4};
        assert(SDL_FillSurfaceRect(stripes, &column, SDL_MapSurfaceRGBA(stripes, color, color, color, 255)));
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, stripes);
    assert(texture);
    assert(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
    const SDL_FRect unfiltered{2, 0, 1, 1};
    assert(SDL_RenderTexture(renderer, texture, nullptr, &unfiltered));
    {
        const ScopedDownsampleFilter sampling(renderer, texture, 4, 4, 1, 1);
        assert(mode(texture) == SDL_SCALEMODE_LINEAR);
        const SDL_FRect destination{0, 0, 1, 1};
        assert(SDL_RenderTexture(renderer, texture, nullptr, &destination));
    }
    assert(mode(texture) == SDL_SCALEMODE_NEAREST);
    SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
    assert(pixels);
    Uint8 r, g, b, a;
    assert(SDL_ReadSurfacePixel(pixels, 0, 0, &r, &g, &b, &a));
    assert(r > 80 && r < 180 && r == g && r == b); // Detail is averaged instead of discarded.
    assert(SDL_ReadSurfacePixel(pixels, 2, 0, &r, &g, &b, &a));
    assert(r == 0 || r == 255); // The old sampling loses one stripe color entirely.
    SDL_DestroySurface(pixels);

    // World-space size alone is insufficient: gameplay draws at 3x zoom.
    assert(SDL_SetRenderScale(renderer, 3, 3));
    {
        const ScopedDownsampleFilter sampling(renderer, texture, 4, 4, 2, 2);
        assert(mode(texture) == SDL_SCALEMODE_NEAREST);
    }
    assert(SDL_SetRenderScale(renderer, 0.5f, 1));
    {
        const ScopedDownsampleFilter sampling(renderer, texture, 4, 4, 4, 4);
        assert(mode(texture) == SDL_SCALEMODE_LINEAR); // Shrinking either axis needs filtering.
    }
    assert(mode(texture) == SDL_SCALEMODE_NEAREST);
    assert(SDL_SetRenderScale(renderer, 1, 1));
    for (float size : {4.0f, 8.0f}) {
        const ScopedDownsampleFilter sampling(renderer, texture, 4, 4, size, size);
        assert(mode(texture) == SDL_SCALEMODE_NEAREST);
    }
    assert(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR));
    {
        const ScopedDownsampleFilter sampling(renderer, texture, 4, 4, 1, 1);
        assert(mode(texture) == SDL_SCALEMODE_LINEAR);
    }
    assert(mode(texture) == SDL_SCALEMODE_LINEAR);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(stripes);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(output);
    SDL_Quit();
}
