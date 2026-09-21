#include "SlimMenuSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "AssetPath.h"
#include "GameSupport.h"
#include "LevelSelect.h"
#include "TextRenderer.h"

namespace {

constexpr const char* kSavedGameSelectionToken = "__DF_SAVEGAME_CONTINUE__";

struct MenuItem {
    enum class Type { Text, Button };
    Type type = Type::Text;
    std::string label;
    std::string command;
    std::string arg;
};

struct SlimMenu {
    std::string name;
    std::string title;
    std::vector<MenuItem> items;
    std::vector<std::pair<std::string, std::string>> preloadGets;
};

struct SlimRuntime {
    std::unordered_map<std::string, std::string> vars;
};

std::string trim(const std::string& in) {
    std::size_t first = 0;
    while (first < in.size() && std::isspace(static_cast<unsigned char>(in[first]))) ++first;
    std::size_t last = in.size();
    while (last > first && std::isspace(static_cast<unsigned char>(in[last - 1]))) --last;
    return in.substr(first, last - first);
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && (i == 0 || line[i - 1] != '\\')) {
                quoted = false;
            } else if (ch == '\\' && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                ++i;
            } else {
                cur.push_back(ch);
            }
            continue;
        }
        if (ch == '"') {
            quoted = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string expandVars(std::string text, const SlimRuntime& runtime, const SlimMenuContext& ctx) {
    if (ctx.levelServerAccountUsername) text = replaceAll(text, "${username}", *ctx.levelServerAccountUsername);
    if (ctx.levelServerUrl) text = replaceAll(text, "${api}", *ctx.levelServerUrl);
    for (const auto& [key, value] : runtime.vars) {
        text = replaceAll(text, "${" + key + "}", value);
    }
    return text;
}

std::string menuPath(const std::string& name) {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return name;
    return "assets/menus/" + name + ".menu";
}

SlimMenu loadMenu(const std::string& name) {
    SlimMenu menu;
    menu.name = name;
    const std::string text = ReadTextFile(menuPath(name));
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> parts = tokenize(line);
        if (parts.empty()) continue;
        const std::string& op = parts[0];
        if (op == "title" && parts.size() >= 2) {
            menu.title = parts[1];
        } else if (op == "text" && parts.size() >= 2) {
            menu.items.push_back(MenuItem{MenuItem::Type::Text, parts[1], {}, {}});
        } else if (op == "button" && parts.size() >= 3) {
            const std::string arg = parts.size() >= 4 ? parts[3] : std::string();
            menu.items.push_back(MenuItem{MenuItem::Type::Button, parts[1], parts[2], arg});
        } else if (op == "get" && parts.size() >= 3) {
            menu.preloadGets.push_back({parts[1], parts[2]});
        } else if (op == "set" && parts.size() >= 3) {
            menu.preloadGets.push_back({parts[1], "literal:" + parts[2]});
        } else {
            SDL_Log("SLIM MENU: ignored %s:%d: %s", menuPath(name).c_str(), lineNo, line.c_str());
        }
    }
    if (menu.title.empty()) menu.title = name;
    return menu;
}

std::string makeApiUrl(const SlimMenuContext& ctx, std::string path) {
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) return path;
    std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) return {};
    if (path.empty() || path[0] != '/') path = "/" + path;
    return base + path;
}

void preloadMenuVars(const SlimMenu& menu, SlimRuntime& runtime, const SlimMenuContext& ctx) {
    for (const auto& [name, request] : menu.preloadGets) {
        if (request.rfind("literal:", 0) == 0) {
            runtime.vars[name] = request.substr(8);
            continue;
        }
        const std::string url = makeApiUrl(ctx, expandVars(request, runtime, ctx));
        runtime.vars[name] = url.empty() ? std::string() : ReadTextFile(url);
    }
}

SDL_Rect buttonRectFor(const SlimMenuContext& ctx, int buttonIndex, int buttonCount) {
    const int w = std::clamp(ctx.baseScreenW * 2 / 5, 220, 520);
    const int h = 54;
    const int gap = 14;
    const int totalH = buttonCount * h + std::max(0, buttonCount - 1) * gap;
    const int startY = std::max(132, ctx.baseScreenH / 2 - totalH / 2);
    return SDL_Rect{ctx.baseScreenW / 2 - w / 2, startY + buttonIndex * (h + gap), w, h};
}

