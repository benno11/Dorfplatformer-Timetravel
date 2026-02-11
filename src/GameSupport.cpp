#include "GameSupport.h"

#include <sdl3/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

#include "AssetPath.h"

bool playerTouchesTileId(const TileMap& map, const Player& player, int idA, int idB) {
    int t = map.tileSize;
    int left = (int)std::floor(player.x / t);
    int right = (int)std::floor((player.x + player.w - 1) / t);
    int top = (int)std::floor(player.y / t);
    int bottom = (int)std::floor((player.y + player.h - 1) / t);
    for (int ty = top; ty <= bottom; ++ty) {
        if (ty < 0 || ty >= map.h) continue;
        for (int tx = left; tx <= right; ++tx) {
            if (tx < 0 || tx >= map.w) continue;
            int id = (int)map.tileIds[ty * map.w + tx];
            if (id == idA || id == idB) return true;
        }
    }
    return false;
}

bool pointInRectF(float x, float y, const SDL_FRect& r) {
    return x >= r.x && y >= r.y && x < (r.x + r.w) && y < (r.y + r.h);
}

int parseLevelIdFromLevelPath(const std::string& levelPath) {
    const std::string name = std::filesystem::path(levelPath).stem().string();
    std::string digits;
    for (char ch : name) {
        if (std::isdigit((unsigned char)ch)) digits.push_back(ch);
    }
    if (digits.empty()) return -1;
    try { return std::stoi(digits); } catch (...) { return -1; }
}

bool readProcessMemoryKB(long& rssKB, long& vmKB) {
    rssKB = -1;
    vmKB = -1;
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            iss >> rssKB;
        } else if (line.rfind("VmSize:", 0) == 0) {
            std::istringstream iss(line.substr(7));
            iss >> vmKB;
        }
    }
    return (rssKB >= 0 || vmKB >= 0);
}

std::string makeBuildUuid() {
    const std::string seed = std::string(__DATE__) + " " + std::string(__TIME__);
    uint64_t h1 = 1469598103934665603ull;
    uint64_t h2 = 1099511628211ull;
    for (unsigned char c : seed) {
        h1 ^= (uint64_t)c;
        h1 *= 1099511628211ull;
        h2 ^= (uint64_t)(c + 31u);
        h2 *= 1469598103934665603ull;
    }
    auto hex4 = [](uint64_t v, int n) {
        static const char* kHex = "0123456789abcdef";
        std::string out;
        out.resize(n);
        for (int i = n - 1; i >= 0; --i) {
            out[i] = kHex[(int)(v & 0xF)];
            v >>= 4;
        }
        return out;
    };
    return hex4(h1, 8) + "-" + hex4(h1 >> 8, 4) + "-" + hex4(h1 >> 24, 4) + "-" + hex4(h2, 4) + "-" + hex4((h2 << 16) ^ h1, 12);
}

static std::string extractBetween(const std::string& s, const std::string& a, const std::string& b) {
    size_t p = s.find(a);
    if (p == std::string::npos) return "";
    p += a.size();
    size_t q = s.find(b, p);
    if (q == std::string::npos) return "";
    return s.substr(p, q - p);
}

static bool parseTextureRect(const std::string& s, SDL_Rect& out) {
    int x = 0, y = 0, w = 0, h = 0;
    if (std::sscanf(s.c_str(), "{{%d,%d},{%d,%d}}", &x, &y, &w, &h) == 4) {
        out.x = x; out.y = y; out.w = w; out.h = h;
        return true;
    }
    return false;
}

std::unordered_map<std::string, Frame> loadPlistFrames(const std::string& plistPath) {
    std::unordered_map<std::string, Frame> frames;
    const std::string text = ReadTextFile(plistPath);
    if (text.empty()) return frames;
    std::istringstream in(text);
    std::string line, currentName;
    bool expectTextureRect = false, expectRotated = false;
    Frame pending{};
    while (std::getline(in, line)) {
        std::string key = extractBetween(line, "<key>", "</key>");
        if (!key.empty() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
            currentName = key.substr(0, key.size() - 4);
            pending = Frame{};
        }
        if (line.find("<key>textureRect</key>") != std::string::npos) { expectTextureRect = true; continue; }
        if (line.find("<key>textureRotated</key>") != std::string::npos) { expectRotated = true; continue; }
        if (expectTextureRect) {
            std::string val = extractBetween(line, "<string>", "</string>");
            if (!val.empty()) parseTextureRect(val, pending.rect);
            expectTextureRect = false;
        }
        if (expectRotated) {
            if (line.find("<true/>") != std::string::npos) pending.rotated = true;
            if (line.find("<false/>") != std::string::npos) pending.rotated = false;
            if (!currentName.empty()) frames[currentName] = pending;
            expectRotated = false;
        }
    }
    return frames;
}

