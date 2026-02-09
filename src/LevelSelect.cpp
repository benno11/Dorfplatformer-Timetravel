#include "LevelSelect.h"
#include "AssetPath.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "TextRenderer.h"
#include "GameSupport.h"

namespace {
std::vector<std::string> loadLevelList() {
    std::vector<std::string> out;
#if defined(__ANDROID__)
    const std::string text = ReadTextFile("assets/levels/levels.json");
    if (!text.empty()) {
        nlohmann::json j;
        try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
        if (j.contains("levels") && j["levels"].is_array()) {
            for (const auto& v : j["levels"]) {
                if (!v.is_string()) continue;
                std::string name = v.get<std::string>();
                if (name.size() >= 7 && name.substr(name.size() - 7) == ".bnnlvl") {
                    name = name.substr(0, name.size() - 7) + ".txt";
                }
                out.push_back(name);
            }
        }
    }
#else
    namespace fs = std::filesystem;
    fs::path dir("assets/levels");
    if (!fs::exists(dir)) return out;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        if (p.extension() == ".txt") out.push_back(p.filename().string());
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}
}

std::string RunLevelSelect(SDL_Window* win, SDL_Renderer* ren) {
    std::vector<std::string> levels = loadLevelList();
    if (levels.empty()) return "";

    int selected = 0;
    int scrollY = 0;
    bool running = true;
    bool chosen = false;
    bool draggingScrollbar = false;
    int dragOffsetY = 0;
    SDL_FingerID activeFinger = 0;
    float lastFingerY = 0.0f;
    float fingerDownY = 0.0f;
    bool fingerMoved = false;

    int winW = 960, winH = 540;
    SDL_GetWindowSize(win, &winW, &winH);

    while (running) {
        SDL_GetWindowSize(win, &winW, &winH);
        float uiScale = std::min((float)winW / 960.0f, (float)winH / 540.0f);
        uiScale = std::clamp(uiScale, 0.75f, 2.0f);
        int rowH = std::max(24, (int)std::lround(28.0f * uiScale));
        int pad = std::max(10, (int)std::lround(16.0f * uiScale));
        int textScale = std::max(1, (int)std::lround(2.0f * uiScale));

        int contentH = (int)levels.size() * rowH;
        int viewportH = std::max(1, winH - pad * 2);
        int maxScroll = std::max(0, contentH - viewportH);
        scrollY = std::clamp(scrollY, 0, maxScroll);

        int barW = std::max(8, (int)std::lround(10.0f * uiScale));
        SDL_Rect track{winW - pad - barW, pad, barW, viewportH};
        float visibleRatio = (contentH > 0) ? std::clamp((float)viewportH / (float)contentH, 0.0f, 1.0f) : 1.0f;
        int thumbH = std::max(std::max(16, (int)std::lround(28.0f * uiScale)), (int)std::lround(track.h * visibleRatio));
        int thumbTravel = std::max(1, track.h - thumbH);
        int thumbY = track.y + ((maxScroll > 0) ? (int)std::lround((float)scrollY / (float)maxScroll * thumbTravel) : 0);
        SDL_Rect thumb{track.x, thumbY, barW, thumbH};

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (e.type == SDL_MOUSEWHEEL) {
                scrollY -= e.wheel.y * rowH;
                if (scrollY < 0) scrollY = 0;
                if (scrollY > maxScroll) scrollY = maxScroll;
            }
            if (e.type == SDL_FINGERDOWN && activeFinger == 0) {
                activeFinger = e.tfinger.fingerID;
                lastFingerY = e.tfinger.y * winH;
                fingerDownY = lastFingerY;
                fingerMoved = false;
            }
            if (e.type == SDL_FINGERMOTION && e.tfinger.fingerID == activeFinger) {
                float y = e.tfinger.y * winH;
                float dy = y - lastFingerY;
                lastFingerY = y;
                if (std::fabs(y - fingerDownY) > 6.0f) fingerMoved = true;
                scrollY -= (int)std::lround(dy);
                if (scrollY < 0) scrollY = 0;
                if (scrollY > maxScroll) scrollY = maxScroll;
            }
            if (e.type == SDL_FINGERUP && e.tfinger.fingerID == activeFinger) {
                float y = e.tfinger.y * winH;
                if (!fingerMoved) {
                    int localY = (int)std::lround(y) - pad + scrollY;
                    if (localY >= 0) {
                        int idx = localY / rowH;
                        if (idx >= 0 && idx < (int)levels.size()) {
                            if (selected == idx) {
                                chosen = true;
                                running = false;
                            } else {
                                selected = idx;
                            }
                        }
                    }
                }
                activeFinger = 0;
                fingerMoved = false;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point pt{(int)e.button.x, (int)e.button.y};
                if (SDL_PointInRect(&pt, &thumb)) {
                    draggingScrollbar = true;
                    dragOffsetY = (int)e.button.y - thumb.y;
                    continue;
                }
                if (SDL_PointInRect(&pt, &track)) {
                    int newThumbY = std::clamp((int)e.button.y - thumbH / 2, track.y, track.y + track.h - thumbH);
                    float t = (float)(newThumbY - track.y) / (float)std::max(1, track.h - thumbH);
                    scrollY = (int)std::lround(t * maxScroll);
                    continue;
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                draggingScrollbar = false;
            }
            if (e.type == SDL_MOUSEMOTION && draggingScrollbar) {
                int newThumbY = std::clamp((int)e.motion.y - dragOffsetY, track.y, track.y + track.h - thumbH);
                float t = (float)(newThumbY - track.y) / (float)std::max(1, track.h - thumbH);
                scrollY = (int)std::lround(t * maxScroll);
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int y = (int)e.button.y - pad + scrollY;
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
                if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) {
                    running = false;
                    break;
                }
                if (e.key.key == SDLK_DOWN) {
                    selected = std::min(selected + 1, (int)levels.size() - 1);
                    int rowTop = selected * rowH;
                    int rowBottom = rowTop + rowH;
                    if (rowBottom - scrollY > pad + viewportH) scrollY = rowBottom - viewportH;
                }
                if (e.key.key == SDLK_UP) {
                    selected = std::max(selected - 1, 0);
                    int rowTop = selected * rowH;
                    if (rowTop < scrollY) scrollY = rowTop;
                }
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    chosen = true;
                    running = false;
                }
            }
        }

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
            SDL_Rect r{pad, y, winW - pad * 3 - barW, rowH - 4};
            SDL_RenderFillRect(ren, &r);

            SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
            DrawText(ren, r.x + 8, r.y + 6, textScale, levels[i]);
        }

        if (maxScroll > 0) {
            SDL_SetRenderDrawColor(ren, 48, 58, 76, 255);
            SDL_RenderFillRect(ren, &track);
            SDL_SetRenderDrawColor(ren, draggingScrollbar ? 220 : 180, draggingScrollbar ? 220 : 180, draggingScrollbar ? 220 : 180, 255);
            SDL_RenderFillRect(ren, &thumb);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (!chosen) return "";
    return "assets/levels/" + levels[selected];
}
