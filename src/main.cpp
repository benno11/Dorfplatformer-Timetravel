#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cctype>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <filesystem>
#include "TileMap.h"
#include "LevelLoader.h"

struct Player {
    float x = 64.0f;
    float y = 64.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    int w = 20;
    int h = 28;
    bool onGround = false;
    bool jumpHeld = false;
    bool jumpWasDown = false;
    float jumpHoldTime = 0.0f;
    bool inWater = false;
    float drownTimer = 0.0f;
    bool freeMove = false;
    int facing = 1; // 1 = right, -1 = left
    int anim = 0;
    float animTime = 0.0f;
};

static std::vector<std::string> loadLevelList() {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    fs::path dir("assets/levels");
    if (!fs::exists(dir)) return out;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        if (p.extension() == ".txt") out.push_back(p.filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

static std::string runLevelSelect(SDL_Window* win, SDL_Renderer* ren) {
    std::vector<std::string> levels = loadLevelList();
    if (levels.empty()) return "";

    const int rowH = 28;
    const int pad = 16;
    int selected = 0;
    int scrollY = 0;
    bool running = true;
    bool chosen = false;

    int winW = 960, winH = 540;
    SDL_GetWindowSize(win, &winW, &winH);

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (e.type == SDL_MOUSEWHEEL) {
                scrollY -= e.wheel.y * rowH;
                int maxScroll = std::max(0, (int)levels.size() * rowH - (winH - pad * 2));
                if (scrollY < 0) scrollY = 0;
                if (scrollY > maxScroll) scrollY = maxScroll;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int y = e.button.y - pad + scrollY;
                if (y >= 0) {
                    int idx = y / rowH;
                    if (idx >= 0 && idx < (int)levels.size()) {
                        selected = idx;
                        if (e.button.clicks >= 2) {
                            chosen = true;
                            running = false;
                        }
                    }
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                    break;
                }
                if (e.key.keysym.sym == SDLK_DOWN) {
                    selected = std::min(selected + 1, (int)levels.size() - 1);
                }
                if (e.key.keysym.sym == SDLK_UP) {
                    selected = std::max(selected - 1, 0);
                }
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    chosen = true;
                    running = false;
                }
            }
        }

        std::string title = "Select Level: " + levels[selected] + " (Enter or double-click)";
        SDL_SetWindowTitle(win, title.c_str());

        SDL_SetRenderDrawColor(ren, 18, 18, 22, 255);
        SDL_RenderClear(ren);

        for (int i = 0; i < (int)levels.size(); ++i) {
            int y = pad + i * rowH - scrollY;
            if (y + rowH < 0 || y > winH) continue;
            if (i == selected) {
                SDL_SetRenderDrawColor(ren, 60, 90, 140, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 40, 50, 70, 255);
            }
            SDL_Rect r{pad, y, winW - pad * 2, rowH - 4};
            SDL_RenderFillRect(ren, &r);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (!chosen) return "";
    return "assets/levels/" + levels[selected];
}

static bool rectHitsSolid(const TileMap& map, float x, float y, int w, int h) {
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int top = (int)std::floor(y / t);
    int bottom = (int)std::floor((y + h - 1) / t);
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            if (map.getSolid(tx, ty)) return true;
        }
    }
    return false;
}

static bool rectHitsSemiSolidDown(const TileMap& map, float oldY, float newY, float x, int w, int h) {
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int oldBottom = (int)std::floor((oldY + h - 1) / t);
    int newBottom = (int)std::floor((newY + h - 1) / t);
    if (newBottom < oldBottom) return false;

    int ty = newBottom;
    for (int tx = left; tx <= right; ++tx) {
        if (!map.getSemiSolid(tx, ty)) continue;
        float tileTop = ty * t;
        float oldBottomY = oldY + h - 1;
        float newBottomY = newY + h - 1;
        if (oldBottomY <= tileTop && newBottomY >= tileTop) return true;
    }
    return false;
}

static bool rectHitsWater(const TileMap& map, float x, float y, int w, int h) {
    int t = map.tileSize;
    int left = (int)std::floor(x / t);
    int right = (int)std::floor((x + w - 1) / t);
    int top = (int)std::floor(y / t);
    int bottom = (int)std::floor((y + h - 1) / t);
    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            if (map.getWater(tx, ty)) return true;
        }
    }
    return false;
}

