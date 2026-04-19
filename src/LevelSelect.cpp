#include "LevelSelect.h"
#include "AssetPath.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include "InputSystem.h"
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif

namespace {
struct LevelEntry {
    std::string label;
    std::string path;
};

struct OnlineLevelsMenuLabels {
    std::string tabCampaign = "CAMPAIGN";
    std::string tabUser = "USER";
    std::string tabLocal = "LOCAL";
    std::string localEditorEntry = "CREATE LOCAL LEVEL (EDITOR)";
    std::string userLevelsHeader = "USER LEVELS";
    std::string downloadButton = "DOWNLOAD (D)";
    std::string uploadButton = "UPLOAD (U)";
    std::string emptyTitle = "No levels found in this tab.";
    std::string emptyLocalHint = "Use CREATE LOCAL LEVEL (EDITOR).";
    std::string emptyCustomHintAndroid = "Add custom levels to assets/custom_levels/levels.json.";
    std::string emptyCustomHintDesktop = "Use custom_levels/ or assets/custom_levels/.";
    std::string localPanelTitle = "LOCAL LEVEL";
    std::string localPanelActions = "ENTER/P: PLAY  DEL/X: DELETE  E: EDIT  U: UPLOAD";
    std::string localPanelBackHint = "ESC: BACK";
    std::string buttonPlay = "PLAY";
    std::string buttonDelete = "DELETE";
    std::string buttonEdit = "EDIT";
    std::string buttonUpload = "UPLOAD";
    std::string buttonBack = "BACK";
};

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

bool isNumericId(const std::string& s) {
    if (s.empty()) return false;
    for (char ch : s) {
        if (!std::isdigit((unsigned char)ch)) return false;
    }
    return true;
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

std::string downloadFolderPath();
std::string localLevelsFolderPath();

OnlineLevelsMenuLabels loadOnlineLevelsMenuLabels() {
    OnlineLevelsMenuLabels labels;
    const std::string text = ReadTextFile("assets/menus/online_levels_menu.json");
    if (text.empty()) return labels;
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        auto readString = [&](const char* key, std::string& out) {
            if (!j.contains(key) || !j[key].is_string()) return;
            out = j[key].get<std::string>();
        };
        readString("tab_campaign", labels.tabCampaign);
        readString("tab_user", labels.tabUser);
        readString("tab_local", labels.tabLocal);
        readString("local_editor_entry", labels.localEditorEntry);
        readString("user_levels_header", labels.userLevelsHeader);
        readString("download_button", labels.downloadButton);
        readString("upload_button", labels.uploadButton);
        readString("empty_title", labels.emptyTitle);
        readString("empty_local_hint", labels.emptyLocalHint);
        readString("empty_custom_hint_android", labels.emptyCustomHintAndroid);
        readString("empty_custom_hint_desktop", labels.emptyCustomHintDesktop);
        readString("local_panel_title", labels.localPanelTitle);
        readString("local_panel_actions", labels.localPanelActions);
        readString("local_panel_back_hint", labels.localPanelBackHint);
        readString("btn_play", labels.buttonPlay);
        readString("btn_delete", labels.buttonDelete);
        readString("btn_edit", labels.buttonEdit);
        readString("btn_upload", labels.buttonUpload);
        readString("btn_back", labels.buttonBack);
    } catch (...) {}
    return labels;
}

unsigned short normalizeTileIdForSave(unsigned short id) {
    // Legacy/runtime empty tile convention is 2; saving 0 can cause bad loads/render fallbacks.
    if (id == 0) return 2;
    return id;
}

std::string writeLocalLevelFile(const std::vector<unsigned short>& rowMajorGrid, int w, int h,
                                const std::string& targetPath = "",
                                const std::vector<ObjectInstance>& objects = {}) {
    if (w <= 0 || h <= 0) return {};
    if ((int)rowMajorGrid.size() != w * h) return {};
    // DFLVL2 stores exact engine row-major tile IDs, avoiding legacy axis/offset transforms.
    std::string data;
    data.reserve((size_t)w * (size_t)h * 4 + 64);
    data += "DFLVL2 ";
    data += std::to_string(w);
    data += " ";
    data += std::to_string(h);
    data += "\n";
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x > 0) data.push_back(' ');
            const unsigned short id = normalizeTileIdForSave(rowMajorGrid[y * w + x]);
            data += std::to_string((int)id);
        }
        data.push_back('\n');
    }
    std::vector<const ObjectInstance*> validObjects;
    validObjects.reserve(objects.size());
    for (const auto& obj : objects) {
        int objectId = 0;
        try { objectId = std::stoi(obj.id); } catch (...) { objectId = 0; }
        if (objectId <= 0) continue;
        validObjects.push_back(&obj);
    }
    data += "OBJ ";
    data += std::to_string((int)validObjects.size());
    data.push_back('\n');
    for (const ObjectInstance* obj : validObjects) {
        int objectId = 0;
        try { objectId = std::stoi(obj->id); } catch (...) { objectId = 0; }
        if (objectId <= 0) continue;
        data += std::to_string(objectId);
        data.push_back(' ');
        data += std::to_string((int)std::lround(obj->x));
        data.push_back(' ');
        data += std::to_string((int)std::lround(obj->y));
        data.push_back('\n');
    }

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
    const std::filesystem::path tmpPath = outPath.string() + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return {};
        out << data;
        out.flush();
        if (!out.good()) return {};
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, outPath, ec);
    if (ec) {
        std::filesystem::remove(outPath, ec);
        ec.clear();
        std::filesystem::rename(tmpPath, outPath, ec);
        if (ec) return {};
    }
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
#if defined(__ANDROID__)
            if (active) {
                if (winW <= 0 || winH <= 0) SDL_GetWindowSize(win, &winW, &winH);
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
#if defined(__ANDROID__)
            if (winW <= 0 || winH <= 0) SDL_GetWindowSize(win, &winW, &winH);
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
    setPromptTextInput(true);
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
        SDL_GetWindowSize(win, &winW, &winH);
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

std::string RunLocalLevelEditor(SDL_Window* win, SDL_Renderer* ren, const std::string& initialPath = "",
                                const std::string& newLevelName = "", int newLevelW = -1, int newLevelH = -1) {
    constexpr int kEditorTileSize = 32;
    int gridW = (newLevelW > 0) ? std::clamp(newLevelW, 5, 400) : 30;
    int gridH = (newLevelH > 0) ? std::clamp(newLevelH, 5, 400) : 17;
    std::vector<unsigned short> grid((size_t)gridW * (size_t)gridH, 2);
    std::vector<ObjectInstance> placedObjects;

    if (!initialPath.empty()) {
        TileMap loadedMap;
        std::vector<ObjectInstance> loadedObjects;
        LevelMeta loadedMeta;
        if (loadLevelBNNLVL(initialPath, loadedMap, loadedObjects, loadedMeta) && loadedMap.w > 0 && loadedMap.h > 0) {
            gridW = loadedMap.w;
            gridH = loadedMap.h;
            grid = loadedMap.tileIds;
            placedObjects = loadedObjects;
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
        28
    };
    const std::vector<int> objectPalette{31, 46, 57, 58, 59, 60, 61, 67};
    int selectedPalette = 1;
    int selectedObjectPalette = 0;
    int tilePaletteScroll = 0;
    int objectPaletteScroll = 0;
    bool objectMode = false;
    bool running = true;
    const Uint64 inputBlockUntilTicks = SDL_GetTicks() + 1000;
    bool paused = false;
    int pauseSel = 0; // 0 Resume, 1 Save, 2 Exit
    std::string savedPath;
    const std::string saveTargetPath = initialPath.empty() ? buildNamedLocalLevelPath(newLevelName) : initialPath;
    std::string statusText;
    Uint64 statusUntil = 0;
    SDL_FingerID activeEditorFinger = 0;
    bool fingerPainting = false;
    bool fingerPaletteScroll = false;
    bool fingerPaletteMoved = false;
    int fingerPaletteLastY = 0;
    float fingerPaletteScrollAccum = 0.0f;
    int viewX = 0;
    int viewY = 0;
    bool middlePanning = false;
    int lastPanMouseX = 0;
    int lastPanMouseY = 0;
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
        if (id == 1) { r = 120; g = 120; b = 130; return; }
        if (id == 2) { r = 24; g = 26; b = 34; return; }
        if (id == 24) { r = 220; g = 180; b = 40; return; }
        if (id == 28) { r = 80; g = 200; b = 90; return; }
        if (id == 30) { r = 240; g = 80; b = 120; return; }
        if (id == 68) { r = 150; g = 100; b = 255; return; }
        r = 180; g = 180; b = 180;
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
    auto objectCellX = [&](const ObjectInstance& obj) -> int {
        return (int)std::floor(obj.x / (float)kEditorTileSize);
    };
    auto objectCellY = [&](const ObjectInstance& obj) -> int {
        return (int)std::floor(obj.y / (float)kEditorTileSize);
    };
    auto eraseObjectAt = [&](int cx, int cy) -> bool {
        const size_t before = placedObjects.size();
        placedObjects.erase(std::remove_if(placedObjects.begin(), placedObjects.end(),
            [&](const ObjectInstance& obj) {
                return objectCellX(obj) == cx && objectCellY(obj) == cy;
            }), placedObjects.end());
        return placedObjects.size() != before;
    };
    auto placeObjectAt = [&](int cx, int cy, bool erase) {
        if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return;
        const bool removedExisting = eraseObjectAt(cx, cy);
        if (erase) return;
        // In object mode, tapping an occupied cell removes the object.
        // Tap again on the same cell to place a new one.
        if (removedExisting) return;
        ObjectInstance obj;
        obj.id = std::to_string(objectPalette[selectedObjectPalette]);
        obj.x = (float)(cx * kEditorTileSize + kEditorTileSize / 2);
        obj.y = (float)(cy * kEditorTileSize + kEditorTileSize / 2);
        placedObjects.push_back(obj);
    };
    auto applyAt = [&](int cx, int cy, bool erase) {
        if (objectMode) placeObjectAt(cx, cy, erase);
        else paintAt(cx, cy, erase);
    };

    while (running) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        const int margin = 12;
        const int sideW = 240;
        const int gridX = margin;
        const int gridY = margin;
        const int panelX = std::max(gridX + 16, winW - margin - sideW);
        const int gridViewW = std::max(120, panelX - gridX - margin);
        const int gridViewH = std::max(120, winH - margin * 2);
        int cell = std::min(gridViewW / std::max(1, gridW), gridViewH / std::max(1, gridH));
        cell = std::max(12, cell);
        const int gridPxW = gridW * cell;
        const int gridPxH = gridH * cell;
        const int maxViewX = std::max(0, gridPxW - gridViewW);
        const int maxViewY = std::max(0, gridPxH - gridViewH);
        viewX = std::clamp(viewX, 0, maxViewX);
        viewY = std::clamp(viewY, 0, maxViewY);
        SDL_Rect gridViewport{gridX, gridY, gridViewW, gridViewH};
        SDL_Rect tileModeBtn{panelX, margin, 76, 30};
        SDL_Rect objectModeBtn{panelX + 84, margin, 76, 30};
        const int paletteStartY = margin + 40;
        SDL_Rect saveBtn{panelX, winH - 140, 160, 36};
        SDL_Rect cancelBtn{panelX, winH - 96, 160, 36};
        const int paletteRowH = 44;
        const int paletteBottomY = saveBtn.y - 8;
        const int paletteViewRows = std::max(1, (paletteBottomY - paletteStartY) / paletteRowH);
        const int paletteCountNow = objectMode ? (int)objectPalette.size() : (int)palette.size();
        int& activePaletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
        const int maxPaletteScroll = std::max(0, paletteCountNow - paletteViewRows);
        activePaletteScroll = std::clamp(activePaletteScroll, 0, maxPaletteScroll);
        SDL_Rect paletteViewport{panelX, paletteStartY, 160, paletteViewRows * paletteRowH};
        SDL_Rect pausePanel{winW / 2 - 180, winH / 2 - 120, 360, 240};
        SDL_Rect pauseResumeBtn{pausePanel.x + 24, pausePanel.y + 150, 96, 40};
        SDL_Rect pauseSaveBtn{pausePanel.x + 132, pausePanel.y + 150, 96, 40};
        SDL_Rect pauseExitBtn{pausePanel.x + 240, pausePanel.y + 150, 96, 40};

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            editorInput.handleEvent(e);
            if (e.type == SDL_QUIT) { running = false; break; }
            if (SDL_GetTicks() < inputBlockUntilTicks) continue;
            if (e.type == SDL_MOUSEWHEEL && !paused) {
                float mx = 0.0f, my = 0.0f;
                SDL_GetMouseState(&mx, &my);
                SDL_Point pt{(int)std::lround(mx), (int)std::lround(my)};
                if (SDL_PointInRect(&pt, &paletteViewport)) {
                    const int wheelY = (int)std::lround(e.wheel.y);
                    activePaletteScroll = std::clamp(activePaletteScroll - wheelY, 0, maxPaletteScroll);
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
            if (e.type == SDL_FINGERDOWN && activeEditorFinger == 0) {
                activeEditorFinger = e.tfinger.fingerID;
                const int tx = (int)std::lround(e.tfinger.x * winW);
                const int ty = (int)std::lround(e.tfinger.y * winH);
                SDL_Point pt{tx, ty};

                if (paused) {
                    if (SDL_PointInRect(&pt, &pauseResumeBtn)) {
                        pauseSel = 0;
                        paused = false;
                    } else if (SDL_PointInRect(&pt, &pauseSaveBtn)) {
                        pauseSel = 1;
                        savedPath = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects);
                        if (!savedPath.empty()) {
                            running = false;
                            break;
                        }
                        statusText = "SAVE FAILED";
                        statusUntil = SDL_GetTicks() + 1800;
                    } else if (SDL_PointInRect(&pt, &pauseExitBtn)) {
                        pauseSel = 2;
                        running = false;
                        break;
                    }
                    continue;
                }

                if (SDL_PointInRect(&pt, &saveBtn)) {
                    savedPath = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects);
                    if (!savedPath.empty()) {
                        running = false;
                        break;
                    }
                    statusText = "SAVE FAILED";
                    statusUntil = SDL_GetTicks() + 1800;
                    continue;
                }
                if (SDL_PointInRect(&pt, &cancelBtn)) {
                    running = false;
                    break;
                }
                if (SDL_PointInRect(&pt, &tileModeBtn)) {
                    objectMode = false;
                    fingerPainting = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &objectModeBtn)) {
                    objectMode = true;
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
                    SDL_Rect r{panelX, paletteStartY + row * paletteRowH, 160, 36};
                    if (SDL_PointInRect(&pt, &r)) {
                        if (objectMode) selectedObjectPalette = i;
                        else selectedPalette = i;
                        fingerPainting = false;
                        break;
                    }
                }
                if (SDL_PointInRect(&pt, &gridViewport)) {
                    const int cx = ((tx - gridX) + viewX) / cell;
                    const int cy = ((ty - gridY) + viewY) / cell;
                    applyAt(cx, cy, false);
                    fingerPainting = true;
                } else {
                    fingerPainting = false;
                }
                continue;
            }
            if (e.type == SDL_FINGERMOTION && e.tfinger.fingerID == activeEditorFinger) {
                const int tx = (int)std::lround(e.tfinger.x * winW);
                const int ty = (int)std::lround(e.tfinger.y * winH);
                SDL_Point pt{tx, ty};
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
                    applyAt(cx, cy, false);
                }
                continue;
            }
            if (e.type == SDL_FINGERUP && e.tfinger.fingerID == activeEditorFinger) {
                if (fingerPaletteScroll) {
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
                            SDL_Rect r{panelX, paletteStartY + row * paletteRowH, 160, 36};
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
                activeEditorFinger = 0;
                fingerPainting = false;
                continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.key == SDLK_F11) {
#if !defined(__ANDROID__)
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
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_a) pauseSel = std::max(0, pauseSel - 1);
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_d) pauseSel = std::min(2, pauseSel + 1);
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (pauseSel == 0) {
                            paused = false;
                        } else if (pauseSel == 1) {
                            savedPath = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects);
                            if (!savedPath.empty()) {
                                running = false;
                                break;
                            }
                            statusText = "SAVE FAILED";
                            statusUntil = SDL_GetTicks() + 1800;
                        } else {
                            running = false;
                            break;
                        }
                    }
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
                if (e.key.key == SDLK_c || e.key.key == SDLK_C) {
                    std::fill(grid.begin(), grid.end(), (unsigned short)2);
                    for (int x = 0; x < gridW; ++x) grid[(gridH - 1) * gridW + x] = 1;
                    const int spawnX = std::min(3, std::max(0, gridW - 1));
                    const int spawnY = std::max(0, gridH - 4);
                    grid[spawnY * gridW + spawnX] = 28;
                    placedObjects.clear();
                }
                if (e.key.key == SDLK_LEFT) viewX = std::max(0, viewX - std::max(8, cell * 2));
                if (e.key.key == SDLK_RIGHT) viewX = std::min(maxViewX, viewX + std::max(8, cell * 2));
                if (e.key.key == SDLK_UP) viewY = std::max(0, viewY - std::max(8, cell * 2));
                if (e.key.key == SDLK_DOWN) viewY = std::min(maxViewY, viewY + std::max(8, cell * 2));
                if (e.key.key == SDLK_s || e.key.key == SDLK_S || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    savedPath = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects);
                    if (!savedPath.empty()) {
                        running = false;
                        break;
                    }
                    statusText = "SAVE FAILED";
                    statusUntil = SDL_GetTicks() + 1800;
                }
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
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) pauseSel = std::max(0, pauseSel - 1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) pauseSel = std::min(2, pauseSel + 1);
                    if (isAcceptBtn) {
                        if (pauseSel == 0) {
                            paused = false;
                        } else if (pauseSel == 1) {
                            savedPath = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects);
                            if (!savedPath.empty()) {
                                running = false;
                                break;
                            }
                            statusText = "SAVE FAILED";
                            statusUntil = SDL_GetTicks() + 1800;
                        } else {
                            running = false;
                            break;
                        }
                    }
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
                if (SDL_PointInRect(&pt, &pauseResumeBtn)) {
                    pauseSel = 0;
                    paused = false;
                    continue;
                }
                if (SDL_PointInRect(&pt, &pauseSaveBtn)) {
                    pauseSel = 1;
                    savedPath = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects);
                    if (!savedPath.empty()) {
                        running = false;
                        break;
                    }
                    statusText = "SAVE FAILED";
                    statusUntil = SDL_GetTicks() + 1800;
                    continue;
                }
                if (SDL_PointInRect(&pt, &pauseExitBtn)) {
                    pauseSel = 2;
                    running = false;
                    break;
                }
                continue;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION) {
                if (paused) continue;
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
                int mx = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
                int my = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
                bool erase = false;
                if (e.type == SDL_MOUSEBUTTONDOWN) erase = (e.button.button == SDL_BUTTON_RIGHT);
                if (e.type == SDL_MOUSEMOTION) erase = (e.motion.state & SDL_BUTTON_RMASK) != 0;
                SDL_Point pt{mx, my};
                if (SDL_PointInRect(&pt, &gridViewport)) {
                    int cx = ((mx - gridX) + viewX) / cell;
                    int cy = ((my - gridY) + viewY) / cell;
                    applyAt(cx, cy, erase);
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_MIDDLE) {
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
                    continue;
                }
                if (SDL_PointInRect(&pt, &objectModeBtn)) {
                    objectMode = true;
                    continue;
                }
                if (SDL_PointInRect(&pt, &saveBtn)) {
                    savedPath = writeLocalLevelFile(grid, gridW, gridH, saveTargetPath, placedObjects);
                    if (!savedPath.empty()) {
                        running = false;
                        break;
                    }
                    statusText = "SAVE FAILED";
                    statusUntil = SDL_GetTicks() + 1800;
                }
                if (SDL_PointInRect(&pt, &cancelBtn)) {
                    running = false;
                    break;
                }
                const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
                int& paletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
                const int maxScroll = std::max(0, paletteCount - paletteViewRows);
                paletteScroll = std::clamp(paletteScroll, 0, maxScroll);
                for (int row = 0; row < paletteViewRows; ++row) {
                    const int i = paletteScroll + row;
                    if (i >= paletteCount) break;
                    SDL_Rect r{panelX, paletteStartY + row * paletteRowH, 160, 36};
                    if (mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h) {
                        if (objectMode) selectedObjectPalette = i;
                        else selectedPalette = i;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 18, 20, 26, 255);
        SDL_RenderClear(ren);

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
            if (id == 67) { r = 120; g = 180; b = 255; return; }
            r = 220; g = 220; b = 240;
        };
        auto objectLabel = [](int id) -> std::string {
            if (id == 31) return "SPRING (31)";
            if (id == 46) return "BUMPER (46)";
            if (id == 57) return "FAST UP (57)";
            if (id == 58) return "FAST DOWN (58)";
            if (id == 59) return "FAST LEFT (59)";
            if (id == 60) return "FAST RIGHT (60)";
            if (id == 61) return "FAST EXIT (61)";
            if (id == 67) return "END SIGN (67)";
            return std::string("Obj ") + std::to_string(id);
        };
        auto objectTag = [](int id) -> const char* {
            if (id == 57) return "UP";
            if (id == 58) return "DN";
            if (id == 59) return "LT";
            if (id == 60) return "RT";
            if (id == 61) return "EX";
            return "";
        };
        for (const auto& obj : placedObjects) {
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
            SDL_SetRenderDrawColor(ren, r, g, b, 200);
            SDL_Rect in{rc.x + std::max(2, cell / 6), rc.y + std::max(2, cell / 6), std::max(4, cell - std::max(4, cell / 3)), std::max(4, cell - std::max(4, cell / 3))};
            SDL_RenderFillRect(ren, &in);
            DrawText(ren, rc.x + 3, rc.y + 3, std::max(1, cell / 24), std::to_string(id));
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
        SDL_Rect border{gridX - 1, gridY - 1, gridViewW + 2, gridViewH + 2};
        SDL_RenderDrawRect(ren, &border);

        DrawText(ren, panelX, winH - 96, 2, "S/ENTER: SAVE+PLAY");
        DrawText(ren, panelX, winH - 72, 2, "C: CLEAR");
        DrawText(ren, panelX, winH - 48, 2, "ESC: PAUSE  O: MODE");
        DrawText(ren, panelX, winH - 24, 2, objectMode ? "OBJ MODE: tap obj to remove / RMB erase" : "TILE MODE: LMB paint / RMB erase");
        DrawText(ren, panelX, winH - 192, 2, "WHEEL/MMB/ARROWS: SCROLL");
        if (!statusText.empty() && SDL_GetTicks() < statusUntil) {
            DrawText(ren, panelX, winH - 168, 2, statusText);
        }
        SDL_SetRenderDrawColor(ren, 55, 95, 70, 255);
        SDL_RenderFillRect(ren, &saveBtn);
        SDL_SetRenderDrawColor(ren, 190, 230, 200, 255);
        SDL_RenderDrawRect(ren, &saveBtn);
        DrawText(ren, saveBtn.x + 36, saveBtn.y + 9, 2, "SAVE");
        SDL_SetRenderDrawColor(ren, 75, 55, 55, 255);
        SDL_RenderFillRect(ren, &cancelBtn);
        SDL_SetRenderDrawColor(ren, 220, 190, 190, 255);
        SDL_RenderDrawRect(ren, &cancelBtn);
        DrawText(ren, cancelBtn.x + 28, cancelBtn.y + 9, 2, "CANCEL");

        SDL_SetRenderDrawColor(ren, objectMode ? 50 : 90, objectMode ? 70 : 110, objectMode ? 90 : 170, 255);
        SDL_RenderFillRect(ren, &tileModeBtn);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &tileModeBtn);
        DrawText(ren, tileModeBtn.x + 12, tileModeBtn.y + 8, 2, "TILES");
        SDL_SetRenderDrawColor(ren, objectMode ? 90 : 50, objectMode ? 110 : 70, objectMode ? 170 : 90, 255);
        SDL_RenderFillRect(ren, &objectModeBtn);
        SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
        SDL_RenderDrawRect(ren, &objectModeBtn);
        DrawText(ren, objectModeBtn.x + 18, objectModeBtn.y + 8, 2, "OBJ");

        const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
        int& paletteScroll = objectMode ? objectPaletteScroll : tilePaletteScroll;
        const int paletteMaxScroll = std::max(0, paletteCount - paletteViewRows);
        paletteScroll = std::clamp(paletteScroll, 0, paletteMaxScroll);
        for (int row = 0; row < paletteViewRows; ++row) {
            const int i = paletteScroll + row;
            if (i >= paletteCount) break;
            const bool selected = objectMode ? (i == selectedObjectPalette) : (i == selectedPalette);
            SDL_Rect r{panelX, paletteStartY + row * paletteRowH, 160, 36};
            SDL_SetRenderDrawColor(ren, selected ? 70 : 45, selected ? 100 : 65, selected ? 160 : 95, 255);
            SDL_RenderFillRect(ren, &r);
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
                } else {
                    DrawText(ren, r.x + 38, r.y + 8, 2, std::string("Tile ") + std::to_string((int)pid));
                }
            } else {
                const int oid = objectPalette[i];
                Uint8 tr = 220, tg = 220, tb = 240;
                objectColor(oid, tr, tg, tb);
                SDL_SetRenderDrawColor(ren, tr, tg, tb, 255);
                SDL_Rect sw{r.x + 6, r.y + 6, 24, 24};
                SDL_RenderFillRect(ren, &sw);
                DrawText(ren, r.x + 38, r.y + 8, 2, objectLabel(oid));
            }
        }
        if (paletteCount > paletteViewRows) {
            SDL_Rect track{panelX + 164, paletteStartY, 8, paletteViewRows * paletteRowH - 8};
            SDL_SetRenderDrawColor(ren, 40, 46, 62, 255);
            SDL_RenderFillRect(ren, &track);
            const int thumbH = std::max(16, (track.h * paletteViewRows) / std::max(1, paletteCount));
            const int thumbY = track.y + ((track.h - thumbH) * paletteScroll) / std::max(1, paletteCount - paletteViewRows);
            SDL_Rect thumb{track.x, thumbY, track.w, thumbH};
            SDL_SetRenderDrawColor(ren, 120, 150, 210, 255);
            SDL_RenderFillRect(ren, &thumb);
        }

        DrawText(ren, panelX, paletteStartY + paletteViewRows * paletteRowH + 14, 2, "LEVEL EDITOR");
        if (paused) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_Rect dim{0, 0, winW, winH};
            SDL_RenderFillRect(ren, &dim);

            SDL_SetRenderDrawColor(ren, 30, 34, 42, 255);
            SDL_RenderFillRect(ren, &pausePanel);
            SDL_SetRenderDrawColor(ren, 180, 200, 230, 255);
            SDL_RenderDrawRect(ren, &pausePanel);
            DrawText(ren, pausePanel.x + 120, pausePanel.y + 24, 3, "PAUSED");
            DrawText(ren, pausePanel.x + 26, pausePanel.y + 70, 2, "ESC: RESUME");
            DrawText(ren, pausePanel.x + 26, pausePanel.y + 94, 2, "ARROWS + ENTER: MENU");

            SDL_Rect pauseBtns[3] = {pauseResumeBtn, pauseSaveBtn, pauseExitBtn};
            const char* labels[3] = {"RESUME", "SAVE", "EXIT"};
            for (int i = 0; i < 3; ++i) {
                SDL_SetRenderDrawColor(ren, i == pauseSel ? 90 : 60, i == pauseSel ? 120 : 80, i == pauseSel ? 170 : 110, 255);
                SDL_RenderFillRect(ren, &pauseBtns[i]);
                SDL_SetRenderDrawColor(ren, 220, 220, 235, 255);
                SDL_RenderDrawRect(ren, &pauseBtns[i]);
                DrawText(ren, pauseBtns[i].x + 12, pauseBtns[i].y + 10, 2, labels[i]);
            }
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (blocksTex) SDL_DestroyTexture(blocksTex);
    return savedPath;
}

