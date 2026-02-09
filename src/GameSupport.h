#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "TileMap.h"
#include "Player.h"

struct Frame {
    SDL_Rect rect{0, 0, 0, 0};
    bool rotated = false;
};

struct FrameEntry {
    std::string name;
    Frame frame;
};

bool playerTouchesTileId(const TileMap& map, const Player& player, int idA, int idB);
bool pointInRectF(float x, float y, const SDL_FRect& r);
int parseLevelIdFromLevelPath(const std::string& levelPath);
bool readProcessMemoryKB(long& rssKB, long& vmKB);
std::string makeBuildUuid();

std::unordered_map<std::string, Frame> loadPlistFrames(const std::string& plistPath);
std::vector<FrameEntry> loadPlistFrameList(const std::string& plistPath);
void renderFrame(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst);
void renderFrameEx(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst, SDL_RendererFlip flip);
SDL_Texture* loadTextureWithColorKey(SDL_Renderer* ren, const std::string& path, Uint8 r, Uint8 g, Uint8 b);

SDL_Rect computePresentRect(int winW, int winH, int baseW, int baseH);
bool windowToGamePoint(int wx, int wy, int winW, int winH, int baseW, int baseH, int& gx, int& gy);
