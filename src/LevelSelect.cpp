#include "BuildInfo.h"
#include "LevelSelect.h"
#include "AssetPath.h"
#include "NetworkTransport.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif

#include "TextRenderer.h"
#include "GameSupport.h"
#include "LevelLoader.h"
#include "LevelParameterObjects.h"
#include "EditorObjectTools.h"
#include "EditorBrush.h"
#include "ButtonAnimation.h"
#include "LocalLevelEdits.h"
#include "ObjectIds.h"
#include "InputSystem.h"
#include "Platform.h"
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif

namespace {
std::unordered_map<int, std::string> defaultEditorObjectTypes() {
    return {
        {31, "spring"},
        {46, "bumper"},
        {57, "fast_travel_up"},
        {58, "fast_travel_down"},
        {59, "fast_travel_left"},
        {60, "fast_travel_right"},
        {61, "fast_travel_exit"},
        {62, "level_wrap_x"},
        {63, "level_wrap_y"},
        {69, "disable_wrap_x"},
        {70, "disable_wrap_y"},
        {64, "key"},
        {65, "door"},
        {67, "end_sign"},
        {ObjectIds::Pickup, "pickup"},
        {ObjectIds::Goal, "goal"},
        {ObjectIds::ShellChraker, "shell_chraker"},
        {ObjectIds::MovingPlatform, "moving_platform"},
        {ObjectIds::Shield, "shield"},
    };
}

std::string editorObjectLabelFromType(const std::string& type, int id) {
    std::string label = type.empty() ? "object" : type;
    for (char& ch : label) {
        if (ch == '_' || ch == '-') ch = ' ';
        else ch = (char)std::toupper((unsigned char)ch);
    }
    return label + " (" + std::to_string(id) + ")";
}

std::vector<int> loadEditorObjectPalette(std::unordered_map<int, std::string>& objectTypeById) {
    objectTypeById = defaultEditorObjectTypes();
    for (const auto& d : LevelParameters::definitions) objectTypeById[d.id] = d.label;
    std::vector<int> palette{31, 46, 57, 58, 59, 60, 61, 67};

    const std::string text = ReadTextFile("object_type_map.json");
    if (!text.empty()) {
        try {
            const nlohmann::json mapJson = nlohmann::json::parse(text);
            if (mapJson.is_object()) {
                std::vector<int> mappedIds;
                for (auto it = mapJson.begin(); it != mapJson.end(); ++it) {
                    int id = 0;
                    try {
                        id = std::stoi(it.key());
                    } catch (...) {
                        continue;
                    }
                    if (id <= 0 || !it.value().is_string()) continue;
                    if (objectTypeById.find(id) == objectTypeById.end()) {
                        objectTypeById[id] = it.value().get<std::string>();
                    }
                    if (std::find(palette.begin(), palette.end(), id) == palette.end()) {
                        mappedIds.push_back(id);
                    }
                }
                std::sort(mappedIds.begin(), mappedIds.end());
                for (int id : mappedIds) {
                    if (std::find(palette.begin(), palette.end(), id) == palette.end()) {
                        palette.push_back(id);
                    }
                }
            }
        } catch (...) {}
    }

    for (const auto& [id, type] : objectTypeById) {
        (void)type;
        if (std::find(palette.begin(), palette.end(), id) == palette.end()) {
            palette.push_back(id);
        }
    }
    // Keep the established first choices, then put all additional IDs in a stable order.
    std::sort(palette.begin() + 8, palette.end());
    return palette;
}

#if defined(__ANDROID__)
void ShowAndroidSoftKeyboard(int x, int y, int w, int h) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
    if (!cls) return;
    jmethodID mid = env->GetStaticMethodID(cls, "showSoftKeyboard", "(IIII)Z");
    if (mid) {
        (void)env->CallStaticBooleanMethod(cls, mid, (jint)x, (jint)y, (jint)w, (jint)h);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(cls);
}

void HideAndroidSoftKeyboard() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
    if (!cls) return;
    jmethodID mid = env->GetStaticMethodID(cls, "hideSoftKeyboard", "()V");
    if (mid) {
        env->CallStaticVoidMethod(cls, mid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(cls);
}
#endif

bool isHttpUrl(const std::string& path) {
    std::string p = path;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return p.rfind("http://", 0) == 0 || p.rfind("https://", 0) == 0;
}

std::string sanitizeFilePart(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char ch : in) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "level";
    return out;
}

int parseTileIdFromFrameName(const std::string& name, bool requireFullNumeric) {
    if (name.empty()) return -1;
    size_t digitsEnd = 0;
    while (digitsEnd < name.size() && std::isdigit((unsigned char)name[digitsEnd])) ++digitsEnd;
    if (digitsEnd == 0) return -1;
    if (requireFullNumeric && digitsEnd != name.size()) return -1;
    int id = -1;
    try { id = std::stoi(name.substr(0, digitsEnd)); } catch (...) { return -1; }
    return id >= 0 ? id : -1;
}

void drawChromeButton(SDL_Renderer* ren, const SDL_Rect& rect, bool active) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, active ? 68 : 44, active ? 92 : 56, active ? 138 : 74, 255);
    SDL_RenderFillRect(ren, &rect);
    SDL_SetRenderDrawColor(ren, 160, 182, 212, 255);
    SDL_RenderDrawRect(ren, &rect);
    if (active) {
        SDL_Rect inner{rect.x + 2, rect.y + 2, std::max(0, rect.w - 4), std::max(0, rect.h - 4)};
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 32);
        SDL_RenderDrawRect(ren, &inner);
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

std::string localLevelsFolderPath();
std::string RunLocalLevelRenamePrompt(SDL_Window* win, SDL_Renderer* ren, const std::string& initialName,
    const std::string& title = "RENAME LOCAL LEVEL", const std::string& fieldLabel = "NAME: ");
struct GrowLevelPromptResult {
    bool accepted = false;
    int width = 30;
    int height = 17;
};
GrowLevelPromptResult RunGrowLevelPrompt(SDL_Window* win, SDL_Renderer* ren, int currentW, int currentH, bool allowShrink = false);

unsigned short normalizeTileIdForSave(unsigned short id) {
    // Legacy/runtime empty tile convention is 2; saving 0 can cause bad loads/render fallbacks.
    if (id == 0) return 2;
    return id;
}

std::string writeLocalLevelFile(const std::vector<unsigned short>& rowMajorGrid, int w, int h,
                                const std::string& targetPath = "",
                                const std::vector<ObjectInstance>& objects = {}, const LevelMeta& config = {},
                                std::string* upgradeBackup = nullptr) {
    if (w <= 0 || h <= 0) return {};
    if ((int)rowMajorGrid.size() != w * h) return {};
    // DFLVL4 adds level configuration while retaining exact row-major tile IDs.
    bool wrapX = config.wrapX;
    bool wrapY = config.wrapY;
    for (const auto& obj : objects) {
        if (obj.id == "62") wrapX = true;
        if (obj.id == "63") wrapY = true;
    }
    int levelId = config.levelId;
    if (levelId <= 0 && !targetPath.empty()) {
        const std::string stem = std::filesystem::path(targetPath).stem().string();
        int start = -1;
        int len = 0;
        for (int i = 0; i < (int)stem.size();) {
            if (!std::isdigit((unsigned char)stem[i])) {
                ++i;
                continue;
            }
            const int s = i;
            while (i < (int)stem.size() && std::isdigit((unsigned char)stem[i])) ++i;
            start = s;
            len = i - s;
        }
        if (start >= 0) {
            levelId = std::atoi(stem.substr((size_t)start, (size_t)len).c_str());
        }
    }
    LevelMeta fileConfig = config;
    fileConfig.levelId = levelId;
    fileConfig.wrapX = wrapX;
    fileConfig.wrapY = wrapY;
    TileMap saveMap;
    saveMap.w = w;
    saveMap.h = h;
    saveMap.tileIds = rowMajorGrid;
    for (auto& tile : saveMap.tileIds) tile = normalizeTileIdForSave(tile);
    const std::string data = SerializeLevelBNNLVL(saveMap, objects, fileConfig);
    if (data.empty()) return {};

    std::filesystem::path outPath;
    if (!targetPath.empty()) {
        outPath = std::filesystem::path(targetPath);
        std::error_code dirEc;
        std::filesystem::create_directories(outPath.parent_path(), dirEc);
    } else {
        const std::string dir = localLevelsFolderPath();
        const long long ts = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        outPath = std::filesystem::path(dir) / ("local_" + std::to_string(ts) + ".bin");
        // Rare collision guard.
        for (int attempt = 0; attempt < 1000 && std::filesystem::exists(outPath); ++attempt) {
            outPath = std::filesystem::path(dir) / ("local_" + std::to_string(ts + attempt + 1) + ".bin");
        }
    }
    // All rewrite paths, including Resize, protect old sources before upgrading.
    if (LevelNeedsUpgrade(config) && std::filesystem::exists(outPath)) {
        std::string localBackup, error;
        std::string& backup = upgradeBackup ? *upgradeBackup : localBackup;
        if (backup.empty() && !LocalLevelEdits::backupForUpgrade(outPath.string(), backup, error)) return {};
    }
    const std::filesystem::path tmpPath = outPath.string() + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return {};
        out << data;
        out.flush();
        if (!out.good()) return {};
    }

    if (!LocalLevelEdits::replaceFile(tmpPath.string(), outPath.string())) return {};
    ClearLocalLevelVerification(outPath.string());
    return outPath.string();
}

struct NewLevelPromptResult {
    bool accepted = false;
    std::string name = "new_level";
    int width = 30;
    int height = 17;
};

std::string buildNamedLocalLevelPath(const std::string& name) {
    const std::string dir = localLevelsFolderPath();
    const std::string base = sanitizeFilePart(name.empty() ? "new_level" : name);
    std::filesystem::path out = std::filesystem::path(dir) / (base + ".bin");
    for (int attempt = 1; std::filesystem::exists(out) && attempt < 10000; ++attempt) {
        out = std::filesystem::path(dir) / (base + "_" + std::to_string(attempt) + ".bin");
    }
    return out.string();
}