static std::vector<char> loadBlockDefs(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
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

static void drawDigit3x5(SDL_Renderer* ren, int x, int y, int scale, int digit) {
    static const char* kDigits[10][5] = {
        {"111","101","101","101","111"}, // 0
        {"010","110","010","010","111"}, // 1
        {"111","001","111","100","111"}, // 2
        {"111","001","111","001","111"}, // 3
        {"101","101","111","001","001"}, // 4
        {"111","100","111","001","111"}, // 5
        {"111","100","111","101","111"}, // 6
        {"111","001","001","001","001"}, // 7
        {"111","101","111","101","111"}, // 8
        {"111","101","111","001","111"}, // 9
    };

    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (kDigits[digit][row][col] != '1') continue;
            SDL_Rect r{ x + col * scale, y + row * scale, scale, scale };
            SDL_RenderFillRect(ren, &r);
        }
    }
}

static void drawNumber3x5(SDL_Renderer* ren, int x, int y, int scale, int value) {
    if (value < 0) value = -value;
    int digits[10];
    int count = 0;
    if (value == 0) {
        digits[count++] = 0;
    } else {
        while (value > 0 && count < 10) {
            digits[count++] = value % 10;
            value /= 10;
        }
    }

    int digitW = 3 * scale;
    int spacing = scale;
    int totalW = count * digitW + (count - 1) * spacing;
    int startX = x - totalW / 2;
    for (int i = count - 1; i >= 0; --i) {
        drawDigit3x5(ren, startX, y, scale, digits[i]);
        startX += digitW + spacing;
    }
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
    std::ifstream in(plistPath);
    if (!in) return frames;

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
    std::ifstream in(plistPath);
    if (!in) return frames;

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
    SDL_RenderCopyEx(ren, tex, &src, &dstRot, 90.0, &center, SDL_FLIP_NONE);
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
    SDL_RenderCopyEx(ren, tex, &src, &dstRot, 90.0, &center, flip);
}

static void drawChar5x7(SDL_Renderer* ren, int x, int y, int scale, char ch) {
    const char* rows[7] = { "00000","00000","00000","00000","00000","00000","00000" };
    switch (ch) {
        case 'A': {
            static const char* r[7] = {"01110","10001","10001","11111","10001","10001","10001"};
            std::copy(r, r + 7, rows);
        } break;
        case 'D': {
            static const char* r[7] = {"11110","10001","10001","10001","10001","10001","11110"};
            std::copy(r, r + 7, rows);
        } break;
        case 'E': {
            static const char* r[7] = {"11111","10000","10000","11110","10000","10000","11111"};
            std::copy(r, r + 7, rows);
        } break;
        case 'I': {
            static const char* r[7] = {"11111","00100","00100","00100","00100","00100","11111"};
            std::copy(r, r + 7, rows);
        } break;
        case 'M': {
            static const char* r[7] = {"10001","11011","10101","10001","10001","10001","10001"};
            std::copy(r, r + 7, rows);
        } break;
        case 'P': {
            static const char* r[7] = {"11110","10001","10001","11110","10000","10000","10000"};
            std::copy(r, r + 7, rows);
        } break;
        case 'Q': {
            static const char* r[7] = {"01110","10001","10001","10001","10101","10010","01101"};
            std::copy(r, r + 7, rows);
        } break;
        case 'R': {
            static const char* r[7] = {"11110","10001","10001","11110","10100","10010","10001"};
            std::copy(r, r + 7, rows);
        } break;
        case 'S': {
            static const char* r[7] = {"01111","10000","10000","01110","00001","00001","11110"};
            std::copy(r, r + 7, rows);
        } break;
        case 'T': {
            static const char* r[7] = {"11111","00100","00100","00100","00100","00100","00100"};
            std::copy(r, r + 7, rows);
        } break;
        case 'U': {
            static const char* r[7] = {"10001","10001","10001","10001","10001","10001","01110"};
            std::copy(r, r + 7, rows);
        } break;
        default:
            break;
    }

    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (rows[row][col] != '1') continue;
            SDL_Rect r{ x + col * scale, y + row * scale, scale, scale };
            SDL_RenderFillRect(ren, &r);
        }
    }
}

