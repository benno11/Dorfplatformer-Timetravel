#pragma once

#include <SDL3/SDL.h>

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
    SDL_Texture** gameTargetRef = nullptr;
    int baseScreenW = 0;
    int baseScreenH = 0;
    std::string buildUuid;
    std::string versionString;
    std::string versionIdString;
    std::string windowsUpdateManifestUrl;

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
    bool* debugModeEnabled = nullptr;
    bool* powerManagementEnabled = nullptr;
    bool* lowPowerModeEnabled = nullptr;
    bool* showExperimentalFeatures = nullptr;
    bool* levelSelectEnabled = nullptr;
    bool* menuMusicEnabled = nullptr;
    bool* muteAllAudio = nullptr;
    bool* showOptionalSidebar = nullptr;
    SDL_Scancode* keyMoveLeft = nullptr;
    SDL_Scancode* keyMoveRight = nullptr;
    SDL_Scancode* keyMoveDown = nullptr;
    SDL_Scancode* keyJump = nullptr;
    SDL_Scancode* keyPause = nullptr;
    int* musicVolume = nullptr;
    int* sfxVolume = nullptr;
    int* uiScalePercent = nullptr;
    std::string* levelServerUrl = nullptr;
    std::string* levelServerAuthToken = nullptr;
    std::string* levelServerAccountUsername = nullptr;
    std::string* accountManagerUrl = nullptr;
    std::string* firebaseApiKey = nullptr;
    bool* extraSettings = nullptr; // flattened [5 categories * 11 options]
    int extraSettingsCount = 0;
    std::string* selectedLevelPath = nullptr;
    bool* devToolsEnabled = nullptr;

    std::function<void()> applyAudioVolumes;
    std::function<void()> applyMenuMusicToggle;
    std::function<void()> updateDynamicResolution;
    std::function<void()> saveClientSettings;
    std::function<bool()> launchUpdater;
    std::function<std::string()> getUpdaterStatusText;
    std::function<std::string()> getUpdaterStatusDetail;
    std::function<std::string()> getUpdaterLatestVersionText;
    std::function<std::string()> getUpdaterNotesText;
    std::function<float()> getUpdaterProgress01;
    std::function<void()> pollUpdaterAutoRelaunch;
};

FrontendAction runFrontendMenu(FrontendMenuContext& ctx);

