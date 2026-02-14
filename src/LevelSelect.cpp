#include "LevelSelect.h"
#include "AssetPath.h"

#include <sdl3/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

#include "TextRenderer.h"
#include "GameSupport.h"
#include "LevelLoader.h"
#include "InputSystem.h"

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
    std::string emptyTitle = "No levels found in this tab.";
    std::string emptyLocalHint = "Use CREATE LOCAL LEVEL (EDITOR).";
    std::string emptyCustomHintAndroid = "Add custom levels to assets/custom_levels/levels.json.";
    std::string emptyCustomHintDesktop = "Use custom_levels/ or assets/custom_levels/.";
    std::string localPanelTitle = "LOCAL LEVEL";
    std::string localPanelActions = "ENTER/P: PLAY  DEL/X: DELETE  E: EDIT";
    std::string localPanelBackHint = "ESC: BACK";
    std::string buttonPlay = "PLAY";
    std::string buttonDelete = "DELETE";
    std::string buttonEdit = "EDIT";
    std::string buttonBack = "BACK";
};

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

std::string RunLocalLevelEditor(SDL_Window* win, SDL_Renderer* ren, const std::string& initialPath = "") {
    constexpr int kEditorTileSize = 32;
    int gridW = 30;
    int gridH = 17;
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

    const std::vector<unsigned short> palette{2, 12, 13, 24, 29, 30, 28};
    const std::vector<int> objectPalette{31, 46, 67};
    int selectedPalette = 1;
    int selectedObjectPalette = 0;
    bool objectMode = false;
    bool running = true;
    bool paused = false;
    int pauseSel = 0; // 0 Resume, 1 Save, 2 Exit
    std::string savedPath;
    const std::string saveTargetPath = initialPath;
    std::string statusText;
    Uint64 statusUntil = 0;
    SDL_FingerID activeEditorFinger = 0;
    bool fingerPainting = false;
    InputSystem editorInput;
    editorInput.scanConnected();

    SDL_Texture* blocksTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/DF_Blocks-uhd.png").c_str());
    if (blocksTex) SDL_SetTextureScaleMode(blocksTex, SDL_SCALEMODE_NEAREST);
    auto blocksFrameList = loadPlistFrameList("assets/Sheets/DF_Blocks-uhd.plist");
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
    auto paintAt = [&](int cx, int cy, bool erase) {
        if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return;
        grid[cy * gridW + cx] = erase ? 2 : palette[selectedPalette];
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
        int cell = std::min((winW - sideW - margin * 3) / std::max(1, gridW), (winH - margin * 2) / std::max(1, gridH));
        cell = std::max(12, cell);
        const int gridPxW = gridW * cell;
        const int gridPxH = gridH * cell;
        const int gridX = margin;
        const int gridY = std::max(margin, (winH - gridPxH) / 2);
        const int panelX = gridX + gridPxW + margin;
        SDL_Rect tileModeBtn{panelX, margin, 76, 30};
        SDL_Rect objectModeBtn{panelX + 84, margin, 76, 30};
        const int paletteStartY = margin + 40;
        SDL_Rect saveBtn{panelX, winH - 140, 160, 36};
        SDL_Rect cancelBtn{panelX, winH - 96, 160, 36};
        SDL_Rect pausePanel{winW / 2 - 180, winH / 2 - 120, 360, 240};
        SDL_Rect pauseResumeBtn{pausePanel.x + 24, pausePanel.y + 150, 96, 40};
        SDL_Rect pauseSaveBtn{pausePanel.x + 132, pausePanel.y + 150, 96, 40};
        SDL_Rect pauseExitBtn{pausePanel.x + 240, pausePanel.y + 150, 96, 40};

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            editorInput.handleEvent(e);
            if (e.type == SDL_QUIT) { running = false; break; }
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
                const int paletteCount = objectMode ? (int)objectPalette.size() : (int)palette.size();
                for (int i = 0; i < paletteCount; ++i) {
                    SDL_Rect r{panelX, paletteStartY + i * 44, 160, 36};
                    if (SDL_PointInRect(&pt, &r)) {
                        if (objectMode) selectedObjectPalette = i;
                        else selectedPalette = i;
                        fingerPainting = false;
                        break;
                    }
                }
                if (tx >= gridX && ty >= gridY && tx < gridX + gridPxW && ty < gridY + gridPxH) {
                    const int cx = (tx - gridX) / cell;
                    const int cy = (ty - gridY) / cell;
                    applyAt(cx, cy, false);
                    fingerPainting = true;
                } else {
                    fingerPainting = false;
                }
                continue;
            }
            if (e.type == SDL_FINGERMOTION && e.tfinger.fingerID == activeEditorFinger) {
                if (paused || !fingerPainting) continue;
                const int tx = (int)std::lround(e.tfinger.x * winW);
                const int ty = (int)std::lround(e.tfinger.y * winH);
                if (tx >= gridX && ty >= gridY && tx < gridX + gridPxW && ty < gridY + gridPxH) {
                    const int cx = (tx - gridX) / cell;
                    const int cy = (ty - gridY) / cell;
                    applyAt(cx, cy, false);
                }
                continue;
            }
            if (e.type == SDL_FINGERUP && e.tfinger.fingerID == activeEditorFinger) {
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
                bool dragPaint = (e.type == SDL_MOUSEMOTION) && ((e.motion.state & SDL_BUTTON_LMASK) || (e.motion.state & SDL_BUTTON_RMASK));
                bool clickPaint = (e.type == SDL_MOUSEBUTTONDOWN) && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT);
                if (!dragPaint && !clickPaint) continue;
                int mx = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
                int my = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
                bool erase = false;
                if (e.type == SDL_MOUSEBUTTONDOWN) erase = (e.button.button == SDL_BUTTON_RIGHT);
                if (e.type == SDL_MOUSEMOTION) erase = (e.motion.state & SDL_BUTTON_RMASK) != 0;
                if (mx >= gridX && my >= gridY && mx < gridX + gridPxW && my < gridY + gridPxH) {
                    int cx = (mx - gridX) / cell;
                    int cy = (my - gridY) / cell;
                    applyAt(cx, cy, erase);
                }
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
                for (int i = 0; i < paletteCount; ++i) {
                    SDL_Rect r{panelX, paletteStartY + i * 44, 160, 36};
                    if (mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h) {
                        if (objectMode) selectedObjectPalette = i;
                        else selectedPalette = i;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 18, 20, 26, 255);
        SDL_RenderClear(ren);

        for (int y = 0; y < gridH; ++y) {
            for (int x = 0; x < gridW; ++x) {
                unsigned short id = grid[y * gridW + x];
                SDL_Rect rc{gridX + x * cell, gridY + y * cell, cell, cell};
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
            if (id == 67) { r = 120; g = 180; b = 255; return; }
            r = 220; g = 220; b = 240;
        };
        for (const auto& obj : placedObjects) {
            int id = 0;
            try { id = std::stoi(obj.id); } catch (...) { id = 0; }
            if (id <= 0) continue;
            const int cx = (int)std::floor(obj.x / (float)kEditorTileSize);
            const int cy = (int)std::floor(obj.y / (float)kEditorTileSize);
            if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) continue;
            SDL_Rect rc{gridX + cx * cell, gridY + cy * cell, cell, cell};
            Uint8 r = 220, g = 220, b = 240;
            objectColor(id, r, g, b);
            SDL_SetRenderDrawColor(ren, r, g, b, 200);
            SDL_Rect in{rc.x + std::max(2, cell / 6), rc.y + std::max(2, cell / 6), std::max(4, cell - std::max(4, cell / 3)), std::max(4, cell - std::max(4, cell / 3))};
            SDL_RenderFillRect(ren, &in);
            DrawText(ren, rc.x + 3, rc.y + 3, std::max(1, cell / 24), std::to_string(id));
        }
        // Draw grid lines after tiles so visual cells align exactly with paint hitboxes.
        SDL_SetRenderDrawColor(ren, 34, 40, 56, 255);
        for (int x = 0; x <= gridW; ++x) {
            int gx = gridX + x * cell;
            SDL_RenderLine(ren, (float)gx, (float)gridY, (float)gx, (float)(gridY + gridPxH));
        }
        for (int y = 0; y <= gridH; ++y) {
            int gy = gridY + y * cell;
            SDL_RenderLine(ren, (float)gridX, (float)gy, (float)(gridX + gridPxW), (float)gy);
        }
        SDL_SetRenderDrawColor(ren, 90, 110, 140, 255);
        SDL_Rect border{gridX - 1, gridY - 1, gridPxW + 2, gridPxH + 2};
        SDL_RenderDrawRect(ren, &border);

        DrawText(ren, panelX, winH - 96, 2, "S/ENTER: SAVE+PLAY");
        DrawText(ren, panelX, winH - 72, 2, "C: CLEAR");
        DrawText(ren, panelX, winH - 48, 2, "ESC: PAUSE  O: MODE");
        DrawText(ren, panelX, winH - 24, 2, objectMode ? "OBJ MODE: tap obj to remove / RMB erase" : "TILE MODE: LMB paint / RMB erase");
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
        for (int i = 0; i < paletteCount; ++i) {
            const bool selected = objectMode ? (i == selectedObjectPalette) : (i == selectedPalette);
            SDL_Rect r{panelX, paletteStartY + i * 44, 160, 36};
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
                DrawText(ren, r.x + 38, r.y + 8, 2, std::string("Tile ") + std::to_string((int)pid));
            } else {
                const int oid = objectPalette[i];
                Uint8 tr = 220, tg = 220, tb = 240;
                objectColor(oid, tr, tg, tb);
                SDL_SetRenderDrawColor(ren, tr, tg, tb, 255);
                SDL_Rect sw{r.x + 6, r.y + 6, 24, 24};
                SDL_RenderFillRect(ren, &sw);
                DrawText(ren, r.x + 38, r.y + 8, 2, std::string("Obj ") + std::to_string(oid));
            }
        }

        DrawText(ren, panelX, paletteStartY + paletteCount * 44 + 14, 2, "LEVEL EDITOR");
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
    if (!fs::exists(dir)) return out;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        const auto ext = p.extension().string();
        if (!ext.empty() && ext != ".txt" && ext != ".bnnlvl" && ext != ".bin") continue;
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
    bool chosen = false;
    std::string chosenPath;
    std::string statusText;
    Uint64 statusUntilTicks = 0;
    bool draggingScrollbar = false;
    int dragOffsetY = 0;
    SDL_FingerID activeFinger = 0;
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
        int tabY = pad;
        int listTop = tabY + tabH + std::max(8, (int)std::lround(10.0f * uiScale));
        SDL_Rect downloadBtn{winW - pad - 170, tabY, 170, tabH};
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
        int viewportH = std::max(1, winH - listTop - pad);
        int maxScroll = std::max(0, contentH - viewportH);
        scroll = std::clamp(scroll, 0, maxScroll);

        int barW = std::max(8, (int)std::lround(10.0f * uiScale));
        SDL_Rect track{winW - pad - barW, listTop, barW, viewportH};
        float visibleRatio = (contentH > 0) ? std::clamp((float)viewportH / (float)contentH, 0.0f, 1.0f) : 1.0f;
        int thumbH = std::max(std::max(16, (int)std::lround(28.0f * uiScale)), (int)std::lround(track.h * visibleRatio));
        int thumbTravel = std::max(1, track.h - thumbH);
        int thumbY = track.y + ((maxScroll > 0) ? (int)std::lround((float)scroll / (float)maxScroll * thumbTravel) : 0);
        SDL_Rect thumb{track.x, thumbY, barW, thumbH};
        const int tabW = 170;
        const int tabGap = 10;
        std::vector<SDL_Rect> tabRects;
        tabRects.reserve(tabs.size());
        for (int i = 0; i < (int)tabs.size(); ++i) {
            tabRects.push_back(SDL_Rect{pad + i * (tabW + tabGap), tabY, tabW, tabH});
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
        auto resolveSelectedPath = [&](const LevelEntry& sel) -> std::string {
            if (sel.path == "__local_editor__") return RunLocalLevelEditor(win, ren);
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
            if (e.type == SDL_MOUSEWHEEL) {
                scroll -= e.wheel.y * rowH;
                if (scroll < 0) scroll = 0;
                if (scroll > maxScroll) scroll = maxScroll;
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
                scroll -= (int)std::lround(dy);
                if (scroll < 0) scroll = 0;
                if (scroll > maxScroll) scroll = maxScroll;
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
                    if (localY >= 0) {
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
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point pt{(int)e.button.x, (int)e.button.y};
                if (localPageOpen && activeTab == localTabIndex) {
                    SDL_Rect panel{winW / 2 - 190, winH / 2 - 120, 380, 240};
                    SDL_Rect playBtn{panel.x + 24, panel.y + 140, 100, 40};
                    SDL_Rect delBtn{panel.x + 140, panel.y + 140, 100, 40};
                    SDL_Rect editBtn{panel.x + 256, panel.y + 140, 100, 40};
                    SDL_Rect backBtn{panel.x + 140, panel.y + 188, 100, 36};
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
                if (SDL_PointInRect(&pt, &thumb)) {
                    draggingScrollbar = true;
                    dragOffsetY = (int)e.button.y - thumb.y;
                    continue;
                }
                if (SDL_PointInRect(&pt, &track)) {
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
                if (y >= 0) {
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

        SDL_SetRenderDrawColor(ren, 18, 18, 22, 255);
        SDL_RenderClear(ren);

        if (showTabs) {
            for (int i = 0; i < (int)tabRects.size(); ++i) {
                SDL_SetRenderDrawColor(ren, activeTab == i ? 70 : 48, activeTab == i ? 100 : 58, activeTab == i ? 150 : 76, 255);
                SDL_RenderFillRect(ren, &tabRects[i]);
                SDL_SetRenderDrawColor(ren, 220, 220, 220, 255);
                SDL_RenderDrawRect(ren, &tabRects[i]);
                DrawText(ren, tabRects[i].x + 10, tabRects[i].y + 8, textScale, tabs[i].first);
            }
        } else if (includeCustom) {
            DrawText(ren, pad, tabY + 8, textScale, menuLabels.userLevelsHeader);
        }

        if (currentTabAllowsDownload) {
            SDL_SetRenderDrawColor(ren, 50, 90, 70, 220);
            SDL_RenderFillRect(ren, &downloadBtn);
            SDL_SetRenderDrawColor(ren, 190, 230, 210, 255);
            SDL_RenderDrawRect(ren, &downloadBtn);
            DrawText(ren, downloadBtn.x + 12, downloadBtn.y + 8, textScale, menuLabels.downloadButton);
        }

        if (levels.empty()) {
            DrawText(ren, pad, listTop + 8, textScale, menuLabels.emptyTitle);
            if (activeTab == localTabIndex) {
                DrawText(ren, pad, listTop + 8 + rowH, textScale, menuLabels.emptyLocalHint);
            } else {
                DrawText(ren, pad, listTop + 8 + rowH, textScale,
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
                if (i == selectedIndex) {
                    SDL_SetRenderDrawColor(ren, 60, 90, 140, 255);
                } else {
                    SDL_SetRenderDrawColor(ren, 40, 50, 70, 255);
                }
                SDL_Rect r{pad, y, winW - pad * 3 - barW, rowH - 4};
                SDL_RenderFillRect(ren, &r);

                SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
                DrawText(ren, r.x + 8, r.y + 6, textScale, levels[i].label);
            }

            if (maxScroll > 0) {
                SDL_SetRenderDrawColor(ren, 48, 58, 76, 255);
                SDL_RenderFillRect(ren, &track);
                SDL_SetRenderDrawColor(ren, draggingScrollbar ? 220 : 180, draggingScrollbar ? 220 : 180, draggingScrollbar ? 220 : 180, 255);
                SDL_RenderFillRect(ren, &thumb);
            }
        }

        if (!statusText.empty() && SDL_GetTicks() < statusUntilTicks) {
            DrawText(ren, pad, winH - pad - std::max(16, (int)std::lround(18.0f * uiScale)), textScale, statusText);
        }

        if (localPageOpen && activeTab == localTabIndex) {
            SDL_Rect panel{winW / 2 - 190, winH / 2 - 120, 380, 240};
            SDL_SetRenderDrawColor(ren, 22, 26, 34, 255);
            SDL_RenderFillRect(ren, &panel);
            SDL_SetRenderDrawColor(ren, 180, 200, 230, 255);
            SDL_RenderDrawRect(ren, &panel);
            std::string name = "N/A";
            if (localPageIndex >= 0 && localPageIndex < (int)localLevels.size()) name = localLevels[localPageIndex].label;
            DrawText(ren, panel.x + 20, panel.y + 18, textScale, menuLabels.localPanelTitle);
            DrawText(ren, panel.x + 20, panel.y + 48, textScale, name);
            DrawText(ren, panel.x + 20, panel.y + 80, textScale, menuLabels.localPanelActions);
            DrawText(ren, panel.x + 20, panel.y + 104, textScale, menuLabels.localPanelBackHint);
            SDL_Rect playBtn{panel.x + 24, panel.y + 140, 100, 40};
            SDL_Rect delBtn{panel.x + 140, panel.y + 140, 100, 40};
            SDL_Rect editBtn{panel.x + 256, panel.y + 140, 100, 40};
            SDL_Rect backBtn{panel.x + 140, panel.y + 188, 100, 36};
            SDL_SetRenderDrawColor(ren, 55, 90, 60, 255);
            SDL_RenderFillRect(ren, &playBtn);
            SDL_SetRenderDrawColor(ren, 95, 55, 55, 255);
            SDL_RenderFillRect(ren, &delBtn);
            SDL_SetRenderDrawColor(ren, 60, 70, 110, 255);
            SDL_RenderFillRect(ren, &editBtn);
            SDL_SetRenderDrawColor(ren, 55, 65, 90, 255);
            SDL_RenderFillRect(ren, &backBtn);
            SDL_SetRenderDrawColor(ren, 220, 220, 230, 255);
            SDL_RenderDrawRect(ren, &playBtn);
            SDL_RenderDrawRect(ren, &delBtn);
            SDL_RenderDrawRect(ren, &editBtn);
            SDL_RenderDrawRect(ren, &backBtn);
            DrawText(ren, playBtn.x + 30, playBtn.y + 11, textScale, menuLabels.buttonPlay);
            DrawText(ren, delBtn.x + 18, delBtn.y + 11, textScale, menuLabels.buttonDelete);
            DrawText(ren, editBtn.x + 30, editBtn.y + 11, textScale, menuLabels.buttonEdit);
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

std::string RunCampaignLevelSelect(SDL_Window* win, SDL_Renderer* ren) {
    return RunLevelSelect(win, ren);
}

std::string RunCustomLevelSelect(SDL_Window* win, SDL_Renderer* ren) {
    return RunLevelSelect(win, ren);
}

bool HasCustomLevels() {
    return !loadCustomLevels().empty();
}
