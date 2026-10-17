#include "WaterEffects.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static SDL_Color pixel(SDL_Surface* image, int x, int y) {
    SDL_Color color{};
    require(SDL_ReadSurfacePixel(image, x, y, &color.r, &color.g, &color.b, &color.a), "Read pixel");
    return color;
}

int main(int argc, char** argv) {
    try {
        TileMap collisionMap;
        collisionMap.resize(8, 8);
        collisionMap.setSolid(3, 3, 1);
        float dropX = 80, dropY = 110;
        require(!WaterVisuals::moveSplash(collisionMap, dropX, dropY, 100, 0, 3, false, false),
            "Fast splash droplets must collide with walls instead of tunneling");
        dropX = 110; dropY = 80;
        require(!WaterVisuals::moveSplash(collisionMap, dropX, dropY, 0, 100, 3, false, false),
            "Splash droplets must collide with floors");
        dropY = 150;
        require(!WaterVisuals::moveSplash(collisionMap, dropX, dropY, 0, -100, 3, false, false),
            "Splash droplets must collide with ceilings");
        dropX = 94.5f; dropY = 110;
        require(!WaterVisuals::moveSplash(collisionMap, dropX, dropY, 0, 0, 4, false, false),
            "Splash collision must include the droplet's visible size");
        dropX = 80; dropY = 80;
        require(WaterVisuals::moveSplash(collisionMap, dropX, dropY, 80, 0, 3, false, false) && dropX == 160,
            "Unobstructed splash droplets must keep moving");
        collisionMap.setSolid(0, 1, 1);
        dropX = 250; dropY = 48;
        require(!WaterVisuals::moveSplash(collisionMap, dropX, dropY, 30, 0, 3, true, false),
            "Splash collision must wrap horizontally");
        collisionMap.setSolid(1, 0, 1);
        dropX = 48; dropY = 250;
        require(!WaterVisuals::moveSplash(collisionMap, dropX, dropY, 0, 30, 3, false, true),
            "Splash collision must wrap vertically");
        dropX = 160; dropY = 70;
        require(!WaterVisuals::moveSplash(collisionMap, dropX, dropY, 0, 100, 3, false, false,
            {{144, 96, 32, 32}}), "Splash droplets must collide with moving platforms");
        collisionMap.setWater(5, 2, 1);
        dropX = 176; dropY = 80;
        require(WaterVisuals::moveSplash(collisionMap, dropX, dropY, 0, 8, 3, false, false),
            "Water itself must not act as a solid obstacle");
        TileMap map;
        map.resize(6, 4);
        for (int y = 2; y < 4; ++y)
            for (int x = 1; x < 5; ++x) map.setWater(x, y, 1);
        const auto regions = WaterVisuals::visibleRegions(map, 0, 0, 192, 128, false, false);
        require(regions.size() == 2, "Adjoining water tiles must merge into rows");
        require(regions[0].surface && !regions[1].surface, "Only exposed water has surface waves");
        require(regions[0].rect.x == 32 && regions[0].rect.w == 128, "Merged water bounds");
        float surface = 0;
        require(WaterVisuals::nearbySurface(map, 64, 48, 80, false, false, surface) && surface == 64,
            "Splash must originate at water surface");
        require(!WaterVisuals::nearbySurface(map, 0, 48, 80, false, false, surface), "No splash over dry land");
        require(!WaterVisuals::wet(map, 7, 2, false, false) && WaterVisuals::wet(map, 7, 2, true, false),
            "Horizontal water wrapping");
        require(WaterVisuals::wet(map, 1, -1, false, true), "Vertical water wrapping");
        require(WaterVisuals::visibleRegions(map, 400, 400, 192, 128, false, false).empty(),
            "Offscreen water must not render");
        const auto wrapped = WaterVisuals::visibleRegions(map, 192, 128, 192, 128, true, true);
        require(wrapped.size() == 2 && wrapped[0].rect.x == 32 && wrapped[0].rect.y == 64,
            "Water regions must repeat across both wrapped map boundaries");
        map.setSolid(2, 1, 1);
        const auto covered = WaterVisuals::visibleRegions(map, 0, 0, 192, 128, false, false);
        require(covered.size() == 4 && covered[0].surface && !covered[1].surface && covered[2].surface,
            "A solid roof must interrupt the water surface without removing underwater regions");
        require(covered[1].rect.x == 64 && covered[1].rect.w == 32, "Only covered water loses its surface");
        require(!WaterVisuals::nearbySurface(map, 80, 48, 80, false, false, surface),
            "Covered water must not spawn surface splashes");
        require(!WaterVisuals::hasSurface(map, 8, 6, true, true), "Solid roof detection must wrap with water");
        map.setSolid(2, 1, 0);
        TileMap edge;
        edge.resize(2, 2);
        edge.setWater(0, 0, 1);
        edge.setSolid(0, 1, 1);
        require(WaterVisuals::hasSurface(edge, 0, 0, false, false), "Unwrapped top edge is exposed");
        require(!WaterVisuals::hasSurface(edge, 0, 0, false, true), "Wrapped roof must hide a top-edge surface");

        require(SDL_Init(0), "SDL init");
        SDL_Surface* output = SDL_CreateSurface(384, 256, SDL_PIXELFORMAT_RGBA8888);
        require(output != nullptr, "Create output surface");
        SDL_Renderer* ren = SDL_CreateSoftwareRenderer(output);
        require(ren != nullptr, "Create software renderer");
        SDL_Texture* scene = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 384, 256);
        require(scene != nullptr, "Create scene target");
        auto drawScene = [&]() {
            require(SDL_SetRenderTarget(ren, scene), "Set scene target");
            SDL_SetRenderScale(ren, 2, 2);
            SDL_SetRenderViewport(ren, nullptr);
            SDL_SetRenderClipRect(ren, nullptr);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren, 15, 20, 30, 255);
            SDL_RenderClear(ren);
            SDL_SetRenderDrawColor(ren, 80, 90, 100, 255);
            for (int x = 0; x < 192; x += 8) {
                const SDL_FRect stripe{(float)x, 0, 4, 128};
                SDL_RenderFillRect(ren, &stripe);
            }
            SDL_SetRenderDrawColor(ren, 210, 30, 30, 255);
            const SDL_FRect player{60, 48, 16, 48};
            SDL_RenderFillRect(ren, &player);
        };
        {
            WaterEffects effects;
            drawScene();
            SDL_Surface* before = SDL_RenderReadPixels(ren, nullptr);
            require(before != nullptr, "Read original scene");
            SDL_SetRenderDrawColor(ren, 11, 22, 33, 44);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            effects.render(ren, scene, map, 0, 0, 192, 128, false, false, 0.0f);
            require(SDL_GetRenderTarget(ren) == scene, "Water must restore the scene target");
            float sx = 0, sy = 0;
            SDL_GetRenderScale(ren, &sx, &sy);
            require(sx == 2 && sy == 2, "Water must restore gameplay scale");
            SDL_BlendMode blend;
            SDL_GetRenderDrawBlendMode(ren, &blend);
            require(blend == SDL_BLENDMODE_BLEND, "Water must restore draw blending");
            Uint8 r, g, b, a;
            SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
            require(r == 11 && g == 22 && b == 33 && a == 44, "Water must restore draw color");
            SDL_Surface* first = SDL_RenderReadPixels(ren, nullptr);
            require(first != nullptr, "Read water scene");
            int changed = 0;
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 384; ++x) {
                const auto old = pixel(before, x, y), now = pixel(first, x, y);
                const bool same = old.r == now.r && old.g == now.g && old.b == now.b;
                if (y < 128 || x < 64 || x >= 320) {
                    if (!same) std::cerr << "Unexpected pixel " << x << "," << y << " before " << (int)old.r << "," << (int)old.g << "," << (int)old.b << " after " << (int)now.r << "," << (int)now.g << "," << (int)now.b << "\n";
                    require(same, "Water effects must stay inside water");
                }
                else if (!same) ++changed;
            }
            require(changed > 1000, "Water must visibly affect the submerged scene");
            const auto submerged = pixel(first, 136, 160);
            require(submerged.r > submerged.g && submerged.b > 30, "Player must remain visible behind additive water");
            drawScene();
            effects.render(ren, scene, map, 0, 0, 192, 128, false, false, 1.0f);
            SDL_Surface* second = SDL_RenderReadPixels(ren, nullptr);
            require(second != nullptr, "Read animated scene");
            int animated = 0;
            for (int y = 132; y < 250; ++y) for (int x = 68; x < 316; ++x) {
                const auto old = pixel(first, x, y), now = pixel(second, x, y);
                if (old.r != now.r || old.g != now.g || old.b != now.b) ++animated;
            }
            require(animated > 100, "Refraction must animate under the water surface");
            effects.splash(96, 64, 200);
            effects.update(0.15f);
            drawScene();
            effects.render(ren, scene, map, 0, 0, 192, 128, false, false, 1.0f);
            SDL_Surface* splashed = SDL_RenderReadPixels(ren, nullptr);
            require(splashed != nullptr, "Read splash scene");
            int ripplePixels = 0;
            for (int y = 120; y < 140; ++y) for (int x = 150; x < 235; ++x) {
                const auto old = pixel(second, x, y), now = pixel(splashed, x, y);
                if (old.r != now.r || old.g != now.g || old.b != now.b) ++ripplePixels;
            }
            require(ripplePixels > 10, "Splash must create an expanding surface ripple");
            if (argc > 1) require(SDL_SaveBMP(splashed, argv[1]), "Save water preview");
            effects.update(1.0f);
            drawScene();
            effects.render(ren, scene, map, 0, 0, 192, 128, false, false, 1.0f);
            SDL_Surface* expired = SDL_RenderReadPixels(ren, nullptr);
            require(expired != nullptr, "Read expired ripple scene");
            for (int y = 120; y < 140; ++y) for (int x = 150; x < 235; ++x) {
                const auto old = pixel(second, x, y), now = pixel(expired, x, y);
                require(old.r == now.r && old.g == now.g && old.b == now.b, "Surface ripples must expire");
            }
            SDL_DestroySurface(expired);
            drawScene();
            const SDL_Rect clip{40, 40, 80, 64};
            SDL_SetRenderClipRect(ren, &clip);
            effects.render(ren, scene, map, 0, 0, 192, 128, false, false, 1.0f);
            SDL_Rect restoredClip{};
            SDL_GetRenderClipRect(ren, &restoredClip);
            require(SDL_RenderClipEnabled(ren) && restoredClip.x == clip.x && restoredClip.y == clip.y &&
                restoredClip.w == clip.w && restoredClip.h == clip.h, "Water must restore renderer clipping");
            SDL_SetRenderClipRect(ren, nullptr);
            SDL_DestroySurface(splashed);
            SDL_DestroySurface(second);
            SDL_DestroySurface(first);
            SDL_DestroySurface(before);
            // Resizing the scene must recreate the refraction copy safely.
            SDL_SetRenderTarget(ren, nullptr);
            SDL_Texture* resized = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 192, 128);
            require(resized != nullptr, "Create resized scene");
            SDL_SetRenderTarget(ren, resized);
            SDL_SetRenderScale(ren, 1, 1);
            effects.render(ren, resized, map, 0, 0, 192, 128, false, false, 2.0f);
            SDL_SetRenderTarget(ren, nullptr);
            SDL_DestroyTexture(resized);
        }
        SDL_DestroyTexture(scene);
        SDL_DestroyRenderer(ren);
        SDL_DestroySurface(output);
        SDL_Quit();
        std::cout << "Water effects tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << ": " << SDL_GetError() << '\n';
        return 1;
    }
}
