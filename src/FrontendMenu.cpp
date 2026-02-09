#include "FrontendMenu.h"

#include <sdl3/SDL.h>
#include <sdl3/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "AssetPath.h"
#include "GameSupport.h"
#include "TextRenderer.h"

FrontendAction runFrontendMenu(FrontendMenuContext& ctx) {
    bool& running = *ctx.running;
    bool& fullscreen = *ctx.fullscreen;
    bool& vsyncEnabled = *ctx.vsyncEnabled;
    bool& clampCamX = *ctx.clampCamX;
    bool& defaultShowFpsCounter = *ctx.defaultShowFpsCounter;
    bool& defaultShowDetailedDebugger = *ctx.defaultShowDetailedDebugger;
    bool& defaultShowHitboxes = *ctx.defaultShowHitboxes;
    bool& defaultShowPlayerHitbox = *ctx.defaultShowPlayerHitbox;
    bool& defaultShowDebugView = *ctx.defaultShowDebugView;
    bool& defaultHideUnknownObjectTypes = *ctx.defaultHideUnknownObjectTypes;
    bool& menuMusicEnabled = *ctx.menuMusicEnabled;
    bool& muteAllAudio = *ctx.muteAllAudio;
    int& musicVolume = *ctx.musicVolume;
    int& sfxVolume = *ctx.sfxVolume;

    auto applyRenderVsync = [&]() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (SDL_RenderSetVSync(ctx.ren, vsyncEnabled ? 1 : 0) != 0) {
            SDL_Log("Could not set renderer VSync=%d: %s", vsyncEnabled ? 1 : 0, SDL_GetError());
        }
#endif
    };

    bool inSettings = false;
    bool inComingSoon = false;
    bool closeMenuOpen = false;
    int closeMenuSel = 0; // 0 Resume, 1 Close Game
    int menuSel = 1;     // 0 Settings, 1 Play, 2 Editor
    int settingsTab = 0; // 0 General, 1 Audio, 2 Debug, 3 Controls
    int settingsSelAudio = 0;
    int settingsSelDebug = 0;
    constexpr int kSettingsTabCount = 4;
    auto showAboutPopup = [&]() {
        const int sdlVer = SDL_GetVersion();
        const int sdlMajor = SDL_VERSIONNUM_MAJOR(sdlVer);
        const int sdlMinor = SDL_VERSIONNUM_MINOR(sdlVer);
        const int sdlPatch = SDL_VERSIONNUM_MICRO(sdlVer);
        const std::string aboutText =
            "Dorfplatformer Timetravel\n\n"
            "In-development build.\n"
            "SDL: " + std::to_string(sdlMajor) + "." + std::to_string(sdlMinor) + "." + std::to_string(sdlPatch) + "\n"
            "\n"
            "Build UUID:\n" + ctx.buildUuid;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "About", aboutText.c_str(), ctx.win);
    };
#if defined(__ANDROID__)
    constexpr int IDX_VSYNC = 0;
    constexpr int IDX_CAM_CLAMP = 1;
    constexpr int IDX_SHOW_FPS = 2;
    constexpr int IDX_SHOW_DETAILED = 3;
    constexpr int IDX_SHOW_HITBOXES = 4;
    constexpr int IDX_SHOW_PLAYER_HITBOX = 5;
    constexpr int IDX_SHOW_DEBUG_VIEW = 6;
    constexpr int IDX_MUSIC = 7;
    constexpr int IDX_SFX = 8;
    constexpr int IDX_ABOUT = 9;
    constexpr int IDX_BACK = 10;
    constexpr int kSettingsCount = 11;
#else
    constexpr int IDX_FULLSCREEN = 0;
    constexpr int IDX_VSYNC = 1;
    constexpr int IDX_CAM_CLAMP = 2;
    constexpr int IDX_SHOW_FPS = 3;
    constexpr int IDX_SHOW_DETAILED = 4;
    constexpr int IDX_SHOW_HITBOXES = 5;
    constexpr int IDX_SHOW_PLAYER_HITBOX = 6;
    constexpr int IDX_SHOW_DEBUG_VIEW = 7;
    constexpr int IDX_MUSIC = 8;
    constexpr int IDX_SFX = 9;
    constexpr int IDX_ABOUT = 10;
    constexpr int IDX_BACK = 11;
    constexpr int kSettingsCount = 12;