NewLevelPromptResult RunNewLevelPrompt(SDL_Window* win, SDL_Renderer* ren) {
    NewLevelPromptResult out;
    bool running = true;
    int selectedRow = 0; // 0 name, 1 width, 2 height, 3 create, 4 cancel
    bool promptTextInputActive = false;
    auto setPromptTextInput = [&](bool active, int winW = 0, int winH = 0, const SDL_Rect* focusRect = nullptr) {
        if (active == promptTextInputActive) {
            if (active && focusRect) {
                SDL_Rect textInputArea{
                    focusRect->x,
                    focusRect->y,
                    std::max(1, focusRect->w),
                    std::max(1, focusRect->h)
                };
                SDL_SetTextInputArea(win, &textInputArea, 0);
            }
#if defined(__ANDROID__)
            if (active) {
                if (winW <= 0 || winH <= 0) getWindowSizeInPixelsCompat(win, winW, winH);
                if (focusRect) {
                    ShowAndroidSoftKeyboard(
                        focusRect->x,
                        focusRect->y,
                        std::max(1, focusRect->w),
                        std::max(1, focusRect->h));
                }
            }
#endif
            return;
        }
        promptTextInputActive = active;
        if (active) {
            SDL_StartTextInput(win);
            if (focusRect) {
                SDL_Rect textInputArea{
                    focusRect->x,
                    focusRect->y,
                    std::max(1, focusRect->w),
                    std::max(1, focusRect->h)
                };
                SDL_SetTextInputArea(win, &textInputArea, 0);
            }
#if defined(__ANDROID__)
            if (winW <= 0 || winH <= 0) getWindowSizeInPixelsCompat(win, winW, winH);
            if (focusRect) {
                ShowAndroidSoftKeyboard(
                    focusRect->x,
                    focusRect->y,
                    std::max(1, focusRect->w),
                    std::max(1, focusRect->h));
            } else {
                ShowAndroidSoftKeyboard(0, 0, std::max(1, winW), std::max(1, winH));
            }
#endif
        } else {
            SDL_SetTextInputArea(win, nullptr, 0);
            SDL_StopTextInput(win);
#if defined(__ANDROID__)
            HideAndroidSoftKeyboard();
#endif
        }
    };
    auto setSelectedRow = [&](int row, int winW = 0, int winH = 0, const SDL_Rect* nameFocusRect = nullptr) {
        selectedRow = std::clamp(row, 0, 4);
        setPromptTextInput(selectedRow == 0, winW, winH, nameFocusRect);
    };
    // Keep the name row selected for keyboard/controller navigation, but do not
    // immediately enter text-editing mode.  On touch platforms starting text
    // input also opens the on-screen keyboard, which otherwise covered this
    // dialog as soon as it appeared.  Tapping the name row (or navigating back
    // to it later) explicitly starts text input.
    auto clampSize = [](int v) { return std::clamp(v, 5, 400); };
    auto trimSpaces = [](std::string s) {
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        return s;
    };
    auto isTextInputEventType = [](Uint32 t) -> bool {
        if (t == SDL_TEXTINPUT) return true;
#if defined(SDL_EVENT_TEXT_INPUT)
        if (t == SDL_EVENT_TEXT_INPUT) return true;
#endif
        return false;
    };
    auto isKeyDownEventType = [](Uint32 t) -> bool {
        if (t == SDL_KEYDOWN) return true;
#if defined(SDL_EVENT_KEY_DOWN)
        if (t == SDL_EVENT_KEY_DOWN) return true;
#endif
        return false;
    };
    auto isFingerDownEventType = [](Uint32 t) -> bool {
        if (t == SDL_FINGERDOWN) return true;
#if defined(SDL_EVENT_FINGER_DOWN)
        if (t == SDL_EVENT_FINGER_DOWN) return true;
#endif
        return false;
    };

    while (running) {
        int winW = 0, winH = 0;
        getWindowSizeInPixelsCompat(win, winW, winH);
        SDL_Rect panel{winW / 2 - 260, winH / 2 - 160, 520, 320};
        SDL_Rect rowName{panel.x + 20, panel.y + 56, panel.w - 40, 38};
        SDL_Rect rowW{panel.x + 20, panel.y + 106, panel.w - 40, 38};
        SDL_Rect rowH{panel.x + 20, panel.y + 156, panel.w - 40, 38};
        SDL_Rect rowWMinus{rowW.x + rowW.w - 96, rowW.y + 5, 40, rowW.h - 10};
        SDL_Rect rowWPlus{rowW.x + rowW.w - 46, rowW.y + 5, 40, rowW.h - 10};
        SDL_Rect rowHMinus{rowH.x + rowH.w - 96, rowH.y + 5, 40, rowH.h - 10};
        SDL_Rect rowHPlus{rowH.x + rowH.w - 46, rowH.y + 5, 40, rowH.h - 10};
        SDL_Rect createBtn{panel.x + 80, panel.y + 242, 150, 42};
        SDL_Rect cancelBtn{panel.x + 290, panel.y + 242, 150, 42};
        if (promptTextInputActive) setPromptTextInput(true, winW, winH, &rowName);
        auto handlePointerDown = [&](int px, int py) {
            SDL_Point pt{px, py};
            auto inPadded = [&](const SDL_Rect& r, int pad = 10) -> bool {
                SDL_Rect rr{r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2};
                return SDL_PointInRect(&pt, &rr);
            };
            if (inPadded(rowWMinus, 12)) { setSelectedRow(1, winW, winH); out.width = clampSize(out.width - 1); return; }
            if (inPadded(rowWPlus, 12)) { setSelectedRow(1, winW, winH); out.width = clampSize(out.width + 1); return; }
            if (inPadded(rowHMinus, 12)) { setSelectedRow(2, winW, winH); out.height = clampSize(out.height - 1); return; }
            if (inPadded(rowHPlus, 12)) { setSelectedRow(2, winW, winH); out.height = clampSize(out.height + 1); return; }
            if (inPadded(rowName, 14)) { setSelectedRow(0, winW, winH, &rowName); return; }
            if (inPadded(rowW, 10)) { setSelectedRow(1, winW, winH); return; }
            if (inPadded(rowH, 10)) { setSelectedRow(2, winW, winH); return; }
            if (inPadded(createBtn, 10)) {
                setSelectedRow(3, winW, winH);
                out.name = trimSpaces(out.name);
                if (out.name.empty()) out.name = "new_level";
                out.accepted = true;
                running = false;
                return;
            }
            if (inPadded(cancelBtn, 10)) {
                setSelectedRow(4, winW, winH);
                running = false;
            }
        };

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }
            if (isTextInputEventType(e.type) && selectedRow == 0) {
                if (out.name.size() < 48) {
                    out.name += e.text.text;
                }
                continue;
            }
            if (isKeyDownEventType(e.type) && e.key.repeat == 0) {
                if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) {
                    running = false;
                    break;
                }
                if (e.key.key == SDLK_UP) {
                    setSelectedRow((selectedRow + 4) % 5, winW, winH, &rowName);
                    continue;
                }
                if (e.key.key == SDLK_DOWN || e.key.key == SDLK_TAB) {
                    setSelectedRow((selectedRow + 1) % 5, winW, winH, &rowName);
                    continue;
                }
                if ((e.key.key == SDLK_BACKSPACE || e.key.key == SDLK_DELETE) && selectedRow == 0) {
                    if (!out.name.empty()) out.name.pop_back();
                    continue;
                }
                if (selectedRow == 1 || selectedRow == 2) {
                    int& v = (selectedRow == 1) ? out.width : out.height;
                    const int step = (e.key.key == SDLK_PAGEUP || e.key.key == SDLK_PAGEDOWN) ? 10 : 1;
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_MINUS || e.key.key == SDLK_KP_MINUS || e.key.key == SDLK_PAGEDOWN) {
                        v = clampSize(v - step);
                        continue;
                    }
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS || e.key.key == SDLK_KP_PLUS || e.key.key == SDLK_PAGEUP) {
                        v = clampSize(v + step);
                        continue;
                    }
                }
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    if (selectedRow == 3) {
                        out.name = trimSpaces(out.name);
                        if (out.name.empty()) out.name = "new_level";
                        out.accepted = true;
                        running = false;
                    } else if (selectedRow == 4) {
                        running = false;
                    } else {
                        setSelectedRow((selectedRow + 1) % 5, winW, winH, &rowName);
                    }
                    continue;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                handlePointerDown((int)e.button.x, (int)e.button.y);
            }
            if (isFingerDownEventType(e.type)) {
                const int tx = (int)std::lround(e.tfinger.x * winW);
                const int ty = (int)std::lround(e.tfinger.y * winH);
                handlePointerDown(tx, ty);
            }
            if (e.type == SDL_MOUSEWHEEL) {
                if (selectedRow == 1) out.width = clampSize(out.width + e.wheel.y);
                if (selectedRow == 2) out.height = clampSize(out.height + e.wheel.y);
            }
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 30, 34, 44, 255);
        SDL_RenderFillRect(ren, &panel);
        SDL_SetRenderDrawColor(ren, 200, 210, 230, 255);
        SDL_RenderDrawRect(ren, &panel);
        DrawText(ren, panel.x + 20, panel.y + 20, 3, "CREATE NEW LEVEL");

        auto drawRow = [&](const SDL_Rect& r, bool selected, const std::string& text) {
            SDL_SetRenderDrawColor(ren, selected ? 68 : 50, selected ? 98 : 72, selected ? 158 : 108, 255);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 220, 225, 235, 255);
            SDL_RenderDrawRect(ren, &r);
            DrawText(ren, r.x + 10, r.y + 10, 2, text);
        };
        auto drawAdjustButton = [&](const SDL_Rect& r, const char* label) {
            SDL_SetRenderDrawColor(ren, 44, 60, 92, 255);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 220, 225, 235, 255);
            SDL_RenderDrawRect(ren, &r);
            DrawText(ren, r.x + 12, r.y + 6, 2, label);
        };
        drawRow(rowName, selectedRow == 0, std::string("NAME: ") + out.name);
        drawRow(rowW, selectedRow == 1, std::string("WIDTH: ") + std::to_string(out.width));
        drawRow(rowH, selectedRow == 2, std::string("HEIGHT: ") + std::to_string(out.height));
        drawAdjustButton(rowWMinus, "-");
        drawAdjustButton(rowWPlus, "+");
        drawAdjustButton(rowHMinus, "-");
        drawAdjustButton(rowHPlus, "+");

        SDL_SetRenderDrawColor(ren, selectedRow == 3 ? 70 : 55, selectedRow == 3 ? 110 : 85, selectedRow == 3 ? 90 : 65, 255);
        SDL_RenderFillRect(ren, &createBtn);
        SDL_SetRenderDrawColor(ren, 205, 235, 210, 255);
        SDL_RenderDrawRect(ren, &createBtn);
        DrawText(ren, createBtn.x + 38, createBtn.y + 12, 2, "CREATE");

        SDL_SetRenderDrawColor(ren, selectedRow == 4 ? 110 : 80, selectedRow == 4 ? 70 : 55, selectedRow == 4 ? 90 : 70, 255);
        SDL_RenderFillRect(ren, &cancelBtn);
        SDL_SetRenderDrawColor(ren, 235, 205, 210, 255);
        SDL_RenderDrawRect(ren, &cancelBtn);
        DrawText(ren, cancelBtn.x + 38, cancelBtn.y + 12, 2, "CANCEL");
        DrawText(ren, panel.x + 20, panel.y + panel.h - 24, 2, "Touch +/- or use arrows. ENTER confirms.");

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    setPromptTextInput(false);
    return out;
}

class EditorSkin {
    SDL_Texture* texture = nullptr;
    std::unordered_map<std::string, Frame> frames;
public:
    explicit EditorSkin(SDL_Renderer* ren) {
        texture = loadTextureSafe(ren, "assets/Sheets/DF_Menus-uhd.png");
        frames = loadPlistFrames("assets/Sheets/DF_Menus-uhd.plist");
        if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    ~EditorSkin() { if (texture) SDL_DestroyTexture(texture); }
    void draw(SDL_Renderer* ren, const SDL_Rect& rect, bool active = false, bool panel = false) {
        auto frame = frames.find(panel ? "window" : "button_genreaic");
        if (!texture || frame == frames.end()) { SDL_RenderFillRect(ren, &rect); return; }
        const auto& f = frame->second;
        const Uint8 tint = panel || active ? 255 : 195;
        SDL_SetTextureColorMod(texture, tint, tint, tint);
        if (f.rotated) renderFrame(ren, texture, f, rect);
        else {
            const SDL_FRect src{(float)f.rect.x, (float)f.rect.y, (float)f.rect.w, (float)f.rect.h};
            const SDL_FRect dst{(float)rect.x, (float)rect.y, (float)rect.w, (float)rect.h};
            const float border = panel ? 56.0f : 6.0f;
            SDL_RenderTexture9Grid(ren, texture, &src, border, border, border, border,
                std::min(1.0f, std::min(rect.w, rect.h) / (border * 2)), &dst);
        }
        SDL_SetTextureColorMod(texture, 255, 255, 255);
    }
};

bool RunLevelConfigMenu(SDL_Window* win, SDL_Renderer* ren, LevelMeta& config) {
    LevelMeta draft = config;
    EditorSkin skin(ren);
    int selected = 0;
    bool running = true, applied = false, touching = false;
    SDL_FingerID finger = 0;
    SDL_Texture* savedTarget = SDL_GetRenderTarget(ren);
    int savedW = 0, savedH = 0;
    SDL_RendererLogicalPresentation savedPresentation;
    SDL_GetRenderLogicalPresentation(ren, &savedW, &savedH, &savedPresentation);
    SDL_SetRenderTarget(ren, nullptr);
    SDL_SetRenderLogicalPresentation(ren, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderViewport(ren, nullptr);
    SDL_SetRenderClipRect(ren, nullptr);
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
    const SDL_Rect rows[] = {{80, 92, 480, 52}, {80, 154, 480, 52}, {80, 216, 480, 52},
        {80, 278, 480, 52}, {80, 386, 232, 50}, {328, 386, 232, 50}};
    auto adjust = [&](int row, int direction) {
        int* value = row == 0 ? &draft.world : (row == 1 ? &draft.area : &draft.time);
        const int maximum = row == 0 ? 7 : (row == 1 ? 3 : 4);
        if (row < 3) *value = (*value + direction + maximum + 1) % (maximum + 1);
        else if (row == 3) draft.bouncyTile12 = !draft.bouncyTile12;
    };
    auto activate = [&](int row, int direction) {
        selected = row;
        if (row < 4) adjust(row, direction);
        else { applied = row == 4; running = false; }
    };
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) { SDL_PushEvent(&e); running = false; break; }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                const auto key = e.key.key;
                if (key == SDLK_ESCAPE || key == SDLK_AC_BACK) { running = false; break; }
                if (key == SDLK_UP) selected = (selected + 5) % 6;
                if (key == SDLK_DOWN || key == SDLK_TAB) selected = (selected + 1) % 6;
                if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    if (selected < 4) adjust(selected, key == SDLK_LEFT ? -1 : 1);
                    else selected = selected == 4 ? 5 : 4;
                }
                if (!e.key.repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)) activate(selected, 1);
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const auto button = e.gbutton.button;
                if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) { running = false; break; }
                if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) selected = (selected + 5) % 6;
                if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) selected = (selected + 1) % 6;
                if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                    if (selected < 4) adjust(selected, button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -1 : 1);
                    else selected = selected == 4 ? 5 : 4;
                }
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) activate(selected, 1);
            }
            int winW = 0, winH = 0, x = 0, y = 0;
            SDL_GetWindowSize(win, &winW, &winH);
            bool clicked = false;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID)
                clicked = windowToGamePoint((int)e.button.x, (int)e.button.y, winW, winH, 640, 480, x, y);
            if (e.type == SDL_EVENT_FINGER_DOWN && !touching) { touching = true; finger = e.tfinger.fingerID; }
            if (touching && e.type == SDL_EVENT_FINGER_UP && e.tfinger.fingerID == finger) {
                touching = false;
                clicked = windowToGamePoint((int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH), winW, winH, 640, 480, x, y);
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED || e.type == SDL_EVENT_WINDOW_FOCUS_LOST) touching = false;
            if (clicked) {
                SDL_Point point{x, y};
                for (int row = 0; row < 6; ++row) if (SDL_PointInRect(&point, &rows[row])) {
                    activate(row, row < 3 && x < rows[row].x + 70 ? -1 : 1);
                    break;
                }
            }
        }
        SDL_SetRenderDrawColor(ren, 20, 29, 42, 255);
        SDL_RenderClear(ren);
        skin.draw(ren, SDL_Rect{48, 8, 544, 448}, false, true);
        DrawText(ren, 320 - MeasureTextWidth(3, "Level Config") / 2, 24, 3, "Level Config");
        const int values[] = {draft.world, draft.area, draft.time};
        const char* names[] = {"World", "Area", "Time"};
        for (int row = 0; row < 6; ++row) {
            SDL_SetRenderDrawColor(ren, selected == row ? 65 : 39, selected == row ? 96 : 59, selected == row ? 130 : 82, 255);
            skin.draw(ren, rows[row], selected == row);
            SDL_SetRenderDrawColor(ren, selected == row ? 230 : 130, 205, 235, 255);
            SDL_RenderRect(ren, &rows[row]);
            const std::string label = row < 3 ? std::string(names[row]) + ": " + (values[row] ? std::to_string(values[row]) : "Auto") :
                (row == 3 ? std::string("Bouncy tile 12: ") + (draft.bouncyTile12 ? "ON" : "OFF") : (row == 4 ? "Apply" : "Cancel"));
            DrawText(ren, rows[row].x + (rows[row].w - MeasureTextWidth(2, label)) / 2, rows[row].y + 14, 2, label);
            if (row < 3) {
                DrawText(ren, rows[row].x + 20, rows[row].y + 14, 2, "-");
                DrawText(ren, rows[row].x + rows[row].w - 32, rows[row].y + 14, 2, "+");
            }
        }
        DrawText(ren, 80, 345, 1, "Auto keeps the original level setting. Jump overrides tile 12.");
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    SDL_SetRenderLogicalPresentation(ren, savedW, savedH, savedPresentation);
    SDL_SetRenderTarget(ren, savedTarget);
    if (applied) config = draft;
    return applied;
}


