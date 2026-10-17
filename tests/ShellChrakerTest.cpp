#include "ShellChrakerRenderer.h"
#include <iostream>
#include <stdexcept>

// These atlas frames are unrotated, as are ShellChraker's production frames.
void renderFrame(SDL_Renderer* ren, SDL_Texture* texture, const Frame& frame, const SDL_Rect& dst) {
    SDL_RenderTexture(ren, texture, &frame.rect, &dst);
}
void renderFrameEx(SDL_Renderer* ren, SDL_Texture* texture, const Frame& frame, const SDL_Rect& dst, SDL_RendererFlip flip) {
    SDL_RenderTextureRotated(ren, texture, &frame.rect, &dst, 0, nullptr, flip);
}
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static SDL_Color pixel(SDL_Surface* image, int x, int y) {
    SDL_Color color{};
    require(SDL_ReadSurfacePixel(image, x, y, &color.r, &color.g, &color.b, &color.a), "Read pixel");
    return color;
}
int main() {
    try {
        ShellChraker::State state;
        require(ShellChraker::geometry(state, 128, 64, 32).exposedArm.w == 0, "Idle arm must be harmless");
        ShellChraker::update(state, 0.01f, 80.1f, 0, 32);
        require(!state.cycling, "Player beyond 2.5 blocks must not trigger an attack");
        ShellChraker::update(state, 0.01f, 60, 60, 32);
        require(!state.cycling, "Attack range must account for vertical distance");
        ShellChraker::update(state, ShellChraker::kExtendTime, -80, 0, 32);
        require(state.cycling && state.facing == -1, "Player at 2.5 blocks must trigger an attack toward them");
        auto geometry = ShellChraker::geometry(state, 128, 64, 32);
        require(std::fabs(ShellChraker::extension(state, 32) - 80) < 0.01f, "Arm must fully extend");
        require(geometry.exposedArm.x + geometry.exposedArm.w <= geometry.body.x,
            "Left attack hitbox must exclude the shell and hidden half");
        ShellChraker::update(state, ShellChraker::kRetractTime * 0.5f, 70, 0, 32);
        require(state.facing == -1, "An attack must not reverse if the player crosses behind it");
        require(ShellChraker::extension(state, 32) > 0 && ShellChraker::extension(state, 32) < 80,
            "Arm must retract after extending");
        require(ShellChraker::geometry(state, 128, 64, 32).exposedArm.w > 0,
            "The exposed arm can still harm the player while retracting");
        ShellChraker::update(state, ShellChraker::kRetractTime * 0.5f + 0.01f, 70, 0, 32);
        require(state.cycling && ShellChraker::extension(state, 32) == 0,
            "Arm must remain hidden during its recovery interval");
        ShellChraker::update(state, ShellChraker::kRecoveryTime + 0.01f, 70, 0, 32);
        require(!state.cycling, "Attack must finish its recovery");
        ShellChraker::update(state, 0.05f, 70, 0, 32);
        require(state.cycling && state.facing == 1, "Next attack can turn toward the player");
        const float frozen = state.time;
        ShellChraker::update(state, 0, 70, 0, 32);
        require(state.time == frozen, "Paused updates must not advance the arm");

        require(SDL_Init(0), "SDL init");
        SDL_Surface* output = SDL_CreateSurface(256, 128, SDL_PIXELFORMAT_RGBA8888);
        require(output != nullptr, "Create output");
        SDL_Renderer* ren = SDL_CreateSoftwareRenderer(output);
        require(ren != nullptr, "Create software renderer");
        SDL_Surface* atlas = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA8888);
        require(atlas != nullptr, "Create atlas");
        SDL_Rect left{0, 0, 4, 8}, right{4, 0, 4, 8};
        SDL_FillSurfaceRect(atlas, &left, SDL_MapSurfaceRGBA(atlas, 0, 0, 255, 255));
        SDL_FillSurfaceRect(atlas, &right, SDL_MapSurfaceRGBA(atlas, 255, 0, 0, 255));
        SDL_Texture* texture = SDL_CreateTextureFromSurface(ren, atlas);
        require(texture != nullptr, "Create atlas texture");
        const Frame bodyFrame{{0, 0, 4, 8}, false}, armFrame{{4, 0, 4, 8}, false};
        for (int facing : {-1, 1}) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            state = {ShellChraker::kExtendTime * 0.5f, facing, true};
            geometry = ShellChraker::geometry(state, 128, 64, 32);
            if (facing > 0) require(geometry.exposedArm.x >= geometry.body.x + geometry.body.w,
                "Right attack hitbox must exclude the shell and hidden half");
            RenderShellChraker(ren, texture, &bodyFrame, &armFrame, geometry);
            require(!SDL_RenderClipEnabled(ren), "Enemy rendering must restore disabled clipping");
            SDL_Surface* image = SDL_RenderReadPixels(ren, nullptr);
            require(image != nullptr, "Read enemy render");
            const auto front = pixel(image, facing < 0 ? 96 : 160, 65);
            const auto rear = pixel(image, facing < 0 ? 160 : 96, 65);
            require(front.r == 255 && front.b == 0, "Arm must render in front of the attacking half");
            require(rear.r == 0 && rear.b == 0, "Arm must be culled on the opposite half");
            require(pixel(image, 128, 65).b == 255, "Body must cover the internal arm");
            SDL_DestroySurface(image);
        }
        const SDL_Rect clip{110, 40, 40, 50};
        SDL_SetRenderClipRect(ren, &clip);
        RenderShellChraker(ren, texture, &bodyFrame, &armFrame, geometry);
        SDL_Rect restored;
        SDL_GetRenderClipRect(ren, &restored);
        require(SDL_RenderClipEnabled(ren) && restored.x == clip.x && restored.y == clip.y &&
            restored.w == clip.w && restored.h == clip.h, "Enemy rendering must restore existing clipping");
        SDL_DestroyTexture(texture);
        SDL_DestroySurface(atlas);
        SDL_DestroyRenderer(ren);
        SDL_DestroySurface(output);
        SDL_Quit();
        std::cout << "ShellChraker tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << ": " << SDL_GetError() << '\n';
        return 1;
    }
}
