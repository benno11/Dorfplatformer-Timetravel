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

    bool* running = nullptr;
    bool* menuMusicEnabled = nullptr;
    bool* muteAllAudio = nullptr;
    bool* levelSelectEnabled = nullptr;
    bool* nativeTextResolutionEnabled = nullptr;
    int* musicVolume = nullptr;
    int* sfxVolume = nullptr;
    int* activeSaveSlotIndex = nullptr;
    std::string* levelServerUrl = nullptr;
    std::string* levelServerAuthToken = nullptr;
    std::string* levelServerAccountUsername = nullptr;
    std::string* selectedLevelPath = nullptr;

    std::function<void()> applyAudioVolumes;
    std::function<void()> applyMenuMusicToggle;
    std::function<void()> saveClientSettings;
};

enum class SlimMenuExit {
    Continue,
    Back,
    StartGame,
    Quit
};

SlimMenuExit RunSlimMenu(SlimMenuContext& ctx, const std::string& menuName);