std::string RunLocalLevelEditor(SDL_Window* win, SDL_Renderer* ren, const std::string& initialPath = "",
                                const std::string& newLevelName = "", int newLevelW = -1, int newLevelH = -1) {
    constexpr int kEditorTileSize = 32;
    int gridW = (newLevelW > 0) ? std::clamp(newLevelW, 5, 400) : 30;
    int gridH = (newLevelH > 0) ? std::clamp(newLevelH, 5, 400) : 17;
    std::vector<unsigned short> grid((size_t)gridW * (size_t)gridH, 2);
    std::vector<ObjectInstance> placedObjects;
    LevelMeta editorConfig;
    editorConfig.world = editorConfig.area = editorConfig.time = 1;

    if (!initialPath.empty()) {
        TileMap loadedMap;
        std::vector<ObjectInstance> loadedObjects;
        LevelMeta loadedMeta;
        if (loadLevelBNNLVL(initialPath, loadedMap, loadedObjects, loadedMeta) && loadedMap.w > 0 && loadedMap.h > 0) {
            gridW = loadedMap.w;
            gridH = loadedMap.h;
            grid = loadedMap.tileIds;
            placedObjects = loadedObjects;
            editorConfig = loadedMeta;
        } else {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Level editor", "This level could not be loaded. The original file was not changed.", win);
            return {};
        }
    } else {
        for (int x = 0; x < gridW; ++x) grid[(gridH - 1) * gridW + x] = 1;
        const int spawnX = 3;
        const int spawnY = std::max(0, gridH - 4);
        grid[spawnY * gridW + spawnX] = 28;
    }

    const std::vector<unsigned short> palette{
        2, 7, 12, 13,
        14, 15, 16, 17, 18, 19, 20,
        24, 29, 30, 41, 49,
        51, 52, 53, 54, 55,
        83, 84, 85, 86, 87, 88,
        28, TileIds::BossBarrier
    };
    std::unordered_map<int, std::string> objectTypeById;
    const std::vector<int> objectPalette = loadEditorObjectPalette(objectTypeById);
    int selectedPalette = 1;
    int selectedObjectPalette = 0;
    int tilePaletteScroll = 0;
    int objectPaletteScroll = 0;
    bool objectMode = false;
    bool running = true;
    const Uint64 inputBlockUntilTicks = SDL_GetTicks() + 1000;
    bool paused = false;
    int pauseSel = 0; // Resume, Save & Resume, Save & Exit, Exit
    std::string savedPath;
    std::string saveTargetPath = initialPath.empty() ? buildNamedLocalLevelPath(newLevelName) : initialPath;
    std::string statusText;
    Uint64 statusUntil = 0;
    SDL_FingerID activeEditorFinger = 0;
    bool editorFingerActive = false;
    bool fingerPainting = false;
    bool fingerPaletteScroll = false;
    bool fingerPaletteMoved = false;
    int fingerPaletteLastY = 0;
    float fingerPaletteScrollAccum = 0.0f;
    int activeLayer = 0;
    int brushSize = 1;
    bool configureObjects = false;
    bool touchPanMode = false;
    bool touchEraseMode = false;
    auto toggleTouchPanMode = [&]() {
        touchPanMode = !touchPanMode;
        if (touchPanMode) { touchEraseMode = false; configureObjects = false; }
    };
    auto toggleTouchEraseMode = [&]() {
        touchEraseMode = !touchEraseMode;
        if (touchEraseMode) { touchPanMode = false; configureObjects = false; }
    };
    bool fingerGridPanning = false;
    float fingerGridLastX = 0.0f;
    float fingerGridLastY = 0.0f;
    float fingerGridViewX = 0.0f;
    float fingerGridViewY = 0.0f;
    int viewX = 0;
    int viewY = 0;
    int editorZoomPercent = 100;
    bool middlePanning = false;
    int lastPanMouseX = 0;
    int lastPanMouseY = 0;
    EditorSkin skin(ren);
    ButtonAnimations editorAnimations;
    auto snapshot = [&]() {
        std::string content(reinterpret_cast<const char*>(grid.data()), grid.size() * sizeof(grid[0]));
        return content + SerializeLevelMetaHeader(editorConfig) + SerializeLevelObjects(placedObjects);
    };
    bool upgradePending = !initialPath.empty() && LevelNeedsUpgrade(editorConfig);
    std::string upgradeBackup;
    if (upgradePending) { statusText = "OLD LEVEL - UPGRADE ON SAVE"; statusUntil = SDL_GetTicks() + 5000; }
    std::string savedSnapshot = initialPath.empty() || upgradePending ? std::string() : snapshot();
    Uint64 nextAutosave = SDL_GetTicks() + 15000;
    auto saveEditor = [&](bool exitAfter, bool automatic = false) {
        if (upgradePending && upgradeBackup.empty()) {
            std::string error;
            if (!LocalLevelEdits::backupForUpgrade(saveTargetPath, upgradeBackup, error)) {
                statusText = "UPGRADE BACKUP FAILED - RETRY"; statusUntil = SDL_GetTicks() + 5000; return false;
            }
        }
        const bool upgrading = upgradePending;
        const std::string path = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects, editorConfig, &upgradeBackup);
        if (path.empty()) { statusText = "SAVE FAILED - RETRY IN MENU"; statusUntil = SDL_GetTicks() + 5000; return false; }
        saveTargetPath = savedPath = path;
        upgradePending = false;
        editorConfig.formatVersion = kCurrentLevelFormatVersion;
        editorConfig.legacyEncoding = false;
        savedSnapshot = snapshot();
        nextAutosave = SDL_GetTicks() + 15000;
        statusText = upgrading ? "UPGRADED - BACKUP SAVED" : (automatic ? "AUTO SAVED" : "SAVED");
        statusUntil = SDL_GetTicks() + 2500;
        editorFingerActive = fingerPainting = fingerPaletteScroll = fingerGridPanning = middlePanning = false;
        if (exitAfter) running = false;
        return true;
    };
    auto pauseAction = [&](int action) {
        pauseSel = action;
        if (action == 0) paused = false;
        else if (action == 1) { if (saveEditor(false)) paused = false; }
        else if (action == 2) saveEditor(true);
        else if (snapshot() == savedSnapshot || saveEditor(false, true)) running = false;
    };
    auto openLevelConfig = [&]() {
        editorFingerActive = fingerPainting = fingerPaletteScroll = fingerGridPanning = middlePanning = false;
        RunLevelConfigMenu(win, ren, editorConfig);
    };
    InputSystem editorInput;
    editorInput.scanConnected();

    nlohmann::json texJson;
    {
        const std::string text = ReadTextFile("assets/textures.json");
        if (!text.empty()) {
            try {
                texJson = nlohmann::json::parse(text);
            } catch (...) {
                texJson = nlohmann::json();
            }
        }
    }
    auto texPath = [&](const std::string& section, const std::string& key, const std::string& fallback) -> std::string {
        if (texJson.contains(section) && texJson[section].is_object()) {
            const auto& sectionJson = texJson[section];
            if (sectionJson.contains(key) && sectionJson[key].is_string()) {
                return sectionJson[key].get<std::string>();
            }
        }
        return fallback;
    };

    SDL_Texture* blocksTex = loadTextureWithColorKey(
        ren,
        texPath("textures", "blocks", "assets/Sheets/DF_Blocks-uhd.png"),
        0x9f, 0x61, 0xff);
    if (blocksTex) SDL_SetTextureScaleMode(blocksTex, SDL_SCALEMODE_NEAREST);
    auto blocksFrameList = loadPlistFrameList(texPath("plists", "blocks", "assets/Sheets/DF_Blocks-uhd.plist"));
    std::unordered_map<std::string, Frame> blocksFrameByName;
    blocksFrameByName.reserve(blocksFrameList.size());
    for (const auto& e : blocksFrameList) blocksFrameByName[e.name] = e.frame;
    std::vector<const Frame*> blocksFrameById(65536, nullptr);
    for (const auto& e : blocksFrameList) {
        const int id = parseTileIdFromFrameName(e.name, true);
        if (id < 0 || id >= (int)blocksFrameById.size()) continue;
        blocksFrameById[id] = &e.frame;
    }
    // Fallback: allow names like "3.1" to map to tile 3 if exact numeric frame is missing.
    for (const auto& e : blocksFrameList) {
        const int id = parseTileIdFromFrameName(e.name, false);
        if (id >= 0 && id < (int)blocksFrameById.size() && !blocksFrameById[id]) blocksFrameById[id] = &e.frame;
    }

    auto tileColor = [](unsigned short id, Uint8& r, Uint8& g, Uint8& b) {
        if (id == TileIds::BossBarrier) { r = 175; g = 70; b = 225; return; }
        if (id == 1) { r = 120; g = 120; b = 130; return; }
        if (id == 2) { r = 24; g = 26; b = 34; return; }
        if (id == 24) { r = 220; g = 180; b = 40; return; }
        if (id == 28) { r = 80; g = 200; b = 90; return; }
        if (id == 30) { r = 240; g = 80; b = 120; return; }
        if (id == 68) { r = 150; g = 100; b = 255; return; }
        r = 180; g = 180; b = 180;
    };
    SDL_Texture* editorEntities = loadTextureSafe(ren, "assets/Sheets/DF_Enitys-uhd.png");
    const auto editorEntityFrames = loadPlistFrames("assets/Sheets/DF_Enitys-uhd.plist");
    auto drawObjectTexture = [&](int id, const SDL_Rect& rect, bool ghost) {
        const char* key = id == 31 ? "Spring" : (id == 46 ? "Bumper" :
            (id == ObjectIds::MovingPlatform ? "moving_block" : (id == ObjectIds::Shield ? "Shied_icon" :
            (id == ObjectIds::ShellChraker ? "ShellChraker_base" : ""))));
        const auto frame = editorEntityFrames.find(key);
        if (!editorEntities || frame == editorEntityFrames.end()) return false;
        SDL_SetTextureColorMod(editorEntities, ghost ? 80 : 255, ghost ? 80 : 255, ghost ? 80 : 255);
        renderFrame(ren, editorEntities, frame->second, rect);
        SDL_SetTextureColorMod(editorEntities, 255, 255, 255);
        return true;
    };
    auto isAutoTileId = [](unsigned short id) -> bool {
        return (id >= 3 && id <= 11) || (id >= 38 && id <= 40);
    };
    auto autoTileForCell = [&](int cx, int cy) -> unsigned short {
        auto occupied = [&](int x, int y) -> bool {
            // Treat outside-map space as connected so auto-tiles extend through borders.
            if (x < 0 || y < 0 || x >= gridW || y >= gridH) return true;
            return isAutoTileId(grid[y * gridW + x]);
        };
        const bool u = occupied(cx, cy - 1);
        const bool d = occupied(cx, cy + 1);
        const bool l = occupied(cx - 1, cy);
        const bool r = occupied(cx + 1, cy);

        // One-block-height strip variant.
        if (!u && !d) {
            if (!l && r) return 39;   // left end
            if (l && !r) return 40;   // right end
            return 38;                // middle / single
        }
        // Top edge variants.
        if (!u) {
            if (!l) return 9;         // up-left
            if (!r) return 11;        // up-right
            return 10;                // up-middle
        }
        // Bottom edge variants.
        if (!d) {
            if (!l) return 3;         // bottom-left
            if (!r) return 5;         // bottom-right
            return 4;                 // bottom-middle
        }
        // Vertical middle variants.
        if (!l) return 6;             // left-middle
        if (!r) return 8;             // right-middle
        return 7;                     // middle
    };
    auto refreshAutoTileAround = [&](int cx, int cy) {
        for (int y = cy - 1; y <= cy + 1; ++y) {
            for (int x = cx - 1; x <= cx + 1; ++x) {
                if (x < 0 || y < 0 || x >= gridW || y >= gridH) continue;
                const int idx = y * gridW + x;
                if (!isAutoTileId(grid[idx])) continue;
                grid[idx] = autoTileForCell(x, y);
            }
        }
    };
    auto paintAt = [&](int cx, int cy, bool erase) {
        if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return;
        const int idx = cy * gridW + cx;
        const unsigned short chosen = palette[selectedPalette];
        const bool paintAutoTile = isAutoTileId(chosen);
        if (erase) {
            grid[idx] = 2;
            refreshAutoTileAround(cx, cy);
            return;
        }
        if (paintAutoTile) {
            // Seed with a center tile then resolve neighborhood variants.
            grid[idx] = 7;
            refreshAutoTileAround(cx, cy);
            return;
        }
        grid[idx] = chosen;
        // Keep neighboring auto-tiles valid on every tile placement.
        refreshAutoTileAround(cx, cy);
    };
    auto configureObjectAt = [&](int cx, int cy) {
        const int index = EditorObjects::find(placedObjects, cx, cy, activeLayer);
        if (index < 0) { statusText = "SELECT AN OBJECT ON THIS LAYER"; statusUntil = SDL_GetTicks() + 2500; return; }
        ObjectInstance draft = placedObjects[index];
        auto inputNumber = [&](int initial, int minimum, int maximum, const std::string& title, int& result) {
            const auto text = RunLocalLevelRenamePrompt(win, ren, std::to_string(initial), title,
                "VALUE (" + std::to_string(minimum) + "-" + std::to_string(maximum) + "): ");
            if (text.empty()) return false;
            try {
                size_t end = 0;
                const int value = std::stoi(text, &end);
                if (end != text.size() || value < minimum || value > maximum) throw std::out_of_range("value");
                result = value;
                return true;
            } catch (...) { statusText = "INVALID VALUE - OBJECT UNCHANGED"; statusUntil = SDL_GetTicks() + 2500; return false; }
        };
        editorFingerActive = fingerPainting = false;
        if (const auto* parameter = LevelParameters::definition(draft)) {
            int value = draft.parameterValue.value_or(parameter->initial);
            if (!inputNumber(value, parameter->minimum, parameter->maximum, parameter->label, value)) return;
            draft.parameterValue = value;
        }
        if (!inputNumber(draft.layer, 0, EditorObjects::maxLayer, "OBJECT LAYER", draft.layer)) return;
        placedObjects.erase(placedObjects.begin() + index);
        EditorObjects::replace(placedObjects, draft);
        activeLayer = draft.layer;
    };
    auto placeObjectAt = [&](int cx, int cy, bool erase) {
        if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return;
        if (erase) { EditorObjects::erase(placedObjects, cx, cy, activeLayer, touchEraseMode); return; }
        if (configureObjects) { configureObjectAt(cx, cy); return; }
        if (selectedObjectPalette < 0 || selectedObjectPalette >= (int)objectPalette.size()) return;
        ObjectInstance obj;
        obj.id = std::to_string(objectPalette[selectedObjectPalette]);
        obj.x = (float)(cx * kEditorTileSize + kEditorTileSize / 2);
        obj.y = (float)(cy * kEditorTileSize + kEditorTileSize / 2);
        obj.layer = activeLayer;
        const int existing = EditorObjects::find(placedObjects, cx, cy, activeLayer);
        if (existing >= 0 && placedObjects[existing].id == obj.id) return;
        if (const auto* parameter = LevelParameters::definition(obj)) obj.parameterValue = parameter->initial;
        EditorObjects::replace(placedObjects, obj);
    };
    auto applyAt = [&](int cx, int cy, bool erase) {
        if (configureObjects) { configureObjectAt(cx, cy); return; }
        for (const auto& cell : EditorBrush::cells(gridW, gridH, cx, cy, brushSize)) {
            if (objectMode) placeObjectAt(cell.first, cell.second, erase);
            else paintAt(cell.first, cell.second, erase);
        }
    };

    while (running) {
        int winW = 0, winH = 0;
        getWindowSizeInPixelsCompat(win, winW, winH);
        const int margin = std::clamp(std::min(winW, winH) / 80, 6, 12);
        // Keep the canvas useful on phone-sized windows while allowing larger,
        // finger-friendly controls when more room is available.
        const int sideW = std::clamp(winW / 3, 188, 280);
        const int gridX = margin;
        const int gridY = margin;
        const int panelX = std::max(gridX + 16, winW - margin - sideW);
        const int gridViewW = std::max(120, panelX - gridX - margin);
        const int gridViewH = std::max(120, winH - margin * 2);
        const int baseCell = std::max(12, std::min(gridViewW / std::max(1, gridW), gridViewH / std::max(1, gridH)));
        int cell = std::max(8, (baseCell * editorZoomPercent) / 100);
        const int gridPxW = gridW * cell;
        const int gridPxH = gridH * cell;
        const int maxViewX = std::max(0, gridPxW - gridViewW);
        const int maxViewY = std::max(0, gridPxH - gridViewH);
        viewX = std::clamp(viewX, 0, maxViewX);
        viewY = std::clamp(viewY, 0, maxViewY);
        auto setEditorZoom = [&](int nextZoomPercent, int anchorX, int anchorY) {
            nextZoomPercent = std::clamp(nextZoomPercent, 50, 300);
            if (nextZoomPercent == editorZoomPercent) return;
            const float worldX = ((float)(anchorX - gridX) + (float)viewX) / (float)std::max(1, cell);
            const float worldY = ((float)(anchorY - gridY) + (float)viewY) / (float)std::max(1, cell);
            editorZoomPercent = nextZoomPercent;
            const int nextCell = std::max(8, (baseCell * editorZoomPercent) / 100);
            const int nextMaxViewX = std::max(0, gridW * nextCell - gridViewW);
            const int nextMaxViewY = std::max(0, gridH * nextCell - gridViewH);
            viewX = std::clamp((int)std::lround(worldX * (float)nextCell - (float)(anchorX - gridX)), 0, nextMaxViewX);
            viewY = std::clamp((int)std::lround(worldY * (float)nextCell - (float)(anchorY - gridY)), 0, nextMaxViewY);
        };
        SDL_Rect gridViewport{gridX, gridY, gridViewW, gridViewH};
        const int controlGap = 8;
        const int controlW = (sideW - controlGap) / 2;
        const int controlH = 44;
        SDL_Rect tileModeBtn{panelX, margin, controlW, controlH};
        SDL_Rect objectModeBtn{panelX + controlW + controlGap, margin, controlW, controlH};
        SDL_Rect panModeBtn{panelX, margin + controlH + controlGap, controlW, controlH};
        SDL_Rect eraseModeBtn{panelX + controlW + controlGap, margin + controlH + controlGap, controlW, controlH};
        SDL_Rect zoomOutBtn{panelX, margin + (controlH + controlGap) * 2, controlW, controlH};
        SDL_Rect zoomInBtn{panelX + controlW + controlGap, margin + (controlH + controlGap) * 2, controlW, controlH};
        SDL_Rect layerDownBtn{panelX, margin + (controlH + controlGap) * 3, controlW, controlH};
        SDL_Rect layerUpBtn{panelX + controlW + controlGap, layerDownBtn.y, controlW, controlH};
        SDL_Rect brushDownBtn{panelX, margin + (controlH + controlGap) * 4, controlW, controlH};
        SDL_Rect brushUpBtn{panelX + controlW + controlGap, brushDownBtn.y, controlW, controlH};
        const int paletteStartY = margin + (controlH + controlGap) * 5 + 4;
        const int actionY = winH - margin - 48;
        SDL_Rect configBtn{panelX, actionY, sideW, 44};
        SDL_Rect objectConfigBtn{panelX, actionY - 52, sideW, 44};
        const int paletteRowH = 48;
        const int paletteBottomY = objectConfigBtn.y - 66;
        const int paletteViewRows = std::max(1, (paletteBottomY - paletteStartY) / paletteRowH);
        const int paletteCountNow = objectMode ? (int)objectPalette.size() : (int)palette.size();
        int& activePaletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
        const int maxPaletteScroll = std::max(0, paletteCountNow - paletteViewRows);
        activePaletteScroll = std::clamp(activePaletteScroll, 0, maxPaletteScroll);
        SDL_Rect paletteViewport{panelX, paletteStartY, sideW, paletteViewRows * paletteRowH};
        const int pauseW = std::min(380, winW - margin * 2);
        const int pauseH = std::min(310, winH - margin * 2);
        SDL_Rect pausePanel{(winW - pauseW) / 2, (winH - pauseH) / 2, pauseW, pauseH};
        const int pauseButtonW = pauseW - 32;
        const int pauseButtonH = std::clamp((pauseH - 116) / 4, 16, 44);
        SDL_Rect pauseBtns[4];
        for (int i = 0; i < 4; ++i) pauseBtns[i] = {pausePanel.x + 16, pausePanel.y + 76 + i * (pauseButtonH + 8), pauseButtonW, pauseButtonH};

        SDL_Event e;
        while (running && SDL_PollEvent(&e)) {
            editorInput.handleEvent(e);
            if (e.type == SDL_QUIT) { running = false; break; }
            if (SDL_GetTicks() < inputBlockUntilTicks) continue;
            if (!paused) {
                SDL_Point toolPoint{};
                const bool mouseTool = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID;
                const bool touchTool = e.type == SDL_EVENT_FINGER_DOWN;
                if (mouseTool) toolPoint = {(int)e.button.x, (int)e.button.y};
                if (touchTool) toolPoint = {(int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH)};
                if ((mouseTool || touchTool) && (SDL_PointInRect(&toolPoint, &layerDownBtn) || SDL_PointInRect(&toolPoint, &layerUpBtn))) {
                    activeLayer = std::clamp(activeLayer + (SDL_PointInRect(&toolPoint, &layerUpBtn) ? 1 : -1), 0, EditorObjects::maxLayer);
                    editorFingerActive = fingerPainting = false;
                    continue;
                }
                if ((mouseTool || touchTool) && (SDL_PointInRect(&toolPoint, &brushDownBtn) || SDL_PointInRect(&toolPoint, &brushUpBtn))) {
                    brushSize = std::clamp(brushSize + (SDL_PointInRect(&toolPoint, &brushUpBtn) ? 1 : -1), 1, EditorBrush::maxSize);
                    editorFingerActive = fingerPainting = false; continue;
                }
                if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.key == SDLK_COMMA || e.key.key == SDLK_PERIOD)) {
                    brushSize = std::clamp(brushSize + (e.key.key == SDLK_PERIOD ? 2 : -2), 1, EditorBrush::maxSize); continue;
                }
                const bool configObject = ((mouseTool || touchTool) && SDL_PointInRect(&toolPoint, &objectConfigBtn)) ||
                    (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F3);
                if (configObject) {
                    configureObjects = !configureObjects; objectMode = true;
                    touchPanMode = touchEraseMode = false; editorFingerActive = fingerPainting = false;
                    continue;
                }
                if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_DELETE) { toggleTouchEraseMode(); continue; }
                if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.key == SDLK_LEFTBRACKET || e.key.key == SDLK_RIGHTBRACKET)) {
                    activeLayer = std::clamp(activeLayer + (e.key.key == SDLK_RIGHTBRACKET ? 1 : -1), 0, EditorObjects::maxLayer); continue;
                }
                bool configure = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F2;
                configure = configure || (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST);
                SDL_Point point{};
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
                    point = {(int)e.button.x, (int)e.button.y};
                    configure = configure || SDL_PointInRect(&point, &configBtn);
                }
                if (e.type == SDL_EVENT_FINGER_DOWN) {
                    point = {(int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH)};
                    configure = configure || SDL_PointInRect(&point, &configBtn);
                }
                if (configure) { openLevelConfig(); continue; }
            }
            if (e.type == SDL_MOUSEWHEEL && !paused) {
                float mx = 0.0f, my = 0.0f;
                SDL_GetMouseState(&mx, &my);
                SDL_Point pt{(int)std::lround(mx), (int)std::lround(my)};
                if (SDL_PointInRect(&pt, &paletteViewport)) {
                    const int wheelY = (int)std::lround(e.wheel.y);
                    activePaletteScroll = std::clamp(activePaletteScroll - wheelY, 0, maxPaletteScroll);
                    continue;
                }
                const bool ctrlHeld = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
                if (ctrlHeld && SDL_PointInRect(&pt, &gridViewport)) {
                    const int wheelY = (int)std::lround(e.wheel.y);
                    if (wheelY != 0) setEditorZoom(editorZoomPercent + wheelY * 25, pt.x, pt.y);
                    continue;
                }
                const bool shiftHeld = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                const int step = std::max(8, cell * 2);
                if (shiftHeld || e.wheel.x != 0) {
                    viewX -= e.wheel.x * step;
                    if (shiftHeld) viewX -= e.wheel.y * step;
                } else {
                    viewY -= e.wheel.y * step;
                }
                viewX = std::clamp(viewX, 0, maxViewX);
                viewY = std::clamp(viewY, 0, maxViewY);
                continue;
            }
            if (e.type == SDL_FINGERDOWN && !editorFingerActive) {
                activeEditorFinger = e.tfinger.fingerID;
                editorFingerActive = true;
                const int tx = (int)std::lround(e.tfinger.x * winW);
                const int ty = (int)std::lround(e.tfinger.y * winH);
                SDL_Point pt{tx, ty};

                if (paused) {
                    for (int i = 0; i < 4; ++i) if (SDL_PointInRect(&pt, &pauseBtns[i])) pauseAction(i);
                    continue;
                }

                if (SDL_PointInRect(&pt, &tileModeBtn)) {
                    objectMode = false;
                    touchPanMode = false;
                    fingerPainting = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &objectModeBtn)) {
                    objectMode = true;
                    touchPanMode = false;
                    fingerPainting = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &panModeBtn)) {
                    toggleTouchPanMode();
                    fingerPainting = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &eraseModeBtn)) {
                    toggleTouchEraseMode();
                    fingerPainting = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &zoomOutBtn)) {
                    setEditorZoom(editorZoomPercent - 25, gridX + gridViewW / 2, gridY + gridViewH / 2);
                    fingerPainting = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &zoomInBtn)) {
                    setEditorZoom(editorZoomPercent + 25, gridX + gridViewW / 2, gridY + gridViewH / 2);
                    fingerPainting = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &paletteViewport)) {
                    fingerPainting = false;
                    fingerPaletteScroll = true;
                    fingerPaletteMoved = false;
                    fingerPaletteLastY = ty;
                    fingerPaletteScrollAccum = 0.0f;
                    continue;
                }
                const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
                int& paletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
                const int maxScroll = std::max(0, paletteCount - paletteViewRows);
                paletteScroll = std::clamp(paletteScroll, 0, maxScroll);
                for (int row = 0; row < paletteViewRows; ++row) {
                    const int i = paletteScroll + row;
                    if (i >= paletteCount) break;
                    SDL_Rect r{panelX, paletteStartY + row * paletteRowH, sideW - 12, 42};
                    if (SDL_PointInRect(&pt, &r)) {
                        if (objectMode) selectedObjectPalette = i;
                        else selectedPalette = i;
                        fingerPainting = false;
                        break;
                    }
                }
                if (SDL_PointInRect(&pt, &gridViewport)) {
                    if (touchPanMode) {
                        fingerGridPanning = true;
                        fingerGridLastX = e.tfinger.x * winW;
                        fingerGridLastY = e.tfinger.y * winH;
                        fingerGridViewX = (float)viewX;
                        fingerGridViewY = (float)viewY;
                        fingerPainting = false;
                        continue;
                    }
                    const int cx = ((tx - gridX) + viewX) / cell;
                    const int cy = ((ty - gridY) + viewY) / cell;
                    applyAt(cx, cy, touchEraseMode);
                    fingerPainting = !configureObjects;
                } else {
                    fingerPainting = false;
                }
                continue;
            }
            if (e.type == SDL_FINGERMOTION && editorFingerActive && e.tfinger.fingerID == activeEditorFinger) {
                const int tx = (int)std::lround(e.tfinger.x * winW);
                const int ty = (int)std::lround(e.tfinger.y * winH);
                SDL_Point pt{tx, ty};
                if (fingerGridPanning) {
                    const float touchX = e.tfinger.x * winW;
                    const float touchY = e.tfinger.y * winH;
                    fingerGridViewX -= touchX - fingerGridLastX;
                    fingerGridViewY -= touchY - fingerGridLastY;
                    fingerGridViewX = std::clamp(fingerGridViewX, 0.0f, (float)maxViewX);
                    fingerGridViewY = std::clamp(fingerGridViewY, 0.0f, (float)maxViewY);
                    viewX = (int)std::lround(fingerGridViewX);
                    viewY = (int)std::lround(fingerGridViewY);
                    fingerGridLastX = touchX;
                    fingerGridLastY = touchY;
                    continue;
                }
                if (fingerPaletteScroll) {
                    const int dy = fingerPaletteLastY - ty;
                    fingerPaletteLastY = ty;
                    if (std::abs(dy) >= 2) fingerPaletteMoved = true;
                    fingerPaletteScrollAccum += (float)dy / (float)paletteRowH;
                    int steps = (int)std::trunc(fingerPaletteScrollAccum);
                    if (steps != 0) {
                        const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
                        int& paletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
                        const int maxScroll = std::max(0, paletteCount - paletteViewRows);
                        paletteScroll = std::clamp(paletteScroll + steps, 0, maxScroll);
                        fingerPaletteScrollAccum -= (float)steps;
                    }
                    continue;
                }
                if (paused || !fingerPainting) continue;
                if (SDL_PointInRect(&pt, &gridViewport)) {
                    const int cx = ((tx - gridX) + viewX) / cell;
                    const int cy = ((ty - gridY) + viewY) / cell;
                    applyAt(cx, cy, touchEraseMode);
                }
                continue;
            }
            if ((e.type == SDL_FINGERUP || e.type == SDL_EVENT_FINGER_CANCELED) &&
                editorFingerActive && e.tfinger.fingerID == activeEditorFinger) {
                if (e.type == SDL_FINGERUP && fingerPaletteScroll) {
                    const int tx = (int)std::lround(e.tfinger.x * winW);
                    const int ty = (int)std::lround(e.tfinger.y * winH);
                    SDL_Point pt{tx, ty};
                    if (!fingerPaletteMoved && SDL_PointInRect(&pt, &paletteViewport)) {
                        const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
                        int& paletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
                        const int maxScroll = std::max(0, paletteCount - paletteViewRows);
                        paletteScroll = std::clamp(paletteScroll, 0, maxScroll);
                        for (int row = 0; row < paletteViewRows; ++row) {
                            const int i = paletteScroll + row;
                            if (i >= paletteCount) break;
                            SDL_Rect r{panelX, paletteStartY + row * paletteRowH, sideW - 12, 42};
                            if (SDL_PointInRect(&pt, &r)) {
                                if (objectMode) selectedObjectPalette = i;
                                else selectedPalette = i;
                                break;
                            }
                        }
                    }
                }
                fingerPaletteScroll = false;
                fingerPaletteMoved = false;
                fingerPaletteScrollAccum = 0.0f;
                fingerGridPanning = false;
                activeEditorFinger = 0;
                editorFingerActive = false;
                fingerPainting = false;
                continue;
            }
            // Some platforms still emit compatibility mouse events for a touch
            // even when SDL_HINT_TOUCH_MOUSE_EVENTS is disabled.  Do not let the
            // synthesized click toggle PAN or ERASE a second time.
            const bool touchGeneratedMouseEvent =
                ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) &&
                 e.button.which == SDL_TOUCH_MOUSEID) ||
                (e.type == SDL_MOUSEMOTION && e.motion.which == SDL_TOUCH_MOUSEID);
            if (touchGeneratedMouseEvent) continue;
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.key == SDLK_F11) {
#if !PLATFORMER_MOBILE
                    const Uint32 flags = SDL_GetWindowFlags(win);
                    const bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(win, !isFullscreen);
#endif
                    continue;
                }
                if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) {
                    paused = !paused;
                    continue;
                }
                if (paused) {
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_UP || e.key.key == SDLK_a) pauseSel = std::max(0, pauseSel - 1);
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_DOWN || e.key.key == SDLK_d) pauseSel = std::min(3, pauseSel + 1);
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) pauseAction(pauseSel);
                    continue;
                }
                if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9) {
                    int idx = (int)(e.key.key - SDLK_1);
                    if (objectMode) {
                        if (idx >= 0 && idx < (int)objectPalette.size()) selectedObjectPalette = idx;
                    } else {
                        if (idx >= 0 && idx < (int)palette.size()) selectedPalette = idx;
                    }
                }
                if (e.key.key == SDLK_o || e.key.key == SDLK_O) {
                    objectMode = !objectMode;
                }
                if (e.key.key == SDLK_MINUS || e.key.key == SDLK_KP_MINUS) {
                    setEditorZoom(editorZoomPercent - 25, gridX + gridViewW / 2, gridY + gridViewH / 2);
                }
                if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS || e.key.key == SDLK_KP_PLUS) {
                    setEditorZoom(editorZoomPercent + 25, gridX + gridViewW / 2, gridY + gridViewH / 2);
                }
                if ((e.key.key == SDLK_c || e.key.key == SDLK_C) && touchEraseMode) {
                    std::fill(grid.begin(), grid.end(), (unsigned short)2);
                    for (int x = 0; x < gridW; ++x) grid[(gridH - 1) * gridW + x] = 1;
                    const int spawnX = std::min(3, std::max(0, gridW - 1));
                    const int spawnY = std::max(0, gridH - 4);
                    grid[spawnY * gridW + spawnX] = 28;
                    placedObjects.erase(std::remove_if(placedObjects.begin(), placedObjects.end(), [&](const auto& obj) { return obj.layer == activeLayer; }), placedObjects.end());
                }
                if (e.key.key == SDLK_LEFT) viewX = std::max(0, viewX - std::max(8, cell * 2));
                if (e.key.key == SDLK_RIGHT) viewX = std::min(maxViewX, viewX + std::max(8, cell * 2));
                if (e.key.key == SDLK_UP) viewY = std::max(0, viewY - std::max(8, cell * 2));
                if (e.key.key == SDLK_DOWN) viewY = std::min(maxViewY, viewY + std::max(8, cell * 2));
                if (e.key.key == SDLK_s || e.key.key == SDLK_S) saveEditor(false);

            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const bool isBackBtn =
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK ||
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
                const bool isAcceptBtn =
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_START;
                if (isBackBtn) {
                    paused = !paused;
                    continue;
                }
                if (paused) {
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) pauseSel = std::max(0, pauseSel - 1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT || e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) pauseSel = std::min(3, pauseSel + 1);
                    if (isAcceptBtn) pauseAction(pauseSel);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    if (objectMode) {
                        selectedObjectPalette = (selectedObjectPalette + (int)objectPalette.size() - 1) % (int)objectPalette.size();
                    } else {
                        selectedPalette = (selectedPalette + (int)palette.size() - 1) % (int)palette.size();
                    }
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    if (objectMode) {
                        selectedObjectPalette = (selectedObjectPalette + 1) % (int)objectPalette.size();
                    } else {
                        selectedPalette = (selectedPalette + 1) % (int)palette.size();
                    }
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                    objectMode = !objectMode;
                    continue;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && paused) {
                SDL_Point pt{(int)e.button.x, (int)e.button.y};
                for (int i = 0; i < 4; ++i) if (SDL_PointInRect(&pt, &pauseBtns[i])) pauseAction(i);
                continue;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION) {
                if (paused) continue;
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && touchPanMode) {
                    SDL_Point pt{(int)e.button.x, (int)e.button.y};
                    if (SDL_PointInRect(&pt, &gridViewport)) {
                        middlePanning = true;
                        lastPanMouseX = e.button.x;
                        lastPanMouseY = e.button.y;
                        continue;
                    }
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_MIDDLE) {
                    SDL_Point pt{(int)e.button.x, (int)e.button.y};
                    if (SDL_PointInRect(&pt, &gridViewport)) {
                        middlePanning = true;
                        lastPanMouseX = e.button.x;
                        lastPanMouseY = e.button.y;
                        continue;
                    }
                }
                if (e.type == SDL_MOUSEMOTION && middlePanning) {
                    const int dx = e.motion.x - lastPanMouseX;
                    const int dy = e.motion.y - lastPanMouseY;
                    lastPanMouseX = e.motion.x;
                    lastPanMouseY = e.motion.y;
                    viewX = std::clamp(viewX - dx, 0, maxViewX);
                    viewY = std::clamp(viewY - dy, 0, maxViewY);
                    continue;
                }
                bool dragPaint = (e.type == SDL_MOUSEMOTION) && ((e.motion.state & SDL_BUTTON_LMASK) || (e.motion.state & SDL_BUTTON_RMASK));
                bool clickPaint = (e.type == SDL_MOUSEBUTTONDOWN) && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT);
                if (!dragPaint && !clickPaint) continue;
                if (configureObjects && dragPaint) continue;
                int mx = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
                int my = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
                bool erase = false;
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    erase = (e.button.button == SDL_BUTTON_RIGHT) ||
                            (e.button.button == SDL_BUTTON_LEFT && touchEraseMode);
                }
                if (e.type == SDL_MOUSEMOTION) {
                    erase = ((e.motion.state & SDL_BUTTON_RMASK) != 0) ||
                            (((e.motion.state & SDL_BUTTON_LMASK) != 0) && touchEraseMode);
                }
                SDL_Point pt{mx, my};
                if (SDL_PointInRect(&pt, &gridViewport)) {
                    int cx = ((mx - gridX) + viewX) / cell;
                    int cy = ((my - gridY) + viewY) / cell;
                    applyAt(cx, cy, erase);
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP &&
                (e.button.button == SDL_BUTTON_MIDDLE || e.button.button == SDL_BUTTON_LEFT)) {
                middlePanning = false;
                continue;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (paused) continue;
                int mx = e.button.x;
                int my = e.button.y;
                SDL_Point pt{mx, my};
                if (SDL_PointInRect(&pt, &tileModeBtn)) {
                    objectMode = false;
                    touchPanMode = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &objectModeBtn)) {
                    objectMode = true;
                    touchPanMode = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &panModeBtn)) {
                    toggleTouchPanMode();
                    continue;
                }
                if (SDL_PointInRect(&pt, &eraseModeBtn)) {
                    toggleTouchEraseMode();
                    continue;
                }
                if (SDL_PointInRect(&pt, &zoomOutBtn)) {
                    setEditorZoom(editorZoomPercent - 25, gridX + gridViewW / 2, gridY + gridViewH / 2);
                    continue;
                }
                if (SDL_PointInRect(&pt, &zoomInBtn)) {
                    setEditorZoom(editorZoomPercent + 25, gridX + gridViewW / 2, gridY + gridViewH / 2);
                    continue;
                }
                const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
                int& paletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
                const int maxScroll = std::max(0, paletteCount - paletteViewRows);
                paletteScroll = std::clamp(paletteScroll, 0, maxScroll);
                for (int row = 0; row < paletteViewRows; ++row) {
                    const int i = paletteScroll + row;
                    if (i >= paletteCount) break;
                    SDL_Rect r{panelX, paletteStartY + row * paletteRowH, sideW - 12, 42};
                    if (mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h) {
                        if (objectMode) selectedObjectPalette = i;
                        else selectedPalette = i;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 18, 20, 26, 255);
        SDL_RenderClear(ren);
        skin.draw(ren, SDL_Rect{panelX - 6, margin - 6, sideW + 12, winH - margin * 2 + 12}, false, true);
        SDL_SetRenderClipRect(ren, &gridViewport);

        const int drawX0 = std::max(0, viewX / cell);
        const int drawY0 = std::max(0, viewY / cell);
        const int drawX1 = std::min(gridW - 1, (viewX + gridViewW + cell - 1) / cell);
        const int drawY1 = std::min(gridH - 1, (viewY + gridViewH + cell - 1) / cell);
        for (int y = drawY0; y <= drawY1; ++y) {
            for (int x = drawX0; x <= drawX1; ++x) {
                unsigned short id = grid[y * gridW + x];
                SDL_Rect rc{gridX + x * cell - viewX, gridY + y * cell - viewY, cell, cell};
                const Frame* f = (id < blocksFrameById.size()) ? blocksFrameById[id] : nullptr;
                if (blocksTex && f) {
                    renderFrame(ren, blocksTex, *f, rc);
                } else {
                    Uint8 r, g, b;
                    tileColor(id, r, g, b);
                    SDL_SetRenderDrawColor(ren, r, g, b, 255);
                    SDL_RenderFillRect(ren, &rc);
                }
            }
        }
        auto objectColor = [](int id, Uint8& r, Uint8& g, Uint8& b) {
            if (id == 31) { r = 110; g = 230; b = 140; return; }
            if (id == 46) { r = 240; g = 110; b = 120; return; }
            if (id == 57) { r = 120; g = 240; b = 255; return; } // fast travel up
            if (id == 58) { r = 120; g = 220; b = 200; return; } // fast travel down
            if (id == 59) { r = 255; g = 210; b = 120; return; } // fast travel left
            if (id == 60) { r = 255; g = 170; b = 120; return; } // fast travel right
            if (id == 61) { r = 210; g = 150; b = 255; return; } // fast travel exit
            if (id == 62) { r = 100; g = 170; b = 255; return; } // level wrap x
            if (id == 63) { r = 100; g = 205; b = 255; return; } // level wrap y
            if (id == 69) { r = 255; g = 135; b = 80; return; } // disable wrap x
            if (id == 70) { r = 255; g = 95; b = 145; return; } // disable wrap y
            if (id == 64) { r = 255; g = 230; b = 90; return; } // key
            if (id == 65) { r = 150; g = 110; b = 255; return; } // door
            if (id == 67) { r = 120; g = 180; b = 255; return; }
            if (id == ObjectIds::Pickup) { r = 120; g = 255; b = 185; return; } // pickup
            if (id == ObjectIds::Goal) { r = 255; g = 245; b = 150; return; } // goal
            if (id == ObjectIds::ShellChraker) { r = 220; g = 45; b = 35; return; }
            if (id == ObjectIds::MovingPlatform) { r = 130; g = 155; b = 185; return; }
            if (id == ObjectIds::Shield) { r = 90; g = 160; b = 255; return; }
            r = 220; g = 220; b = 240;
        };
        auto objectLabel = [&](int id) -> std::string {
            if (id == 31) return "SPRING (31)";
            if (id == 46) return "BUMPER (46)";
            if (id == 57) return "FAST UP (57)";
            if (id == 58) return "FAST DOWN (58)";
            if (id == 59) return "FAST LEFT (59)";
            if (id == 60) return "FAST RIGHT (60)";
            if (id == 61) return "FAST EXIT (61)";
            if (id == 67) return "END SIGN (67)";
            auto it = objectTypeById.find(id);
            if (it != objectTypeById.end()) return editorObjectLabelFromType(it->second, id);
            return std::string("Obj ") + std::to_string(id);
        };
        auto objectTag = [](int id) -> const char* {
            if (id == 57) return "UP";
            if (id == 58) return "DN";
            if (id == 59) return "LT";
            if (id == 60) return "RT";
            if (id == 61) return "EX";
            if (id == 62) return "WX";
            if (id == 63) return "WY";
            if (id == 69) return "NX";
            if (id == 70) return "NY";
            return "";
        };
        auto editorOrder = EditorObjects::renderOrder(placedObjects);
        std::stable_partition(editorOrder.begin(), editorOrder.end(), [&](int index) { return placedObjects[index].layer != activeLayer; });
        for (int objectIndex : editorOrder) {
            const auto& obj = placedObjects[objectIndex];
            int id = 0;
            try { id = std::stoi(obj.id); } catch (...) { id = 0; }
            if (id <= 0) continue;
            const int cx = (int)std::floor(obj.x / (float)kEditorTileSize);
            const int cy = (int)std::floor(obj.y / (float)kEditorTileSize);
            if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) continue;
            SDL_Rect rc{gridX + cx * cell - viewX, gridY + cy * cell - viewY, cell, cell};
            if (rc.x + rc.w < gridX || rc.y + rc.h < gridY || rc.x > gridX + gridViewW || rc.y > gridY + gridViewH) continue;
            Uint8 r = 220, g = 220, b = 240;
            objectColor(id, r, g, b);
            if (obj.layer != activeLayer) { r /= 3; g /= 3; b /= 3; }
            SDL_SetRenderDrawColor(ren, r, g, b, 200);
            SDL_Rect in{rc.x + std::max(2, cell / 6), rc.y + std::max(2, cell / 6), std::max(4, cell - std::max(4, cell / 3)), std::max(4, cell - std::max(4, cell / 3))};
            if (!drawObjectTexture(id, in, obj.layer != activeLayer)) SDL_RenderFillRect(ren, &in);
            const std::string valueTag = (obj.parameterValue ? std::to_string(id) + "=" + std::to_string(*obj.parameterValue) : std::to_string(id)) + " L" + std::to_string(obj.layer);
            DrawText(ren, rc.x + 3, rc.y + 3, std::max(1, cell / 24), valueTag);
            const char* tag = objectTag(id);
            if (tag[0] != '\0') {
                const int tagScale = std::max(1, cell / 24);
                const int tx = rc.x + rc.w - 4 - MeasureTextWidth(tagScale, tag);
                const int ty = rc.y + rc.h - 12;
                DrawText(ren, tx, ty, tagScale, tag);
            }
        }
        // Draw grid lines after tiles so visual cells align exactly with paint hitboxes.
        SDL_SetRenderDrawColor(ren, 34, 40, 56, 255);
        for (int x = drawX0; x <= drawX1 + 1; ++x) {
            int gx = gridX + x * cell - viewX;
            SDL_RenderLine(ren, (float)gx, (float)gridY, (float)gx, (float)(gridY + gridViewH));
        }
        for (int y = drawY0; y <= drawY1 + 1; ++y) {
            int gy = gridY + y * cell - viewY;
            SDL_RenderLine(ren, (float)gridX, (float)gy, (float)(gridX + gridViewW), (float)gy);
        }
        SDL_SetRenderDrawColor(ren, 90, 110, 140, 255);
        if (!paused && !configureObjects && !touchPanMode && SDL_GetMouseFocus() == win) {
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            if (mx >= gridX && mx < gridX + gridViewW && my >= gridY && my < gridY + gridViewH) {
                const int cx = ((int)mx - gridX + viewX) / cell, cy = ((int)my - gridY + viewY) / cell;
                SDL_SetRenderDrawColor(ren, touchEraseMode ? 255 : 140, touchEraseMode ? 95 : 220, 130, 255);
                for (const auto& position : EditorBrush::cells(gridW, gridH, cx, cy, brushSize)) {
                    SDL_Rect preview{gridX + position.first * cell - viewX, gridY + position.second * cell - viewY, cell, cell};
                    SDL_RenderRect(ren, &preview);
                }
            }
        }
        SDL_SetRenderClipRect(ren, nullptr);
        SDL_Rect border{gridX - 1, gridY - 1, gridViewW + 2, gridViewH + 2};
        SDL_RenderDrawRect(ren, &border);

        const int helpScale = 1;
        DrawText(ren, panelX + 4, objectConfigBtn.y - 40, helpScale,
            configureObjects ? "TAP OBJECT TO CONFIGURE" : (touchPanMode ? "DRAG TO PAN" : (touchEraseMode ? "DELETE MODE" : "PLACE / OVERRIDE")));
        DrawText(ren, panelX + 4, objectConfigBtn.y - 24, helpScale, "F2: LEVEL  F3: OBJECT  ESC: MENU");
        if (!statusText.empty() && SDL_GetTicks() < statusUntil)
            DrawText(ren, panelX + 4, objectConfigBtn.y - 56, 1, statusText);
        skin.draw(ren, configBtn);
        DrawText(ren, configBtn.x + (configBtn.w - MeasureTextWidth(2, "LEVEL CONFIG")) / 2, configBtn.y + 12, 2, "LEVEL CONFIG");
        skin.draw(ren, objectConfigBtn, configureObjects);
        DrawText(ren, objectConfigBtn.x + (objectConfigBtn.w - MeasureTextWidth(2, "CONFIGURE OBJECT")) / 2, objectConfigBtn.y + 12, 2, "CONFIGURE OBJECT");
        skin.draw(ren, brushDownBtn);
        skin.draw(ren, brushUpBtn);
        const std::string brushLabel = "BRUSH " + std::to_string(brushSize) + "x" + std::to_string(brushSize);
        DrawText(ren, brushDownBtn.x + 8, brushDownBtn.y + 14, 2, "-");
        DrawText(ren, brushUpBtn.x + brushUpBtn.w - 20, brushUpBtn.y + 14, 2, "+");
        DrawText(ren, panelX + (sideW - MeasureTextWidth(2, brushLabel)) / 2, brushDownBtn.y + 14, 2, brushLabel);
        skin.draw(ren, layerDownBtn);
        skin.draw(ren, layerUpBtn);
        const std::string layerLabel = "LAYER " + std::to_string(activeLayer);
        DrawText(ren, layerDownBtn.x + 8, layerDownBtn.y + 14, 2, "-");
        DrawText(ren, layerUpBtn.x + layerUpBtn.w - 20, layerUpBtn.y + 14, 2, "+");
        DrawText(ren, panelX + (sideW - MeasureTextWidth(2, layerLabel)) / 2, layerDownBtn.y + 14, 2, layerLabel);

        SDL_SetRenderDrawColor(ren, objectMode ? 50 : 90, objectMode ? 70 : 110, objectMode ? 90 : 170, 255);
        skin.draw(ren, tileModeBtn, !objectMode);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &tileModeBtn);
        const char* tileModeLabel = controlW < 105 ? "TILE" : "TILES";
        DrawText(ren, tileModeBtn.x + std::max(4, (tileModeBtn.w - MeasureTextWidth(2, tileModeLabel)) / 2), tileModeBtn.y + 13, 2, tileModeLabel);
        SDL_SetRenderDrawColor(ren, objectMode ? 90 : 50, objectMode ? 110 : 70, objectMode ? 170 : 90, 255);
        skin.draw(ren, objectModeBtn, objectMode);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &objectModeBtn);
        const char* objectModeLabel = controlW < 120 ? "OBJ" : "OBJECTS";
        DrawText(ren, objectModeBtn.x + std::max(4, (objectModeBtn.w - MeasureTextWidth(2, objectModeLabel)) / 2), objectModeBtn.y + 13, 2, objectModeLabel);

        SDL_SetRenderDrawColor(ren, touchPanMode ? 90 : 50, touchPanMode ? 120 : 70, touchPanMode ? 170 : 90, 255);
        skin.draw(ren, panModeBtn, touchPanMode);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &panModeBtn);
        DrawText(ren, panModeBtn.x + std::max(4, (panModeBtn.w - MeasureTextWidth(2, "PAN")) / 2), panModeBtn.y + 13, 2, "PAN");
        SDL_SetRenderDrawColor(ren, touchEraseMode ? 150 : 50, touchEraseMode ? 75 : 70, touchEraseMode ? 75 : 90, 255);
        skin.draw(ren, eraseModeBtn, touchEraseMode);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &eraseModeBtn);
        DrawText(ren, eraseModeBtn.x + std::max(4, (eraseModeBtn.w - MeasureTextWidth(2, "DELETE")) / 2), eraseModeBtn.y + 13, 2, "DELETE");

        SDL_SetRenderDrawColor(ren, 55, 65, 90, 255);
        skin.draw(ren, zoomOutBtn, false);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &zoomOutBtn);
        DrawText(ren, zoomOutBtn.x + std::max(4, (zoomOutBtn.w - MeasureTextWidth(2, "-")) / 2), zoomOutBtn.y + 13, 2, "-");
        SDL_SetRenderDrawColor(ren, 55, 65, 90, 255);
        skin.draw(ren, zoomInBtn, false);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &zoomInBtn);
        DrawText(ren, zoomInBtn.x + std::max(4, (zoomInBtn.w - MeasureTextWidth(2, "+")) / 2), zoomInBtn.y + 13, 2, "+");
        const std::string zoomLabel = std::to_string(editorZoomPercent) + "%";
        DrawText(ren, panelX + std::max(4, (sideW - MeasureTextWidth(1, zoomLabel)) / 2), zoomOutBtn.y + zoomOutBtn.h - 13, 1, zoomLabel);

        const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
        int& paletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
        const int paletteMaxScroll = std::max(0, paletteCount - paletteViewRows);
        paletteScroll = std::clamp(paletteScroll, 0, paletteMaxScroll);
        for (int row = 0; row < paletteViewRows; ++row) {
            const int i = paletteScroll + row;
            if (i >= paletteCount) break;
            const bool selected = objectMode ? (i == selectedObjectPalette) : (i == selectedPalette);
            SDL_Rect r{panelX, paletteStartY + row * paletteRowH, sideW - 12, 42};
            SDL_SetRenderDrawColor(ren, selected ? 70 : 45, selected ? 100 : 65, selected ? 160 : 95, 255);
            skin.draw(ren, r, selected);
            SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
            SDL_RenderDrawRect(ren, &r);
            if (!objectMode) {
                const unsigned short pid = palette[i];
                const Frame* pf = (pid < blocksFrameById.size()) ? blocksFrameById[pid] : nullptr;
                SDL_Rect sw{r.x + 6, r.y + 6, 24, 24};
                if (blocksTex && pf) {
                    renderFrame(ren, blocksTex, *pf, sw);
                } else {
                    Uint8 tr, tg, tb;
                    tileColor(pid, tr, tg, tb);
                    SDL_SetRenderDrawColor(ren, tr, tg, tb, 255);
                    SDL_RenderFillRect(ren, &sw);
                }
                if (pid == 7) {
                    DrawText(ren, r.x + 38, r.y + 8, 2, "Main Ground Tiles");
                } else if (pid == TileIds::BossBarrier) {
                    DrawText(ren, r.x + 38, r.y + 8, 2, "Boss Barrier (98)");
                } else {
                    DrawText(ren, r.x + 38, r.y + 8, 2, std::string("Tile ") + std::to_string((int)pid));
                }
            } else {
                const int oid = objectPalette[i];
                Uint8 tr = 220, tg = 220, tb = 240;
                objectColor(oid, tr, tg, tb);
                SDL_SetRenderDrawColor(ren, tr, tg, tb, 255);
                SDL_Rect sw{r.x + 6, r.y + 6, 24, 24};
                if (!drawObjectTexture(oid, sw, false)) SDL_RenderFillRect(ren, &sw);
                const std::string label = objectLabel(oid);
                const int textScale = MeasureTextWidth(2, label) <= r.w - 42 ? 2 : 1;
                DrawText(ren, r.x + 38, r.y + (r.h - textScale * 10) / 2, textScale, label);
            }
        }
        if (paletteCount > paletteViewRows) {
            SDL_Rect track{panelX + sideW - 8, paletteStartY, 8, paletteViewRows * paletteRowH - 8};
            SDL_SetRenderDrawColor(ren, 40, 46, 62, 255);
            SDL_RenderFillRect(ren, &track);
            const int thumbH = std::max(16, (track.h * paletteViewRows) / std::max(1, paletteCount));
            const int thumbY = track.y + ((track.h - thumbH) * paletteScroll) / std::max(1, paletteCount - paletteViewRows);
            SDL_Rect thumb{track.x, thumbY, track.w, thumbH};
            SDL_SetRenderDrawColor(ren, 120, 150, 210, 255);
            SDL_RenderFillRect(ren, &thumb);
        }


        if (paused) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
            SDL_Rect dim{0, 0, winW, winH};
            SDL_RenderFillRect(ren, &dim);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            skin.draw(ren, pausePanel, false, true);
            DrawText(ren, pausePanel.x + (pausePanel.w - MeasureTextWidth(3, "PAUSED")) / 2, pausePanel.y + 16, 3, "PAUSED");
            const std::string notice = SDL_GetTicks() < statusUntil ? statusText : "AUTO SAVE EVERY 15 SECONDS";
            DrawText(ren, pausePanel.x + (pausePanel.w - MeasureTextWidth(1, notice)) / 2, pausePanel.y + 52, 1, notice);
            const char* labels[4] = {"RESUME", "SAVE & RESUME", "SAVE & EXIT", "EXIT"};
            for (int i = 0; i < 4; ++i) {
                const SDL_Rect rect = editorAnimations.drawRect(i, pauseBtns[i], i == pauseSel, win, winW, winH);
                skin.draw(ren, rect, i == pauseSel);
                const int labelScale = pauseButtonW < 200 ? 1 : 2;
                DrawText(ren, rect.x + (rect.w - MeasureTextWidth(labelScale, labels[i])) / 2,
                    rect.y + (rect.h - labelScale * 10) / 2, labelScale, labels[i]);
            }
        }
        if (SDL_GetTicks() >= nextAutosave) {
            nextAutosave = SDL_GetTicks() + 15000;
            if (snapshot() != savedSnapshot) saveEditor(false, true);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (snapshot() != savedSnapshot) saveEditor(false, true);
    if (editorEntities) SDL_DestroyTexture(editorEntities);
    if (blocksTex) SDL_DestroyTexture(blocksTex);
    return savedPath;
}