std::string decodeServerLevelPayload(const std::string& payload) {
    auto decodeNumericalEncodingV2 = [](const std::string& text) -> std::string {
        if (text.empty()) return {};
        for (char ch : text) {
            if (ch < '1' || ch > '8') return {};
        }

        const size_t encodedBytes = (text.size() * 3) / 8;
        std::string out;
        out.resize(encodedBytes);
        size_t ptr = 0;
        size_t i = 0;

        for (; i + 7 < text.size(); i += 8) {
            const int a = text[i] - '1';
            const int b = text[i + 1] - '1';
            const int c = text[i + 2] - '1';
            const int d = text[i + 3] - '1';
            const int e = text[i + 4] - '1';
            const int f = text[i + 5] - '1';
            const int g = text[i + 6] - '1';
            const int h = text[i + 7] - '1';
            out[ptr++] = (char)((a << 5) | (b << 2) | (c >> 1));
            out[ptr++] = (char)(((c & 0b1) << 7) | (d << 4) | (e << 1) | (f >> 2));
            out[ptr++] = (char)(((f & 0b11) << 6) | (g << 3) | h);
        }

        switch (encodedBytes - ptr) {
            case 1: {
                if (i + 2 >= text.size()) return {};
                const int a = text[i] - '1';
                const int b = text[i + 1] - '1';
                const int c = text[i + 2] - '1';
                out[ptr] = (char)((a << 5) | (b << 2) | (c >> 1));
                break;
            }
            case 2: {
                if (i + 5 >= text.size()) return {};
                const int a = text[i] - '1';
                const int b = text[i + 1] - '1';
                const int c = text[i + 2] - '1';
                const int d = text[i + 3] - '1';
                const int e = text[i + 4] - '1';
                const int f = text[i + 5] - '1';
                out[ptr++] = (char)((a << 5) | (b << 2) | (c >> 1));
                out[ptr] = (char)(((c & 0b1) << 7) | (d << 4) | (e << 1) | (f >> 2));
                break;
            }
            default:
                break;
        }

        return out;
    };

    auto trim = [](std::string s) {
        size_t a = 0;
        while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
        size_t b = s.size();
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    };

    const std::string t = trim(payload);
    if (t.empty()) return t;
    if (t[0] != '{' && t[0] != '[' && t[0] != '"') {
        std::string decoded = decodeNumericalEncodingV2(t);
        return decoded.empty() ? payload : decoded;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(t);
    } catch (...) {
        return payload;
    }

    auto decodeNumericArray = [](const nlohmann::json& arr) -> std::string {
        if (!arr.is_array()) return {};
        std::string out;
        out.reserve(arr.size());
        for (const auto& v : arr) {
            if (!v.is_number_integer()) return {};
            int c = v.get<int>();
            if (c < 0 || c > 255) return {};
            out.push_back((char)c);
        }
        return out;
    };

    auto extractEncoded = [&](const nlohmann::json& node) -> std::string {
        if (node.is_string()) {
            const std::string s = node.get<std::string>();
            const std::string decoded = decodeNumericalEncodingV2(s);
            return decoded.empty() ? s : decoded;
        }
        if (node.is_array()) return decodeNumericArray(node);
        if (node.is_object()) {
            const char* keys[] = {"data", "encoded", "level", "payload", "value"};
            for (const char* k : keys) {
                if (!node.contains(k)) continue;
                const auto& v = node[k];
                if (v.is_string()) {
                    const std::string s = v.get<std::string>();
                    const std::string decoded = decodeNumericalEncodingV2(s);
                    return decoded.empty() ? s : decoded;
                }
                if (v.is_array()) {
                    std::string d = decodeNumericArray(v);
                    if (!d.empty()) return d;
                }
            }
        }
        return {};
    };

    std::string decoded = extractEncoded(j);
    return decoded.empty() ? payload : decoded;
}

