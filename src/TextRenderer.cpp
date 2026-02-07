#include "TextRenderer.h"

#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <unordered_map>

namespace {
std::unordered_map<int, TTF_Font*> gFontCache;
std::string gFontPath;
bool gTtfInited = false;

TTF_Font* getFont(int scale) {
    if (!gTtfInited || gFontPath.empty()) return nullptr;
    int pt = std::max(12, scale * 8);
    auto it = gFontCache.find(pt);
    if (it != gFontCache.end()) return it->second;
    TTF_Font* font = TTF_OpenFont(gFontPath.c_str(), pt);
    if (!font) return nullptr;
    gFontCache[pt] = font;
    return font;
}
}

bool InitTextRenderer(const std::string& fontPath) {
    gFontPath = fontPath;
    gTtfInited = (TTF_Init() == 0);
    if (!gTtfInited) {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
    }
    return gTtfInited;
}

void ShutdownTextRenderer() {
    for (auto& kv : gFontCache) {
        if (kv.second) TTF_CloseFont(kv.second);
    }
    gFontCache.clear();
    if (gTtfInited) TTF_Quit();
    gTtfInited = false;
}

void DrawText(SDL_Renderer* ren, int x, int y, int scale, const std::string& text) {
    if (text.empty()) return;
    TTF_Font* font = getFont(scale);
    if (!font) return;

    Uint8 r = 255, g = 255, b = 255, a = 255;
    SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
    SDL_Color color{r, g, b, a};

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (!tex) {
        SDL_FreeSurface(surf);
        return;
    }
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    SDL_RenderCopy(ren, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

int MeasureTextWidth(int scale, const std::string& text) {
    TTF_Font* font = getFont(scale);
    if (!font) return 0;
    int w = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &w, nullptr) != 0) return 0;
    return w;
}

void DrawNumberCentered(SDL_Renderer* ren, int x, int y, int scale, int value) {
    std::string s = std::to_string(value);
    int textW = MeasureTextWidth(scale, s);
    DrawText(ren, x - textW / 2, y, scale, s);
}

void DrawDebugNumber(SDL_Renderer* ren, int x, int y, int scale, const std::string& label, int value) {
    DrawText(ren, x, y, scale, label);
    int labelW = MeasureTextWidth(scale, label);
    DrawText(ren, x + labelW + 8, y, scale, std::to_string(value));
}