#endif
    int settingsSel = 0;
    enum class SliderDragTarget { None, Music, Sfx };
    SliderDragTarget sliderDrag = SliderDragTarget::None;
    SDL_FingerID sliderDragFinger = 0;
    SDL_Event e;
    const int settingsStartY = 150;
    const int settingsRowH = 30;
    const int settingsTabX = 16;
    const int settingsTabY = 118;
    const int settingsTabW = 120;
    const int settingsTabH = 30;
    auto settingsRowY = [&](int idx) -> int { return settingsStartY + idx * settingsRowH; };
    auto settingsRowBtn = [&](int idx) -> SDL_Rect {
        return SDL_Rect{ctx.baseScreenW / 2 - 140, settingsRowY(idx), 280, 28};
    };
    auto settingsTabBtn = [&](int idx) -> SDL_Rect {
        return SDL_Rect{settingsTabX, settingsTabY + idx * (settingsTabH + 6), settingsTabW, settingsTabH};
    };
    auto musicSliderRect = [&]() -> SDL_Rect {
        return SDL_Rect{ctx.baseScreenW / 2 - 70, settingsRowY(2) + 22, 200, 10};
    };
    auto sfxSliderRect = [&]() -> SDL_Rect {
        return SDL_Rect{ctx.baseScreenW / 2 - 70, settingsRowY(3) + 22, 200, 10};
    };
    auto sliderValueFromPoint = [&](int x, const SDL_Rect& slider) -> int {
        int rel = x - slider.x;
        if (rel < 0) rel = 0;
        if (rel > slider.w) rel = slider.w;
        return (int)std::lround((rel / (double)std::max(1, slider.w)) * 128.0);
    };
    auto mouseToGamePoint = [&](int mx, int my, SDL_Point& pt) -> bool {
        int winW = 0, winH = 0, gx = 0, gy = 0;
        SDL_GetWindowSize(ctx.win, &winW, &winH);
        if (!windowToGamePoint(mx, my, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy)) return false;
        pt.x = gx;
        pt.y = gy;
        return true;
    };
    auto mainMenuBtnRect = [&](int idx) -> SDL_Rect {
        const int btnW = 112;
        const int btnH = 112;
        const int gap = 44;
        const int totalW = btnW * 3 + gap * 2;
        const int startX = ctx.baseScreenW / 2 - totalW / 2;
        return SDL_Rect{startX + idx * (btnW + gap), 210, btnW, btnH};
    };
    SDL_Texture* mainMenuTex = IMG_LoadTexture(ctx.ren, ResolveAssetPath("assets/Sheets/DF_Main_menu-uhd.png").c_str());
    auto mainMenuFrames = loadPlistFrames("assets/Sheets/DF_Main_menu-uhd.plist");
    auto getMenuFrame = [&](const char* name) -> const Frame* {
        auto it = mainMenuFrames.find(name);
        if (it == mainMenuFrames.end()) return nullptr;
        return &it->second;
    };
    auto cleanupMenuAssets = [&]() {
        if (mainMenuTex) {
            SDL_DestroyTexture(mainMenuTex);
            mainMenuTex = nullptr;
        }
    };

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                cleanupMenuAssets();
                return FrontendAction::Quit;
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (closeMenuOpen) {
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_w) closeMenuSel = (closeMenuSel + 1) % 2;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) closeMenuSel = (closeMenuSel + 1) % 2;
                    if (e.key.key == SDLK_ESCAPE) closeMenuOpen = false;
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (closeMenuSel == 0) {
                            closeMenuOpen = false;
                        } else {
                            running = false;
                            cleanupMenuAssets();
                            return FrontendAction::Quit;
                        }
                    }
                    continue;
                }
                if (!inSettings) {
                    if (inComingSoon) {
                        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            inComingSoon = false;
                        }
                        continue;
                    }
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_a) menuSel = (menuSel + 2) % 3;
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_d) menuSel = (menuSel + 1) % 3;
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_w) menuSel = (menuSel + 2) % 3;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) menuSel = (menuSel + 1) % 3;
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (menuSel == 0) inSettings = true;
                        if (menuSel == 1) {
                            cleanupMenuAssets();
                            return FrontendAction::StartGame;
                        }
                        if (menuSel == 2) inComingSoon = true;
                    }
                    if (e.key.key == SDLK_ESCAPE) {
                        closeMenuOpen = true;
                        closeMenuSel = 0;
                    }
                } else {
                    if (e.key.key == SDLK_TAB || e.key.key == SDLK_q || e.key.key == SDLK_e) {
                        settingsTab = (settingsTab + 1) % kSettingsTabCount;
                        continue;
                    }
                    if (e.key.key == SDLK_1) { settingsTab = 0; continue; }
                    if (e.key.key == SDLK_2) { settingsTab = 1; continue; }
                    if (e.key.key == SDLK_3) { settingsTab = 2; continue; }
                    if (e.key.key == SDLK_4) { settingsTab = 3; continue; }
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_w) settingsSel = (settingsSel + kSettingsCount - 1) % kSettingsCount;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) settingsSel = (settingsSel + 1) % kSettingsCount;
                    if (e.key.key == SDLK_v) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    if (e.key.key == SDLK_c) clampCamX = !clampCamX;
                    if (e.key.key == SDLK_f) defaultShowFpsCounter = !defaultShowFpsCounter;
                    if (e.key.key == SDLK_g) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                    if (e.key.key == SDLK_h) defaultShowHitboxes = !defaultShowHitboxes;
                    if (e.key.key == SDLK_p) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                    if (e.key.key == SDLK_d) defaultShowDebugView = !defaultShowDebugView;
                    if (e.key.key == SDLK_ESCAPE) inSettings = false;

                    if (settingsTab == 1) {
                        constexpr int kAudioCount = 5;
                        if (e.key.key == SDLK_UP || e.key.key == SDLK_w) settingsSelAudio = (settingsSelAudio + kAudioCount - 1) % kAudioCount;
                        if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) settingsSelAudio = (settingsSelAudio + 1) % kAudioCount;
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
                            e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT) {
                            const int dir = (e.key.key == SDLK_LEFT) ? -1 : (e.key.key == SDLK_RIGHT ? 1 : 0);
                            if (settingsSelAudio == 0) menuMusicEnabled = !menuMusicEnabled;
                            else if (settingsSelAudio == 1) muteAllAudio = !muteAllAudio;
                            else if (settingsSelAudio == 2 && dir != 0) musicVolume = std::clamp(musicVolume + dir * 8, 0, 128);
                            else if (settingsSelAudio == 3 && dir != 0) sfxVolume = std::clamp(sfxVolume + dir * 8, 0, 128);
                            else if (settingsSelAudio == 4) inSettings = false;
                            if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
                            if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                        }
                        continue;
                    }
                    if (settingsTab == 2) {
                        constexpr int kDebugCount = 7;
                        if (e.key.key == SDLK_UP || e.key.key == SDLK_w) settingsSelDebug = (settingsSelDebug + kDebugCount - 1) % kDebugCount;
                        if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) settingsSelDebug = (settingsSelDebug + 1) % kDebugCount;
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            if (settingsSelDebug == 0) defaultShowFpsCounter = !defaultShowFpsCounter;
                            else if (settingsSelDebug == 1) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                            else if (settingsSelDebug == 2) defaultShowHitboxes = !defaultShowHitboxes;
                            else if (settingsSelDebug == 3) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                            else if (settingsSelDebug == 4) defaultShowDebugView = !defaultShowDebugView;
                            else if (settingsSelDebug == 5) defaultHideUnknownObjectTypes = !defaultHideUnknownObjectTypes;
                            else if (settingsSelDebug == 6) inSettings = false;
                        }
                        continue;
                    }
                    if (settingsTab == 3) {
                        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) inSettings = false;
                        continue;
                    }

                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
                        e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT) {
                        const int dir = (e.key.key == SDLK_LEFT) ? -1 : (e.key.key == SDLK_RIGHT ? 1 : 0);
#if defined(__ANDROID__)
                        if (settingsSel == IDX_VSYNC) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                        else if (settingsSel == IDX_CAM_CLAMP) clampCamX = !clampCamX;
                        else if (settingsSel == IDX_SHOW_FPS) defaultShowFpsCounter = !defaultShowFpsCounter;
                        else if (settingsSel == IDX_SHOW_DETAILED) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                        else if (settingsSel == IDX_SHOW_HITBOXES) defaultShowHitboxes = !defaultShowHitboxes;
                        else if (settingsSel == IDX_SHOW_PLAYER_HITBOX) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                        else if (settingsSel == IDX_SHOW_DEBUG_VIEW) defaultShowDebugView = !defaultShowDebugView;
                        else if (settingsSel == IDX_MUSIC && dir != 0) musicVolume = std::clamp(musicVolume + dir * 8, 0, 128);
                        else if (settingsSel == IDX_SFX && dir != 0) sfxVolume = std::clamp(sfxVolume + dir * 8, 0, 128);
                        else if (settingsSel == IDX_ABOUT) showAboutPopup();
                        else inSettings = false;
#else
                        if (settingsSel == IDX_FULLSCREEN) { fullscreen = !fullscreen; SDL_SetWindowFullscreen(ctx.win, fullscreen); }
                        else if (settingsSel == IDX_VSYNC) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                        else if (settingsSel == IDX_CAM_CLAMP) clampCamX = !clampCamX;
                        else if (settingsSel == IDX_SHOW_FPS) defaultShowFpsCounter = !defaultShowFpsCounter;
                        else if (settingsSel == IDX_SHOW_DETAILED) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                        else if (settingsSel == IDX_SHOW_HITBOXES) defaultShowHitboxes = !defaultShowHitboxes;
                        else if (settingsSel == IDX_SHOW_PLAYER_HITBOX) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                        else if (settingsSel == IDX_SHOW_DEBUG_VIEW) defaultShowDebugView = !defaultShowDebugView;
                        else if (settingsSel == IDX_MUSIC && dir != 0) musicVolume = std::clamp(musicVolume + dir * 8, 0, 128);
                        else if (settingsSel == IDX_SFX && dir != 0) sfxVolume = std::clamp(sfxVolume + dir * 8, 0, 128);
                        else if (settingsSel == IDX_ABOUT) showAboutPopup();
                        else inSettings = false;
#endif
                        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                    }
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) sliderDrag = SliderDragTarget::None;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point pt{};
                if (!mouseToGamePoint(e.button.x, e.button.y, pt)) continue;
                if (closeMenuOpen) {
                    SDL_Rect modal{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
                    SDL_Rect resumeBtn{modal.x + 26, modal.y + 94, 140, 56};
                    SDL_Rect closeBtn{modal.x + 194, modal.y + 94, 140, 56};
                    if (SDL_PointInRect(&pt, &resumeBtn)) {
                        closeMenuSel = 0;
                        closeMenuOpen = false;
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &closeBtn)) {
                        closeMenuSel = 1;
                        running = false;
                        cleanupMenuAssets();
                        return FrontendAction::Quit;
                    }
                    continue;
                }
                if (!inSettings) {
                    if (inComingSoon) {
                        SDL_Rect backBtn{ctx.baseScreenW / 2 - 110, 430, 220, 54};
                        if (SDL_PointInRect(&pt, &backBtn)) inComingSoon = false;
                        continue;
                    }
                    SDL_Rect settingsBtn = mainMenuBtnRect(0);
                    SDL_Rect playBtn = mainMenuBtnRect(1);
                    SDL_Rect editorBtn = mainMenuBtnRect(2);
                    if (SDL_PointInRect(&pt, &settingsBtn)) { menuSel = 0; inSettings = true; continue; }
                    if (SDL_PointInRect(&pt, &playBtn)) {
                        menuSel = 1;
                        cleanupMenuAssets();
                        return FrontendAction::StartGame;
                    }
                    if (SDL_PointInRect(&pt, &editorBtn)) { menuSel = 2; inComingSoon = true; continue; }
                } else {
                    for (int ti = 0; ti < kSettingsTabCount; ++ti) {
                        SDL_Rect tr = settingsTabBtn(ti);
                        if (SDL_PointInRect(&pt, &tr)) {
                            settingsTab = ti;
                            if (ti == 1) settingsSelAudio = 0;
                            if (ti == 2) settingsSelDebug = 0;
                            continue;
                        }
                    }
                    if (settingsTab == 3) continue;
                    if (settingsTab == 1) {
                        SDL_Rect row0 = settingsRowBtn(0);
                        SDL_Rect row1 = settingsRowBtn(1);
                        SDL_Rect row2 = settingsRowBtn(2);
                        SDL_Rect row3 = settingsRowBtn(3);
                        SDL_Rect row4 = settingsRowBtn(4);
                        SDL_Rect musicSlider = musicSliderRect();
                        SDL_Rect sfxSlider = sfxSliderRect();
                        if (SDL_PointInRect(&pt, &musicSlider)) {
                            musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                            sliderDrag = SliderDragTarget::Music;
                        } else if (SDL_PointInRect(&pt, &sfxSlider)) {
                            sfxVolume = sliderValueFromPoint(pt.x, sfxSlider);
                            sliderDrag = SliderDragTarget::Sfx;
                        } else if (SDL_PointInRect(&pt, &row0)) {
                            menuMusicEnabled = !menuMusicEnabled;
                        } else if (SDL_PointInRect(&pt, &row1)) {
                            muteAllAudio = !muteAllAudio;
                        } else if (SDL_PointInRect(&pt, &row4)) {
                            inSettings = false;
                        }
                        settingsSelAudio = SDL_PointInRect(&pt, &row0) ? 0 :
                                          SDL_PointInRect(&pt, &row1) ? 1 :
                                          SDL_PointInRect(&pt, &row2) ? 2 :
                                          SDL_PointInRect(&pt, &row3) ? 3 :
                                          SDL_PointInRect(&pt, &row4) ? 4 : settingsSelAudio;
                        if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
                        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                        continue;
                    }
                    if (settingsTab == 2) {
                        SDL_Rect row0 = settingsRowBtn(0);
                        SDL_Rect row1 = settingsRowBtn(1);
                        SDL_Rect row2 = settingsRowBtn(2);
                        SDL_Rect row3 = settingsRowBtn(3);
                        SDL_Rect row4 = settingsRowBtn(4);
                        SDL_Rect row5 = settingsRowBtn(5);
                        SDL_Rect row6 = settingsRowBtn(6);
                        if (SDL_PointInRect(&pt, &row0)) defaultShowFpsCounter = !defaultShowFpsCounter;
                        else if (SDL_PointInRect(&pt, &row1)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                        else if (SDL_PointInRect(&pt, &row2)) defaultShowHitboxes = !defaultShowHitboxes;
                        else if (SDL_PointInRect(&pt, &row3)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                        else if (SDL_PointInRect(&pt, &row4)) defaultShowDebugView = !defaultShowDebugView;
                        else if (SDL_PointInRect(&pt, &row5)) defaultHideUnknownObjectTypes = !defaultHideUnknownObjectTypes;
                        else if (SDL_PointInRect(&pt, &row6)) inSettings = false;
                        settingsSelDebug = SDL_PointInRect(&pt, &row0) ? 0 :
                                           SDL_PointInRect(&pt, &row1) ? 1 :
                                           SDL_PointInRect(&pt, &row2) ? 2 :
                                           SDL_PointInRect(&pt, &row3) ? 3 :
                                           SDL_PointInRect(&pt, &row4) ? 4 :
                                           SDL_PointInRect(&pt, &row5) ? 5 :
                                           SDL_PointInRect(&pt, &row6) ? 6 : settingsSelDebug;
                        continue;
                    }
                    SDL_Rect aboutBtn = settingsRowBtn(IDX_ABOUT);
                    SDL_Rect backBtn = settingsRowBtn(IDX_BACK);
#if defined(__ANDROID__)
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect fpsBtn = settingsRowBtn(IDX_SHOW_FPS);
                    SDL_Rect dbgBtn = settingsRowBtn(IDX_SHOW_DETAILED);
                    SDL_Rect hitBtn = settingsRowBtn(IDX_SHOW_HITBOXES);
                    SDL_Rect playerHitBtn = settingsRowBtn(IDX_SHOW_PLAYER_HITBOX);
                    SDL_Rect debugViewBtn = settingsRowBtn(IDX_SHOW_DEBUG_VIEW);
                    if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &dbgBtn)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                    else if (SDL_PointInRect(&pt, &hitBtn)) defaultShowHitboxes = !defaultShowHitboxes;
                    else if (SDL_PointInRect(&pt, &playerHitBtn)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                    else if (SDL_PointInRect(&pt, &debugViewBtn)) defaultShowDebugView = !defaultShowDebugView;
                    else if (SDL_PointInRect(&pt, &aboutBtn)) showAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) inSettings = false;
#else
                    SDL_Rect fullBtn = settingsRowBtn(IDX_FULLSCREEN);
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect fpsBtn = settingsRowBtn(IDX_SHOW_FPS);
                    SDL_Rect dbgBtn = settingsRowBtn(IDX_SHOW_DETAILED);
                    SDL_Rect hitBtn = settingsRowBtn(IDX_SHOW_HITBOXES);
                    SDL_Rect playerHitBtn = settingsRowBtn(IDX_SHOW_PLAYER_HITBOX);
                    SDL_Rect debugViewBtn = settingsRowBtn(IDX_SHOW_DEBUG_VIEW);
                    if (SDL_PointInRect(&pt, &fullBtn)) { fullscreen = !fullscreen; SDL_SetWindowFullscreen(ctx.win, fullscreen); }
                    else if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &dbgBtn)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                    else if (SDL_PointInRect(&pt, &hitBtn)) defaultShowHitboxes = !defaultShowHitboxes;
                    else if (SDL_PointInRect(&pt, &playerHitBtn)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                    else if (SDL_PointInRect(&pt, &debugViewBtn)) defaultShowDebugView = !defaultShowDebugView;
                    else if (SDL_PointInRect(&pt, &aboutBtn)) showAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) inSettings = false;
#endif
                        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                }
            }
            if (e.type == SDL_FINGERDOWN) {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(ctx.win, &winW, &winH);
                int wx = (int)std::lround(e.tfinger.x * winW);
                int wy = (int)std::lround(e.tfinger.y * winH);
                SDL_Point pt{};
                if (!mouseToGamePoint(wx, wy, pt)) continue;
                if (closeMenuOpen) {
                    SDL_Rect modal{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
                    SDL_Rect resumeBtn{modal.x + 26, modal.y + 94, 140, 56};
                    SDL_Rect closeBtn{modal.x + 194, modal.y + 94, 140, 56};
                    if (SDL_PointInRect(&pt, &resumeBtn)) {
                        closeMenuSel = 0;
                        closeMenuOpen = false;
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &closeBtn)) {
                        closeMenuSel = 1;
                        running = false;
                        cleanupMenuAssets();
                        return FrontendAction::Quit;
                    }
                    continue;
                }
                if (!inSettings) {
                    if (inComingSoon) {
                        SDL_Rect backBtn{ctx.baseScreenW / 2 - 110, 430, 220, 54};
                        if (SDL_PointInRect(&pt, &backBtn)) inComingSoon = false;
                        continue;
                    }
                    SDL_Rect settingsBtn = mainMenuBtnRect(0);
                    SDL_Rect playBtn = mainMenuBtnRect(1);
                    SDL_Rect editorBtn = mainMenuBtnRect(2);
                    if (SDL_PointInRect(&pt, &settingsBtn)) { menuSel = 0; inSettings = true; continue; }
                    if (SDL_PointInRect(&pt, &playBtn)) {
                        menuSel = 1;
                        cleanupMenuAssets();
                        return FrontendAction::StartGame;
                    }
                    if (SDL_PointInRect(&pt, &editorBtn)) { menuSel = 2; inComingSoon = true; continue; }
                } else {
                    for (int ti = 0; ti < kSettingsTabCount; ++ti) {
                        SDL_Rect tr = settingsTabBtn(ti);
                        if (SDL_PointInRect(&pt, &tr)) {
                            settingsTab = ti;
                            if (ti == 1) settingsSelAudio = 0;
                            if (ti == 2) settingsSelDebug = 0;
                            continue;
                        }
                    }
                    if (settingsTab == 3) continue;
                    if (settingsTab == 1) {
                        SDL_Rect row0 = settingsRowBtn(0);
                        SDL_Rect row1 = settingsRowBtn(1);
                        SDL_Rect row2 = settingsRowBtn(2);
                        SDL_Rect row3 = settingsRowBtn(3);
                        SDL_Rect row4 = settingsRowBtn(4);
                        SDL_Rect musicSlider = musicSliderRect();
                        SDL_Rect sfxSlider = sfxSliderRect();
                        if (SDL_PointInRect(&pt, &musicSlider)) {
                            musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                            sliderDrag = SliderDragTarget::Music;
                            sliderDragFinger = e.tfinger.fingerID;
                        } else if (SDL_PointInRect(&pt, &sfxSlider)) {
                            sfxVolume = sliderValueFromPoint(pt.x, sfxSlider);
                            sliderDrag = SliderDragTarget::Sfx;
                            sliderDragFinger = e.tfinger.fingerID;
                        } else if (SDL_PointInRect(&pt, &row0)) {
                            menuMusicEnabled = !menuMusicEnabled;
                        } else if (SDL_PointInRect(&pt, &row1)) {
                            muteAllAudio = !muteAllAudio;
                        } else if (SDL_PointInRect(&pt, &row4)) {
                            inSettings = false;
                        }
                        settingsSelAudio = SDL_PointInRect(&pt, &row0) ? 0 :
                                          SDL_PointInRect(&pt, &row1) ? 1 :
                                          SDL_PointInRect(&pt, &row2) ? 2 :
                                           SDL_PointInRect(&pt, &row3) ? 3 :
                                           SDL_PointInRect(&pt, &row4) ? 4 : settingsSelAudio;
                        if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
                        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                        continue;
                    }
                    if (settingsTab == 2) {
                        SDL_Rect row0 = settingsRowBtn(0);
                        SDL_Rect row1 = settingsRowBtn(1);
                        SDL_Rect row2 = settingsRowBtn(2);
                        SDL_Rect row3 = settingsRowBtn(3);
                        SDL_Rect row4 = settingsRowBtn(4);
                        SDL_Rect row5 = settingsRowBtn(5);
                        SDL_Rect row6 = settingsRowBtn(6);
                        if (SDL_PointInRect(&pt, &row0)) defaultShowFpsCounter = !defaultShowFpsCounter;
                        else if (SDL_PointInRect(&pt, &row1)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                        else if (SDL_PointInRect(&pt, &row2)) defaultShowHitboxes = !defaultShowHitboxes;
                        else if (SDL_PointInRect(&pt, &row3)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                        else if (SDL_PointInRect(&pt, &row4)) defaultShowDebugView = !defaultShowDebugView;
                        else if (SDL_PointInRect(&pt, &row5)) defaultHideUnknownObjectTypes = !defaultHideUnknownObjectTypes;
                        else if (SDL_PointInRect(&pt, &row6)) inSettings = false;
                        settingsSelDebug = SDL_PointInRect(&pt, &row0) ? 0 :
                                           SDL_PointInRect(&pt, &row1) ? 1 :
                                           SDL_PointInRect(&pt, &row2) ? 2 :
                                           SDL_PointInRect(&pt, &row3) ? 3 :
                                           SDL_PointInRect(&pt, &row4) ? 4 :
                                           SDL_PointInRect(&pt, &row5) ? 5 :
                                           SDL_PointInRect(&pt, &row6) ? 6 : settingsSelDebug;
                        continue;
                    }
                    SDL_Rect aboutBtn = settingsRowBtn(IDX_ABOUT);
                    SDL_Rect backBtn = settingsRowBtn(IDX_BACK);
#if defined(__ANDROID__)
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect fpsBtn = settingsRowBtn(IDX_SHOW_FPS);
                    SDL_Rect dbgBtn = settingsRowBtn(IDX_SHOW_DETAILED);
                    SDL_Rect hitBtn = settingsRowBtn(IDX_SHOW_HITBOXES);
                    SDL_Rect playerHitBtn = settingsRowBtn(IDX_SHOW_PLAYER_HITBOX);
                    SDL_Rect debugViewBtn = settingsRowBtn(IDX_SHOW_DEBUG_VIEW);
                    if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &dbgBtn)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                    else if (SDL_PointInRect(&pt, &hitBtn)) defaultShowHitboxes = !defaultShowHitboxes;
                    else if (SDL_PointInRect(&pt, &playerHitBtn)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                    else if (SDL_PointInRect(&pt, &debugViewBtn)) defaultShowDebugView = !defaultShowDebugView;
                    else if (SDL_PointInRect(&pt, &aboutBtn)) showAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) inSettings = false;
#else
                    SDL_Rect fullBtn = settingsRowBtn(IDX_FULLSCREEN);
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect fpsBtn = settingsRowBtn(IDX_SHOW_FPS);
                    SDL_Rect dbgBtn = settingsRowBtn(IDX_SHOW_DETAILED);
                    SDL_Rect hitBtn = settingsRowBtn(IDX_SHOW_HITBOXES);
                    SDL_Rect playerHitBtn = settingsRowBtn(IDX_SHOW_PLAYER_HITBOX);
                    SDL_Rect debugViewBtn = settingsRowBtn(IDX_SHOW_DEBUG_VIEW);
                    if (SDL_PointInRect(&pt, &fullBtn)) { fullscreen = !fullscreen; SDL_SetWindowFullscreen(ctx.win, fullscreen); }
                    else if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &dbgBtn)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                    else if (SDL_PointInRect(&pt, &hitBtn)) defaultShowHitboxes = !defaultShowHitboxes;
                    else if (SDL_PointInRect(&pt, &playerHitBtn)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                    else if (SDL_PointInRect(&pt, &debugViewBtn)) defaultShowDebugView = !defaultShowDebugView;
                    else if (SDL_PointInRect(&pt, &aboutBtn)) showAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) inSettings = false;
#endif
                    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                }
            }
            if (e.type == SDL_FINGERMOTION && inSettings && sliderDrag != SliderDragTarget::None &&
                e.tfinger.fingerID == sliderDragFinger) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                SDL_GetWindowSize(ctx.win, &winW, &winH);
                int wx = (int)std::lround(e.tfinger.x * winW);
                int wy = (int)std::lround(e.tfinger.y * winH);
                if (!windowToGamePoint(wx, wy, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy)) continue;
                SDL_Point pt{gx, gy};
                SDL_Rect musicSlider = musicSliderRect();
                SDL_Rect sfxSlider = sfxSliderRect();
                if (sliderDrag == SliderDragTarget::Music) musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                if (sliderDrag == SliderDragTarget::Sfx) sfxVolume = sliderValueFromPoint(pt.x, sfxSlider);
                if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
            }
            if (e.type == SDL_FINGERUP && e.tfinger.fingerID == sliderDragFinger) {
                sliderDrag = SliderDragTarget::None;
            }
            if (e.type == SDL_MOUSEMOTION && inSettings && sliderDrag != SliderDragTarget::None) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                SDL_GetWindowSize(ctx.win, &winW, &winH);
                if (!windowToGamePoint(e.motion.x, e.motion.y, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy)) continue;
                SDL_Point pt{gx, gy};
                SDL_Rect musicSlider = musicSliderRect();
                SDL_Rect sfxSlider = sfxSliderRect();
                if (sliderDrag == SliderDragTarget::Music) musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                if (sliderDrag == SliderDragTarget::Sfx) sfxVolume = sliderValueFromPoint(pt.x, sfxSlider);
                if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
            }
            // Keep mouse/touch handling behavior in main for now to minimize risk.
        }

        SDL_SetRenderTarget(ctx.ren, ctx.gameTarget);
        SDL_SetRenderDrawColor(ctx.ren, 12, 14, 18, 255);
        SDL_RenderClear(ctx.ren);
        if (!inSettings) {
            if (inComingSoon) {
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx.ren, 8, 12, 20, 255);
                SDL_Rect bg{0, 0, ctx.baseScreenW, ctx.baseScreenH};
                SDL_RenderFillRect(ctx.ren, &bg);
                SDL_SetRenderDrawColor(ctx.ren, 28, 40, 64, 255);
                SDL_Rect panel{ctx.baseScreenW / 2 - 260, 120, 520, 320};
                SDL_RenderFillRect(ctx.ren, &panel);
                SDL_SetRenderDrawColor(ctx.ren, 130, 170, 220, 255);
                SDL_RenderDrawRect(ctx.ren, &panel);
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);

                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(3, "EDITOR") / 2, 164, 3, "EDITOR");
                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, "COMING SOON") / 2, 214, 2, "COMING SOON");
                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, "Level editing tools are in development.") / 2, 252, 2, "Level editing tools are in development.");
                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, "You can play the game now and return later.") / 2, 278, 2, "You can play the game now and return later.");

                SDL_Rect backBtn{ctx.baseScreenW / 2 - 110, 430, 220, 54};
                SDL_SetRenderDrawColor(ctx.ren, 60, 85, 120, 255);
                SDL_RenderFillRect(ctx.ren, &backBtn);
                SDL_SetRenderDrawColor(ctx.ren, 180, 210, 240, 255);
                SDL_RenderDrawRect(ctx.ren, &backBtn);
                DrawText(ctx.ren, backBtn.x + (backBtn.w - MeasureTextWidth(2, "BACK")) / 2, backBtn.y + 16, 2, "BACK");
            } else {
            const Frame* mainLogo = getMenuFrame("Main_logo");
            const Frame* btnSprite = getMenuFrame("btn_sprite");
            const Frame* playLogo = getMenuFrame("play_btn_logo");
            const Frame* settingsLogo = getMenuFrame("settings_btn_logo");
            const Frame* editorLogo = getMenuFrame("Editor_logo");
            if (mainMenuTex && mainLogo) {
                SDL_Rect dst{ctx.baseScreenW / 2 - 320, 42, 640, 66};
                renderFrame(ctx.ren, mainMenuTex, *mainLogo, dst);
            }
            SDL_Rect menuBtns[3] = {mainMenuBtnRect(0), mainMenuBtnRect(1), mainMenuBtnRect(2)};
            if (mainMenuTex && btnSprite) {
                for (int i = 0; i < 3; ++i) {
                    renderFrame(ctx.ren, mainMenuTex, *btnSprite, menuBtns[i]);
                }
            }
            for (int i = 0; i < 3; ++i) {
                if (i == menuSel) {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 42);
                    SDL_RenderFillRect(ctx.ren, &menuBtns[i]);
                    SDL_SetRenderDrawColor(ctx.ren, 180, 200, 230, 255);
                    SDL_RenderDrawRect(ctx.ren, &menuBtns[i]);
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                }
            }
            if (mainMenuTex && settingsLogo) {
                SDL_Rect b = mainMenuBtnRect(0);
                SDL_Rect dst{b.x + 20, b.y + 16, 72, 72};
                renderFrame(ctx.ren, mainMenuTex, *settingsLogo, dst);
            }
            if (mainMenuTex && playLogo) {
                SDL_Rect b = mainMenuBtnRect(1);
                SDL_Rect dst{b.x + 30, b.y + 12, 52, 88};
                renderFrame(ctx.ren, mainMenuTex, *playLogo, dst);
            }
            if (mainMenuTex && editorLogo) {
                SDL_Rect b = mainMenuBtnRect(2);
                SDL_Rect dst{b.x + 34, b.y + 16, 44, 80};
                renderFrame(ctx.ren, mainMenuTex, *editorLogo, dst);
            }
            if (!mainMenuTex || !settingsLogo || !playLogo || !editorLogo) {
                DrawText(ctx.ren, menuBtns[0].x + 14, menuBtns[0].y + 82, 2, "SETTINGS");
                DrawText(ctx.ren, menuBtns[1].x + 24, menuBtns[1].y + 82, 2, "PLAY");
                DrawText(ctx.ren, menuBtns[2].x + 18, menuBtns[2].y + 82, 2, "EDITOR");
            }
            if (!mainMenuTex || !mainLogo) {
                const std::string title = "Dorfplatformer Timetravel";
                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(3, title) / 2, 84, 3, title);
            }
            if (closeMenuOpen) {
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, 155);
                SDL_Rect dim{0, 0, ctx.baseScreenW, ctx.baseScreenH};
                SDL_RenderFillRect(ctx.ren, &dim);
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);

                SDL_Rect modal{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
                SDL_SetRenderDrawColor(ctx.ren, 26, 32, 42, 245);
                SDL_RenderFillRect(ctx.ren, &modal);
                SDL_SetRenderDrawColor(ctx.ren, 170, 190, 220, 255);
                SDL_RenderDrawRect(ctx.ren, &modal);
                DrawText(ctx.ren, modal.x + (modal.w - MeasureTextWidth(2, "CLOSE GAME?")) / 2, modal.y + 18, 2, "CLOSE GAME?");

                SDL_Rect resumeBtn{modal.x + 26, modal.y + 94, 140, 56};
                SDL_Rect closeBtn{modal.x + 194, modal.y + 94, 140, 56};
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 0 ? 120 : 70, 95, 85, 200);
                SDL_RenderFillRect(ctx.ren, &resumeBtn);
                SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 1 ? 120 : 70, 70, 75, 220);
                SDL_RenderFillRect(ctx.ren, &closeBtn);
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ctx.ren, 190, 200, 220, 255);
                SDL_RenderDrawRect(ctx.ren, &resumeBtn);
                SDL_RenderDrawRect(ctx.ren, &closeBtn);
                DrawText(ctx.ren, resumeBtn.x + (resumeBtn.w - MeasureTextWidth(2, "RESUME")) / 2, resumeBtn.y + 18, 2, "RESUME");
                DrawText(ctx.ren, closeBtn.x + (closeBtn.w - MeasureTextWidth(2, "CLOSE")) / 2, closeBtn.y + 18, 2, "CLOSE");
            }
            }
        } else {
            const std::string title = "SETTINGS";
            DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(3, title) / 2, 84, 3, title);
            const char* tabNames[kSettingsTabCount] = {"GENERAL", "AUDIO", "DEBUG", "CONTROLS"};
            for (int ti = 0; ti < kSettingsTabCount; ++ti) {
                SDL_Rect tr = settingsTabBtn(ti);
                SDL_SetRenderDrawColor(ctx.ren, settingsTab == ti ? 80 : 50, 90, 120, 220);
                SDL_RenderFillRect(ctx.ren, &tr);
                SDL_SetRenderDrawColor(ctx.ren, 180, 200, 230, 255);
                SDL_RenderDrawRect(ctx.ren, &tr);
                DrawText(ctx.ren, tr.x + (tr.w - MeasureTextWidth(2, tabNames[ti])) / 2, tr.y + 4, 2, tabNames[ti]);
            }
            if (settingsTab == 3) {
                DrawText(ctx.ren, 130, 162, 2, "MOVEMENT:  A/D  or  LEFT/RIGHT");
                DrawText(ctx.ren, 130, 192, 2, "JUMP:      SPACE/W/UP");
                DrawText(ctx.ren, 130, 222, 2, "DOWN:      S/DOWN");
                DrawText(ctx.ren, 130, 252, 2, "PAUSE:     ESC");
            } else if (settingsTab == 1) {
                std::vector<std::string> rows = {
                    std::string("MENU MUSIC: ") + (menuMusicEnabled ? "ON" : "OFF"),
                    std::string("MUTE ALL: ") + (muteAllAudio ? "ON" : "OFF"),
                    std::string("MUSIC: ") + std::to_string((musicVolume * 100) / 128) + "%",
                    std::string("SFX: ") + std::to_string((sfxVolume * 100) / 128) + "%",
                    "BACK"
                };
                for (int i = 0; i < (int)rows.size(); ++i) {
                    int y = settingsRowY(i);
                    if (i == settingsSelAudio) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 48);
                        SDL_Rect hl{ctx.baseScreenW / 2 - 140, y - 2, 280, 30};
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, rows[i]) / 2, y, 2, rows[i]);
                }
                auto drawSlider = [&](const SDL_Rect& slider, int value) {
                    SDL_SetRenderDrawColor(ctx.ren, 70, 80, 95, 255);
                    SDL_RenderFillRect(ctx.ren, &slider);
                    SDL_SetRenderDrawColor(ctx.ren, 130, 150, 180, 255);
                    SDL_RenderDrawRect(ctx.ren, &slider);
                    int fillW = (int)std::lround((value / 128.0f) * slider.w);
                    SDL_Rect fill{slider.x, slider.y, std::clamp(fillW, 0, slider.w), slider.h};
                    SDL_SetRenderDrawColor(ctx.ren, 180, 220, 255, 255);
                    SDL_RenderFillRect(ctx.ren, &fill);
                };
                drawSlider(musicSliderRect(), musicVolume);
                drawSlider(sfxSliderRect(), sfxVolume);
            } else if (settingsTab == 2) {
                std::vector<std::string> rows = {
                    std::string("FPS COUNTER: ") + (defaultShowFpsCounter ? "ON" : "OFF"),
                    std::string("DETAILED DEBUGGER: ") + (defaultShowDetailedDebugger ? "ON" : "OFF"),
                    std::string("SHOW HITBOXES: ") + (defaultShowHitboxes ? "ON" : "OFF"),
                    std::string("PLAYER HITBOX: ") + (defaultShowPlayerHitbox ? "ON" : "OFF"),
                    std::string("DEBUG HUD: ") + (defaultShowDebugView ? "ON" : "OFF"),
                    std::string("HIDE UNKNOWN OBJECT TYPES: ") + (defaultHideUnknownObjectTypes ? "ON" : "OFF"),
                    "BACK"
                };
                for (int i = 0; i < (int)rows.size(); ++i) {
                    int y = settingsRowY(i);
                    if (i == settingsSelDebug) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 48);
                        SDL_Rect hl{ctx.baseScreenW / 2 - 140, y - 2, 280, 30};
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, rows[i]) / 2, y, 2, rows[i]);
                }
            } else {
#if defined(__ANDROID__)
                std::vector<std::string> rows = {
                    std::string("VSYNC: ") + (vsyncEnabled ? "ON" : "OFF"),
                    std::string("CAM CLAMP: ") + (clampCamX ? "ON" : "OFF"),
                    std::string("FPS COUNTER: ") + (defaultShowFpsCounter ? "ON" : "OFF"),
                    std::string("DETAILED DEBUGGER: ") + (defaultShowDetailedDebugger ? "ON" : "OFF"),
                    std::string("SHOW HITBOXES: ") + (defaultShowHitboxes ? "ON" : "OFF"),
                    std::string("PLAYER HITBOX: ") + (defaultShowPlayerHitbox ? "ON" : "OFF"),
                    std::string("DEBUG HUD: ") + (defaultShowDebugView ? "ON" : "OFF"),
                    std::string("MUSIC: ") + std::to_string((musicVolume * 100) / 128) + "%",
                    std::string("SFX: ") + std::to_string((sfxVolume * 100) / 128) + "%",
                    "ABOUT",
                    "BACK"
                };
#else
                std::vector<std::string> rows = {
                    std::string("FULLSCREEN: ") + (fullscreen ? "ON" : "OFF"),
                    std::string("VSYNC: ") + (vsyncEnabled ? "ON" : "OFF"),
                    std::string("CAM CLAMP: ") + (clampCamX ? "ON" : "OFF"),
                    std::string("FPS COUNTER: ") + (defaultShowFpsCounter ? "ON" : "OFF"),
                    std::string("DETAILED DEBUGGER: ") + (defaultShowDetailedDebugger ? "ON" : "OFF"),
                    std::string("SHOW HITBOXES: ") + (defaultShowHitboxes ? "ON" : "OFF"),
                    std::string("PLAYER HITBOX: ") + (defaultShowPlayerHitbox ? "ON" : "OFF"),
                    std::string("DEBUG HUD: ") + (defaultShowDebugView ? "ON" : "OFF"),
                    std::string("MUSIC: ") + std::to_string((musicVolume * 100) / 128) + "%",
                    std::string("SFX: ") + std::to_string((sfxVolume * 100) / 128) + "%",
                    "ABOUT",
                    "BACK"
                };
#endif
                for (int i = 0; i < (int)rows.size(); ++i) {
                    int y = settingsRowY(i);
                    if (i == settingsSel) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 48);
                        SDL_Rect hl{ctx.baseScreenW / 2 - 140, y - 2, 280, 30};
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, rows[i]) / 2, y, 2, rows[i]);
                }
            }
        }

        SDL_SetRenderTarget(ctx.ren, nullptr);
        int winW = 0, winH = 0;
        SDL_GetWindowSize(ctx.win, &winW, &winH);
        SDL_Rect presentDst = computePresentRect(winW, winH, ctx.baseScreenW, ctx.baseScreenH);
        SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, 255);
        SDL_RenderClear(ctx.ren);
        SDL_RenderCopy(ctx.ren, ctx.gameTarget, nullptr, &presentDst);
        SDL_RenderPresent(ctx.ren);
    }
    cleanupMenuAssets();
    return FrontendAction::Quit;
}
