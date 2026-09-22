#pragma once

#include <string>
#include <SDL3/SDL.h>

struct LevelEntry {
    std::string label;
    std::string path;
    int difficulty = 0;
    int downloads = 0;
    int likes = 0;
    int dislikes = 0;
};

std::string RunLevelSelect(SDL_Window* win, SDL_Renderer* ren);
std::string RunCampaignLevelSelect(SDL_Window* win, SDL_Renderer* ren);
std::string RunCustomLevelSelect(SDL_Window* win, SDL_Renderer* ren);
std::string OpenLocalLevelEditorForMenu(SDL_Window* win, SDL_Renderer* ren, const std::string& initialPath);
bool uploadLocalLevelToServer(const LevelEntry& level, std::string& statusText);
bool HasCustomLevels();

bool IsLocalLevelVerified(const std::string& levelPath);
void SetLocalLevelVerified(const std::string& levelPath, bool verified);
void ClearLocalLevelVerification(const std::string& levelPath);

