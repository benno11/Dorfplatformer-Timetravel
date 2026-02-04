#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <fstream>
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

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Log("SDL_Init completed");

    SDL_Window* win = SDL_CreateWindow(
        "Dorfplatformer Timetravel",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 540,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Log("Window and renderer created");

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

    loadLevelBNNLVL(levelPath, map, objects, meta);

    Player player;
    for (const auto& o : objects) {
        if (o.id == "player") {
            player.x = o.x;
            player.y = o.y;
            break;
        }
    }

    bool running = true;
    bool fullscreen = false;
    SDL_Event e;
    Uint32 lastTicks = SDL_GetTicks();

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastTicks = now;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float move = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) move -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move += 1.0f;

        float accel = 1800.0f;
        float maxSpeed = 260.0f;
        float friction = 1600.0f;
        float gravity = 2000.0f;
        float jumpSpeed = 620.0f;

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

        if (player.onGround && (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])) {
            player.vy = -jumpSpeed;
            player.onGround = false;
        }

        player.vy += gravity * dt;

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
            player.y = newY;
            player.onGround = false;
        } else {
            int dir = (player.vy > 0) ? 1 : -1;
            while (!rectHitsSolid(map, player.x, player.y + dir, player.w, player.h)) {
                player.y += dir;
            }
            if (dir > 0) player.onGround = true;
            player.vy = 0.0f;
        }

        int screenW = 960;
        int screenH = 540;
        float camX = player.x + player.w * 0.5f - screenW * 0.5f;
        float camY = player.y + player.h * 0.5f - screenH * 0.5f;
        float maxCamX = map.w * map.tileSize - screenW;
        float maxCamY = map.h * map.tileSize - screenH;
        camX = std::clamp(camX, 0.0f, std::max(0.0f, maxCamX));
        camY = std::clamp(camY, 0.0f, std::max(0.0f, maxCamY));

        SDL_SetRenderDrawColor(ren, 18,18,22,255);
        SDL_RenderClear(ren);

        map.renderBgDebug(ren, camX, camY);
        map.renderWater(ren, camX, camY);
        map.renderSemiSolid(ren, camX, camY);
        map.renderSolid(ren, camX, camY);

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
                else if (isSemi) SDL_SetRenderDrawColor(ren, 255, 200, 60, 255);
                else SDL_SetRenderDrawColor(ren, 80, 160, 255, 255);
                SDL_RenderDrawRectF(ren, &rc);
            }
        }

        SDL_SetRenderDrawColor(ren, 120, 220, 120, 255);
        for (const auto& obj : objects) {
            float ox = obj.x - 8.0f;
            float oy = obj.y - 8.0f;
            SDL_FRect orc{ ox - camX, oy - camY, 16.0f, 16.0f };
            SDL_RenderDrawRectF(ren, &orc);
        }

        SDL_SetRenderDrawColor(ren, 200, 200, 230, 255);
        SDL_FRect pr{ player.x - camX, player.y - camY, (float)player.w, (float)player.h };
        SDL_RenderFillRectF(ren, &pr);
        SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
        SDL_RenderDrawRectF(ren, &pr);

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Log("Shutting down");
    SDL_Quit();
    return 0;
}