std::string localLevelsFolderPath() {
    const std::string base = GetAppSaveRootPath();
    std::filesystem::path dir = std::filesystem::path(base) / "local levels";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

} // namespace

std::string OpenLocalLevelEditorForMenu(SDL_Window* win, SDL_Renderer* ren, const std::string& initialPath) {
    return RunLocalLevelEditor(win, ren, initialPath);
}

std::string RenameLocalLevelForMenu(SDL_Window* win, SDL_Renderer* ren, const std::string& path, std::string& status) {
    CancelNativeTextOverlay(ren);
    std::string initial = std::filesystem::path(path).stem().string();
    std::replace(initial.begin(), initial.end(), '_', ' ');
    const auto name = RunLocalLevelRenamePrompt(win, ren, initial);
    if (name.empty()) { status = "Rename cancelled."; return {}; }
    std::string renamed;
    if (!LocalLevelEdits::renameFile(path, name, renamed, status)) return {};
    if (renamed == path) { status = "Name unchanged."; return renamed; }
    ClearLocalLevelVerification(path);
    ClearLocalLevelVerification(renamed);
    status = "Local level renamed.";
    return renamed;
}

bool ResizeLocalLevelForMenu(SDL_Window* win, SDL_Renderer* ren, const std::string& path, std::string& status) {
    CancelNativeTextOverlay(ren);
    TileMap map;
    std::vector<ObjectInstance> objects;
    LevelMeta meta;
    if (!loadLevelBNNLVL(path, map, objects, meta)) { status = "Could not open this local level."; return false; }
    const auto draft = RunGrowLevelPrompt(win, ren, map.w, map.h, true);
    if (!draft.accepted) { status = "Resize cancelled."; return false; }
    LocalLevelEdits::ResizePlan plan;
    if (!LocalLevelEdits::planResize(map, objects, draft.width, draft.height, plan)) {
        status = "Could not resize this level. Dimensions must be 5-400 tiles.";
        return false;
    }
    if (plan.croppedTiles || plan.croppedObjects) {
        const std::string message = "Resize to " + std::to_string(draft.width) + " x " + std::to_string(draft.height) +
            " tiles? This removes " + std::to_string(plan.croppedTiles) + " tiles and " + std::to_string(plan.croppedObjects) +
            " objects beyond the new right and bottom edges.";
        const SDL_MessageBoxButtonData buttons[]{{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Resize"}};
        SDL_MessageBoxData dialog{};
        dialog.flags = SDL_MESSAGEBOX_WARNING;
        dialog.window = win;
        dialog.title = "Crop Local Level";
        dialog.message = message.c_str();
        dialog.numbuttons = 2;
        dialog.buttons = buttons;
        int choice = 0;
        if (!SDL_ShowMessageBox(&dialog, &choice) || choice != 1) { status = "Resize cancelled."; return false; }
    }
    if (writeLocalLevelFile(plan.tiles, draft.width, draft.height, path, plan.objects, meta).empty()) {
        status = "Could not save the resized level. The original file was kept.";
        return false;
    }
    status = "Resized to " + std::to_string(draft.width) + " x " + std::to_string(draft.height) + " tiles. Verification cleared.";
    return true;
}

namespace {

std::string RunLocalLevelRenamePrompt(SDL_Window* win, SDL_Renderer* ren, const std::string& initialName,
    const std::string& title, const std::string& fieldLabel) {
    std::string name = initialName;
    EditorSkin skin(ren);
    bool accepted = false;
    bool running = true;
    bool textInputActive = false;
    bool replaceName = true;
    auto trimSpaces = [](std::string s) {
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        return s;
    };
    auto setTextInput = [&](bool active, const SDL_Rect* focus = nullptr) {
        if (active == textInputActive) return;
        textInputActive = active;
        if (active) {
            SDL_StartTextInput(win);
            if (focus) SDL_SetTextInputArea(win, focus, 0);
        } else {
            SDL_SetTextInputArea(win, nullptr, 0);
            SDL_StopTextInput(win);
        }
    };
    setTextInput(true);
    while (running) {
        int winW = 0, winH = 0;
        getWindowSizeInPixelsCompat(win, winW, winH);
        SDL_Rect panel{winW / 2 - 260, winH / 2 - 110, 520, 220};
        SDL_Rect rowName{panel.x + 20, panel.y + 66, panel.w - 40, 40};
        SDL_Rect okBtn{panel.x + 80, panel.y + 148, 150, 42};
        SDL_Rect cancelBtn{panel.x + 290, panel.y + 148, 150, 42};
        auto activateRename = [&]() {
            name = trimSpaces(name);
            accepted = !name.empty();
            running = false;
        };
        setTextInput(true, &rowName);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { SDL_PushEvent(&e); running = false; break; }
            if (e.type == SDL_TEXTINPUT
#if defined(SDL_EVENT_TEXT_INPUT)
                || e.type == SDL_EVENT_TEXT_INPUT
#endif
            ) {
                if (replaceName) { name.clear(); replaceName = false; }
                if (name.size() < 64) name += std::string(e.text.text).substr(0, 64 - name.size());
                continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) { running = false; break; }
                if ((e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && e.key.key == SDLK_a) { replaceName = true; continue; }
                if (e.key.key == SDLK_END || e.key.key == SDLK_RIGHT) { replaceName = false; continue; }
                if (e.key.key == SDLK_BACKSPACE || e.key.key == SDLK_DELETE) {
                    if (replaceName) name.clear();
                    else if (!name.empty()) {
                        size_t pos = name.size() - 1;
                        while (pos && ((unsigned char)name[pos] & 0xc0) == 0x80) --pos;
                        name.erase(pos);
                    }
                    replaceName = false;
                    continue;
                }
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    name = trimSpaces(name);
                    accepted = !name.empty();
                    running = false;
                    break;
                }
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) { running = false; break; }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_START) { activateRename(); break; }
            }
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                SDL_Point point{(int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH)};
                if (SDL_PointInRect(&point, &okBtn)) { activateRename(); break; }
                if (SDL_PointInRect(&point, &cancelBtn)) { running = false; break; }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
                SDL_Point pt{(int)e.button.x, (int)e.button.y};
                if (SDL_PointInRect(&pt, &okBtn)) {
                    name = trimSpaces(name);
                    accepted = !name.empty();
                    running = false;
                    break;
                }
                if (SDL_PointInRect(&pt, &cancelBtn)) {
                    running = false;
                    break;
                }
            }
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 30, 34, 44, 255);
        skin.draw(ren, panel, false, true);
        SDL_SetRenderDrawColor(ren, 200, 210, 230, 255);
        SDL_RenderDrawRect(ren, &panel);
        const int titleScale = MeasureTextWidth(3, title) <= panel.w - 40 ? 3 : 2;
        DrawText(ren, panel.x + 20, panel.y + 20, titleScale, title);
        SDL_SetRenderDrawColor(ren, 68, 98, 158, 255);
        skin.draw(ren, rowName, true);
        SDL_SetRenderDrawColor(ren, 220, 225, 235, 255);
        SDL_RenderDrawRect(ren, &rowName);
        DrawText(ren, rowName.x + 10, rowName.y + 11, 2, fieldLabel + name);
        SDL_SetRenderDrawColor(ren, 55, 95, 70, 255);
        skin.draw(ren, okBtn);
        SDL_SetRenderDrawColor(ren, 205, 235, 210, 255);
        SDL_RenderDrawRect(ren, &okBtn);
        DrawText(ren, okBtn.x + 48, okBtn.y + 12, 2, "OK");
        SDL_SetRenderDrawColor(ren, 80, 55, 70, 255);
        skin.draw(ren, cancelBtn);
        SDL_SetRenderDrawColor(ren, 235, 205, 210, 255);
        SDL_RenderDrawRect(ren, &cancelBtn);
        DrawText(ren, cancelBtn.x + 34, cancelBtn.y + 12, 2, "CANCEL");
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    setTextInput(false);
    return accepted ? name : std::string();
}

