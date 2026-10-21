#pragma once
#include "CharacterAppearance.h"
#include <SDL3/SDL.h>

namespace CharacterCreator {
inline SDL_Color rgbaPixel(const std::string& data, int pixel) {
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    auto channel = [&](int c) -> Uint8 {
        const int offset = pixel * 8 + c * 2;
        return (Uint8)(digit(data[offset]) * 16 + digit(data[offset + 1]));
    };
    return {channel(0), channel(1), channel(2), channel(3)};
}
inline SDL_Color texturePixel(const Appearance& a, Part part, Color base, int x, int y) {
    const int index = (int)part;
    if (index < 0 || index >= 6) return {base.r, base.g, base.b, 255};
    x = (x % 8 + 8) % 8;
    y = (y % 8 + 8) % 8;
    const int texture = a.textures[index];
    if (texture == 6 && a.texturePixels[index].size() == 512) {
        return rgbaPixel(a.texturePixels[index], y * 8 + x);
    }
    float shade = 1.0f;
    if (texture == 1) shade = y % 3 == 0 ? 0.65f : 1.1f;
    else if (texture == 2) shade = ((x / 2 + y / 2) % 2) ? 0.7f : 1.15f;
    else if (texture == 3) shade = x % 3 == 1 && y % 3 == 1 ? 1.5f : 0.85f;
    else if (texture == 4) shade = (x + y * 2) % 4 == 0 ? 1.3f : 0.8f;
    else if (texture == 5) shade = y % 4 == 0 ? 1.5f : (y % 4 == 1 ? 1.1f : 0.65f);
    return {(Uint8)std::min(255.0f, base.r * shade), (Uint8)std::min(255.0f, base.g * shade),
        (Uint8)std::min(255.0f, base.b * shade), 255};
}

// Store a self-contained 8x8 tile, including alpha, instead of a fragile file path.
inline std::string pixelsFromSurface(SDL_Surface* surface, int width, int height) {
    if (!surface || surface->w <= 0 || surface->h <= 0) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string pixels;
    pixels.reserve((size_t)width * height * 8);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        Uint8 r, g, b, alpha;
        if (!SDL_ReadSurfacePixel(surface, x * surface->w / width, y * surface->h / height, &r, &g, &b, &alpha)) return {};
        for (Uint8 value : {r, g, b, alpha}) { pixels += hex[value >> 4]; pixels += hex[value & 15]; }
    }
    return pixels;
}
inline std::string textureFromSurface(SDL_Surface* surface) { return pixelsFromSurface(surface, 8, 8); }
} // namespace CharacterCreator