std::string downloadFolderPath() {
    const std::string base = GetAppSaveRootPath();
    std::filesystem::path dir = std::filesystem::path(base) / "user_levels";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

std::string localLevelsFolderPath() {
    const std::string base = GetAppSaveRootPath();
    std::filesystem::path dir = std::filesystem::path(base) / "local levels";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

std::string cacheRemoteLevel(const LevelEntry& entry) {
    if (!isHttpUrl(entry.path)) return entry.path;

    SDL_Log("NET/UI: download requested label=%s url=%s", entry.label.c_str(), entry.path.c_str());
    const std::string rawPayload = ReadTextFile(entry.path);
    if (rawPayload.empty()) {
        SDL_Log("NET/UI: download failed label=%s url=%s", entry.label.c_str(), entry.path.c_str());
        return {};
    }
    const std::string payload = decodeServerLevelPayload(rawPayload);

    const std::string cacheDir = downloadFolderPath();
    const std::string fileName = isNumericId(entry.label)
        ? (entry.label + ".bin")
        : (sanitizeFilePart(entry.label) + "_" + std::to_string((unsigned long long)std::hash<std::string>{}(entry.path)) + ".bin");
    const std::filesystem::path outPath = std::filesystem::path(cacheDir) / fileName;
    const std::filesystem::path tmpPath = outPath.string() + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            SDL_Log("NET/UI: cache open failed path=%s", tmpPath.string().c_str());
            return {};
        }
        out << payload;
        out.flush();
        if (!out.good()) {
            SDL_Log("NET/UI: cache write failed path=%s", tmpPath.string().c_str());
            return {};
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, outPath, ec);
    if (ec) {
        std::filesystem::remove(outPath, ec);
        ec.clear();
        std::filesystem::rename(tmpPath, outPath, ec);
        if (ec) {
            SDL_Log("NET/UI: cache rename failed tmp=%s dst=%s",
                    tmpPath.string().c_str(), outPath.string().c_str());
            return {};
        }
    }
    SDL_Log("NET/UI: cache ok label=%s dst=%s raw_bytes=%d decoded_bytes=%d",
            entry.label.c_str(), outPath.string().c_str(), (int)rawPayload.size(), (int)payload.size());
    return outPath.string();
}

std::string cachedPathForEntry(const LevelEntry& entry) {
    const std::string cacheDir = downloadFolderPath();
    const std::string fileName = isNumericId(entry.label)
        ? (entry.label + ".bin")
        : (sanitizeFilePart(entry.label) + "_" + std::to_string((unsigned long long)std::hash<std::string>{}(entry.path)) + ".bin");
    return (std::filesystem::path(cacheDir) / fileName).string();
}

std::vector<LevelEntry> loadLevelListFromJson(const std::string& jsonPath, const std::string& baseDir) {
    std::vector<LevelEntry> out;
    const std::string text = ReadTextFile(jsonPath);
    if (text.empty()) return out;
    nlohmann::json j;
    try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
    if (!j.contains("levels") || !j["levels"].is_array()) return out;
    for (const auto& v : j["levels"]) {
        if (!v.is_string()) continue;
        std::string name = v.get<std::string>();
        const std::filesystem::path labelPath(name);
        std::string label = labelPath.stem().string();
        if (label.empty()) label = labelPath.filename().string();
        const std::string parent = labelPath.parent_path().generic_string();
        if (!parent.empty() && parent != ".") {
            label = parent + "/" + label;
        }
        std::string path = baseDir.empty() ? name : (baseDir + "/" + name);
        // Campaign manifests list .bnnlvl while packed files are .txt; normalize to existing local file.
        if (!isHttpUrl(path) && !FileExists(path)) {
            const std::string bnn = ".bnnlvl";
            if (path.size() > bnn.size() && path.substr(path.size() - bnn.size()) == bnn) {
                std::string txtPath = path.substr(0, path.size() - bnn.size()) + ".txt";
                if (FileExists(txtPath)) path = txtPath;
            }
        }
        out.push_back(LevelEntry{label, path});
    }
    std::sort(out.begin(), out.end(), [](const LevelEntry& a, const LevelEntry& b) { return a.label < b.label; });
    return out;
}

std::vector<LevelEntry> loadLevelListFromDir(const std::string& dirPath, bool prettyLabel = false) {
    std::vector<LevelEntry> out;
    namespace fs = std::filesystem;
    fs::path dir(dirPath);
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) return out;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        const auto entry = *it;
        std::error_code typeEc;
        if (entry.is_regular_file(typeEc) && !typeEc) {
            auto p = entry.path();
            const auto ext = p.extension().string();
            if (ext.empty() || ext == ".txt" || ext == ".bnnlvl" || ext == ".bin") {
                std::string label = p.stem().string();
                if (label.empty()) label = p.filename().string();
                const std::string relParent = p.parent_path().lexically_relative(dir).generic_string();
                if (!relParent.empty() && relParent != ".") {
                    label = relParent + "/" + label;
                }
                if (prettyLabel) {
                    for (char& ch : label) {
                        if (ch == '_') ch = ' ';
                    }
                }
                out.push_back(LevelEntry{label, p.string()});
            }
        }
        it.increment(ec);
    }
    std::sort(out.begin(), out.end(), [](const LevelEntry& a, const LevelEntry& b) { return a.label < b.label; });
    return out;
}

