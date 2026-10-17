#include "Shield.h"
#include "ShieldRenderer.h"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        Player player;
        player.hasShield = true;
        player.vx = 150;
        player.vy = -200;
        require(Shield::absorbDamage(player), "Shield must absorb one hit");
        require(!player.hasShield && player.vx == 150 && player.vy == -200,
            "Absorbing damage must consume the shield without knockback");
        require(!Shield::absorbDamage(player), "Consumed shields cannot absorb another hit");
        require(SDL_Init(0), "SDL init");
        SDL_Surface* output = SDL_CreateSurface(160, 160, SDL_PIXELFORMAT_RGBA8888);
        require(output != nullptr, "Create output");
        SDL_Renderer* ren = SDL_CreateSoftwareRenderer(output);
        require(ren != nullptr, "Create renderer");
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 11, 22, 33, 44);
        RenderShieldField(ren, {65, 52, 30, 56}, 0.0f);
        SDL_BlendMode blend;
        SDL_GetRenderDrawBlendMode(ren, &blend);
        require(blend == SDL_BLENDMODE_BLEND, "Force field must restore blending");
        Uint8 red, green, blue, alpha;
        SDL_GetRenderDrawColor(ren, &red, &green, &blue, &alpha);
        require(red == 11 && green == 22 && blue == 33 && alpha == 44, "Force field must restore draw color");
        SDL_Surface* image = SDL_RenderReadPixels(ren, nullptr);
        require(image != nullptr, "Read rendered field");
        int colored = 0, outsidePlayer = 0;
        for (int y = 0; y < 160; ++y) for (int x = 0; x < 160; ++x) {
            SDL_ReadSurfacePixel(image, x, y, &red, &green, &blue, &alpha);
            if (blue > 0) {
                ++colored;
                if (x < 65 || x >= 95 || y < 52 || y >= 108) ++outsidePlayer;
                require(x >= 55 && x <= 105 && y >= 42 && y <= 118,
                    "Force field must stay around the player");
            }
        }
        require(colored > 200 && outsidePlayer > 100, "Force field must visibly surround the player");
        SDL_DestroySurface(image);
        SDL_DestroyRenderer(ren);
        SDL_DestroySurface(output);
        SDL_Quit();
        std::cout << "Shield tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << ": " << SDL_GetError() << '\n';
        return 1;
    }
}
