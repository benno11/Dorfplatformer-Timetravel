#pragma once

#include "TileMap.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace WaterVisuals {
// Return false on impact; callers retire the droplet rather than drawing it
// through the obstacle. Small steps also catch walls during long frames.
inline bool moveSplash(const TileMap& map, float& x, float& y, float dx, float dy,
                       float size, bool wrapX, bool wrapY,
                       const std::vector<SDL_FRect>& platforms = {}) {
    if (map.tileSize <= 0 || map.w <= 0 || map.h <= 0) return false;
    const float radius = std::max(0.5f, size * 0.5f);
    const int steps = std::max(1, (int)std::ceil(std::max(std::fabs(dx), std::fabs(dy))));
    for (int step = 0; step <= steps; ++step) {
        const float px = x + dx * (step / (float)steps);
        const float py = y + dy * (step / (float)steps);
        const int left = (int)std::floor((px - radius) / map.tileSize);
        const int right = (int)std::floor((px + radius) / map.tileSize);
        const int top = (int)std::floor((py - radius) / map.tileSize);
        const int bottom = (int)std::floor((py + radius) / map.tileSize);
        for (int ty = top; ty <= bottom; ++ty)
            for (int tx = left; tx <= right; ++tx) {
                const int qx = wrapX ? (tx % map.w + map.w) % map.w : tx;
                const int qy = wrapY ? (ty % map.h + map.h) % map.h : ty;
                if (map.getSolid(qx, qy)) return false;
            }
        for (auto platform : platforms) {
            const float width = (float)(map.w * map.tileSize);
            const float height = (float)(map.h * map.tileSize);
            if (wrapX) platform.x -= std::floor((platform.x + platform.w * 0.5f - px + width * 0.5f) / width) * width;
            if (wrapY) platform.y -= std::floor((platform.y + platform.h * 0.5f - py + height * 0.5f) / height) * height;
            if (px + radius >= platform.x && px - radius <= platform.x + platform.w &&
                py + radius >= platform.y && py - radius <= platform.y + platform.h) return false;
        }
    }
    x += dx;
    y += dy;
    return true;
}

inline bool wet(const TileMap& map, int x, int y, bool wrapX, bool wrapY) {
    if (map.w <= 0 || map.h <= 0) return false;
    if (wrapX) x = (x % map.w + map.w) % map.w;
    if (wrapY) y = (y % map.h + map.h) % map.h;
    return x >= 0 && x < map.w && y >= 0 && y < map.h && map.water[y * map.w + x] != 0;
}

inline bool hasSurface(const TileMap& map, int x, int y, bool wrapX, bool wrapY) {
    if (!wet(map, x, y, wrapX, wrapY) || wet(map, x, y - 1, wrapX, wrapY)) return false;
    if (wrapX) x = (x % map.w + map.w) % map.w;
    int above = y - 1;
    if (wrapY) above = (above % map.h + map.h) % map.h;
    return above < 0 || above >= map.h || !map.solid[above * map.w + x];
}

struct Region {
    SDL_FRect rect;
    bool surface;
};

// Merge adjoining cells with the same surface state so refraction has no tile seams.
inline std::vector<Region> visibleRegions(const TileMap& map, float camX, float camY,
                                          int width, int height, bool wrapX, bool wrapY) {
    std::vector<Region> regions;
    if (map.tileSize <= 0 || width <= 0 || height <= 0) return regions;
    const int left = (int)std::floor(camX / map.tileSize);
    const int right = (int)std::ceil((camX + width) / map.tileSize);
    const int top = (int)std::floor(camY / map.tileSize);
    const int bottom = (int)std::ceil((camY + height) / map.tileSize);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right;) {
            if (!wet(map, x, y, wrapX, wrapY)) { ++x; continue; }
            const int start = x++;
            const bool surface = hasSurface(map, start, y, wrapX, wrapY);
            while (x < right && wet(map, x, y, wrapX, wrapY) &&
                   hasSurface(map, x, y, wrapX, wrapY) == surface) ++x;
            regions.push_back({{start * (float)map.tileSize - camX, y * (float)map.tileSize - camY,
                (x - start) * (float)map.tileSize, (float)map.tileSize}, surface});
        }
    }
    return regions;
}