std::vector<LevelEntry> loadCampaignLevels() {
#if defined(__ANDROID__)
    return loadLevelListFromJson("assets/levels/levels.json", "assets/levels");
#else
    return loadLevelListFromDir("assets/levels");
#endif
}

std::vector<LevelEntry> loadCustomLevels() {
    auto addUniqueByPath = [](std::vector<LevelEntry>& dst, const std::vector<LevelEntry>& src) {
        for (const auto& e : src) {
            bool exists = false;
            for (const auto& d : dst) {
                if (d.path == e.path) {
                    exists = true;
                    break;
                }
            }
            if (!exists) dst.push_back(e);
        }
    };

    std::vector<LevelEntry> out;
    const std::string levelServerUrl = GetLevelServerUrl();
    const std::string levelServerAuthToken = GetLevelServerAuthToken();
    auto buildFirebaseUrl = [&](const std::string& base, const std::string& path, const std::string& extraQuery = "") -> std::string {
        std::string u = base;
        while (!u.empty() && u.back() == '/') u.pop_back();
        std::string p = path;
        if (!p.empty() && p.front() != '/') p = "/" + p;
        u += p;
        if (u.size() < 5 || u.substr(u.size() - 5) != ".json") u += ".json";
        std::string query = extraQuery;
        if (!levelServerAuthToken.empty()) {
            if (!query.empty()) query += "&";
            query += "auth=" + levelServerAuthToken;
        }
        if (!query.empty()) u += "?" + query;
        return u;
    };
    if (!levelServerUrl.empty()) {
        std::string base = levelServerUrl;
        while (!base.empty() && base.back() == '/') base.pop_back();
        std::string levelsRootText = ReadTextFile(buildFirebaseUrl(base, "/levels", "shallow=true"));
        if (levelsRootText.empty()) {
            levelsRootText = ReadTextFile(buildFirebaseUrl(base, "/levels"));
        }
        if (!levelsRootText.empty()) {
            nlohmann::json levelsRoot;
            try { levelsRoot = nlohmann::json::parse(levelsRootText); } catch (...) { levelsRoot = nlohmann::json(); }
            if (levelsRoot.is_object()) {
                for (auto it = levelsRoot.begin(); it != levelsRoot.end(); ++it) {
                    const std::string id = it.key();
                    const std::string path = buildFirebaseUrl(base, "/levels/" + id + "/data");
                    addUniqueByPath(out, {LevelEntry{id, path}});
                }
            }
        }
        addUniqueByPath(out, loadLevelListFromJson(buildFirebaseUrl(base, "/custom_levels/levels"), base + "/custom_levels"));
        addUniqueByPath(out, loadLevelListFromJson(buildFirebaseUrl(base, "/custom_levels"), ""));
    }
#if defined(__ANDROID__)
    addUniqueByPath(out, loadLevelListFromJson("assets/custom_levels/levels.json", "assets/custom_levels"));
    std::sort(out.begin(), out.end(), [](const LevelEntry& a, const LevelEntry& b) { return a.label < b.label; });
    return out;
#else
    addUniqueByPath(out, loadLevelListFromDir("custom_levels"));
    std::vector<LevelEntry> assetsOut = loadLevelListFromDir("assets/custom_levels");
    addUniqueByPath(out, assetsOut);
    std::sort(out.begin(), out.end(), [](const LevelEntry& a, const LevelEntry& b) { return a.label < b.label; });
    return out;
#endif
}
}