static void drawText5x7(SDL_Renderer* ren, int x, int y, int scale, const std::string& text) {
    int spacing = scale;
    int charW = 5 * scale;
    int cursorX = x;
    for (char ch : text) {
        if (ch == ' ') {
            cursorX += charW + spacing;
            continue;
        }
        drawChar5x7(ren, cursorX, y, scale, (char)std::toupper((unsigned char)ch));
        cursorX += charW + spacing;
    }
}

static void drawDebugNumber(SDL_Renderer* ren, int x, int y, int scale, const std::string& label, int value) {
    drawText5x7(ren, x, y, scale, label);
    drawNumber3x5(ren, x + (int)label.size() * (5 * scale + scale) + 6, y + scale, scale, value);
}

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Log("SDL_Init completed");
    IMG_Init(IMG_INIT_PNG);

    SDL_Window* win = SDL_CreateWindow(
        "Dorfplatformer Timetravel",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 540,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Log("Window and renderer created");

    nlohmann::json texJson;
    {
        std::ifstream tin("assets/textures.json");
        if (tin) {
            try { tin >> texJson; } catch (...) { texJson = nlohmann::json(); }
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
    SDL_Texture* pauseTex = IMG_LoadTexture(ren, texPath("textures", "pause", "assets/Sheets/DF_Pause-uhd.png").c_str());
    auto pauseFrames = loadPlistFrames(pausePlist);

    std::string blocksPlist = texPath("plists", "blocks", "assets/Sheets/DF_Blocks-uhd.plist");
    SDL_Texture* blocksTex = IMG_LoadTexture(ren, texPath("textures", "blocks", "assets/Sheets/DF_Blocks-uhd.png").c_str());
    auto blocksFrameList = loadPlistFrameList(blocksPlist);
    std::unordered_map<std::string, Frame> blocksFrameByName;
    blocksFrameByName.reserve(blocksFrameList.size());
    for (const auto& e : blocksFrameList) blocksFrameByName[e.name] = e.frame;

    std::string playerPlist = texPath("plists", "player", "assets/Sheets/DF_Player1-uhd.plist");
    SDL_Texture* playerTex = IMG_LoadTexture(ren, texPath("textures", "player", "assets/Sheets/DF_Player1-uhd.png").c_str());
    auto playerFrameList = loadPlistFrameList(playerPlist);
    std::unordered_map<std::string, Frame> playerFramesByName;
    playerFramesByName.reserve(playerFrameList.size());
    for (const auto& e : playerFrameList) playerFramesByName[e.name] = e.frame;
    const Frame* fallbackPlayerFrame = !playerFrameList.empty() ? &playerFrameList[0].frame : nullptr;

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
        std::ifstream in(path);
        if (!in) return cfg;
        nlohmann::json j;
        try {
            in >> j;
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
        SDL_Quit();
        return 1;
    }

    std::string levelPath = runLevelSelect(win, ren);
    if (levelPath.empty()) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    TileMap map;
    std::vector<ObjectInstance> objects;
    LevelMeta meta;
    Player player;
    std::vector<char> blockDefs = loadBlockDefs("assets/blockdefined.txt");

    auto reloadLevel = [&]() {
        loadLevelBNNLVL(levelPath, map, objects, meta);

        player = Player{};
        for (const auto& o : objects) {
            if (o.id == "player") {
                player.x = o.x;
                player.y = o.y;
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
    };

    reloadLevel();

    bool running = true;
    bool fullscreen = false;
    bool paused = false;
    bool showHitboxes = false;
    bool clampCamX = true;
    int pauseSelection = 0; // 0 = Resume, 1 = Restart, 2 = Quit
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
            running = false;
        }
    };

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastTicks = now;

        float inputMove = 0.0f;
        bool inputDown = false;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
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
                SDL_Point pt{e.button.x, e.button.y};
                if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
            }
            if (paused && e.type == SDL_FINGERDOWN) {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                SDL_Point pt{(int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH)};
                if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
            }
        }

        if (!paused) {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            bool shiftDown = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
            player.freeMove = shiftDown;
            if (player.freeMove) {
                float freeSpeed = 420.0f;
                float dx = 0.0f;
                float dy = 0.0f;
                if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) dx -= 1.0f;
                if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
                if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) dy -= 1.0f;
                if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) dy += 1.0f;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.0f) { dx /= len; dy /= len; }
                player.x += dx * freeSpeed * dt;
                player.y += dy * freeSpeed * dt;
                player.vx = 0.0f;
                player.vy = 0.0f;
                player.onGround = false;
                player.jumpHeld = false;
                player.jumpHoldTime = 0.0f;
                player.jumpWasDown = false;
            }

            if (player.freeMove) {
                goto RENDER_ONLY;
            }
            float move = 0.0f;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) move -= 1.0f;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move += 1.0f;
            bool downHeld = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
            inputMove = move;
            inputDown = downHeld;

            bool inWater = rectHitsWater(map, player.x, player.y, player.w, player.h);
            player.inWater = inWater;

            float accel = inWater ? 900.0f : 1800.0f;
            float maxSpeed = inWater ? 180.0f : 260.0f;
            float friction = inWater ? 900.0f : 1600.0f;
            float gravity = inWater ? 900.0f : 2000.0f;
            float jumpSpeed = 900.0f;
            float jumpHoldGravity = 300.0f; // lower gravity while holding jump
            float jumpHoldMax = 0.42f;      // seconds of extra lift
            float jumpCutSpeed = 220.0f;    // cap upward speed on early release
            float swimUpSpeed = 380.0f;
            float swimRise = 900.0f;

            if (move != 0.0f) {
                player.vx += move * accel * dt;
                if (player.vx > maxSpeed) player.vx = maxSpeed;
                if (player.vx < -maxSpeed) player.vx = -maxSpeed;
            } else {
                if (player.vx > 0.0f) {
                    player.vx = std::max(0.0f, player.vx - friction * dt);
                } else if (player.vx < 0.0f) {
                    player.vx = std::min(0.0f, player.vx + friction * dt);
                }
            }

            bool jumpDown = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
            bool jumpPressed = jumpDown && !player.jumpWasDown;
            bool jumpReleased = !jumpDown && player.jumpWasDown;

            if (player.onGround && jumpPressed) {
                player.vy = -jumpSpeed;
                player.onGround = false;
                player.jumpHeld = true;
                player.jumpHoldTime = 0.0f;
            }

            if (!inWater && player.jumpHeld && jumpDown && player.jumpHoldTime < jumpHoldMax) {
                player.vy += jumpHoldGravity * dt;
                player.jumpHoldTime += dt;
            } else {
                player.jumpHeld = false;
            }

            if (jumpReleased && player.vy < -jumpCutSpeed) {
                player.vy = -jumpCutSpeed;
            }

            if (inWater) {
                if (jumpDown) {
                    player.vy -= swimRise * dt;
                    if (player.vy < -swimUpSpeed) player.vy = -swimUpSpeed;
                }
                if (player.vy > 420.0f) player.vy = 420.0f;
            }

            player.vy += gravity * dt;
            player.jumpWasDown = jumpDown;

            if (inWater) {
                player.drownTimer += dt;
                if (player.drownTimer >= 45.0f) {
                    reloadLevel();
                    player.drownTimer = 0.0f;
                }
            } else {
                player.drownTimer = 0.0f;
            }

            // Horizontal move
            float newX = player.x + player.vx * dt;
            if (!rectHitsSolid(map, newX, player.y, player.w, player.h)) {
                player.x = newX;
            } else {
                int dir = (player.vx > 0) ? 1 : -1;
                while (!rectHitsSolid(map, player.x + dir, player.y, player.w, player.h)) {
                    player.x += dir;
                }
                player.vx = 0.0f;
            }

            // Vertical move
            float newY = player.y + player.vy * dt;
            if (!rectHitsSolid(map, player.x, newY, player.w, player.h)) {
                if (player.vy > 0 && rectHitsSemiSolidDown(map, player.y, newY, player.x, player.w, player.h)) {
                    int t = map.tileSize;
                    int bottomTile = (int)std::floor((newY + player.h - 1) / t);
                    player.y = bottomTile * t - player.h + 1;
                    player.onGround = true;
                    player.vy = 0.0f;
                } else {
                    player.y = newY;
                    player.onGround = false;
                }
            } else {
                int dir = (player.vy > 0) ? 1 : -1;
                while (!rectHitsSolid(map, player.x, player.y + dir, player.w, player.h)) {
                    player.y += dir;
                }
                if (dir > 0) player.onGround = true;
                player.vy = 0.0f;
            }
        }

