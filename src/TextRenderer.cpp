#include "TextRenderer.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>

namespace {
std::unordered_map<int, TTF_Font*> gFontCache;
std::string gFontPath;
bool gTtfInited = false;
std::unordered_map<std::string, int> gDebugLabelWidthCache;
// Render text well above logical UI size, then downsample with linear filtering
// so menu and HUD text stays sharp without the chunky low-res edges.
constexpr int kFontRenderScale = 10;
float gTextScaleMultiplier = 1.0f;
constexpr SDL_ScaleMode kTextTextureScaleMode = SDL_SCALEMODE_LINEAR;
struct TextCacheEntry {
    SDL_Texture* tex = nullptr;
    int w = 0;
    int h = 0;
    Uint64 lastUsedTicks = 0;
};
struct RendererTextCache {
    std::unordered_map<std::string, TextCacheEntry> entries;
    std::deque<std::string> order;
};
std::unordered_map<SDL_Renderer*, RendererTextCache> gTextCacheByRenderer;
constexpr size_t kMaxTextCacheEntries = 1024;

static std::string makeTextCacheKey(int scale, const std::string& text, const SDL_Color& color) {
    return std::to_string(scale) + "|" +
           std::to_string((int)color.r) + "," +
           std::to_string((int)color.g) + "," +
           std::to_string((int)color.b) + "," +
           std::to_string((int)color.a) + "|" + text;
}

TTF_Font* getFont(int scale) {
    if (!gTtfInited || gFontPath.empty()) return nullptr;
    int pt = std::max(12, scale * 8 * kFontRenderScale);
    auto it = gFontCache.find(pt);
    if (it != gFontCache.end()) return it->second;
    TTF_Font* font = TTF_OpenFont(gFontPath.c_str(), pt);
    if (!font) return nullptr;
    TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
    gFontCache[pt] = font;
    return font;
}

int effectiveTextScale(int scale) {
    const float m = std::clamp(gTextScaleMultiplier, 0.5f, 2.0f);
    return std::max(1, (int)std::lround((float)std::max(1, scale) * m));
}
}

bool InitTextRenderer(const std::string& fontPath) {
    gFontPath = fontPath;
    gTtfInited = TTF_Init();
    if (!gTtfInited) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
    }
    return gTtfInited;
}

void ShutdownTextRenderer() {
    for (auto& kv : gFontCache) {
        if (kv.second) TTF_CloseFont(kv.second);
    }
    gFontCache.clear();
    gDebugLabelWidthCache.clear();
    for (auto& byRenderer : gTextCacheByRenderer) {
        for (auto& kv : byRenderer.second.entries) {
            if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
        }
    }
    gTextCacheByRenderer.clear();
    if (gTtfInited) TTF_Quit();
    gTtfInited = false;
}

void ClearTextRendererCache(SDL_Renderer* ren) {
    auto clearRendererCache = [](RendererTextCache& cache) {
        for (auto& kv : cache.entries) {
            if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
        }
        cache.entries.clear();
        cache.order.clear();
    };

    if (ren) {
        auto it = gTextCacheByRenderer.find(ren);
        if (it == gTextCacheByRenderer.end()) return;
        clearRendererCache(it->second);
        if (it->second.entries.empty() && it->second.order.empty()) {
            gTextCacheByRenderer.erase(it);
        }
        return;
    }

    for (auto& byRenderer : gTextCacheByRenderer) {
        clearRendererCache(byRenderer.second);
    }
    gTextCacheByRenderer.clear();
}

void CollectTextRendererGarbage(Uint64 maxIdleMs, size_t targetEntriesPerRenderer) {
    const Uint64 now = SDL_GetTicks();
    for (auto rendererIt = gTextCacheByRenderer.begin(); rendererIt != gTextCacheByRenderer.end();) {
        auto& cache = rendererIt->second;

        for (auto entryIt = cache.entries.begin(); entryIt != cache.entries.end();) {
            const bool stale = (now >= entryIt->second.lastUsedTicks) &&
                               ((now - entryIt->second.lastUsedTicks) > maxIdleMs);
            if (!stale) {
                ++entryIt;
                continue;
            }
            if (entryIt->second.tex) SDL_DestroyTexture(entryIt->second.tex);
            entryIt = cache.entries.erase(entryIt);
        }

        while (cache.order.size() > targetEntriesPerRenderer) {
            const std::string oldKey = cache.order.front();
            cache.order.pop_front();
            auto itOld = cache.entries.find(oldKey);
            if (itOld == cache.entries.end()) continue;
            if (itOld->second.tex) SDL_DestroyTexture(itOld->second.tex);
            cache.entries.erase(itOld);
        }

        for (auto orderIt = cache.order.begin(); orderIt != cache.order.end();) {
            if (cache.entries.find(*orderIt) == cache.entries.end()) {
                orderIt = cache.order.erase(orderIt);
            } else {
                ++orderIt;
            }
        }

        if (cache.entries.empty()) {
            rendererIt = gTextCacheByRenderer.erase(rendererIt);
        } else {
            ++rendererIt;
        }
    }

    if (gDebugLabelWidthCache.size() > 2048) {
        gDebugLabelWidthCache.clear();
    }
}

void SetTextScaleMultiplier(float multiplier) {
    gTextScaleMultiplier = multiplier;
}

float GetTextScaleMultiplier() {
    return gTextScaleMultiplier;
}

void DrawText(SDL_Renderer* ren, int x, int y, int scale, const std::string& text) {
    DrawTextColored(ren, x, y, scale, text, SDL_Color{255, 255, 255, 255});
}

