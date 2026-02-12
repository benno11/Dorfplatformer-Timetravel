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
    std::string versionString;

    bool* running = nullptr;
    bool* fullscreen = nullptr;
    bool* vsyncEnabled = nullptr;
    bool* clampCamX = nullptr;
    bool* defaultShowFpsCounter = nullptr;
    bool* defaultShowDetailedDebugger = nullptr;
    bool* defaultShowHitboxes = nullptr;
    bool* defaultShowPlayerHitbox = nullptr;
    bool* defaultShowDebugView = nullptr;
    bool* defaultHideUnknownObjectTypes = nullptr;
    bool* powerManagementEnabled = nullptr;
    bool* menuMusicEnabled = nullptr;
    bool* muteAllAudio = nullptr;
    int* musicVolume = nullptr;
    int* sfxVolume = nullptr;
    int* uiScalePercent = nullptr;
    bool* extraSettings = nullptr; // flattened [5 categories * 11 options]
    int extraSettingsCount = 0;
    std::string* selectedLevelPath = nullptr;

    std::function<void()> applyAudioVolumes;
    std::function<void()> applyMenuMusicToggle;
};

FrontendAction runFrontendMenu(FrontendMenuContext& ctx);
