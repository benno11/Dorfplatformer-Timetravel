#pragma once

#include <string>
#include <sdl3/SDL.h>

std::string RunLevelSelect(SDL_Window* win, SDL_Renderer* ren);
std::string RunCampaignLevelSelect(SDL_Window* win, SDL_Renderer* ren);
std::string RunCustomLevelSelect(SDL_Window* win, SDL_Renderer* ren);
bool HasCustomLevels();
