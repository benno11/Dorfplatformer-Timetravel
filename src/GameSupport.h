#pragma once

#include <sdl3/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "TileMap.h"
#include "Player.h"

// SDL3 helper overloads for legacy SDL_Rect call sites.
static inline bool SDL_RenderRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
    if (!rect) return SDL_RenderRect(renderer, static_cast<const SDL_FRect*>(nullptr));
    const SDL_FRect r{(float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h};
    return SDL_RenderRect(renderer, &r);
}

static inline bool SDL_RenderFillRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
    if (!rect) return SDL_RenderFillRect(renderer, static_cast<const SDL_FRect*>(nullptr));
    const SDL_FRect r{(float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h};
    return SDL_RenderFillRect(renderer, &r);
}

static inline bool SDL_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect) {
    const SDL_FRect srcf{srcrect ? (float)srcrect->x : 0.0f, srcrect ? (float)srcrect->y : 0.0f, srcrect ? (float)srcrect->w : 0.0f, srcrect ? (float)srcrect->h : 0.0f};
    const SDL_FRect dstf{dstrect ? (float)dstrect->x : 0.0f, dstrect ? (float)dstrect->y : 0.0f, dstrect ? (float)dstrect->w : 0.0f, dstrect ? (float)dstrect->h : 0.0f};
    return SDL_RenderTexture(renderer, texture, srcrect ? &srcf : nullptr, dstrect ? &dstf : nullptr);
}

static inline bool SDL_RenderTextureRotated(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect, double angle, const SDL_Point* center, SDL_FlipMode flip) {
    const SDL_FRect srcf{srcrect ? (float)srcrect->x : 0.0f, srcrect ? (float)srcrect->y : 0.0f, srcrect ? (float)srcrect->w : 0.0f, srcrect ? (float)srcrect->h : 0.0f};
    const SDL_FRect dstf{dstrect ? (float)dstrect->x : 0.0f, dstrect ? (float)dstrect->y : 0.0f, dstrect ? (float)dstrect->w : 0.0f, dstrect ? (float)dstrect->h : 0.0f};
    SDL_FPoint cf;
    SDL_FPoint* cfp = nullptr;
    if (center) {
      cf.x = (float)center->x;
      cf.y = (float)center->y;
      cfp = &cf;
    }
    return SDL_RenderTextureRotated(renderer, texture, srcrect ? &srcf : nullptr, dstrect ? &dstf : nullptr, angle, cfp, flip);
}

struct Frame {
    SDL_Rect rect{0, 0, 0, 0};
    bool rotated = false;
};

struct FrameEntry {
    std::string name;
    Frame frame;
};

bool playerTouchesTileId(const TileMap& map, const Player& player, int idA, int idB, bool wrapX = false, bool wrapY = false);
bool pointInRectF(float x, float y, const SDL_FRect& r);
int parseLevelIdFromLevelPath(const std::string& levelPath);
bool readProcessMemoryKB(long& rssKB, long& vmKB);
std::string makeBuildUuid();

std::unordered_map<std::string, Frame> loadPlistFrames(const std::string& plistPath);
std::vector<FrameEntry> loadPlistFrameList(const std::string& plistPath);
void renderFrame(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst);
void renderFrameEx(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst, SDL_RendererFlip flip);
SDL_Texture* loadTextureWithColorKey(SDL_Renderer* ren, const std::string& path, Uint8 r, Uint8 g, Uint8 b);

SDL_Rect computePresentRect(int winW, int winH, int baseW, int baseH, float uiScale = 1.0f);
bool windowToGamePoint(int wx, int wy, int winW, int winH, int baseW, int baseH, int& gx, int& gy, float uiScale = 1.0f);