RENDER_ONLY:

        int screenW = 960;
        int screenH = 540;
        float camX = player.x + player.w * 0.5f - screenW * 0.5f;
        float camY = player.y + player.h * 0.5f - screenH * 0.5f;
        float maxCamX = map.w * map.tileSize - screenW;
        float maxCamY = map.h * map.tileSize - screenH;
        if (clampCamX) camX = std::clamp(camX, 0.0f, std::max(0.0f, maxCamX));
        camY = std::clamp(camY, 0.0f, std::max(0.0f, maxCamY));

        SDL_SetRenderDrawColor(ren, 12, 14, 18, 255);
        SDL_RenderClear(ren);

        map.renderBgDebug(ren, camX, camY);

        // Tile textures (DF_Blocks)
        if (blocksTex && !blocksFrameList.empty()) {
            for (int y = 0; y < map.h; y++) {
                for (int x = 0; x < map.w; x++) {
                    int idx = y * map.w + x;
                    unsigned short id = map.tileIds[idx];
                    if (id == 0) continue;

                    const Frame* frame = nullptr;
                    auto it = blocksFrameByName.find(std::to_string(id));
                    if (it != blocksFrameByName.end()) {
                        frame = &it->second;
                    } else if (id < blocksFrameList.size()) {
                        frame = &blocksFrameList[id].frame;
                    } else if (id > 0 && (id - 1) < blocksFrameList.size()) {
                        frame = &blocksFrameList[id - 1].frame;
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
                    if (!isSolid && !isSemi && !isWater) continue;
                    SDL_FRect rc{ x * map.tileSize - camX, y * map.tileSize - camY,
                                 (float)map.tileSize, (float)map.tileSize };
                    if (isSolid) SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
                    else if (isSemi) SDL_SetRenderDrawColor(ren, 120, 220, 255, 255);
                    else SDL_SetRenderDrawColor(ren, 60, 120, 220, 255);
                    SDL_RenderDrawRectF(ren, &rc);
                }
            }
        }

        if (showHitboxes) {
            // Debug UI
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
            SDL_Rect dbgPanel{10, 10, 280, 168};
            SDL_RenderFillRect(ren, &dbgPanel);
            SDL_SetRenderDrawColor(ren, 80, 90, 110, 220);
            SDL_RenderDrawRect(ren, &dbgPanel);

            SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
            drawText5x7(ren, 18, 18, 2, "DEBUG");
            drawDebugNumber(ren, 18, 36, 2, "PX", (int)player.x);
            drawDebugNumber(ren, 18, 50, 2, "PY", (int)player.y);
            drawDebugNumber(ren, 18, 64, 2, "VX", (int)player.vx);
            drawDebugNumber(ren, 18, 78, 2, "VY", (int)player.vy);
            drawDebugNumber(ren, 140, 36, 2, "CAMX", (int)camX);
            drawDebugNumber(ren, 140, 50, 2, "CAMY", (int)camY);
            drawDebugNumber(ren, 140, 64, 2, "WTR", player.inWater ? 1 : 0);
            drawDebugNumber(ren, 140, 78, 2, "DRN", (int)(45.0f - player.drownTimer));
            float maxCamX = std::max(0.0f, (float)(map.h * map.tileSize - screenW));
            float maxCamY = std::max(0.0f, (float)(map.w * map.tileSize - screenH));
            drawDebugNumber(ren, 18, 92, 2, "BW", map.w);
            drawDebugNumber(ren, 140, 92, 2, "BH", map.h);
            drawDebugNumber(ren, 18, 106, 2, "CMINX", 0);
            drawDebugNumber(ren, 140, 106, 2, "CMAXX", (int)maxCamX);
            drawDebugNumber(ren, 18, 120, 2, "CMINY", 0);
            drawDebugNumber(ren, 140, 120, 2, "CMAXY", (int)maxCamY);
            int fps = (dt > 0.0f) ? (int)(1.0f / dt) : 0;
            drawDebugNumber(ren, 18, 134, 2, "FPS", fps);
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
                    if (!map.solid[idx] && !map.semisolid[idx] && !map.water[idx]) continue;
                    int screenX = (int)(x * map.tileSize - camX);
                    int screenY = (int)(y * map.tileSize - camY);
                    int centerX = screenX + map.tileSize / 2;
                    int centerY = screenY + map.tileSize / 2;
                    drawNumber3x5(ren, centerX, centerY - (5 * 2) / 2, 2, id);
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 120, 220, 120, 255);
        for (const auto& obj : objects) {
            float ox = obj.x - 8.0f;
            float oy = obj.y - 8.0f;
            SDL_FRect orc{ ox - camX, oy - camY, 16.0f, 16.0f };
            SDL_RenderDrawRectF(ren, &orc);
        }

        SDL_FRect pr{ player.x - camX, player.y - camY, (float)player.w, (float)player.h };
        std::string renderFrameName;
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
                int pw = renderFramePtr->rotated ? renderFramePtr->rect.h : renderFramePtr->rect.w;
                int ph = renderFramePtr->rotated ? renderFramePtr->rect.w : renderFramePtr->rect.h;
                SDL_Rect dst{
                    (int)(pr.x + (pr.w - pw) * 0.5f),
                    (int)(pr.y + (pr.h - ph) * 0.5f),
                    pw,
                    ph
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

        // Debug text (top-left)
        {
            int fps = (dt > 0.0f) ? (int)(1.0f / dt) : 0;
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
            SDL_Rect dbg{8, 8, 220, 68};
            SDL_RenderFillRect(ren, &dbg);
            SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
            drawText5x7(ren, 14, 14, 2, std::string("FPS ") + std::to_string(fps));
            drawText5x7(ren, 14, 32, 2, std::string("ANIM ") + debugAnimName);
            if (!renderFrameName.empty()) {
                std::string id = renderFrameName;
                if (id.size() > 4 && id.substr(id.size() - 4) == ".png") id.resize(id.size() - 4);
                drawText5x7(ren, 14, 50, 2, std::string("ID ") + id);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
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
                drawText5x7(ren, screenW / 2 - 60, screenH / 2 - 70, 3, "PAUSED");

                SDL_Rect resumeBtn{screenW / 2 - 140, screenH / 2 + 10, 100, 36};
                SDL_Rect restartBtn{screenW / 2 - 50, screenH / 2 + 10, 100, 36};
                SDL_Rect quitBtn{screenW / 2 + 40, screenH / 2 + 10, 100, 36};
                pauseBtnContinue = resumeBtn;
                pauseBtnRestart = restartBtn;
                pauseBtnExit = quitBtn;

                SDL_SetRenderDrawColor(ren, pauseSelection == 0 ? 70 : 45, pauseSelection == 0 ? 120 : 70, pauseSelection == 0 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &resumeBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                drawText5x7(ren, resumeBtn.x + 35, resumeBtn.y + 8, 2, "RESUME");

                SDL_SetRenderDrawColor(ren, pauseSelection == 1 ? 70 : 45, pauseSelection == 1 ? 120 : 70, pauseSelection == 1 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &restartBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                drawText5x7(ren, restartBtn.x + 18, restartBtn.y + 8, 2, "RESTART");

                SDL_SetRenderDrawColor(ren, pauseSelection == 2 ? 120 : 70, pauseSelection == 2 ? 70 : 50, pauseSelection == 2 ? 70 : 60, 255);
                SDL_RenderFillRect(ren, &quitBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                drawText5x7(ren, quitBtn.x + 30, quitBtn.y + 8, 2, "QUIT");
            }

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (blocksTex) SDL_DestroyTexture(blocksTex);
    if (pauseTex) SDL_DestroyTexture(pauseTex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Log("Shutting down");
    IMG_Quit();
    SDL_Quit();
    return 0;
}