void renderMenu(const SlimMenu& menu, SlimRuntime& runtime, const SlimMenuContext& ctx, int selected) {
    SDL_SetRenderTarget(ctx.ren, ctx.gameTarget);
    SDL_SetRenderDrawColor(ctx.ren, 20, 26, 34, 255);
    SDL_RenderClear(ctx.ren);

    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx.ren, 66, 101, 126, 255);
    SDL_Rect top{0, 0, ctx.baseScreenW, std::max(92, ctx.baseScreenH / 6)};
    SDL_RenderFillRect(ctx.ren, &top);
    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);

    const int titleScale = 4;
    const std::string title = expandVars(menu.title, runtime, ctx);
    DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(titleScale, title) / 2, 34, titleScale, title);

    int buttonCount = 0;
    for (const auto& item : menu.items) if (item.type == MenuItem::Type::Button) ++buttonCount;
    int buttonIndex = 0;
    int textY = 106;
    for (const auto& item : menu.items) {
        const std::string label = expandVars(item.label, runtime, ctx);
        if (item.type == MenuItem::Type::Text) {
            DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, label) / 2, textY, 2, label);
            textY += 28;
            continue;
        }
        SDL_Rect r = buttonRectFor(ctx, buttonIndex, buttonCount);
        const bool isSelected = buttonIndex == selected;
        SDL_SetRenderDrawColor(ctx.ren, isSelected ? 96 : 54, isSelected ? 116 : 74, isSelected ? 134 : 92, 255);
        SDL_RenderFillRect(ctx.ren, &r);
        SDL_SetRenderDrawColor(ctx.ren, isSelected ? 220 : 160, isSelected ? 232 : 184, isSelected ? 242 : 204, 255);
        SDL_RenderRect(ctx.ren, &r);
        DrawText(ctx.ren, r.x + (r.w - MeasureTextWidth(2, label)) / 2, r.y + (r.h - 20) / 2, 2, label);
        ++buttonIndex;
    }
}

MenuItem* buttonAt(SlimMenu& menu, int selected) {
    int buttonIndex = 0;
    for (auto& item : menu.items) {
        if (item.type != MenuItem::Type::Button) continue;
        if (buttonIndex == selected) return &item;
        ++buttonIndex;
    }
    return nullptr;
}

int buttonCount(const SlimMenu& menu) {
    int count = 0;
    for (const auto& item : menu.items) if (item.type == MenuItem::Type::Button) ++count;
    return count;
}

bool toggleBool(const std::string& name, SlimMenuContext& ctx) {
    bool* target = nullptr;
    if (name == "menu_music_enabled") target = ctx.menuMusicEnabled;
    if (name == "mute_all_audio") target = ctx.muteAllAudio;
    if (name == "level_select_enabled") target = ctx.levelSelectEnabled;
    if (!target) return false;
    *target = !*target;
    if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    if (ctx.saveClientSettings) ctx.saveClientSettings();
    return true;
}

bool adjustInt(const std::string& name, int delta, SlimMenuContext& ctx) {
    int* target = nullptr;
    if (name == "music_volume") target = ctx.musicVolume;
    if (name == "sfx_volume") target = ctx.sfxVolume;
    if (!target) return false;
    *target = std::clamp(*target + delta, 0, 128);
    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    if (ctx.saveClientSettings) ctx.saveClientSettings();
    return true;
}