GrowLevelPromptResult RunGrowLevelPrompt(SDL_Window* win, SDL_Renderer* ren, int currentW, int currentH, bool allowShrink) {
    GrowLevelPromptResult out;
    out.width = allowShrink ? std::clamp(currentW, 5, 400) : currentW;
    out.height = allowShrink ? std::clamp(currentH, 5, 400) : currentH;
    bool running = true;
    int selectedRow = 0;
    auto clampW = [&](int v) { return std::clamp(v, allowShrink ? 5 : currentW, allowShrink ? 400 : std::max(400, currentW)); };
    auto clampH = [&](int v) { return std::clamp(v, allowShrink ? 5 : currentH, allowShrink ? 400 : std::max(400, currentH)); };
    while (running) {
        int winW = 0, winH = 0;
        getWindowSizeInPixelsCompat(win, winW, winH);
        SDL_Rect panel{winW / 2 - 260, winH / 2 - 140, 520, 280};
        SDL_Rect rowW{panel.x + 20, panel.y + 66, panel.w - 40, 38};
        SDL_Rect rowH{panel.x + 20, panel.y + 116, panel.w - 40, 38};
        SDL_Rect rowWMinus{rowW.x + rowW.w - 96, rowW.y + 5, 40, rowW.h - 10};
        SDL_Rect rowWPlus{rowW.x + rowW.w - 46, rowW.y + 5, 40, rowW.h - 10};
        SDL_Rect rowHMinus{rowH.x + rowH.w - 96, rowH.y + 5, 40, rowH.h - 10};
        SDL_Rect rowHPlus{rowH.x + rowH.w - 46, rowH.y + 5, 40, rowH.h - 10};
        SDL_Rect growBtn{panel.x + 80, panel.y + 202, 150, 42};
        SDL_Rect cancelBtn{panel.x + 290, panel.y + 202, 150, 42};
        auto acceptGrow = [&]() {
            out.accepted = (out.width != currentW || out.height != currentH);
            running = false;
        };
        auto handlePoint = [&](int px, int py) {
            SDL_Point pt{px, py};
            if (SDL_PointInRect(&pt, &rowWMinus)) { selectedRow = 0; out.width = clampW(out.width - 1); return; }
            if (SDL_PointInRect(&pt, &rowWPlus)) { selectedRow = 0; out.width = clampW(out.width + 1); return; }
            if (SDL_PointInRect(&pt, &rowHMinus)) { selectedRow = 1; out.height = clampH(out.height - 1); return; }
            if (SDL_PointInRect(&pt, &rowHPlus)) { selectedRow = 1; out.height = clampH(out.height + 1); return; }
            if (SDL_PointInRect(&pt, &rowW)) { selectedRow = 0; return; }
            if (SDL_PointInRect(&pt, &rowH)) { selectedRow = 1; return; }
            if (SDL_PointInRect(&pt, &growBtn)) { acceptGrow(); return; }
            if (SDL_PointInRect(&pt, &cancelBtn)) { running = false; return; }
        };
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { SDL_PushEvent(&e); running = false; break; }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) { running = false; break; }
                if (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN || e.key.key == SDLK_TAB) { selectedRow = 1 - selectedRow; continue; }
                int& v = selectedRow == 0 ? out.width : out.height;
                const int step = (e.key.key == SDLK_PAGEUP || e.key.key == SDLK_PAGEDOWN) ? 10 : 1;
                auto clampSelected = [&](int value) { return selectedRow == 0 ? clampW(value) : clampH(value); };
                if (e.key.key == SDLK_LEFT || e.key.key == SDLK_MINUS || e.key.key == SDLK_KP_MINUS || e.key.key == SDLK_PAGEDOWN) { v = clampSelected(v - step); continue; }
                if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS || e.key.key == SDLK_KP_PLUS || e.key.key == SDLK_PAGEUP) { v = clampSelected(v + step); continue; }
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) { acceptGrow(); break; }
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const auto button = e.gbutton.button;
                if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) { running = false; break; }
                if (button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) selectedRow = 1 - selectedRow;
                if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                    const int step = button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -1 : 1;
                    if (selectedRow == 0) out.width = clampW(out.width + step);
                    else out.height = clampH(out.height + step);
                }
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) { acceptGrow(); break; }
            }
            if (e.type == SDL_EVENT_FINGER_DOWN) handlePoint((int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH));
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) handlePoint((int)e.button.x, (int)e.button.y);
            if (e.type == SDL_MOUSEWHEEL) {
                if (selectedRow == 0) out.width = clampW(out.width + e.wheel.y);
                else out.height = clampH(out.height + e.wheel.y);
            }
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 30, 34, 44, 255);
        SDL_RenderFillRect(ren, &panel);
        SDL_SetRenderDrawColor(ren, 200, 210, 230, 255);
        SDL_RenderDrawRect(ren, &panel);
        DrawText(ren, panel.x + 20, panel.y + 20, 3, allowShrink ? "RESIZE LOCAL LEVEL" : "GROW LOCAL LEVEL");
        auto drawRow = [&](const SDL_Rect& r, bool selected, const std::string& text) {
            SDL_SetRenderDrawColor(ren, selected ? 68 : 50, selected ? 98 : 72, selected ? 158 : 108, 255);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 220, 225, 235, 255);
            SDL_RenderDrawRect(ren, &r);
            DrawText(ren, r.x + 10, r.y + 10, 2, text);
        };
        auto drawAdjust = [&](const SDL_Rect& r, const char* label) {
            SDL_SetRenderDrawColor(ren, 44, 60, 92, 255);
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 220, 225, 235, 255);
            SDL_RenderDrawRect(ren, &r);
            DrawText(ren, r.x + 12, r.y + 6, 2, label);
        };
        drawRow(rowW, selectedRow == 0, std::string("WIDTH: ") + std::to_string(out.width));
        drawRow(rowH, selectedRow == 1, std::string("HEIGHT: ") + std::to_string(out.height));
        drawAdjust(rowWMinus, "-");
        drawAdjust(rowWPlus, "+");
        drawAdjust(rowHMinus, "-");
        drawAdjust(rowHPlus, "+");
        if (allowShrink) DrawText(ren, panel.x + 20, panel.y + 174, 2, "5-400 TILES; KEEPS TOP-LEFT AREA");
        SDL_SetRenderDrawColor(ren, 55, 95, 70, 255);
        SDL_RenderFillRect(ren, &growBtn);
        SDL_SetRenderDrawColor(ren, 205, 235, 210, 255);
        SDL_RenderDrawRect(ren, &growBtn);
        DrawText(ren, growBtn.x + 42, growBtn.y + 12, 2, allowShrink ? "APPLY" : "GROW");
        SDL_SetRenderDrawColor(ren, 80, 55, 70, 255);
        SDL_RenderFillRect(ren, &cancelBtn);
        SDL_SetRenderDrawColor(ren, 235, 205, 210, 255);
        SDL_RenderDrawRect(ren, &cancelBtn);
        DrawText(ren, cancelBtn.x + 34, cancelBtn.y + 12, 2, "CANCEL");
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    return out;
}

}

