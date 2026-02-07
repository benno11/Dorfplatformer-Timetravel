#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#if __has_include(<SDL2/SDL_mixer.h>)
#include <SDL2/SDL_mixer.h>
#define HAS_SDL_MIXER 1
#elif __has_include(<SDL_mixer.h>)
#include <SDL_mixer.h>
#define HAS_SDL_MIXER 1
#else
#define HAS_SDL_MIXER 0
#endif
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cctype>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <iostream>
#if defined(__ANDROID__)
#include <jni.h>
#endif
#include "TileMap.h"
#include "LevelLoader.h"
#include "Player.h"
#include "PlayerController.h"
#include "TextRenderer.h"
#include "LevelSelect.h"
#include "AssetPath.h"


static std::vector<int> loadLevelNumberList(const std::string& path) {
    const std::string text = ReadTextFile(path);
    if (text.empty()) return {};
    std::istringstream f(text);
    std::vector<int> out;
    std::string line;
    while (std::getline(f, line)) {
        std::string digits;
        for (char ch : line) {
            if (std::isdigit((unsigned char)ch)) digits.push_back(ch);
        }
        if (digits.size() < 3) continue;
        int v = 0;
        try { v = std::stoi(digits.substr(0, 3)); } catch (...) { continue; }
        out.push_back(v);
    }
    return out;
}

static int parseLevelIndexFromPath(const std::string& levelPath) {
    const std::string name = std::filesystem::path(levelPath).stem().string(); // level_001
    std::string digits;
    for (char ch : name) {
        if (std::isdigit((unsigned char)ch)) digits.push_back(ch);
    }
    if (digits.empty()) return -1;
    try { return std::stoi(digits); } catch (...) { return -1; }
}

static int NextLevelId(int currentLevelId) {
    if (currentLevelId >= 1 && currentLevelId <= 50) {
        int world = (currentLevelId - 1) / 10 + 1;           // 1..5
        int inWorldLevel = (currentLevelId - 1) % 10 + 1;    // 1..10
        if (inWorldLevel >= 1 && inWorldLevel <= 4) return (world - 1) * 10 + 5;
        if (inWorldLevel >= 5 && inWorldLevel <= 8) return (world - 1) * 10 + 9;
        if (inWorldLevel >= 9 && inWorldLevel <= 10) return world * 10 + 1;
    }

    // World 6 special routing.
    if (currentLevelId == 51 || currentLevelId == 52) return 53;
    if (currentLevelId == 53 || currentLevelId == 54) return 55;
    if (currentLevelId == 55) return 57;
    if (currentLevelId == 56) return 58;

    return -1;
}

static std::string LevelPathFromId(int levelId) {
    if (levelId <= 0) return "";
    std::ostringstream ss;
    ss << "assets/levels/level_" << std::setw(3) << std::setfill('0') << levelId << ".txt";
    std::string path = ss.str();
    if (!FileExists(path)) return "";
    return path;
}



static std::vector<char> loadBlockDefs(const std::string& path) {
    const std::string text = ReadTextFile(path);
    if (text.empty()) return {};
    std::istringstream f(text);
    std::vector<char> defs;
    std::string line;
    while (std::getline(f, line)) {
        char c = 0;
        for (char ch : line) {
            if (!std::isspace((unsigned char)ch)) { c = ch; break; }
        }
        defs.push_back(c);
    }
    return defs;
}

static void applyBlockDefAt(TileMap& map, int idx, unsigned short tileId, const std::vector<char>& defs) {
    char d = (tileId < defs.size()) ? defs[tileId] : 0;
    map.semisolid[idx] = (d == '=') ? 1 : 0;
    map.water[idx]     = (d == '^') ? 1 : 0;
    map.solid[idx]     = (d != '=' && d != '^' && d != '#') ? 0 : 1;
}

static int collectCoinsAtPlayer(TileMap& map, const Player& player, const std::vector<char>& defs) {
    int t = map.tileSize;
    int left = (int)std::floor(player.x / t);
    int right = (int)std::floor((player.x + player.w - 1) / t);
    int top = (int)std::floor(player.y / t);
    int bottom = (int)std::floor((player.y + player.h - 1) / t);

    int collected = 0;
    for (int ty = top; ty <= bottom; ++ty) {
        if (ty < 0 || ty >= map.h) continue;
        for (int tx = left; tx <= right; ++tx) {
            if (tx < 0 || tx >= map.w) continue;
            int idx = ty * map.w + tx;
            if (map.tileIds[idx] != 24) continue;
            map.tileIds[idx] = 2;
            applyBlockDefAt(map, idx, 2, defs);
            collected++;
        }
    }
    return collected;
}

static bool playerTouchesTileId(const TileMap& map, const Player& player, int idA, int idB) {
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

static bool pointInRectF(float x, float y, const SDL_FRect& r) {
    return x >= r.x && y >= r.y && x < (r.x + r.w) && y < (r.y + r.h);
}


struct Frame {
    SDL_Rect rect{0,0,0,0};
    bool rotated = false;
};

struct FrameEntry {
    std::string name;
    Frame frame;
};

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
        out.x = x;
        out.y = y;
        out.w = w;
        out.h = h;
        return true;
    }
    return false;
}

