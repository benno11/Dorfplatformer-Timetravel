#pragma once

#include <string>
#include <sdl3/SDL.h>

bool InitTextRenderer(const std::string& fontPath);
void ShutdownTextRenderer();

void DrawText(SDL_Renderer* ren, int x, int y, int scale, const std::string& text);
void DrawNumberCentered(SDL_Renderer* ren, int x, int y, int scale, int value);
void DrawDebugNumber(SDL_Renderer* ren, int x, int y, int scale, const std::string& label, int value);
int MeasureTextWidth(int scale, const std::string& text);