bool uploadLocalLevelToServer(const LevelEntry& level, std::string& statusText);

static std::string RunLevelSelectImpl(SDL_Window* win, SDL_Renderer* ren, bool includeCampaign, bool includeCustom) {
    const OnlineLevelsMenuLabels menuLabels = loadOnlineLevelsMenuLabels();
    std::vector<LevelEntry> campaignLevels = loadCampaignLevels();
    std::vector<LevelEntry> customLevels;
    if (includeCustom) {
        customLevels = loadCustomLevels();
    }
    std::vector<LevelEntry> localLevels = loadLevelListFromDir(localLevelsFolderPath(), true);
    if (!includeCampaign) campaignLevels.clear();
    if (includeCustom) {
        localLevels.insert(localLevels.begin(), LevelEntry{menuLabels.localEditorEntry, "__local_editor__"});
    } else {
        localLevels.clear();
    }
    if (campaignLevels.empty() && customLevels.empty() && localLevels.empty()) return "";

    int activeTab = 0;
    int selected[3] = {0, 0, 0};
    int scrollY[3] = {0, 0, 0};
    bool running = true;
    const Uint64 inputBlockUntilTicks = SDL_GetTicks() + 1000;
    bool chosen = false;
    std::string chosenPath;
    std::string statusText;
    Uint64 statusUntilTicks = 0;
    bool draggingScrollbar = false;
    bool draggingScrollbarTouch = false;
    int dragOffsetY = 0;
    SDL_FingerID activeFinger = 0;
    SDL_FingerID scrollbarFinger = 0;
    float lastFingerY = 0.0f;
    float fingerDownY = 0.0f;
    bool fingerMoved = false;
    bool localPageOpen = false;
    int localPageIndex = -1;

    int winW = 960, winH = 540;
    SDL_GetWindowSize(win, &winW, &winH);

    while (running) {
        SDL_GetWindowSize(win, &winW, &winH);
        float uiScale = std::min((float)winW / 960.0f, (float)winH / 540.0f);
        uiScale = std::clamp(uiScale, 0.75f, 2.0f);
        int rowH = std::max(24, (int)std::lround(28.0f * uiScale));
        int pad = std::max(10, (int)std::lround(16.0f * uiScale));
        int textScale = std::max(1, (int)std::lround(2.0f * uiScale));
        int tabH = std::max(30, (int)std::lround(40.0f * uiScale));
        const int shellPad = std::max(12, (int)std::lround(16.0f * uiScale));
        const int sidebarW = std::max(170, (int)std::lround(190.0f * uiScale));
        const int infoW = (winW >= 880) ? std::max(180, (int)std::lround(220.0f * uiScale)) : 0;
        const SDL_Rect shell{shellPad, shellPad, std::max(1, winW - shellPad * 2), std::max(1, winH - shellPad * 2)};
        const SDL_Rect sidePanel{shell.x + shellPad, shell.y + shellPad, sidebarW, std::max(1, shell.h - shellPad * 2)};
        const SDL_Rect infoPanel{
            shell.x + shell.w - shellPad - infoW,
            shell.y + shellPad,
            infoW,
            std::max(1, shell.h - shellPad * 2)
        };
        const int contentX = sidePanel.x + sidePanel.w + shellPad;
        const int contentW = std::max(1, shell.w - (contentX - shell.x) - shellPad - infoW);
        const SDL_Rect contentPanel{contentX, shell.y + shellPad, contentW, std::max(1, shell.h - shellPad * 2)};
        int listTop = contentPanel.y + std::max(40, (int)std::lround(46.0f * uiScale));
        SDL_Rect downloadBtn{contentPanel.x + contentPanel.w - std::max(170, (int)std::lround(190.0f * uiScale)), shell.y + shellPad, std::max(170, (int)std::lround(190.0f * uiScale)), tabH};
        int userTabIndex = -1;
        int localTabIndex = -1;
        std::vector<std::pair<std::string, const std::vector<LevelEntry>*>> tabs;
        if (!campaignLevels.empty()) { tabs.push_back({menuLabels.tabCampaign, &campaignLevels}); }
        if (includeCustom) {
            userTabIndex = (int)tabs.size();
            tabs.push_back({menuLabels.tabUser, &customLevels});
            localTabIndex = (int)tabs.size();
            tabs.push_back({menuLabels.tabLocal, &localLevels});
        }
        if (tabs.empty()) return "";
        if (activeTab < 0 || activeTab >= (int)tabs.size()) activeTab = 0;
        if (activeTab != localTabIndex) {
            localPageOpen = false;
            localPageIndex = -1;
        }
        const bool showTabs = tabs.size() > 1;
        const bool currentTabAllowsDownload = (activeTab == userTabIndex);
        const std::vector<LevelEntry>& levels = *tabs[activeTab].second;
        int& selectedIndex = selected[activeTab];
        int& scroll = scrollY[activeTab];
        if (selectedIndex >= (int)levels.size()) selectedIndex = std::max(0, (int)levels.size() - 1);

        int contentH = (int)levels.size() * rowH;
        int viewportH = std::max(1, contentPanel.h - (listTop - contentPanel.y) - shellPad);
        int listBottom = listTop + viewportH;
        int maxScroll = std::max(0, contentH - viewportH);
        scroll = std::clamp(scroll, 0, maxScroll);

        int barW = std::max(10, (int)std::lround(14.0f * uiScale));
        SDL_Rect track{contentPanel.x + contentPanel.w - shellPad - barW, listTop, barW, viewportH};
        float visibleRatio = (contentH > 0) ? std::clamp((float)viewportH / (float)contentH, 0.0f, 1.0f) : 1.0f;
        int thumbH = std::max(std::max(24, (int)std::lround(36.0f * uiScale)), (int)std::lround(track.h * visibleRatio));
        int thumbTravel = std::max(1, track.h - thumbH);
        int thumbY = track.y + ((maxScroll > 0) ? (int)std::lround((float)scroll / (float)maxScroll * thumbTravel) : 0);
        SDL_Rect thumb{track.x, thumbY, barW, thumbH};
        const int sliderHitPadX = std::max(6, (int)std::lround(8.0f * uiScale));
        const int sliderHitPadY = std::max(4, (int)std::lround(6.0f * uiScale));
        SDL_Rect trackHit{track.x - sliderHitPadX, track.y - sliderHitPadY, track.w + sliderHitPadX * 2, track.h + sliderHitPadY * 2};
        SDL_Rect thumbHit{thumb.x - sliderHitPadX, thumb.y - sliderHitPadY, thumb.w + sliderHitPadX * 2, thumb.h + sliderHitPadY * 2};
        const int tabW = sidePanel.w;
        const int tabGap = std::max(8, (int)std::lround(10.0f * uiScale));
        const int tabStartY = sidePanel.y + std::max(8, (int)std::lround(10.0f * uiScale));
        std::vector<SDL_Rect> tabRects;
        tabRects.reserve(tabs.size());
        for (int i = 0; i < (int)tabs.size(); ++i) {
            tabRects.push_back(SDL_Rect{sidePanel.x, tabStartY + i * (tabH + tabGap), tabW, tabH});
        }

        auto switchTab = [&](int tab) {
            if (tab < 0 || tab >= (int)tabs.size()) return;
            activeTab = tab;
        };
        auto showStatus = [&](const std::string& s) {
            statusText = s;
            statusUntilTicks = SDL_GetTicks() + 2200;
        };
        auto tryDownloadSelected = [&]() {
            if (levels.empty()) return;
            const LevelEntry& e = levels[selectedIndex];
            if (e.path == "__local_editor__") {
                showStatus("Select this row to open editor.");
                return;
            }
            if (!isHttpUrl(e.path)) {
                showStatus("Selected level is local.");
                return;
            }
            const std::string cachedPath = cachedPathForEntry(e);
            if (FileExists(cachedPath)) {
                showStatus("Already downloaded.");
                return;
            }
            const std::string out = cacheRemoteLevel(e);
            if (out.empty()) showStatus("Download failed.");
            else showStatus("Downloaded to save folder.");
        };
        auto tryUploadLocalAt = [&](int idx) {
            if (idx < 0 || idx >= (int)localLevels.size()) {
                showStatus("No local level selected.");
                return;
            }
            std::string uploadStatus;
            if (uploadLocalLevelToServer(localLevels[idx], uploadStatus)) {
                showStatus(uploadStatus);
                customLevels = loadCustomLevels();
            } else {
                showStatus(uploadStatus);
            }
        };
        auto resolveSelectedPath = [&](const LevelEntry& sel) -> std::string {
            if (sel.path == "__local_editor__") {
                const NewLevelPromptResult prompt = RunNewLevelPrompt(win, ren);
                if (!prompt.accepted) return {};
                return RunLocalLevelEditor(win, ren, "", prompt.name, prompt.width, prompt.height);
            }
            std::string p = cacheRemoteLevel(sel);
            if (p.empty()) p = sel.path;
            return p;
        };
        auto resolveLocalPageSelection = [&]() -> std::string {
            if (!localPageOpen || localTabIndex < 0) return {};
            if (localPageIndex < 0 || localPageIndex >= (int)localLevels.size()) return {};
            return resolveSelectedPath(localLevels[localPageIndex]);
        };
        InputSystem selectInput;
        selectInput.scanConnected();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            selectInput.handleEvent(e);
            if (e.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (SDL_GetTicks() < inputBlockUntilTicks) continue;
            if (e.type == SDL_MOUSEWHEEL) {
                scroll -= e.wheel.y * rowH;
                if (scroll < 0) scroll = 0;
                if (scroll > maxScroll) scroll = maxScroll;
            }
            if (e.type == SDL_FINGERDOWN && activeFinger == 0) {
                const int fx = (int)std::lround(e.tfinger.x * winW);
                const int fy = (int)std::lround(e.tfinger.y * winH);
                SDL_Point fpt{fx, fy};
                if (maxScroll > 0 && SDL_PointInRect(&fpt, &trackHit)) {
                    draggingScrollbarTouch = true;
                    scrollbarFinger = e.tfinger.fingerID;
                    dragOffsetY = fy - thumb.y;
                    if (SDL_PointInRect(&fpt, &thumbHit)) {
                        int newThumbY = std::clamp(fy - dragOffsetY, track.y, track.y + track.h - thumbH);
                        float t = (float)(newThumbY - track.y) / (float)std::max(1, track.h - thumbH);
                        scroll = (int)std::lround(t * maxScroll);
                    } else {
                        int newThumbY = std::clamp(fy - thumbH / 2, track.y, track.y + track.h - thumbH);
                        float t = (float)(newThumbY - track.y) / (float)std::max(1, track.h - thumbH);
                        scroll = (int)std::lround(t * maxScroll);
                    }
                    continue;
                }
                activeFinger = e.tfinger.fingerID;
                lastFingerY = e.tfinger.y * winH;
                fingerDownY = lastFingerY;
                fingerMoved = false;
            }
            if (e.type == SDL_FINGERMOTION && draggingScrollbarTouch && e.tfinger.fingerID == scrollbarFinger) {
                const int fy = (int)std::lround(e.tfinger.y * winH);
                int newThumbY = std::clamp(fy - dragOffsetY, track.y, track.y + track.h - thumbH);
                float t = (float)(newThumbY - track.y) / (float)std::max(1, track.h - thumbH);
                scroll = (int)std::lround(t * maxScroll);
                continue;
            }
            if (e.type == SDL_FINGERMOTION && e.tfinger.fingerID == activeFinger) {
                float y = e.tfinger.y * winH;
                float dy = y - lastFingerY;
                lastFingerY = y;
                if (std::fabs(y - fingerDownY) > 6.0f) fingerMoved = true;
                scroll -= (int)std::lround(dy);
                if (scroll < 0) scroll = 0;
                if (scroll > maxScroll) scroll = maxScroll;
            }
            if (e.type == SDL_FINGERUP && draggingScrollbarTouch && e.tfinger.fingerID == scrollbarFinger) {
                draggingScrollbarTouch = false;
                scrollbarFinger = 0;
                continue;
            }
            if (e.type == SDL_FINGERUP && e.tfinger.fingerID == activeFinger) {
                const int tapX = (int)std::lround(e.tfinger.x * winW);
                const int tapY = (int)std::lround(e.tfinger.y * winH);
                if (!fingerMoved) {
                    SDL_Point pt{tapX, tapY};
                    if (currentTabAllowsDownload && SDL_PointInRect(&pt, &downloadBtn)) {
                        tryDownloadSelected();
                        activeFinger = 0;
                        fingerMoved = false;
                        continue;
                    }
                    int localY = tapY - listTop + scroll;
                    if (tapY >= listTop && tapY < listBottom && localY >= 0) {
                        int idx = localY / rowH;
                        if (idx >= 0 && idx < (int)levels.size()) {
                            if (selectedIndex == idx) {
                                if (activeTab == localTabIndex && levels[idx].path != "__local_editor__") {
                                    localPageOpen = true;
                                    localPageIndex = idx;
                                } else {
                                    chosenPath = resolveSelectedPath(levels[idx]);
                                    if (!chosenPath.empty()) {
                                        chosen = true;
                                        running = false;
                                    }
                                }
                            } else {
                                selectedIndex = idx;
                            }
                        }
                    }
                }
                activeFinger = 0;
                fingerMoved = false;
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED && draggingScrollbarTouch && e.tfinger.fingerID == scrollbarFinger) {
                draggingScrollbarTouch = false;
                scrollbarFinger = 0;
                continue;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point pt{(int)e.button.x, (int)e.button.y};
                if (localPageOpen && activeTab == localTabIndex) {
                    SDL_Rect panel{winW / 2 - 230, winH / 2 - 120, 460, 240};
                    SDL_Rect playBtn{panel.x + 20, panel.y + 140, 96, 40};
                    SDL_Rect delBtn{panel.x + 130, panel.y + 140, 96, 40};
                    SDL_Rect editBtn{panel.x + 240, panel.y + 140, 96, 40};
                    SDL_Rect uploadBtn{panel.x + 350, panel.y + 140, 96, 40};
                    SDL_Rect backBtn{panel.x + 180, panel.y + 188, 100, 36};
                    if (SDL_PointInRect(&pt, &playBtn)) {
                        chosenPath = resolveLocalPageSelection();
                        if (!chosenPath.empty()) {
                            chosen = true;
                            running = false;
                        }
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &delBtn)) {
                        if (localPageIndex >= 0 && localPageIndex < (int)localLevels.size()) {
                            std::filesystem::remove(localLevels[localPageIndex].path);
                            localLevels.erase(localLevels.begin() + localPageIndex);
                            localPageOpen = false;
                            localPageIndex = -1;
                            selected[activeTab] = std::clamp(selected[activeTab], 0, std::max(0, (int)localLevels.size() - 1));
                        }
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &editBtn)) {
                        std::string loadPath = (localPageIndex >= 0 && localPageIndex < (int)localLevels.size())
                            ? localLevels[localPageIndex].path : std::string();
                        std::string p = RunLocalLevelEditor(win, ren, loadPath);
                        if (!p.empty()) {
                            chosenPath = p;
                            chosen = true;
                            running = false;
                        }
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &uploadBtn)) {
                        tryUploadLocalAt(localPageIndex);
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &backBtn)) {
                        localPageOpen = false;
                        localPageIndex = -1;
                        continue;
                    }
                }
                if (showTabs) {
                    bool tabHandled = false;
                    for (int i = 0; i < (int)tabRects.size(); ++i) {
                        if (SDL_PointInRect(&pt, &tabRects[i])) {
                            switchTab(i);
                            tabHandled = true;
                            break;
                        }
                    }
                    if (tabHandled) continue;
                }
                if (currentTabAllowsDownload && SDL_PointInRect(&pt, &downloadBtn)) {
                    tryDownloadSelected();
                    continue;
                }
                if (maxScroll > 0 && SDL_PointInRect(&pt, &thumbHit)) {
                    draggingScrollbar = true;
                    dragOffsetY = (int)e.button.y - thumb.y;
                    continue;
                }
                if (maxScroll > 0 && SDL_PointInRect(&pt, &trackHit)) {
                    int newThumbY = std::clamp((int)e.button.y - thumbH / 2, track.y, track.y + track.h - thumbH);
                    float t = (float)(newThumbY - track.y) / (float)std::max(1, track.h - thumbH);
                    scroll = (int)std::lround(t * maxScroll);
                    continue;
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                draggingScrollbar = false;
            }
            if (e.type == SDL_MOUSEMOTION && draggingScrollbar) {
                int newThumbY = std::clamp((int)e.motion.y - dragOffsetY, track.y, track.y + track.h - thumbH);
                float t = (float)(newThumbY - track.y) / (float)std::max(1, track.h - thumbH);
                scroll = (int)std::lround(t * maxScroll);
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int y = (int)e.button.y - listTop + scroll;
                if ((int)e.button.y >= listTop && (int)e.button.y < listBottom && y >= 0) {
                    int idx = y / rowH;
                    if (idx >= 0 && idx < (int)levels.size()) {
                        selectedIndex = idx;
                        if (e.button.clicks >= 2) {
                            if (activeTab == localTabIndex && levels[idx].path != "__local_editor__") {
                                localPageOpen = true;
                                localPageIndex = idx;
                            } else {
                                chosenPath = resolveSelectedPath(levels[idx]);
                                if (!chosenPath.empty()) {
                                    chosen = true;
                                    running = false;
                                }
                            }
                        }
                    }
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.key == SDLK_F11) {
#if !defined(__ANDROID__)
                    const Uint32 flags = SDL_GetWindowFlags(win);
                    const bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(win, !isFullscreen);
#endif
                    continue;
                }
                if (localPageOpen && activeTab == localTabIndex) {
                    if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) {
                        localPageOpen = false;
                        localPageIndex = -1;
                        continue;
                    }
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_p || e.key.key == SDLK_P) {
                        chosenPath = resolveLocalPageSelection();
                        if (!chosenPath.empty()) {
                            chosen = true;
                            running = false;
                        }
                        continue;
                    }
                    if (e.key.key == SDLK_DELETE || e.key.key == SDLK_x || e.key.key == SDLK_X) {
                        if (localPageIndex >= 0 && localPageIndex < (int)localLevels.size()) {
                            std::filesystem::remove(localLevels[localPageIndex].path);
                            localLevels.erase(localLevels.begin() + localPageIndex);
                            localPageOpen = false;
                            localPageIndex = -1;
                            selected[activeTab] = std::clamp(selected[activeTab], 0, std::max(0, (int)localLevels.size() - 1));
                        }
                        continue;
                    }
                    if (e.key.key == SDLK_e || e.key.key == SDLK_E) {
                        std::string loadPath = (localPageIndex >= 0 && localPageIndex < (int)localLevels.size())
                            ? localLevels[localPageIndex].path : std::string();
                        std::string p = RunLocalLevelEditor(win, ren, loadPath);
                        if (!p.empty()) {
                            chosenPath = p;
                            chosen = true;
                            running = false;
                        }
                        continue;
                    }
                    if (e.key.key == SDLK_u || e.key.key == SDLK_U) {
                        tryUploadLocalAt(localPageIndex);
                        continue;
                    }
                }
                if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) {
                    running = false;
                    break;
                }
                if (showTabs && (e.key.key == SDLK_TAB || e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT)) {
                    int dir = (e.key.key == SDLK_LEFT) ? -1 : 1;
                    switchTab((activeTab + dir + (int)tabs.size()) % (int)tabs.size());
                    continue;
                }
                if (e.key.key == SDLK_DOWN) {
                    selectedIndex = std::min(selectedIndex + 1, (int)levels.size() - 1);
                    int rowTop = selectedIndex * rowH;
                    int rowBottom = rowTop + rowH;
                    if (rowBottom - scroll > viewportH) scroll = rowBottom - viewportH;
                }
                if (e.key.key == SDLK_UP) {
                    selectedIndex = std::max(selectedIndex - 1, 0);
                    int rowTop = selectedIndex * rowH;
                    if (rowTop < scroll) scroll = rowTop;
                }
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    if (!levels.empty()) {
                        if (activeTab == localTabIndex && levels[selectedIndex].path != "__local_editor__") {
                            localPageOpen = true;
                            localPageIndex = selectedIndex;
                        } else {
                            chosenPath = resolveSelectedPath(levels[selectedIndex]);
                            if (!chosenPath.empty()) {
                                chosen = true;
                                running = false;
                            }
                        }
                    }
                }
                if (currentTabAllowsDownload && (e.key.key == SDLK_d || e.key.key == SDLK_D)) {
                    tryDownloadSelected();
                }
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const bool isBackBtn =
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK ||
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
                const bool isAcceptBtn =
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_START;
                if (localPageOpen && activeTab == localTabIndex) {
                    if (isBackBtn) {
                        localPageOpen = false;
                        localPageIndex = -1;
                        continue;
                    }
                    if (isAcceptBtn) {
                        chosenPath = resolveLocalPageSelection();
                        if (!chosenPath.empty()) {
                            chosen = true;
                            running = false;
                        }
                        continue;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                        if (localPageIndex >= 0 && localPageIndex < (int)localLevels.size()) {
                            std::filesystem::remove(localLevels[localPageIndex].path);
                            localLevels.erase(localLevels.begin() + localPageIndex);
                            localPageOpen = false;
                            localPageIndex = -1;
                            selected[activeTab] = std::clamp(selected[activeTab], 0, std::max(0, (int)localLevels.size() - 1));
                        }
                        continue;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                        std::string loadPath = (localPageIndex >= 0 && localPageIndex < (int)localLevels.size())
                            ? localLevels[localPageIndex].path : std::string();
                        std::string p = RunLocalLevelEditor(win, ren, loadPath);
                        if (!p.empty()) {
                            chosenPath = p;
                            chosen = true;
                            running = false;
                        }
                        continue;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                        tryUploadLocalAt(localPageIndex);
                        continue;
                    }
                }
                if (isBackBtn) {
                    running = false;
                    break;
                }
                if (showTabs && (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                    int dir = (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -1 : 1;
                    switchTab((activeTab + dir + (int)tabs.size()) % (int)tabs.size());
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    selectedIndex = std::min(selectedIndex + 1, (int)levels.size() - 1);
                    int rowTop = selectedIndex * rowH;
                    int rowBottom = rowTop + rowH;
                    if (rowBottom - scroll > viewportH) scroll = rowBottom - viewportH;
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    selectedIndex = std::max(selectedIndex - 1, 0);
                    int rowTop = selectedIndex * rowH;
                    if (rowTop < scroll) scroll = rowTop;
                    continue;
                }
                if (isAcceptBtn) {
                    if (!levels.empty()) {
                        if (activeTab == localTabIndex && levels[selectedIndex].path != "__local_editor__") {
                            localPageOpen = true;
                            localPageIndex = selectedIndex;
                        } else {
                            chosenPath = resolveSelectedPath(levels[selectedIndex]);
                            if (!chosenPath.empty()) {
                                chosen = true;
                                running = false;
                            }
                        }
                    }
                    continue;
                }
                if (currentTabAllowsDownload && e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                    tryDownloadSelected();
                    continue;
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 23, 29, 40, 255);
        SDL_RenderClear(ren);

        SDL_SetRenderDrawColor(ren, 34, 43, 56, 230);
        SDL_RenderFillRect(ren, &shell);
        SDL_SetRenderDrawColor(ren, 160, 182, 212, 255);
        SDL_RenderDrawRect(ren, &shell);

        SDL_SetRenderDrawColor(ren, 28, 36, 48, 220);
        SDL_RenderFillRect(ren, &sidePanel);
        SDL_SetRenderDrawColor(ren, 160, 182, 212, 255);
        SDL_RenderDrawRect(ren, &sidePanel);

        SDL_SetRenderDrawColor(ren, 28, 36, 48, 220);
        SDL_RenderFillRect(ren, &contentPanel);
        SDL_SetRenderDrawColor(ren, 160, 182, 212, 255);
        SDL_RenderDrawRect(ren, &contentPanel);

        if (infoW > 0) {
            SDL_SetRenderDrawColor(ren, 28, 36, 48, 220);
            SDL_RenderFillRect(ren, &infoPanel);
            SDL_SetRenderDrawColor(ren, 160, 182, 212, 255);
            SDL_RenderDrawRect(ren, &infoPanel);
        }

        DrawText(ren, sidePanel.x + 10, sidePanel.y + 10, std::clamp(2 + (int)std::lround(uiScale), 2, 4), "LEVEL SELECT");
        for (int i = 0; i < (int)tabRects.size(); ++i) {
            drawChromeButton(ren, tabRects[i], activeTab == i);
            DrawText(ren, tabRects[i].x + 12, tabRects[i].y + std::max(8, (tabRects[i].h - 10 * textScale) / 2), textScale, tabs[i].first);
        }

        if (infoW > 0) {
            const std::string activeName = tabs[activeTab].first;
            const int infoScale = std::max(2, textScale);
            DrawText(ren, infoPanel.x + 12, infoPanel.y + 12, infoScale, "QUICK INFO");
            DrawText(ren, infoPanel.x + 12, infoPanel.y + 42, textScale, std::string("TAB: ") + activeName);
            DrawText(ren, infoPanel.x + 12, infoPanel.y + 66, textScale, std::string("ROWS: ") + std::to_string((int)levels.size()));
            DrawText(ren, infoPanel.x + 12, infoPanel.y + 90, textScale, currentTabAllowsDownload ? "D: DOWNLOAD" : "D: NO ACTION");
            DrawText(ren, infoPanel.x + 12, infoPanel.y + 114, textScale, localTabIndex >= 0 && activeTab == localTabIndex ? "ENTER/P: OPEN LOCAL CARD" : "ENTER/P: PLAY");
            DrawText(ren, infoPanel.x + 12, infoPanel.y + 138, textScale, "ESC/BACK: EXIT");
            if (localTabIndex >= 0 && activeTab == localTabIndex) {
                DrawText(ren, infoPanel.x + 12, infoPanel.y + 162, textScale, "E/NORTH: EDIT");
                DrawText(ren, infoPanel.x + 12, infoPanel.y + 186, textScale, "U/SHOULDER: UPLOAD");
            }
        }

        {
            const int titleScale = std::clamp((int)std::lround(2.0f + 0.35f * uiScale), 2, 4);
            const std::string title = (activeTab >= 0 && activeTab < (int)tabs.size()) ? tabs[activeTab].first : std::string("LEVELS");
            DrawText(ren, contentPanel.x + 12, contentPanel.y + 10, titleScale, title);
        }

        if (currentTabAllowsDownload) {
            drawChromeButton(ren, downloadBtn, true);
            DrawText(ren, downloadBtn.x + 12, downloadBtn.y + 8, textScale, menuLabels.downloadButton);
        }

        SDL_Rect listClip{
            contentPanel.x + 12,
            listTop + 2,
            std::max(1, contentPanel.w - 24 - barW),
            std::max(1, viewportH - 8)
        };
        SDL_SetRenderClipRect(ren, &listClip);
        if (levels.empty()) {
            DrawText(ren, listClip.x, listTop + 10, textScale, menuLabels.emptyTitle);
            if (activeTab == localTabIndex) {
                DrawText(ren, listClip.x, listTop + 10 + rowH, textScale, menuLabels.emptyLocalHint);
            } else {
                DrawText(ren, listClip.x, listTop + 10 + rowH, textScale,
#if defined(__ANDROID__)
                     menuLabels.emptyCustomHintAndroid);
#else
                     menuLabels.emptyCustomHintDesktop);
#endif
            }
        } else {
            for (int i = 0; i < (int)levels.size(); ++i) {
                int y = listTop + i * rowH - scroll;
                if (y + rowH < 0 || y > winH) continue;
                SDL_Rect r{listClip.x, y, listClip.w, rowH - 4};
                drawChromeButton(ren, r, i == selectedIndex);
                DrawText(ren, r.x + 12, r.y + std::max(6, (r.h - 10 * textScale) / 2), textScale, levels[i].label);
            }

            if (maxScroll > 0) {
                SDL_SetRenderDrawColor(ren, 36, 45, 58, 255);
                SDL_RenderFillRect(ren, &track);
                SDL_SetRenderDrawColor(ren, draggingScrollbar ? 220 : 180, draggingScrollbar ? 220 : 180, draggingScrollbar ? 220 : 180, 255);
                SDL_RenderFillRect(ren, &thumb);
            }
        }
        SDL_SetRenderClipRect(ren, nullptr);

        if (!statusText.empty() && SDL_GetTicks() < statusUntilTicks) {
            DrawText(ren, contentPanel.x + 12, shell.y + shell.h - shellPad - std::max(16, (int)std::lround(18.0f * uiScale)), textScale, statusText);
        }

        if (localPageOpen && activeTab == localTabIndex) {
            SDL_Rect panel{winW / 2 - 250, winH / 2 - 140, 500, 280};
            SDL_SetRenderDrawColor(ren, 28, 36, 48, 245);
            SDL_RenderFillRect(ren, &panel);
            SDL_SetRenderDrawColor(ren, 180, 200, 230, 255);
            SDL_RenderDrawRect(ren, &panel);
            std::string name = "N/A";
            if (localPageIndex >= 0 && localPageIndex < (int)localLevels.size()) name = localLevels[localPageIndex].label;
            DrawText(ren, panel.x + 20, panel.y + 18, textScale, menuLabels.localPanelTitle);
            DrawText(ren, panel.x + 20, panel.y + 48, textScale, name);
            DrawText(ren, panel.x + 20, panel.y + 80, textScale, menuLabels.localPanelActions);
            DrawText(ren, panel.x + 20, panel.y + 104, textScale, menuLabels.localPanelBackHint);
            SDL_Rect playBtn{panel.x + 20, panel.y + 140, 96, 40};
            SDL_Rect delBtn{panel.x + 130, panel.y + 140, 96, 40};
            SDL_Rect editBtn{panel.x + 240, panel.y + 140, 96, 40};
            SDL_Rect uploadBtn{panel.x + 350, panel.y + 140, 96, 40};
            SDL_Rect backBtn{panel.x + 180, panel.y + 188, 100, 36};
            drawChromeButton(ren, playBtn, true);
            drawChromeButton(ren, delBtn, false);
            drawChromeButton(ren, editBtn, false);
            drawChromeButton(ren, uploadBtn, false);
            drawChromeButton(ren, backBtn, false);
            DrawText(ren, playBtn.x + 24, playBtn.y + 11, textScale, menuLabels.buttonPlay);
            DrawText(ren, delBtn.x + 14, delBtn.y + 11, textScale, menuLabels.buttonDelete);
            DrawText(ren, editBtn.x + 24, editBtn.y + 11, textScale, menuLabels.buttonEdit);
            DrawText(ren, uploadBtn.x + 8, uploadBtn.y + 11, textScale, menuLabels.buttonUpload);
            DrawText(ren, backBtn.x + 30, backBtn.y + 8, textScale, menuLabels.buttonBack);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    if (!chosen) return "";
    return chosenPath;
}

std::string RunLevelSelect(SDL_Window* win, SDL_Renderer* ren) {
    return RunLevelSelectImpl(win, ren, true, true);
}

std::string buildFirebaseLevelUploadUrl(const std::string& base,
                                        const std::string& authToken,
                                        const std::string& levelId) {
    std::string u = base;
    while (!u.empty() && u.back() == '/') u.pop_back();
    u += "/levels/" + sanitizeFilePart(levelId) + ".json";
    if (!authToken.empty()) {
        u += "?auth=" + authToken;
    }
    return u;
}

bool resolveAccountUsernameFromToken(const std::string& authToken, std::string& accountUsernameOut) {
    accountUsernameOut.clear();
    if (authToken.empty()) return false;
    std::string apiKey;
    const std::string cfgText = ReadTextFile("assets/config.json");
    if (!cfgText.empty()) {
        try {
            const nlohmann::json cfgJson = nlohmann::json::parse(cfgText);
            if (cfgJson.is_object()) {
                if (cfgJson.contains("firebase_api_key") && cfgJson["firebase_api_key"].is_string()) {
                    apiKey = cfgJson["firebase_api_key"].get<std::string>();
                } else if (cfgJson.contains("level_api_key") && cfgJson["level_api_key"].is_string()) {
                    apiKey = cfgJson["level_api_key"].get<std::string>();
                }
            }
        } catch (...) {}
    }
    if (apiKey.empty()) return false;

#if defined(HAVE_CURL) && HAVE_CURL

    CURL* curl = curl_easy_init();
    if (curl) {
        const std::string url = "https://identitytoolkit.googleapis.com/v1/accounts:lookup?key=" + apiKey;
        nlohmann::json req;
        req["idToken"] = authToken;
        const std::string body = req.dump();
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string respBody;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "DF-New/1.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
            +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                std::string* out = static_cast<std::string*>(userdata);
                out->append(ptr, size * nmemb);
                return size * nmemb;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respBody);
        const CURLcode rc = curl_easy_perform(curl);
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
                    cls, "firebaseLookupAccount",
                    "(Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
                if (mid) {
                    jstring jApi = env->NewStringUTF(apiKey.c_str());
                    jstring jToken = env->NewStringUTF(authToken.c_str());
                    jobject jRespObj = env->CallStaticObjectMethod(cls, mid, jApi, jToken, (jint)10000);
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

bool uploadLocalLevelToServer(const LevelEntry& level, std::string& statusText) {
    if (level.path.empty() || level.path == "__local_editor__") {
        statusText = "No local level selected.";
        return false;
    }
    if (isHttpUrl(level.path)) {
        statusText = "Only local levels can be uploaded.";
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
    const std::string uploadUrl = buildFirebaseLevelUploadUrl(levelServerUrl, authToken, levelId);
    nlohmann::json payload;
    payload["name"] = level.label;
    payload["level_id"] = levelId;
    payload["owner"] = accountUsername;
    payload["source"] = "local";
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
            cls, "firebaseUploadLevel",
            "(Ljava/lang/String;Ljava/lang/String;I)I");
        if (!mid) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(cls);
            return false;
        }
        jstring jUrl = env->NewStringUTF(uploadUrl.c_str());
        jstring jBody = env->NewStringUTF(body.c_str());
        jint code = env->CallStaticIntMethod(cls, mid, jUrl, jBody, (jint)15000);
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
    curl_easy_setopt(curl, CURLOPT_URL, uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DF-New/1.0");
    const CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
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

std::string RunCampaignLevelSelect(SDL_Window* win, SDL_Renderer* ren) {
    return RunLevelSelectImpl(win, ren, true, false);
}

std::string RunCustomLevelSelect(SDL_Window* win, SDL_Renderer* ren) {
    return RunLevelSelectImpl(win, ren, false, true);
}

bool HasCustomLevels() {
    return !loadCustomLevels().empty();
}