static std::unordered_map<std::string, Frame> loadPlistFrames(const std::string& plistPath) {
    std::unordered_map<std::string, Frame> frames;
    const std::string text = ReadTextFile(plistPath);
    if (text.empty()) return frames;
    std::istringstream in(text);

    std::string line;
    std::string currentName;
    bool expectTextureRect = false;
    bool expectRotated = false;
    Frame pending{};

    while (std::getline(in, line)) {
        std::string key = extractBetween(line, "<key>", "</key>");
        if (!key.empty() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
            currentName = key.substr(0, key.size() - 4);
            pending = Frame{};
        }
        if (line.find("<key>textureRect</key>") != std::string::npos) {
            expectTextureRect = true;
            continue;
        }
        if (line.find("<key>textureRotated</key>") != std::string::npos) {
            expectRotated = true;
            continue;
        }
        if (expectTextureRect) {
            std::string val = extractBetween(line, "<string>", "</string>");
            if (!val.empty()) {
                parseTextureRect(val, pending.rect);
            }
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

static std::vector<FrameEntry> loadPlistFrameList(const std::string& plistPath) {
    std::vector<FrameEntry> frames;
    const std::string text = ReadTextFile(plistPath);
    if (text.empty()) return frames;
    std::istringstream in(text);

    std::string line;
    std::string currentName;
    bool expectTextureRect = false;
    bool expectRotated = false;
    Frame pending{};

    while (std::getline(in, line)) {
        std::string key = extractBetween(line, "<key>", "</key>");
        if (!key.empty() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
            currentName = key.substr(0, key.size() - 4);
            pending = Frame{};
        }
        if (line.find("<key>textureRect</key>") != std::string::npos) {
            expectTextureRect = true;
            continue;
        }
        if (line.find("<key>textureRotated</key>") != std::string::npos) {
            expectRotated = true;
            continue;
        }
        if (expectTextureRect) {
            std::string val = extractBetween(line, "<string>", "</string>");
            if (!val.empty()) {
                parseTextureRect(val, pending.rect);
            }
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

static void renderFrame(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst) {
    if (!tex) return;
    if (!f.rotated) {
        SDL_RenderCopy(ren, tex, &f.rect, &dst);
        return;
    }
    SDL_Rect src = f.rect;
    SDL_Rect dstRot = dst;
    dstRot.w = dst.h;
    dstRot.h = dst.w;
    SDL_Point center{dstRot.w / 2, dstRot.h / 2};
    SDL_RenderCopyEx(ren, tex, &src, &dstRot, -90.0, &center, SDL_FLIP_NONE);
}

static void renderFrameEx(SDL_Renderer* ren, SDL_Texture* tex, const Frame& f, const SDL_Rect& dst, SDL_RendererFlip flip) {
    if (!tex) return;
    if (!f.rotated) {
        SDL_RenderCopyEx(ren, tex, &f.rect, &dst, 0.0, nullptr, flip);
        return;
    }
    SDL_Rect src = f.rect;
    SDL_Rect dstRot = dst;
    dstRot.w = dst.h;
    dstRot.h = dst.w;
    SDL_Point center{dstRot.w / 2, dstRot.h / 2};
    SDL_RenderCopyEx(ren, tex, &src, &dstRot, -90.0, &center, flip);
}

static SDL_Texture* loadTextureWithColorKey(SDL_Renderer* ren, const std::string& path, Uint8 r, Uint8 g, Uint8 b) {
    const std::string resolved = ResolveAssetPath(path);
    SDL_Surface* surf = IMG_Load(resolved.c_str());
    if (!surf) return nullptr;
    Uint32 key = SDL_MapRGB(surf->format, r, g, b);
    SDL_SetColorKey(surf, SDL_TRUE, key);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

static SDL_Rect computePresentRect(int winW, int winH, int baseW, int baseH) {
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

static bool windowToGamePoint(int wx, int wy, int winW, int winH, int baseW, int baseH, int& gx, int& gy) {
    SDL_Rect dst = computePresentRect(winW, winH, baseW, baseH);
    if (wx < dst.x || wy < dst.y || wx >= dst.x + dst.w || wy >= dst.y + dst.h) return false;
    float u = (float)(wx - dst.x) / (float)dst.w;
    float v = (float)(wy - dst.y) / (float)dst.h;
    gx = std::clamp((int)std::floor(u * baseW), 0, baseW - 1);
    gy = std::clamp((int)std::floor(v * baseH), 0, baseH - 1);
    return true;
}

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Log("SDL_Init completed");
    IMG_Init(IMG_INIT_PNG);
    InitTextRenderer(ResolveAssetPath("assets/Fonts/Main.ttf"));
    bool audioReady = false;
#if HAS_SDL_MIXER
    int mixerFlags = MIX_INIT_MP3;
    audioReady = (Mix_Init(mixerFlags) & mixerFlags) == mixerFlags &&
                 Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0;
    if (!audioReady) SDL_Log("Audio mixer init failed: %s", Mix_GetError());
#else
    SDL_Log("SDL_mixer not found at compile time: music playback disabled.");
#endif

    constexpr int kBaseScreenW = 960;
    constexpr int kBaseScreenH = 540;

    SDL_Window* win = SDL_CreateWindow(
        "Dorfplatformer Timetravel",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kBaseScreenW, kBaseScreenH,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Log("Window and renderer created");
    SDL_Texture* gameTarget = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        kBaseScreenW, kBaseScreenH
    );

    nlohmann::json texJson;
    {
        const std::string text = ReadTextFile("assets/textures.json");
        if (!text.empty()) {
            try { texJson = nlohmann::json::parse(text); } catch (...) { texJson = nlohmann::json(); }
        }
    }
    auto texPath = [&](const std::string& section, const std::string& key, const std::string& fallback) -> std::string {
        if (texJson.contains(section) && texJson[section].is_object()) {
            const auto& t = texJson[section];
            if (t.contains(key) && t[key].is_string()) return t[key].get<std::string>();
        }
        return fallback;
    };

    std::string pausePlist = texPath("plists", "pause", "assets/Sheets/DF_Pause-uhd.plist");
    SDL_Texture* pauseTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "pause", "assets/Sheets/DF_Pause-uhd.png")).c_str());
    auto pauseFrames = loadPlistFrames(pausePlist);

    std::string blocksPlist = texPath("plists", "blocks", "assets/Sheets/DF_Blocks-uhd.plist");
    SDL_Texture* blocksTex = loadTextureWithColorKey(ren, texPath("textures", "blocks", "assets/Sheets/DF_Blocks-uhd.png"), 0x9f, 0x61, 0xff);
    auto blocksFrameList = loadPlistFrameList(blocksPlist);
    std::unordered_map<std::string, Frame> blocksFrameByName;
    blocksFrameByName.reserve(blocksFrameList.size());
    for (const auto& e : blocksFrameList) blocksFrameByName[e.name] = e.frame;

    std::string bgPlist = texPath("plists", "background", "assets/Sheets/DF_Background-uhd.plist");
    SDL_Texture* bgTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "background", "assets/Sheets/DF_Background-uhd.png")).c_str());
    auto bgFrameList = loadPlistFrameList(bgPlist);
    std::unordered_map<std::string, Frame> bgFrameByName;
    bgFrameByName.reserve(bgFrameList.size());
    for (const auto& e : bgFrameList) bgFrameByName[e.name] = e.frame;

    std::unordered_map<int, std::string> tileFrameById;
    {
        const std::string text = ReadTextFile("assets/tile_defs.json");
        if (!text.empty()) {
            nlohmann::json j;
            try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
            auto readSection = [&](const char* key) {
                if (!j.contains(key) || !j[key].is_object()) return;
                for (auto it = j[key].begin(); it != j[key].end(); ++it) {
                    if (!it.value().is_object()) continue;
                    if (!it.value().contains("frame") || !it.value()["frame"].is_string()) continue;
                    int id = 0;
                    try { id = std::stoi(it.key()); } catch (...) { continue; }
                    tileFrameById[id] = it.value()["frame"].get<std::string>();
                }
            };
            readSection("bg");
            readSection("fg");
        }
    }

    std::string playerPlist = texPath("plists", "player", "assets/Sheets/DF_Player1-uhd.plist");
    SDL_Texture* playerTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "player", "assets/Sheets/DF_Player1-uhd.png")).c_str());
    auto playerFrameList = loadPlistFrameList(playerPlist);
    std::unordered_map<std::string, Frame> playerFramesByName;
    playerFramesByName.reserve(playerFrameList.size());
    for (const auto& e : playerFrameList) playerFramesByName[e.name] = e.frame;
    const Frame* fallbackPlayerFrame = !playerFrameList.empty() ? &playerFrameList[0].frame : nullptr;
