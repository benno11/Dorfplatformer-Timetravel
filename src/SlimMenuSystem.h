#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <string>

struct SlimMenuContext {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* gameTarget = nullptr;
    int baseScreenW = 0;
    int baseScreenH = 0;
    std::string buildUuid;
    std::string buildTimestamp;
    std::string buildTimezone;
    std::string versionString;
    std::string versionIdString;

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
    bool* lowPowerModeEnabled = nullptr;
    bool* showExperimentalFeatures = nullptr;
    bool* menuMusicEnabled = nullptr;
    bool* muteAllAudio = nullptr;
    bool* levelSelectEnabled = nullptr;
    bool* nativeTextResolutionEnabled = nullptr;
    int* musicVolume = nullptr;
    int* sfxVolume = nullptr;
    int* uiScalePercent = nullptr;
    int* uiEdgePadding = nullptr;
    int* activeSaveSlotIndex = nullptr;
    bool* extraSettings = nullptr;
    int extraSettingsCount = 0;
    SDL_Scancode* keyMoveLeft = nullptr;
    SDL_Scancode* keyMoveRight = nullptr;
    SDL_Scancode* keyMoveDown = nullptr;
    SDL_Scancode* keyJump = nullptr;
    SDL_Scancode* keyPause = nullptr;
    std::string* levelServerUrl = nullptr;
    std::string* accountManagerUrl = nullptr;
    std::string* levelServerAuthToken = nullptr;
    std::string* levelServerAccountUsername = nullptr;
    std::string* selectedLevelPath = nullptr;
    bool* debuggerAttached = nullptr;

    std::function<void()> applyAudioVolumes;
    std::function<void()> applyMenuMusicToggle;
    std::function<bool(bool)> applyFullscreen;
    std::function<void()> applyRenderVsync;
    std::function<void()> updateDynamicResolution;
    std::function<void()> saveClientSettings;
    std::function<std::string()> getUpdaterStatusText;
};

enum class SlimMenuExit {
    Continue,
    Back,
    StartGame,
    Quit
};

SlimMenuExit RunSlimMenu(SlimMenuContext& ctx, const std::string& menuName);