inline bool nearbySurface(const TileMap& map, float x, float top, float bottom,
                          bool wrapX, bool wrapY, float& surfaceY) {
    if (map.tileSize <= 0) return false;
    const int tx = (int)std::floor(x / map.tileSize);
    const int first = (int)std::floor(top / map.tileSize) - 1;
    const int last = (int)std::floor(bottom / map.tileSize) + 1;
    for (int y = first; y <= last; ++y) {
        if (hasSurface(map, tx, y, wrapX, wrapY)) {
            surfaceY = y * (float)map.tileSize;
            return true;
        }
    }
    return false;
}

inline bool overlapsWater(const TileMap& map, const SDL_FRect& body, bool wrapX, bool wrapY) {
    if (map.tileSize <= 0 || body.w <= 0 || body.h <= 0) return false;
    const int left = (int)std::floor(body.x / map.tileSize);
    const int right = (int)std::ceil((body.x + body.w) / map.tileSize);
    const int top = (int)std::floor(body.y / map.tileSize);
    const int bottom = (int)std::ceil((body.y + body.h) / map.tileSize);
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (wet(map, x, y, wrapX, wrapY)) return true;
    return false;
}

// Runtime-only history: initial placement and teleports do not make splashes.
struct InteractionState {
    SDL_FRect previous{};
    bool initialized = false;
    bool submerged = false;
    float bubbleTimer = 0.0f;
    float wakeTimer = 0.0f;
};
struct Interaction {
    bool splash = false, bubble = false, wake = false;
    float x = 0, surfaceY = 0, vx = 0, vy = 0;
};
inline Interaction interact(const TileMap& map, const SDL_FRect& body,
                            InteractionState& state, float dt, bool wrapX, bool wrapY) {
    Interaction result;
    const bool submerged = overlapsWater(map, body, wrapX, wrapY);
    if (!state.initialized || dt <= 0.0f) {
        state = {body, true, submerged, 0.12f, 0.0f};
        return result;
    }
    float dx = body.x - state.previous.x, dy = body.y - state.previous.y;
    const float width = (float)(map.w * map.tileSize), height = (float)(map.h * map.tileSize);
    if (wrapX && width > 0) dx -= std::round(dx / width) * width;
    if (wrapY && height > 0) dy -= std::round(dy / height) * height;
    // Suppress discontinuities such as scene changes and debugger repositioning.
    if (std::fabs(dx) > map.tileSize * 4 || std::fabs(dy) > map.tileSize * 4) {
        state = {body, true, submerged, 0.12f, 0.0f};
        return result;
    }
    result.vx = dx / dt;
    result.vy = dy / dt;
    result.x = body.x + body.w * 0.5f;
    const float previousTop = body.y - dy;
    const bool surface = nearbySurface(map, result.x, std::min(body.y, previousTop),
        std::max(body.y + body.h, previousTop + state.previous.h), wrapX, wrapY, result.surfaceY);
    const float previousBottom = previousTop + state.previous.h;
    const float bottom = body.y + body.h;
    result.splash = surface && ((submerged != state.submerged &&
        result.surfaceY >= std::min(body.y, previousTop) && result.surfaceY <= std::max(bottom, previousBottom)) ||
        (previousBottom <= result.surfaceY && bottom > result.surfaceY) ||
        (previousBottom > result.surfaceY && bottom <= result.surfaceY));
    state.bubbleTimer -= dt;
    state.wakeTimer -= dt;
    const float speed = std::fabs(result.vx) + std::fabs(result.vy);
    if (submerged && speed > 12.0f && state.bubbleTimer <= 0.0f) {
        result.bubble = true;
        state.bubbleTimer = 0.24f - std::min(speed / 2000.0f, 0.12f);
    }
    if (surface && body.y <= result.surfaceY && bottom > result.surfaceY &&
        std::fabs(result.vx) > 20.0f && state.wakeTimer <= 0.0f) {
        result.wake = true;
        state.wakeTimer = 0.16f;
    }
    state.previous = body;
    state.submerged = submerged;
    return result;
}
} // namespace WaterVisuals