#if HAS_SDL_MIXER
    Mix_Chunk* coinSfx = nullptr;
    if (audioReady) {
        coinSfx = Mix_LoadWAV(ResolveAssetPath("assets/Audio/sfx/Coin.mp3").c_str());
        if (!coinSfx) SDL_Log("Could not load coin sfx: %s", Mix_GetError());
    }
#endif

    auto getPlayerFrame = [&](const std::string& name) -> const Frame* {
        auto it = playerFramesByName.find(name);
        if (it == playerFramesByName.end()) return fallbackPlayerFrame;
        return &it->second;
    };

    struct AnimConfig {
        float fps = 15.0f;
        std::vector<std::string> idle;
        std::vector<std::string> walk;
        std::vector<std::string> jump;
        std::vector<std::string> fall;
        std::vector<std::string> crouch;
        std::vector<std::string> skid;
        std::vector<std::string> hurt;
        std::vector<std::string> death;
    };

    auto loadAnimConfig = [&](const std::string& path) -> AnimConfig {
        AnimConfig cfg;
        const std::string text = ReadTextFile(path);
        if (text.empty()) return cfg;
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(text);
        } catch (...) {
            return cfg;
        }
        if (j.contains("fps") && j["fps"].is_number()) cfg.fps = (float)j["fps"].get<double>();
        auto readList = [&](const char* key, std::vector<std::string>& out) {
            if (!j.contains(key) || !j[key].is_array()) return;
            out.clear();
            for (const auto& v : j[key]) {
                if (v.is_string()) out.push_back(v.get<std::string>());
            }
        };
        readList("idle", cfg.idle);
        readList("walk", cfg.walk);
        readList("jump", cfg.jump);
        readList("fall", cfg.fall);
        readList("crouch", cfg.crouch);
        readList("skid", cfg.skid);
        readList("hurt", cfg.hurt);
        readList("death", cfg.death);
        return cfg;
    };

    AnimConfig animCfg = loadAnimConfig("assets/player_anim.json");
    float jumpBufferMax = 0.12f;
    MovementConfig movementCfg{};
    {
        const std::string text = ReadTextFile("assets/config.json");
        if (!text.empty()) {
            nlohmann::json cfg;
            try { cfg = nlohmann::json::parse(text); } catch (...) { cfg = nlohmann::json(); }
            if (cfg.contains("jump_buffer_seconds") && cfg["jump_buffer_seconds"].is_number()) {
                jumpBufferMax = (float)cfg["jump_buffer_seconds"].get<double>();
                if (jumpBufferMax < 0.0f) jumpBufferMax = 0.0f;
            }
            if (cfg.contains("movement") && cfg["movement"].is_object()) {
                const auto& m = cfg["movement"];
                auto readMove = [&](const char* key, float& out) {
                    if (m.contains(key) && m[key].is_number()) out = (float)m[key].get<double>();
                };
                readMove("accel_in_water", movementCfg.accelInWater);
                readMove("accel_ground", movementCfg.accelGround);
                readMove("max_speed_in_water", movementCfg.maxSpeedInWater);
                readMove("max_speed_ground", movementCfg.maxSpeedGround);
                readMove("friction_in_water", movementCfg.frictionInWater);
                readMove("friction_ground", movementCfg.frictionGround);
                readMove("gravity_in_water", movementCfg.gravityInWater);
                readMove("gravity_ground", movementCfg.gravityGround);
                readMove("jump_speed", movementCfg.jumpSpeed);
                readMove("jump_hold_gravity", movementCfg.jumpHoldGravity);
                readMove("jump_hold_max", movementCfg.jumpHoldMax);
                readMove("jump_cut_speed", movementCfg.jumpCutSpeed);
                readMove("swim_up_speed", movementCfg.swimUpSpeed);
                readMove("swim_rise", movementCfg.swimRise);
            }
        }
    }

    if (!pauseTex || !blocksTex || !playerTex || pauseFrames.empty() || blocksFrameList.empty() || playerFrameList.empty()) {
        std::string msg = "Failed to load assets:";
        if (!pauseTex) msg += "\n- pause texture";
        if (pauseFrames.empty()) msg += "\n- pause plist";
        if (!blocksTex) msg += "\n- blocks texture";
        if (blocksFrameList.empty()) msg += "\n- blocks plist";
        if (!playerTex) msg += "\n- player texture";
        if (playerFrameList.empty()) msg += "\n- player plist";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Asset Load Error", msg.c_str(), win);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        if (audioReady) {
#if HAS_SDL_MIXER
            Mix_CloseAudio();
            Mix_Quit();
#endif
        }
#if HAS_SDL_MIXER
        if (coinSfx) Mix_FreeChunk(coinSfx);