std::vector<FrameEntry> loadPlistFrameList(const std::string& plistPath) {
    std::vector<FrameEntry> frames;
    const std::string text = ReadTextFile(plistPath);
    if (text.empty()) return frames;
    std::istringstream in(text);
    std::string line, currentName;
    bool expectTextureRect = false, expectRotated = false;
    Frame pending{};
    while (std::getline(in, line)) {
        std::string key = extractBetween(line, "<key>", "</key>");
        if (!key.empty() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
            currentName = key.substr(0, key.size() - 4);
            pending = Frame{};
        }
        if (line.find("<key>textureRect</key>") != std::string::npos) { expectTextureRect = true; continue; }
        if (line.find("<key>textureRotated</key>") != std::string::npos) { expectRotated = true; continue; }
        if (expectTextureRect) {
            std::string val = extractBetween(line, "<string>", "</string>");
            if (!val.empty()) parseTextureRect(val, pending.rect);
            expectTextureRect = false;
        }
        if (expectRotated) {
            if (line.find("<true/>") != std::string::npos) pending.rotated = true;
            if (line.find("<false/>") != std::string::npos) pending.rotated = false;
            if (!currentName.empty()) frames.push_back(FrameEntry{currentName, pending});
            expectRotated = false;
        }
    }
    return frames;
}

void renderFrame(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst) {
    if (!tex) return;
    if (!f.rotated) {
        SDL_RenderCopy(ren, tex, &f.rect, &dst);
        return;
    }
    SDL_Point center{dst.w / 2, dst.h / 2};
    SDL_RenderCopyEx(ren, tex, &f.rect, &dst, -90.0, &center, SDL_FLIP_NONE);
}

void renderFrameEx(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst, SDL_RendererFlip flip) {
    if (!tex) return;
    if (!f.rotated) {
        SDL_RenderCopyEx(ren, tex, &f.rect, &dst, 0.0, nullptr, flip);
        return;
    }
    SDL_Point center{dst.w / 2, dst.h / 2};
    SDL_RenderCopyEx(ren, tex, &f.rect, &dst, -90.0, &center, flip);
}

SDL_Texture* loadTextureWithColorKey(SDL_Renderer* ren, const std::string& path, Uint8 r, Uint8 g, Uint8 b) {
    const std::string resolved = ResolveAssetPath(path);
    SDL_Surface* surf = IMG_Load(resolved.c_str());
    if (!surf) return nullptr;
    Uint32 key = SDL_MapSurfaceRGB(surf, r, g, b);
    SDL_SetColorKey(surf, SDL_TRUE, key);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

SDL_Rect computePresentRect(int winW, int winH, int baseW, int baseH) {
    if (winW <= 0 || winH <= 0 || baseW <= 0 || baseH <= 0) return SDL_Rect{0, 0, winW, winH};
    float sx = (float)winW / (float)baseW;
    float sy = (float)winH / (float)baseH;
    float s = std::min(sx, sy);
    int w = std::max(1, (int)std::floor(baseW * s));
    int h = std::max(1, (int)std::floor(baseH * s));
    int x = (winW - w) / 2;
    int y = (winH - h) / 2;
    return SDL_Rect{x, y, w, h};
}

bool windowToGamePoint(int wx, int wy, int winW, int winH, int baseW, int baseH, int& gx, int& gy) {
    SDL_Rect dst = computePresentRect(winW, winH, baseW, baseH);
    if (wx < dst.x || wy < dst.y || wx >= dst.x + dst.w || wy >= dst.y + dst.h) return false;
    float u = (float)(wx - dst.x) / (float)dst.w;
    float v = (float)(wy - dst.y) / (float)dst.h;
    gx = std::clamp((int)std::floor(u * baseW), 0, baseW - 1);
    gy = std::clamp((int)std::floor(v * baseH), 0, baseH - 1);
    return true;
}