class WaterEffects {
    SDL_Texture* snapshot_ = nullptr;
    int snapshotW_ = 0, snapshotH_ = 0;
    struct Ripple { float x, y, age, strength; };
    std::vector<Ripple> ripples_;
public:
    WaterEffects() = default;
    WaterEffects(const WaterEffects&) = delete;
    WaterEffects& operator=(const WaterEffects&) = delete;
    ~WaterEffects() { reset(); }
    void reset() {
        if (snapshot_) SDL_DestroyTexture(snapshot_);
        snapshot_ = nullptr;
        snapshotW_ = snapshotH_ = 0;
        ripples_.clear();
    }
    void splash(float x, float y, float speed) {
        if (ripples_.size() >= 32) ripples_.erase(ripples_.begin());
        ripples_.push_back({x, y, 0.0f, std::clamp(speed / 220.0f, 0.4f, 1.5f)});
    }
    void update(float dt) {
        for (auto& ripple : ripples_) ripple.age += dt;
        ripples_.erase(std::remove_if(ripples_.begin(), ripples_.end(),
            [](const Ripple& ripple) { return ripple.age >= 0.8f; }), ripples_.end());
    }
    void render(SDL_Renderer* ren, SDL_Texture* scene, const TileMap& map,
                float camX, float camY, int width, int height, bool wrapX, bool wrapY, float time) {
        const auto regions = WaterVisuals::visibleRegions(map, camX, camY, width, height, wrapX, wrapY);
        if (regions.empty()) return;
        SDL_BlendMode blend;
        SDL_GetRenderDrawBlendMode(ren, &blend);
        Uint8 red, green, blue, alpha;
        SDL_GetRenderDrawColor(ren, &red, &green, &blue, &alpha);
        float scaleX = 1.0f, scaleY = 1.0f, pixelW = 0.0f, pixelH = 0.0f;
        SDL_GetRenderScale(ren, &scaleX, &scaleY);
        SDL_GetTextureSize(scene, &pixelW, &pixelH);
        if (snapshotW_ != (int)pixelW || snapshotH_ != (int)pixelH) {
            if (snapshot_) SDL_DestroyTexture(snapshot_);
            snapshot_ = nullptr;
        }
        if (!snapshot_) {
            snapshot_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, (int)pixelW, (int)pixelH);
            snapshotW_ = (int)pixelW;
            snapshotH_ = (int)pixelH;
            if (snapshot_) {
                SDL_SetTextureBlendMode(snapshot_, SDL_BLENDMODE_NONE);
                SDL_SetTextureScaleMode(snapshot_, SDL_SCALEMODE_LINEAR);
            }
        }
        bool copied = false;
        SDL_Rect viewport, clip;
        SDL_GetRenderViewport(ren, &viewport);
        SDL_GetRenderClipRect(ren, &clip);
        const bool clipped = SDL_RenderClipEnabled(ren);
        if (snapshot_ && SDL_SetRenderTarget(ren, snapshot_)) {
            SDL_SetRenderScale(ren, 1.0f, 1.0f);
            SDL_SetRenderViewport(ren, nullptr);
            SDL_SetRenderClipRect(ren, nullptr);
            SDL_BlendMode sceneBlend;
            SDL_GetTextureBlendMode(scene, &sceneBlend);
            SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
            copied = SDL_RenderTexture(ren, scene, nullptr, nullptr);
            SDL_SetTextureBlendMode(scene, sceneBlend);
            SDL_SetRenderTarget(ren, scene);
            SDL_SetRenderScale(ren, scaleX, scaleY);
            SDL_SetRenderViewport(ren, &viewport);
            SDL_SetRenderClipRect(ren, clipped ? &clip : nullptr);
        }
        const float viewW = std::min((float)width, pixelW / scaleX);
        const float viewH = std::min((float)height, pixelH / scaleY);
        for (const auto& region : regions) {
            const auto& r = region.rect;
            const float left = std::max(0.0f, r.x), right = std::min(viewW, r.x + r.w);
            const float top = std::max(0.0f, r.y), bottom = std::min(viewH, r.y + r.h);
            if (right <= left || bottom <= top) continue;
            SDL_Rect waterClip{(int)std::ceil(left), (int)std::ceil(top),
                (int)std::floor(right) - (int)std::ceil(left), (int)std::floor(bottom) - (int)std::ceil(top)};
            if (clipped) SDL_GetRectIntersection(&waterClip, &clip, &waterClip);
            SDL_SetRenderClipRect(ren, &waterClip);
            if (copied) {
                // Sample a separate scene copy, never the active render target.
                for (float y = top; y < bottom; y += 3.0f) {
                    const float offset = std::sin((y + camY) * 0.15f + time * 3.2f) * 1.2f +
                        std::sin((y + camY) * 0.07f - time * 1.8f) * 0.6f;
                    const SDL_FRect dst{left, y, right - left, std::min(3.0f, bottom - y)};
                    const SDL_FRect src{std::clamp((left + offset) * scaleX, 0.0f, std::max(0.0f, pixelW - dst.w * scaleX)),
                        y * scaleY, dst.w * scaleX, dst.h * scaleY};
                    SDL_RenderTexture(ren, snapshot_, &src, &dst);
                }
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(ren, 15, 70, 115, 90);
            const SDL_FRect fill{left, top, right - left, bottom - top};
            SDL_RenderFillRect(ren, &fill);
            if (region.surface && r.y >= 0.0f) {
                SDL_SetRenderDrawColor(ren, 100, 205, 255, 155);
                for (float x = left; x < right; x += 4.0f) {
                    auto wave = [&](float at) {
                        return r.y + 1.5f + std::sin((at + camX) * 0.12f + time * 3.0f);
                    };
                    const float end = std::min(x + 4.0f, right - 1.0f / scaleX);
                    if (end >= x) SDL_RenderLine(ren, x, wave(x), end, wave(end));
                }
            }
        }
        SDL_SetRenderClipRect(ren, clipped ? &clip : nullptr);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
        for (const auto& ripple : ripples_) {
            const float radius = (5.0f + ripple.age * 65.0f) * ripple.strength;
            const float wrapW = (float)(map.w * map.tileSize), wrapH = (float)(map.h * map.tileSize);
            float cx = ripple.x - camX, cy = ripple.y - camY;
            if (wrapX) cx -= std::floor((cx + wrapW * 0.5f) / wrapW) * wrapW;
            if (wrapY) cy -= std::floor((cy + wrapH * 0.5f) / wrapH) * wrapH;
            SDL_SetRenderDrawColor(ren, 150, 225, 255, (Uint8)(180.0f * (1.0f - ripple.age / 0.8f)));
            for (int iy = wrapY ? -1 : 0; iy <= (wrapY ? 1 : 0); ++iy)
                for (int ix = wrapX ? -1 : 0; ix <= (wrapX ? 1 : 0); ++ix) {
                    float prevX = cx + ix * wrapW + radius, prevY = cy + iy * wrapH;
                    for (int step = 1; step <= 24; ++step) {
                        const float angle = step * 6.2831853f / 24.0f;
                        const float x = cx + ix * wrapW + std::cos(angle) * radius;
                        const float y = cy + iy * wrapH + std::sin(angle) * radius * 0.15f;
                        if (WaterVisuals::hasSurface(map, (int)std::floor((x + camX) / map.tileSize),
                            (int)std::floor((cy + iy * wrapH + camY + 2.0f) / map.tileSize), wrapX, wrapY))
                            SDL_RenderLine(ren, prevX, prevY, x, y);
                        prevX = x; prevY = y;
                    }
                }
        }
        SDL_SetRenderDrawBlendMode(ren, blend);
        SDL_SetRenderDrawColor(ren, red, green, blue, alpha);
    }
};