#endif
        ShutdownTextRenderer();
        SDL_Quit();
        return 1;
    }
    if (!gameTarget) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Render Target Error", "Failed to create game render target.", win);
        if (playerTex) SDL_DestroyTexture(playerTex);
        if (bgTex) SDL_DestroyTexture(bgTex);
        if (blocksTex) SDL_DestroyTexture(blocksTex);
        if (pauseTex) SDL_DestroyTexture(pauseTex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        ShutdownTextRenderer();
        SDL_Quit();
        return 1;
    }

    bool fullscreen = false;
    bool clampCamX = true;
    const std::vector<int> levelNumberList = loadLevelNumberList("assets/Level Numer List.txt");
    bool running = true;
    while (running) {
        std::string levelPath = RunLevelSelect(win, ren);
        if (levelPath.empty()) {
            break;
        }

        TileMap map;
        std::vector<ObjectInstance> objects;
        LevelMeta meta;
        Player player;
        int coinCount = 0;
#if HAS_SDL_MIXER
        Mix_Music* levelMusic = nullptr;
#endif
        char TimeWarpID = 'N';
        int world_id = 0;
        int Level_Part_id = 0;
        int Time_ID = 0;
        std::vector<char> blockDefs = loadBlockDefs("assets/blockdefined.txt");

        auto reloadLevel = [&]() {
            loadLevelBNNLVL(levelPath, map, objects, meta);
            coinCount = 0;

        player = Player{};
        for (const auto& o : objects) {
            if (o.id == "player") {
                player.x = o.x;
                player.y = o.y - 12.0f;
                break;
            }
        }

            // Teleport player to first tile id 28 and replace it with tile id 2.
            for (int idx = 0; idx < (int)map.tileIds.size(); ++idx) {
                if (map.tileIds[idx] != 28) continue;
                int tx = idx % map.w;
                int ty = idx / map.w;
                player.x = tx * map.tileSize;
                player.y = ty * map.tileSize;
                map.tileIds[idx] = 2;
                applyBlockDefAt(map, idx, 2, blockDefs);
                break;
            }

            // Ensure player does not start inside solid geometry.
            if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                bool resolved = false;
                const int maxSteps = std::max(1, map.h * map.tileSize);
                for (int step = 1; step <= maxSteps; ++step) {
                    float upY = player.y - (float)step;
                    if (!RectHitsSolid(map, player.x, upY, player.w, player.h)) {
                        player.y = upY;
                        resolved = true;
                        break;
                    }
                    float downY = player.y + (float)step;
                    if (!RectHitsSolid(map, player.x, downY, player.w, player.h)) {
                        player.y = downY;
                        resolved = true;
                        break;
                    }
                }
                if (!resolved) {
                    player.vx = 0.0f;
                    player.vy = 0.0f;
                }
            }

            // Time warp marker from tile IDs:
            // 43 => 'P', 45 => 'F' (45 wins if both are present).
            TimeWarpID = 'N';
            for (unsigned short id : map.tileIds) {
                if (id == 43 && TimeWarpID == 'N') TimeWarpID = 'P';
                if (id == 45) TimeWarpID = 'F';
            }

            int levelIndex = parseLevelIndexFromPath(levelPath); // level_001 => 1
            if (levelIndex > 0 && levelIndex <= (int)levelNumberList.size()) {
                int code = levelNumberList[levelIndex - 1];
                world_id = code / 100;
                Level_Part_id = (code / 10) % 10;
                Time_ID = code % 10;
            } else {
                world_id = 0;
                Level_Part_id = 0;
                Time_ID = 0;
            }

            if (audioReady) {
#if HAS_SDL_MIXER
                std::string musicPath;
                if (world_id >= 6 && world_id <= 7) {
                    musicPath = "assets/Audio/Music/" + std::to_string(world_id) + "." + std::to_string(Level_Part_id) + ".mp3";
                } else {
                    musicPath = "assets/Audio/Music/" + std::to_string(world_id) + "." + std::to_string(Time_ID) + ".mp3";
                }
                if (levelMusic) {
                    Mix_FreeMusic(levelMusic);
                    levelMusic = nullptr;
                }
                levelMusic = Mix_LoadMUS(ResolveAssetPath(musicPath).c_str());
                if (levelMusic) {
                    Mix_PlayMusic(levelMusic, -1);
                } else {
                    SDL_Log("Could not load music: %s (%s)", musicPath.c_str(), Mix_GetError());
                }
#endif
            }
        };

        reloadLevel();

        bool levelRunning = true;
        bool paused = false;
        bool showHitboxes = false;
        int pauseSelection = 0; // 0 = Resume, 1 = Restart, 2 = Quit
        bool returnToSelect = false;
        std::unordered_map<SDL_FingerID, SDL_FPoint> activeTouches;
        SDL_Event e;
        Uint32 lastTicks = SDL_GetTicks();

        SDL_Rect pauseBtnContinue{0,0,0,0};
        SDL_Rect pauseBtnRestart{0,0,0,0};
        SDL_Rect pauseBtnExit{0,0,0,0};

        auto handlePauseSelect = [&](int sel) {
            pauseSelection = sel;
            if (pauseSelection == 0) {
                paused = false;
            } else if (pauseSelection == 1) {
                reloadLevel();
                paused = false;
            } else {
                returnToSelect = true;
                levelRunning = false;
            }
        };

        while (levelRunning) {
            Uint32 now = SDL_GetTicks();
            float dt = (now - lastTicks) / 1000.0f;
            if (dt > 0.05f) dt = 0.05f;
            lastTicks = now;

        float inputMove = 0.0f;
        bool inputDown = false;
        int screenW = kBaseScreenW;
        int screenH = kBaseScreenH;
        float uiSize = std::clamp(std::min((float)screenW, (float)screenH) * 0.16f, 110.0f, 190.0f);
        float uiPad = std::clamp(uiSize * 0.22f, 20.0f, 44.0f);
        float uiGap = std::clamp(uiSize * 0.18f, 12.0f, 34.0f);
        SDL_FRect touchLeftBtn{uiPad, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchRightBtn{uiPad + uiSize + uiGap, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchDownBtn{uiPad + (uiSize + uiGap) * 2.0f, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchJumpBtn{screenW - uiPad - uiSize, screenH - uiPad - uiSize, uiSize, uiSize};
        bool touchLeft = false;
        bool touchRight = false;
        bool touchDown = false;
        bool touchJump = false;

        auto computeTouchButtons = [&]() {
            touchLeft = false;
            touchRight = false;
            touchDown = false;
            touchJump = false;
            if (paused) return;
            int winW = 0, winH = 0;
            SDL_GetWindowSize(win, &winW, &winH);
            auto expandRect = [](const SDL_FRect& r, float pad) {
                return SDL_FRect{r.x - pad, r.y - pad, r.w + pad * 2.0f, r.h + pad * 2.0f};
            };
            float hitPad = std::max(8.0f, uiSize * 0.12f);
            SDL_FRect leftHit = expandRect(touchLeftBtn, hitPad);
            SDL_FRect rightHit = expandRect(touchRightBtn, hitPad);
            SDL_FRect downHit = expandRect(touchDownBtn, hitPad);
            SDL_FRect jumpHit = expandRect(touchJumpBtn, hitPad);
            for (const auto& kv : activeTouches) {
                int wx = (int)std::lround(kv.second.x * winW);
                int wy = (int)std::lround(kv.second.y * winH);
                int gx = 0, gy = 0;
                if (!windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy)) continue;
                float px = (float)gx;
                float py = (float)gy;
                if (pointInRectF(px, py, leftHit)) touchLeft = true;
                if (pointInRectF(px, py, rightHit)) touchRight = true;
                if (pointInRectF(px, py, downHit)) touchDown = true;
                if (pointInRectF(px, py, jumpHit)) touchJump = true;
            }
        };
        computeTouchButtons();

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; levelRunning = false; }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                paused = true;
            }
            if (e.type == SDL_FINGERDOWN) {
                activeTouches[e.tfinger.fingerId] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
            }
            if (e.type == SDL_FINGERMOTION) {
                activeTouches[e.tfinger.fingerId] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
            }
            if (e.type == SDL_FINGERUP) {
                activeTouches.erase(e.tfinger.fingerId);
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
                if (e.key.keysym.sym == SDLK_F12) {
                    showHitboxes = !showHitboxes;
                }
                if (e.key.keysym.sym == SDLK_F10) {
                    clampCamX = !clampCamX;
                }
                if (e.key.keysym.sym == SDLK_F9) {
                    int currentLevelId = parseLevelIndexFromPath(levelPath);
                    int nextLevelId = NextLevelId(currentLevelId);
                    std::string nextPath = LevelPathFromId(nextLevelId);
                    if (!nextPath.empty()) {
                        levelPath = nextPath;
                        reloadLevel();
                    }
                }
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    paused = !paused;
                }
                if (paused) {
                    if (e.key.keysym.sym == SDLK_LEFT || e.key.keysym.sym == SDLK_a) {
                        pauseSelection = std::max(0, pauseSelection - 1);
                    }
                    if (e.key.keysym.sym == SDLK_RIGHT || e.key.keysym.sym == SDLK_d) {
                        pauseSelection = std::min(2, pauseSelection + 1);
                    }
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        handlePauseSelect(pauseSelection);
                    }
                }
            }
            if (paused && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                if (windowToGamePoint(e.button.x, e.button.y, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy)) {
                    SDL_Point pt{gx, gy};
                    if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                    else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                    else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
                }
            }
            if (paused && e.type == SDL_FINGERDOWN) {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                int wx = (int)(e.tfinger.x * winW);
                int wy = (int)(e.tfinger.y * winH);
                int gx = 0, gy = 0;
                if (windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy)) {
                    SDL_Point pt{gx, gy};
                    if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                    else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                    else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
                }
            }
        }
        computeTouchButtons();

        if (!paused) {
            float touchMove = 0.0f;
            if (touchLeft) touchMove -= 1.0f;
            if (touchRight) touchMove += 1.0f;
            PlayerUpdateResult upd = UpdatePlayerMovement(
                player, map, dt, jumpBufferMax, movementCfg,
                touchMove, touchDown, touchJump, inputMove, inputDown, reloadLevel
            );
            if (upd == PlayerUpdateResult::RenderOnly) {
                goto RENDER_ONLY;
            }
            if (upd == PlayerUpdateResult::Reloaded) {
                continue;
            }

            int collectedNow = collectCoinsAtPlayer(map, player, blockDefs);
            if (collectedNow > 0) {
                coinCount += collectedNow;
#if HAS_SDL_MIXER
                if (audioReady && coinSfx) Mix_PlayChannel(-1, coinSfx, 0);
#endif
            }

            if (playerTouchesTileId(map, player, 30, 68)) {
                int currentLevelId = parseLevelIndexFromPath(levelPath);
                int nextLevelId = NextLevelId(currentLevelId);
                std::string nextPath = LevelPathFromId(nextLevelId);
                if (!nextPath.empty()) {
                    levelPath = nextPath;
                    reloadLevel();
                    continue;
                }
            }
        }

