#include "ParallaxRenderer.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kParallaxBaseWidthFactor = 0.75f;
constexpr float kParallaxBaseHeightFactor = 0.75f;

const Frame* FindBgFrameIn(const std::unordered_map<std::string, Frame>& frameByName, const char* name) {
    if (!name) return nullptr;
    auto it = frameByName.find(name);
    if (it != frameByName.end()) return &it->second;
    const std::string s(name);
    if (s.size() > 4 && s.substr(s.size() - 4) == ".png") {
        auto itNoExt = frameByName.find(s.substr(0, s.size() - 4));
        if (itNoExt != frameByName.end()) return &itNoExt->second;
    }
    return nullptr;
}
}

void RenderParallaxBackground(
    SDL_Renderer* ren,
    int currentWorldId,
    float camX,
    float camY,
    int mapW,
    int mapH,
    int tileSize,
    int worldViewW,
    int worldViewH,
    const std::array<float, 3>& parallaxLayerScales,
    const ParallaxWorldAssets& world1,
    const ParallaxWorldAssets& world2,
    const ParallaxWorldAssets& world4,
    const ParallaxWorldAssets& world5
) {
    const ParallaxWorldAssets* active = &world1;
    const char* layerBackName = "far.png";
    const char* layerMidName = "middle.png";
    const char* layerMidAltName = nullptr;
    const char* layerFrontName = "front.png";
    const char* layerFrontAltName = nullptr;

    if (currentWorldId == 2 && world2.texture) {
        active = &world2;
    } else if (currentWorldId == 4 && world4.texture) {
        active = &world4;
        layerMidName = "mid.png";
        layerMidAltName = "middle.png";
    } else if (currentWorldId == 5 && world5.texture) {
        active = &world5;
    }

    if (!active->texture || !active->framesByName) return;

    const Frame* parallaxLayer0 = FindBgFrameIn(*active->framesByName, layerBackName);
    const Frame* parallaxLayer1 = FindBgFrameIn(*active->framesByName, layerMidName);
    if (!parallaxLayer1 && layerMidAltName) {
        parallaxLayer1 = FindBgFrameIn(*active->framesByName, layerMidAltName);
    }
    const Frame* parallaxLayer2 = FindBgFrameIn(*active->framesByName, layerFrontName);
    if (!parallaxLayer2 && layerFrontAltName) {
        parallaxLayer2 = FindBgFrameIn(*active->framesByName, layerFrontAltName);
    }
    if ((!parallaxLayer0 || !parallaxLayer1 || !parallaxLayer2) && active->frameList && !active->frameList->empty()) {
        const size_t n = active->frameList->size();
        if (!parallaxLayer0) parallaxLayer0 = &(*active->frameList)[0];
        if (!parallaxLayer1) parallaxLayer1 = &(*active->frameList)[std::min<size_t>(1, n - 1)];
        if (!parallaxLayer2) parallaxLayer2 = &(*active->frameList)[std::min<size_t>(2, n - 1)];
    }

    if (!parallaxLayer0 && !parallaxLayer1 && !parallaxLayer2) return;

    struct Layer {
        const Frame* frame;
        Uint8 alpha;
        float scale;
        float horizontalFactor;
        float verticalFactor;
    };
    Layer layers[3] = {
        {parallaxLayer0, 255, parallaxLayerScales[0], 0.50f, 0.50f},
        {parallaxLayer1, 255, parallaxLayerScales[1], 0.50f, 0.50f},
        {parallaxLayer2, 255, parallaxLayerScales[2], 0.78f, 0.78f}
    };

    SDL_SetTextureBlendMode(active->texture, SDL_BLENDMODE_BLEND);
    for (const auto& layer : layers) {
        const Frame* f = layer.frame;
        if (!f) continue;
        const int srcW = f->rotated ? f->rect.h : f->rect.w;
        const int srcH = f->rotated ? f->rect.w : f->rect.h;
        const float targetW = std::max(1.0f, (float)worldViewW * kParallaxBaseWidthFactor);
        const float targetH = std::max(1.0f, (float)worldViewH * kParallaxBaseHeightFactor);
        const float fitScale = std::min(targetW / (float)srcW, targetH / (float)srcH);
        const float parallaxScale = std::max(0.01f, fitScale * layer.scale);
        const int fw = std::max(1, (int)std::lround((float)srcW * parallaxScale));
        const int fh = std::max(1, (int)std::lround((float)srcH * parallaxScale));
        if (fw <= 0 || fh <= 0) continue;

        const float oxRaw = camX * layer.horizontalFactor;
        const float mapCamSpanY = std::max(1.0f, (float)(mapH * tileSize - worldViewH));
        const float parallaxCamY = mapCamSpanY - camY;
        const float normalizedY = std::clamp(parallaxCamY / mapCamSpanY, 0.0f, 1.0f);
        const float oy = normalizedY * ((float)fh * layer.verticalFactor);

        float ox = std::fmod(oxRaw, (float)fw);
        if (ox < 0.0f) ox += (float)fw;
        const float yF = (float)worldViewH - (float)fh + oy;
        const int y = (int)std::floor(yF);

        SDL_SetTextureAlphaMod(active->texture, layer.alpha);
        for (int x = -1; x <= worldViewW / fw + 1; ++x) {
            SDL_Rect dst{
                (int)(x * fw - ox),
                y,
                fw,
                fh
            };
            renderFrame(ren, active->texture, *f, dst);
        }
    }
    SDL_SetTextureAlphaMod(active->texture, 255);
}