std::string buildGameLevelUploadUrl(const std::string& base,
                                        const std::string& levelId) {
    std::string u = base;
    while (!u.empty() && u.back() == '/') u.pop_back();
    u += "/levels/" + sanitizeFilePart(levelId) + ".json";

    return u;
}

bool resolveAccountUsernameFromToken(const std::string& authToken, std::string& accountUsernameOut) {
    accountUsernameOut.clear();
    if (authToken.empty()) return false;
    std::string serverBase = GetLevelServerUrl();
    while (!serverBase.empty() && serverBase.back() == '/') serverBase.pop_back();
    if (serverBase.empty()) return false;

#if defined(HAVE_CURL) && HAVE_CURL

    CURL* curl = curl_easy_init();
    if (curl) {
        const std::string url = serverBase + "/api/auth/lookup";
        nlohmann::json req;
        req["idToken"] = authToken;
        const std::string body = req.dump();
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        const std::string authorization = "Authorization: Bearer " + authToken;
        headers = curl_slist_append(headers, authorization.c_str());
        std::string respBody;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, PLATFORMER_CLIENT_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
            +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                std::string* out = static_cast<std::string*>(userdata);
                out->append(ptr, size * nmemb);
                return size * nmemb;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respBody);
        const CURLcode rc = PerformNetworkRequest(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (rc == CURLE_OK && code >= 200 && code < 300) {
            try {
                const nlohmann::json resp = nlohmann::json::parse(respBody);
                if (resp.is_object() && resp.contains("users") && resp["users"].is_array() && !resp["users"].empty()) {
                    const nlohmann::json& user = resp["users"][0];
                    std::string candidate;
                    if (user.contains("displayName") && user["displayName"].is_string()) {
                        candidate = user["displayName"].get<std::string>();
                    }
                    if (candidate.empty() && user.contains("email") && user["email"].is_string()) {
                        const std::string email = user["email"].get<std::string>();
                        const std::size_t atPos = email.find('@');
                        candidate = (atPos == std::string::npos) ? email : email.substr(0, atPos);
                    }
                    candidate = sanitizeFilePart(candidate);
                    if (!candidate.empty()) {
                        accountUsernameOut = candidate;
                        return true;
                    }
                }
            } catch (...) {}
        }
    }
#endif

#if defined(__ANDROID__)
    {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (env) {
            jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(
                    cls, "gameServerLookupAccount",
                    "(Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
                if (mid) {
                    jstring jApi = env->NewStringUTF(serverBase.c_str());
                    jstring jToken = env->NewStringUTF(authToken.c_str());
                    jobject jRespObj = PerformJavaNetworkObject(env, cls, mid, {jApi, jToken}, (jint)10000);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (jApi) env->DeleteLocalRef(jApi);
                    if (jToken) env->DeleteLocalRef(jToken);
                    std::string respBody;
                    if (jRespObj) {
                        jstring jResp = static_cast<jstring>(jRespObj);
                        const char* cResp = env->GetStringUTFChars(jResp, nullptr);
                        if (cResp) {
                            respBody = cResp;
                            env->ReleaseStringUTFChars(jResp, cResp);
                        }
                        env->DeleteLocalRef(jRespObj);
                    }
                    env->DeleteLocalRef(cls);
                    if (!respBody.empty()) {
                        try {
                            const nlohmann::json resp = nlohmann::json::parse(respBody);
                            if (resp.is_object() && resp.contains("users") && resp["users"].is_array() && !resp["users"].empty()) {
                                const nlohmann::json& user = resp["users"][0];
                                std::string candidate;
                                if (user.contains("displayName") && user["displayName"].is_string()) {
                                    candidate = user["displayName"].get<std::string>();
                                }
                                if (candidate.empty() && user.contains("email") && user["email"].is_string()) {
                                    const std::string email = user["email"].get<std::string>();
                                    const std::size_t atPos = email.find('@');
                                    candidate = (atPos == std::string::npos) ? email : email.substr(0, atPos);
                                }
                                candidate = sanitizeFilePart(candidate);
                                if (!candidate.empty()) {
                                    accountUsernameOut = candidate;
                                    return true;
                                }
                            }
                        } catch (...) {}
                    }
                } else {
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    env->DeleteLocalRef(cls);
                }
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
    }
#endif

    return false;
}

std::string readLevelApiVersionIdFromConfig() {
    const std::string cfgText = ReadTextFile("assets/config.json");
    if (cfgText.empty()) return {};
    try {
        const nlohmann::json cfgJson = nlohmann::json::parse(cfgText);
        if (!cfgJson.is_object()) return {};
        auto jsonValueToString = [](const nlohmann::json& value) -> std::string {
            if (value.is_string()) return value.get<std::string>();
            if (value.is_number_integer()) return std::to_string(value.get<long long>());
            if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
            if (value.is_number_float()) {
                std::ostringstream oss;
                oss << value.get<double>();
                return oss.str();
            }
            return std::string();
        };
        if (cfgJson.contains("level_api_version_id")) {
            const std::string versionId = jsonValueToString(cfgJson["level_api_version_id"]);
            if (!versionId.empty()) return versionId;
        }
        if (cfgJson.contains("version_id")) {
            const std::string versionId = jsonValueToString(cfgJson["version_id"]);
            if (!versionId.empty()) return versionId;
        }
    } catch (...) {}
    return {};
}

bool uploadLocalLevelToServer(const LevelEntry& level, std::string& statusText) {
    if (level.path.empty() || level.path == "__local_editor__") {
        statusText = "No local level selected.";
        return false;
    }
    if (isHttpUrl(level.path)) {
        statusText = "Only local levels can be uploaded.";
        return false;
    }
    if (!IsLocalLevelVerified(level.path)) {
        statusText = "Beat this level before uploading.";
        return false;
    }
    const std::string levelServerUrl = GetLevelServerUrl();
    if (levelServerUrl.empty()) {
        statusText = "Level server URL is not configured.";
        return false;
    }
    const std::string authToken = GetLevelServerAuthToken();
    std::string accountUsernameRaw = GetLevelServerAccountUsername();
    if (accountUsernameRaw.empty() && !authToken.empty()) {
        std::string resolvedUsername;
        if (resolveAccountUsernameFromToken(authToken, resolvedUsername) && !resolvedUsername.empty()) {
            accountUsernameRaw = resolvedUsername;
            SetLevelServerAccountUsername(accountUsernameRaw);
            SDL_Log("NET/UI: upload resolved username from token username=%s", accountUsernameRaw.c_str());
        }
    }
    if (accountUsernameRaw.empty()) {
        statusText = "Sign in before upload.";
        return false;
    }
    const std::string accountUsername = sanitizeFilePart(accountUsernameRaw);

    std::ifstream in(level.path, std::ios::binary);
    if (!in.is_open()) {
        statusText = "Could not open local level file.";
        return false;
    }
    std::string levelData((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (levelData.empty()) {
        statusText = "Local level file is empty.";
        return false;
    }

    const std::string levelName = sanitizeFilePart(level.label);
    const std::string levelId = accountUsername + "-" + levelName;
    const std::string levelApiVersionId = readLevelApiVersionIdFromConfig();
    const std::string uploadUrl = buildGameLevelUploadUrl(levelServerUrl, levelId);
    nlohmann::json payload;
    payload["name"] = level.label;
    payload["level_id"] = levelId;
    payload["owner"] = accountUsername;
    payload["source"] = "local";
    if (!levelApiVersionId.empty()) {
        payload["api_version_id"] = levelApiVersionId;
        payload["level_api_version_id"] = levelApiVersionId;
    }
    payload["data"] = levelData;
    payload["uploaded_at"] = (long long)std::time(nullptr);
    const std::string body = payload.dump();

    auto tryAndroidJavaUpload = [&]() -> bool {
#if defined(__ANDROID__)
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!env) return false;
        jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
        if (!cls) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            return false;
        }
        jmethodID mid = env->GetStaticMethodID(
            cls, "gameServerUploadLevel",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)I");
        if (!mid) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(cls);
            return false;
        }
        jstring jUrl = env->NewStringUTF(uploadUrl.c_str());
        jstring jBody = env->NewStringUTF(body.c_str());
        jstring jToken = env->NewStringUTF(authToken.c_str());
        jint code = PerformJavaNetworkInt(env, cls, mid, {jUrl, jBody, jToken}, (jint)15000);
        if (jToken) env->DeleteLocalRef(jToken);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (jUrl) env->DeleteLocalRef(jUrl);
        if (jBody) env->DeleteLocalRef(jBody);
        env->DeleteLocalRef(cls);
        if (code >= 200 && code < 300) {
            statusText = "Uploaded as " + levelId + ".";
            SDL_Log("NET/UI: upload ok (Java) file=%s id=%s code=%d", level.path.c_str(), levelId.c_str(), (int)code);
            return true;
        }
        SDL_Log("NET/UI: upload failed (Java) file=%s id=%s code=%d", level.path.c_str(), levelId.c_str(), (int)code);
#endif
        return false;
    };

#if defined(HAVE_CURL) && HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (tryAndroidJavaUpload()) return true;
        statusText = "Upload failed (curl init).";
        return false;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string authorization = "Authorization: Bearer " + authToken;
    headers = curl_slist_append(headers, authorization.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, PLATFORMER_CLIENT_USER_AGENT);
    const CURLcode rc = PerformNetworkRequest(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc == CURLE_ABORTED_BY_CALLBACK) {
        statusText = "Upload cancelled.";
        return false;
    }
    if (rc != CURLE_OK || code < 200 || code >= 300) {
        if (tryAndroidJavaUpload()) return true;
        statusText = "Upload failed.";
        SDL_Log("NET/UI: upload failed file=%s code=%ld curl=%d", level.path.c_str(), code, (int)rc);
        return false;
    }
    statusText = "Uploaded as " + levelId + ".";
    SDL_Log("NET/UI: upload ok file=%s id=%s", level.path.c_str(), levelId.c_str());
    return true;
#else
    if (tryAndroidJavaUpload()) return true;
    (void)uploadUrl;
    (void)body;
    statusText = "Upload unavailable (curl disabled).";
    return false;
#endif
}
