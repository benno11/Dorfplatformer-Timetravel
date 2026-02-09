#pragma once

#include <sdl3/SDL.h>

#include <functional>
#include <string>

enum class FrontendAction {
    StartGame,
    Quit
};

struct FrontendMenuContext {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* gameTarget = nullptr;
    int baseScreenW = 0;
    int baseScreenH = 0;
    std::string buildUuid;

    bool* running = nullptr;
    bool* fullscreen = nullptr;
    bool* vsyncEnabled = nullptr;
    bool* clampCamX = nullptr;
    bool* defaultShowFpsCounter = nullptr;
    bool* defaultShowDetailedDebugger = nullptr;
    bool* defaultShowHitboxes = nullptr;
    bool* defaultShowPlayerHitbox = nullptr;
    bool* defaultShowDebugView = nullptr;
    bool* menuMusicEnabled = nullptr;
    bool* muteAllAudio = nullptr;
    float* fastTravelChangeDelay = nullptr;
    int* musicVolume = nullptr;
    int* sfxVolume = nullptr;

    std::function<void()> applyAudioVolumes;
};

FrontendAction runFrontendMenu(FrontendMenuContext& ctx);