RENDER_ONLY:

        float camX = player.x + player.w * 0.5f - screenW * 0.5f;
        float camY = player.y + player.h * 0.5f - screenH * 0.5f;
        float maxCamX = map.w * map.tileSize - screenW - map.tileSize;
        float maxCamY = map.h * map.tileSize - screenH;
        if (clampCamX) camX = std::clamp(camX, (float)map.tileSize, std::max((float)map.tileSize, maxCamX));
        camY = std::clamp(camY, (float)map.tileSize, std::max((float)map.tileSize, maxCamY));

        SDL_SetRenderTarget(ren, gameTarget);
        SDL_SetRenderDrawColor(ren, 12, 14, 18, 255);
        SDL_RenderClear(ren);

        // Parallax background (3 layers)
        if (bgTex && !bgFrameByName.empty()) {
            auto getBg = [&](const std::string& name) -> const Frame* {
                auto it = bgFrameByName.find(name);
                if (it != bgFrameByName.end()) return &it->second;
                if (name.size() > 4 && name.substr(name.size() - 4) == ".png") {
                    std::string noExt = name.substr(0, name.size() - 4);
                    it = bgFrameByName.find(noExt);
                    if (it != bgFrameByName.end()) return &it->second;
                }
                return nullptr;
            };
            struct Layer { const char* frame; float factor; float vfactor; };
            Layer layers[3] = {
                {"w1b.png", 0.5f, 0.5f},
                {"w1f.png", 0.5f, 0.5f},
                {"w1f.png", 0.7f, 0.7f}
            };
            for (const auto& layer : layers) {
                const Frame* f = getBg(layer.frame);
                if (!f) continue;
                int fw = f->rotated ? f->rect.h : f->rect.w;
                int fh = f->rotated ? f->rect.w : f->rect.h;
                if (fw <= 0 || fh <= 0) continue;
                float ox = std::fmod(camX * layer.factor, (float)fw);
                if (ox < 0) ox += fw;
                float maxCamY = std::max(1.0f, (float)(map.h * map.tileSize - screenH));
                float parallaxCamY = maxCamY - camY;
                float t = std::clamp((parallaxCamY / maxCamY) * layer.vfactor, 0.0f, 1.0f);
                float yF = (float)screenH - (float)fh + t * (float)fh;
                int y = (int)std::lround(yF);
                for (int x = -1; x <= screenW / fw + 1; ++x) {
                    SDL_Rect dst{
                        (int)(x * fw - ox),
                        y,
                        fw,
                        fh
                    };
                    renderFrame(ren, bgTex, *f, dst);
                }
            }
        }

        map.renderBgDebug(ren, camX, camY);

        // Tile textures (DF_Blocks)
        if (blocksTex && !blocksFrameList.empty()) {
            for (int y = 0; y < map.h; y++) {
                if (y == 0) continue; // Hide top tile-grid layer.
                for (int x = 0; x < map.w; x++) {
                    int idx = y * map.w + x;
                    unsigned short id = map.tileIds[idx];
                    if (id == 0) continue;

                    const Frame* frame = nullptr;
                    std::string frameKey = std::to_string(id);
                    if (id == 24) {
                        // 24-step cycle: c1..c8, each shown for 3 steps.
                        const int step = (int)((SDL_GetTicks() / 100) % 24);
                        const int cIndex = (step / 3) + 1; // 1..8
                        frameKey = std::string("c") + std::to_string(cIndex);
                    }
                    auto it = blocksFrameByName.find(frameKey);
                    if (it != blocksFrameByName.end()) {
                        frame = &it->second;
                    }

                    if (!frame) continue;
                    SDL_Rect dst{(int)(x * map.tileSize - camX),
                                 (int)(y * map.tileSize - camY),
                                 map.tileSize, map.tileSize};
                    renderFrame(ren, blocksTex, *frame, dst);
                }
            }
        } else {
            map.renderWater(ren, camX, camY);
            map.renderSemiSolid(ren, camX, camY);
            map.renderSolid(ren, camX, camY);
        }

        std::string debugAnimName = "IDLE";
        std::string renderFrameName;
        switch (player.anim) {
            case 1: debugAnimName = "WALK"; break;
            case 2: debugAnimName = "JUMP"; break;
            case 3: debugAnimName = "FALL"; break;
            case 4: debugAnimName = "CROUCH"; break;
            case 5: debugAnimName = "SKID"; break;
            case 6: debugAnimName = "HURT"; break;
            case 7: debugAnimName = "DEATH"; break;
            default: debugAnimName = "IDLE"; break;
        }

        if (showHitboxes) {
            // Tile hitboxes
            for (int y = 0; y < map.h; y++) {
                for (int x = 0; x < map.w; x++) {
                    int idx = y * map.w + x;
                    bool isSolid = map.solid[idx] != 0;
                    bool isSemi = map.semisolid[idx] != 0;
                    bool isWater = map.water[idx] != 0;
                    int id = (int)map.tileIds[idx];
                    bool isAirDebug = (!isSolid && !isSemi && !isWater && id != 2);
                    if (!isSolid && !isSemi && !isWater && !isAirDebug) continue;
                    SDL_FRect rc{ x * map.tileSize - camX, y * map.tileSize - camY,
                                 (float)map.tileSize, (float)map.tileSize };
                    if (isSolid) SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
                    else if (isSemi) SDL_SetRenderDrawColor(ren, 120, 220, 255, 255);
                    else if (isWater) SDL_SetRenderDrawColor(ren, 60, 120, 220, 255);
                    else SDL_SetRenderDrawColor(ren, 180, 180, 180, 255);
                    SDL_RenderDrawRectF(ren, &rc);
                }
            }

            // Player hitbox
            SDL_SetRenderDrawColor(ren, 255, 200, 80, 255);
            float playerHitboxScreenY = player.y - camY;
            if (playerHitboxScreenY < 0.0f) playerHitboxScreenY = 0.0f;
            SDL_FRect pr{ player.x - camX, playerHitboxScreenY, (float)player.w, (float)player.h };
            SDL_RenderDrawRectF(ren, &pr);
        }


        SDL_SetRenderDrawColor(ren, 120, 220, 120, 255);
        for (const auto& obj : objects) {
            float ox = obj.x - 8.0f;
            float oy = obj.y - 8.0f;
            SDL_FRect orc{ ox - camX, oy - camY, 16.0f, 16.0f };
            SDL_RenderDrawRectF(ren, &orc);
        }

        SDL_FRect pr{ player.x - camX, player.y - camY, (float)player.w, (float)player.h };
        {
            enum PlayerAnim {
                ANIM_IDLE,
                ANIM_WALK,
                ANIM_JUMP,
                ANIM_FALL,
                ANIM_CROUCH,
                ANIM_SKID,
                ANIM_HURT,
                ANIM_DEATH
            };

            auto framesFromNames = [&](const std::vector<std::string>& names) {
                std::vector<const Frame*> out;
                out.reserve(names.size());
                for (const auto& n : names) out.push_back(getPlayerFrame(n));
                return out;
            };

            const std::vector<const Frame*> idle = framesFromNames(animCfg.idle);
            const std::vector<const Frame*> walk = framesFromNames(animCfg.walk);
            const std::vector<const Frame*> jump = framesFromNames(animCfg.jump);
            const std::vector<const Frame*> fall = framesFromNames(animCfg.fall);
            const std::vector<const Frame*> crouch = framesFromNames(animCfg.crouch);
            const std::vector<const Frame*> skid = framesFromNames(animCfg.skid);
            const std::vector<const Frame*> hurt = framesFromNames(animCfg.hurt);
            const std::vector<const Frame*> death = framesFromNames(animCfg.death);

            int newAnim = ANIM_IDLE;
            float vxAbs = std::abs(player.vx);
            if (player.freeMove) {
                newAnim = ANIM_IDLE;
            } else if (!player.onGround) {
                newAnim = (player.vy < 0.0f) ? ANIM_JUMP : ANIM_FALL;
            } else if (inputDown) {
                newAnim = ANIM_CROUCH;
            } else if (vxAbs > 8.0f) {
                bool opposing = (player.vx > 20.0f && inputMove < -0.1f) || (player.vx < -20.0f && inputMove > 0.1f);
                newAnim = opposing ? ANIM_SKID : ANIM_WALK;
            }

            if (inputMove < -0.1f) player.facing = -1;
            if (inputMove > 0.1f) player.facing = 1;

            if (newAnim != player.anim) {
                player.anim = newAnim;
                player.animTime = 0.0f;
            } else {
                player.animTime += dt;
            }

            const std::vector<const Frame*>* seq = &idle;
            switch (player.anim) {
                case ANIM_WALK: seq = &walk; break;
                case ANIM_JUMP: seq = &jump; break;
                case ANIM_FALL: seq = &fall; break;
                case ANIM_CROUCH: seq = &crouch; break;
                case ANIM_SKID: seq = &skid; break;
                case ANIM_HURT: seq = &hurt; break;
                case ANIM_DEATH: seq = &death; break;
                default: seq = &idle; break;
            }

            const Frame* renderFramePtr = nullptr;
            int frameCount = (int)seq->size();
            if (frameCount > 0) {
                float fps = animCfg.fps > 0.0f ? animCfg.fps : 15.0f;
                int idx = (int)(player.animTime * fps) % frameCount;
                renderFramePtr = (*seq)[idx];
                if (player.anim == ANIM_IDLE && idx < (int)animCfg.idle.size()) {
                    renderFrameName = animCfg.idle[idx];
                } else if (player.anim == ANIM_WALK && idx < (int)animCfg.walk.size()) {
                    renderFrameName = animCfg.walk[idx];
                } else if (player.anim == ANIM_JUMP && idx < (int)animCfg.jump.size()) {
                    renderFrameName = animCfg.jump[idx];
                } else if (player.anim == ANIM_FALL && idx < (int)animCfg.fall.size()) {
                    renderFrameName = animCfg.fall[idx];
                } else if (player.anim == ANIM_CROUCH && idx < (int)animCfg.crouch.size()) {
                    renderFrameName = animCfg.crouch[idx];
                } else if (player.anim == ANIM_SKID && idx < (int)animCfg.skid.size()) {
                    renderFrameName = animCfg.skid[idx];
                } else if (player.anim == ANIM_HURT && idx < (int)animCfg.hurt.size()) {
                    renderFrameName = animCfg.hurt[idx];
                } else if (player.anim == ANIM_DEATH && idx < (int)animCfg.death.size()) {
                    renderFrameName = animCfg.death[idx];
                }
            }

            if (playerTex && renderFramePtr) {
                SDL_Rect dst{
                    (int)pr.x,
                    (int)(pr.y + pr.h - pr.h),
                    (int)pr.w,
                    (int)pr.h
                };
                SDL_RendererFlip flip = (player.facing < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                renderFrameEx(ren, playerTex, *renderFramePtr, dst, flip);
            } else {
                SDL_SetRenderDrawColor(ren, 200, 200, 230, 255);
                SDL_RenderFillRectF(ren, &pr);
                SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                SDL_RenderDrawRectF(ren, &pr);
            }
        }

        if (paused) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 10, 10, 14, 180);
            SDL_Rect overlay{0, 0, screenW, screenH};
            SDL_RenderFillRect(ren, &overlay);

            bool hasPanel = pauseTex && pauseFrames.count("Panel") != 0;
            bool hasContinue = pauseTex && pauseFrames.count("Continuebtn") != 0;
            bool hasRestart = pauseTex && pauseFrames.count("Retartbtn") != 0;
            bool hasExit = pauseTex && pauseFrames.count("exitbtn") != 0;

            if (hasPanel && hasContinue && hasRestart && hasExit) {
                const Frame& panelFrame = pauseFrames["Panel"];
                SDL_Rect panelDst{screenW / 2 - panelFrame.rect.w / 2,
                                  screenH / 2 - panelFrame.rect.h / 2,
                                  panelFrame.rect.w, panelFrame.rect.h};
                renderFrame(ren, pauseTex, panelFrame, panelDst);

                const Frame& continueFrame = pauseFrames["Continuebtn"];
                const Frame& restartFrame = pauseFrames["Retartbtn"];
                const Frame& exitFrame = pauseFrames["exitbtn"];

                int contW = continueFrame.rotated ? continueFrame.rect.h : continueFrame.rect.w;
                int contH = continueFrame.rotated ? continueFrame.rect.w : continueFrame.rect.h;
                int restartW = restartFrame.rotated ? restartFrame.rect.h : restartFrame.rect.w;
                int restartH = restartFrame.rotated ? restartFrame.rect.w : restartFrame.rect.h;
                int exitW = exitFrame.rotated ? exitFrame.rect.h : exitFrame.rect.w;
                int exitH = exitFrame.rotated ? exitFrame.rect.w : exitFrame.rect.h;

                int spacing = 18;
                int totalW = contW + restartW + exitW + spacing * 2;
                int startX = screenW / 2 - totalW / 2;
                int centerY = screenH / 2 + 30;

                SDL_Rect contDst{startX, centerY - contH / 2, contW, contH};
                SDL_Rect restartDst{startX + contW + spacing, centerY - restartH / 2, restartW, restartH};
                SDL_Rect exitDst{startX + contW + spacing + restartW + spacing, centerY - exitH / 2, exitW, exitH};
                pauseBtnContinue = contDst;
                pauseBtnRestart = restartDst;
                pauseBtnExit = exitDst;

                renderFrame(ren, pauseTex, continueFrame, contDst);
                renderFrame(ren, pauseTex, restartFrame, restartDst);
                renderFrame(ren, pauseTex, exitFrame, exitDst);

                SDL_SetRenderDrawColor(ren, 255, 255, 255, 200);
                SDL_Rect highlight = (pauseSelection == 0) ? contDst : (pauseSelection == 1 ? restartDst : exitDst);
                SDL_RenderDrawRect(ren, &highlight);
            } else {
                SDL_Rect panel{screenW / 2 - 140, screenH / 2 - 90, 280, 180};
                SDL_SetRenderDrawColor(ren, 30, 30, 38, 230);
                SDL_RenderFillRect(ren, &panel);
                SDL_SetRenderDrawColor(ren, 80, 90, 110, 255);
                SDL_RenderDrawRect(ren, &panel);

                SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
                DrawText(ren, screenW / 2 - 60, screenH / 2 - 70, 3, "PAUSED");

                SDL_Rect resumeBtn{screenW / 2 - 140, screenH / 2 + 10, 100, 36};
                SDL_Rect restartBtn{screenW / 2 - 50, screenH / 2 + 10, 100, 36};
                SDL_Rect quitBtn{screenW / 2 + 40, screenH / 2 + 10, 100, 36};
                pauseBtnContinue = resumeBtn;
                pauseBtnRestart = restartBtn;
                pauseBtnExit = quitBtn;

                SDL_SetRenderDrawColor(ren, pauseSelection == 0 ? 70 : 45, pauseSelection == 0 ? 120 : 70, pauseSelection == 0 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &resumeBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, resumeBtn.x + 35, resumeBtn.y + 8, 2, "RESUME");

                SDL_SetRenderDrawColor(ren, pauseSelection == 1 ? 70 : 45, pauseSelection == 1 ? 120 : 70, pauseSelection == 1 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &restartBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, restartBtn.x + 18, restartBtn.y + 8, 2, "RESTART");

                SDL_SetRenderDrawColor(ren, pauseSelection == 2 ? 120 : 70, pauseSelection == 2 ? 70 : 50, pauseSelection == 2 ? 70 : 60, 255);
                SDL_RenderFillRect(ren, &quitBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, quitBtn.x + 30, quitBtn.y + 8, 2, "QUIT");
            }

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        if (showHitboxes) {
            // Debug UI (highest render priority)
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
            SDL_Rect dbgPanel{10, 10, 340, 196};
            SDL_RenderFillRect(ren, &dbgPanel);
            SDL_SetRenderDrawColor(ren, 80, 90, 110, 220);
            SDL_RenderDrawRect(ren, &dbgPanel);

            SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
            DrawText(ren, 18, 18, 2, "DEBUG");
            DrawDebugNumber(ren, 18, 36, 2, "PX", (int)player.x);
            DrawDebugNumber(ren, 18, 50, 2, "PY", (int)player.y);
            DrawDebugNumber(ren, 18, 64, 2, "VX", (int)player.vx);
            DrawDebugNumber(ren, 18, 78, 2, "VY", (int)player.vy);
            DrawDebugNumber(ren, 140, 36, 2, "CAMX", (int)camX);
            DrawDebugNumber(ren, 140, 50, 2, "CAMY", (int)camY);
            DrawDebugNumber(ren, 140, 64, 2, "WTR", player.inWater ? 1 : 0);
            DrawDebugNumber(ren, 140, 78, 2, "DRN", (int)(45.0f - player.drownTimer));
            float maxCamX = std::max(0.0f, (float)(map.h * map.tileSize - screenW));
            float maxCamY = std::max(0.0f, (float)(map.w * map.tileSize - screenH));
            DrawDebugNumber(ren, 18, 92, 2, "BW", map.w);
            DrawDebugNumber(ren, 140, 92, 2, "BH", map.h);
            DrawDebugNumber(ren, 18, 106, 2, "CMINX", 0);
            DrawDebugNumber(ren, 140, 106, 2, "CMAXX", (int)maxCamX);
            DrawDebugNumber(ren, 18, 120, 2, "CMINY", 0);
            DrawDebugNumber(ren, 140, 120, 2, "CMAXY", (int)maxCamY);
            int fps = (dt > 0.0f) ? (int)(1.0f / dt) : 0;
            DrawDebugNumber(ren, 18, 134, 2, "FPS", fps);
            DrawText(ren, 18, 148, 2, std::string("ANIM ") + debugAnimName);
            if (!renderFrameName.empty()) {
                std::string id = renderFrameName;
                if (id.size() > 4 && id.substr(id.size() - 4) == ".png") id.resize(id.size() - 4);
                DrawText(ren, 18, 162, 2, std::string("ID ") + id);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            // Camera bounds (level extents)
            SDL_SetRenderDrawColor(ren, 40, 200, 140, 255);
            SDL_FRect bounds{
                -camX,
                -camY,
                (float)(map.w * map.tileSize),
                (float)(map.h * map.tileSize)
            };
            SDL_RenderDrawRectF(ren, &bounds);

            // Tile IDs (debug)
            SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
            for (int y = 0; y < map.h; y++) {
                for (int x = 0; x < map.w; x++) {
                    int idx = y * map.w + x;
                    int id = (int)map.tileIds[idx];
                    int screenX = (int)(x * map.tileSize - camX);
                    int screenY = (int)(y * map.tileSize - camY);
                    int centerX = screenX + map.tileSize / 2;
                    int centerY = screenY + map.tileSize / 2;
                    DrawNumberCentered(ren, centerX, centerY - (5 * 2) / 2, 2, id);
                }
            }
        }

        if (!paused) {
            bool showMobileUi = (SDL_GetNumTouchDevices() > 0) || !activeTouches.empty();
            if (showMobileUi) {
                auto drawTouchBtn = [&](const SDL_FRect& r, bool active) {
                    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ren, active ? 180 : 120, active ? 180 : 120, active ? 180 : 120, active ? 120 : 80);
                    SDL_RenderFillRectF(ren, &r);
                    SDL_SetRenderDrawColor(ren, 230, 230, 230, 180);
                    SDL_RenderDrawRectF(ren, &r);
                };
                drawTouchBtn(touchLeftBtn, touchLeft);
                drawTouchBtn(touchRightBtn, touchRight);
                drawTouchBtn(touchDownBtn, touchDown);
                drawTouchBtn(touchJumpBtn, touchJump);
                SDL_SetRenderDrawColor(ren, 240, 240, 240, 220);
                int touchLabelScale = std::clamp((int)std::lround(uiSize / 44.0f), 2, 4);
                auto drawBtnLabel = [&](const SDL_FRect& btn, const std::string& text) {
                    int tw = MeasureTextWidth(touchLabelScale, text);
                    int tx = (int)std::lround(btn.x + (btn.w - tw) * 0.5f);
                    int ty = (int)std::lround(btn.y + (btn.h * 0.5f) - (10.0f * touchLabelScale));
                    DrawText(ren, tx, ty, touchLabelScale, text);
                };
                drawBtnLabel(touchLeftBtn, "L");
                drawBtnLabel(touchRightBtn, "R");
                drawBtnLabel(touchDownBtn, "DN");
                drawBtnLabel(touchJumpBtn, "JMP");
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            }
        }

        SDL_SetRenderTarget(ren, nullptr);
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Rect presentDst = computePresentRect(winW, winH, kBaseScreenW, kBaseScreenH);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, gameTarget, nullptr, &presentDst);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
        }
        if (!running) break;
        if (audioReady) {
#if HAS_SDL_MIXER
            if (levelMusic) {
            Mix_HaltMusic();
            Mix_FreeMusic(levelMusic);
            levelMusic = nullptr;
            }
#endif
        }
        if (returnToSelect) continue;
    }

    if (blocksTex) SDL_DestroyTexture(blocksTex);
    if (pauseTex) SDL_DestroyTexture(pauseTex);
    if (gameTarget) SDL_DestroyTexture(gameTarget);
    ShutdownTextRenderer();
    if (audioReady) {
#if HAS_SDL_MIXER
        Mix_HaltMusic();
        Mix_CloseAudio();
        Mix_Quit();
#endif
    }
#if HAS_SDL_MIXER
    if (coinSfx) Mix_FreeChunk(coinSfx);
#endif
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Log("Shutting down");
    IMG_Quit();
    SDL_Quit();
    return 0;
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_com_Benno111_dorfplatformertimetravel_MainActivity_runNativeGame(JNIEnv*, jobject) {
    char arg0[] = "platformer";
    char* argv[] = { arg0, nullptr };
    main(1, argv);
}
#endif