SlimMenuExit execute(MenuItem& item, SlimRuntime& runtime, SlimMenuContext& ctx, std::string& currentMenu) {
    const std::string command = item.command;
    const std::string arg = expandVars(item.arg, runtime, ctx);
    if (command == "back") return SlimMenuExit::Back;
    if (command == "quit") {
        if (ctx.running) *ctx.running = false;
        return SlimMenuExit::Quit;
    }
    if (command == "menu") {
        if (!arg.empty()) currentMenu = arg;
        return SlimMenuExit::Continue;
    }
    if (command == "campaign") {
        if (!ctx.selectedLevelPath) return SlimMenuExit::Back;
        const std::string path = RunCampaignLevelSelect(ctx.win, ctx.ren);
        if (path.empty()) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = path;
        return SlimMenuExit::StartGame;
    }
    if (command == "levels") {
        if (!ctx.selectedLevelPath) return SlimMenuExit::Back;
        const std::string path = RunLevelSelect(ctx.win, ctx.ren);
        if (path.empty()) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = path;
        return SlimMenuExit::StartGame;
    }
    if (command == "custom") {
        if (!ctx.selectedLevelPath) return SlimMenuExit::Back;
        const std::string path = RunCustomLevelSelect(ctx.win, ctx.ren);
        if (path.empty()) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = path;
        return SlimMenuExit::StartGame;
    }
    if (command == "saved") {
        if (ctx.selectedLevelPath) *ctx.selectedLevelPath = kSavedGameSelectionToken;
        return SlimMenuExit::StartGame;
    }
    if (command == "start") {
        if (ctx.selectedLevelPath) *ctx.selectedLevelPath = arg;
        return SlimMenuExit::StartGame;
    }
    if (command == "toggle") {
        (void)toggleBool(arg, ctx);
        return SlimMenuExit::Continue;
    }
    if (command == "inc") {
        (void)adjustInt(arg, 8, ctx);
        return SlimMenuExit::Continue;
    }
    if (command == "dec") {
        (void)adjustInt(arg, -8, ctx);
        return SlimMenuExit::Continue;
    }
    if (command == "get") {
        const std::string::size_type eq = arg.find('=');
        if (eq != std::string::npos) {
            const std::string name = arg.substr(0, eq);
            const std::string url = makeApiUrl(ctx, arg.substr(eq + 1));
            runtime.vars[name] = url.empty() ? std::string() : ReadTextFile(url);
        }
        return SlimMenuExit::Continue;
    }
    SDL_Log("SLIM MENU: unknown command '%s'", command.c_str());
    return SlimMenuExit::Continue;
}

} // namespace

SlimMenuExit RunSlimMenu(SlimMenuContext& ctx, const std::string& menuName) {
    SlimRuntime runtime;
    std::string currentMenu = menuName;
    while (ctx.running && *ctx.running) {
        SlimMenu menu = loadMenu(currentMenu);
        preloadMenuVars(menu, runtime, ctx);
        int selected = 0;
        SDL_Event e;
        while (ctx.running && *ctx.running) {
            const int count = std::max(1, buttonCount(menu));
            selected = std::clamp(selected, 0, count - 1);
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    *ctx.running = false;
                    return SlimMenuExit::Quit;
                }
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.repeat == 0) {
                    if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) return SlimMenuExit::Back;
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_LEFT) selected = (selected + count - 1) % count;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_RIGHT) selected = (selected + 1) % count;
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (MenuItem* item = buttonAt(menu, selected)) {
                            const std::string before = currentMenu;
                            const SlimMenuExit exit = execute(*item, runtime, ctx, currentMenu);
                            if (exit == SlimMenuExit::Back) return exit;
                            if (exit == SlimMenuExit::StartGame || exit == SlimMenuExit::Quit) return exit;
                            if (currentMenu != before) goto next_menu;
                        }
                    }
                }
                const bool isMouseButtonDown =
#if defined(SDL_EVENT_MOUSE_BUTTON_DOWN)
                    (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ||
#endif
                    (e.type == SDL_MOUSEBUTTONDOWN);
                if (isMouseButtonDown && e.button.button == SDL_BUTTON_LEFT) {
                    int winW = 0, winH = 0, gx = 0, gy = 0;
                    getWindowSizeInPixelsCompat(ctx.win, winW, winH);
                    if (!windowToGamePoint(e.button.x, e.button.y, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy, 1.0f)) continue;
                    for (int i = 0; i < count; ++i) {
                        SDL_Rect r = buttonRectFor(ctx, i, count);
                        SDL_Point pt{gx, gy};
                        if (!SDL_PointInRect(&pt, &r)) continue;
                        selected = i;
                        if (MenuItem* item = buttonAt(menu, selected)) {
                            const std::string before = currentMenu;
                            const SlimMenuExit exit = execute(*item, runtime, ctx, currentMenu);
                            if (exit == SlimMenuExit::Back) return exit;
                            if (exit == SlimMenuExit::StartGame || exit == SlimMenuExit::Quit) return exit;
                            if (currentMenu != before) goto next_menu;
                        }
                    }
                }
            }
            renderMenu(menu, runtime, ctx, selected);
            SDL_SetRenderTarget(ctx.ren, nullptr);
            SDL_RenderTexture(ctx.ren, ctx.gameTarget, static_cast<const SDL_Rect*>(nullptr), static_cast<const SDL_Rect*>(nullptr));
            SDL_RenderPresent(ctx.ren);
            SDL_Delay(16);
        }
next_menu:
        continue;
    }
    return SlimMenuExit::Quit;
}