void DrawTextColored(SDL_Renderer* ren, int x, int y, int scale, const std::string& text, const SDL_Color& color) {
    if (text.empty()) return;
    const int scaled = effectiveTextScale(scale);
    TTF_Font* font = getFont(scaled);
    if (!font) return;

    const SDL_Color fillColor = color;
    const SDL_Color outlineColor{0, 0, 0, 255};
    const int finalOutlinePx = std::max(1, scaled / 3);
    const int outlinePx = finalOutlinePx * kFontRenderScale;
    auto& rendererCache = gTextCacheByRenderer[ren];

    const std::string key = makeTextCacheKey(scaled, text, fillColor);
    auto itCached = rendererCache.entries.find(key);
    if (itCached == rendererCache.entries.end()) {
        SDL_Surface* fillSurf = TTF_RenderText_Blended(font, text.c_str(), text.size(), fillColor);
        if (!fillSurf) return;
        SDL_Surface* outlineSurf = TTF_RenderText_Blended(font, text.c_str(), text.size(), outlineColor);
        if (!outlineSurf) {
            SDL_FreeSurface(fillSurf);
            return;
        }

        SDL_Surface* composed = SDL_CreateSurface(fillSurf->w + outlinePx * 2,
                                                  fillSurf->h + outlinePx * 2,
                                                  SDL_PIXELFORMAT_RGBA32);
        if (!composed) {
            SDL_FreeSurface(fillSurf);
            SDL_FreeSurface(outlineSurf);
            return;
        }
        SDL_FillSurfaceRect(composed, nullptr, SDL_MapSurfaceRGBA(composed, 0, 0, 0, 0));

        SDL_Rect od{outlinePx - outlinePx, outlinePx, outlineSurf->w, outlineSurf->h};
        SDL_BlitSurface(outlineSurf, nullptr, composed, &od);
        od = SDL_Rect{outlinePx + outlinePx, outlinePx, outlineSurf->w, outlineSurf->h};
        SDL_BlitSurface(outlineSurf, nullptr, composed, &od);
        od = SDL_Rect{outlinePx, outlinePx - outlinePx, outlineSurf->w, outlineSurf->h};
        SDL_BlitSurface(outlineSurf, nullptr, composed, &od);
        od = SDL_Rect{outlinePx, outlinePx + outlinePx, outlineSurf->w, outlineSurf->h};
        SDL_BlitSurface(outlineSurf, nullptr, composed, &od);
        SDL_Rect fd{outlinePx, outlinePx, fillSurf->w, fillSurf->h};
        SDL_BlitSurface(fillSurf, nullptr, composed, &fd);

        SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, composed);
        SDL_FreeSurface(fillSurf);
        SDL_FreeSurface(outlineSurf);
        SDL_FreeSurface(composed);
        if (!tex) return;
        SDL_SetTextureScaleMode(tex, kTextTextureScaleMode);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

        TextCacheEntry entry;
        entry.tex = tex;
        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(tex, &tw, &th);
        entry.w = (int)tw;
        entry.h = (int)th;
        entry.lastUsedTicks = SDL_GetTicks();
        rendererCache.entries[key] = entry;
        rendererCache.order.push_back(key);
        itCached = rendererCache.entries.find(key);

        while (rendererCache.order.size() > kMaxTextCacheEntries) {
            const std::string& oldKey = rendererCache.order.front();
            auto itOld = rendererCache.entries.find(oldKey);
            if (itOld != rendererCache.entries.end()) {
                if (itOld->second.tex) SDL_DestroyTexture(itOld->second.tex);
                rendererCache.entries.erase(itOld);
            }
            rendererCache.order.pop_front();
        }
    }
    itCached->second.lastUsedTicks = SDL_GetTicks();

    SDL_FRect dst{
        (float)x,
        (float)y,
        (float)std::lround((float)itCached->second.w / (float)kFontRenderScale),
        (float)std::lround((float)itCached->second.h / (float)kFontRenderScale)
    };
    SDL_RenderTexture(ren, itCached->second.tex, nullptr, &dst);
}

int MeasureTextWidth(int scale, const std::string& text) {
    const int scaled = effectiveTextScale(scale);
    TTF_Font* font = getFont(scaled);
    if (!font) return 0;
    int w = 0;
    if (!TTF_GetStringSize(font, text.c_str(), text.size(), &w, nullptr)) return 0;
    const int finalOutlinePx = std::max(1, scaled / 3);
    const int outlinePx = finalOutlinePx * kFontRenderScale;
    return (w + outlinePx * 2) / kFontRenderScale;
}

void DrawNumberCentered(SDL_Renderer* ren, int x, int y, int scale, int value) {
    std::string s = std::to_string(value);
    int textW = MeasureTextWidth(scale, s);
    DrawText(ren, x - textW / 2, y, scale, s);
}

void DrawDebugNumber(SDL_Renderer* ren, int x, int y, int scale, const std::string& label, int value) {
    DrawText(ren, x, y, scale, label);
    std::string cacheKey = std::to_string(scale) + "|" + label;
    int labelW = 0;
    auto it = gDebugLabelWidthCache.find(cacheKey);
    if (it != gDebugLabelWidthCache.end()) {
        labelW = it->second;
    } else {
        labelW = MeasureTextWidth(scale, label);
        gDebugLabelWidthCache[cacheKey] = labelW;
    }
    DrawText(ren, x + labelW + 8, y, scale, std::to_string(value));
}

