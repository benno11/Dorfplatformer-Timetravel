#include "FrontendMenu.h"

#include <sdl3/SDL.h>
#include <sdl3/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

#include "AssetPath.h"
#include "GameSupport.h"
#include "InputSystem.h"
#include "LevelSelect.h"
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
    bool closeMenuOpen = false;
    int closeMenuSel = 0; // 0 Resume, 1 Close Game
    int menuSel = -1;    // 0 Settings, 1 Play, 2 Editor
    int settingsTab = 0; // 0 General, 1 Audio, 2 Debug, 3 Controls
    int settingsSelAudio = 0;
    int settingsSelDebug = 0;
    bool pendingSettingsExitCleanup = false;
    Uint64 inputBlockUntilTicks = 0;
    constexpr Uint64 kMenuInputBlockMs = 110;
    constexpr int kSettingsTabCount = 4;
    auto blockMenuInput = [&]() {
        inputBlockUntilTicks = SDL_GetTicks() + kMenuInputBlockMs;
    };
    auto setInSettings = [&](bool value) {
        if (inSettings != value) {
            blockMenuInput();
            if (!value) pendingSettingsExitCleanup = true;
        }
        inSettings = value;
    };
    auto setCloseMenuOpen = [&](bool value) {
        if (closeMenuOpen != value) blockMenuInput();
        closeMenuOpen = value;
    };
    auto setSettingsTab = [&](int value) {
        settingsTab = value;
    };
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
    Uint64 aboutPopupBlockUntilTicks = 0;
    auto tryShowAboutPopup = [&]() {
        const Uint64 now = SDL_GetTicks();
        if (now < aboutPopupBlockUntilTicks) return;
        // Prevent duplicate About windows from mixed touch/mouse/keyboard bursts.
        aboutPopupBlockUntilTicks = now + 600;
        showAboutPopup();
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
    auto applySettingsExitCleanup = [&]() {
        if (!pendingSettingsExitCleanup) return;
        pendingSettingsExitCleanup = false;
        // Ensure main menu resumes in an interactive state after leaving settings.
        if (menuSel < 0 || menuSel > 2) menuSel = 1;
        sliderDrag = SliderDragTarget::None;
        sliderDragFinger = 0;
    };
    Uint64 lastTouchDownTicks = 0;
    int lastTouchDownWinX = -100000;
    int lastTouchDownWinY = -100000;
    constexpr Uint64 kSyntheticMouseSuppressMs = 220;
    constexpr int kSyntheticMouseSuppressDistPx = 36;
    std::unordered_set<SDL_FingerID> activeTouchFingers;
    SDL_Event e;
    const int settingsStartY = 150;
    const int settingsRowH = 36;
    const int settingsTabX = 16;
    const int settingsTabY = 118;
    const int settingsTabW = 120;
    const int settingsTabH = 30;
    auto settingsRowY = [&](int idx) -> int { return settingsStartY + idx * settingsRowH; };
    auto settingsRowBtn = [&](int idx) -> SDL_Rect {
        return SDL_Rect{ctx.baseScreenW / 2 - 140, settingsRowY(idx), 280, 30};
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
    auto isLikelySyntheticMouseFromTouch = [&](int mx, int my) -> bool {
        if (lastTouchDownTicks == 0) return false;
        const Uint64 nowTicks = SDL_GetTicks();
        if (nowTicks < lastTouchDownTicks || (nowTicks - lastTouchDownTicks) > kSyntheticMouseSuppressMs) {
            return false;
        }
        const int dx = mx - lastTouchDownWinX;
        const int dy = my - lastTouchDownWinY;
        return (dx * dx + dy * dy) <= (kSyntheticMouseSuppressDistPx * kSyntheticMouseSuppressDistPx);
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
    SDL_Texture* menuFallbackTex = IMG_LoadTexture(ctx.ren, ResolveAssetPath("assets/Sheets/DF_Menus-uhd.png").c_str());
    auto menuFallbackFrames = loadPlistFrames("assets/Sheets/DF_Menus-uhd.plist");
    SDL_Texture* menuBgTex = IMG_LoadTexture(ctx.ren, ResolveAssetPath("assets/Sheets/DF_Background-uhd.png").c_str());
    auto menuBgFrames = loadPlistFrames("assets/Sheets/DF_Background-uhd.plist");
    auto getMenuFrame = [&](const char* name, SDL_Texture*& outTex) -> const Frame* {
        auto it = mainMenuFrames.find(name);
        if (it != mainMenuFrames.end()) {
            outTex = mainMenuTex;
            return &it->second;
        }
        auto itFallback = menuFallbackFrames.find(name);
        if (itFallback != menuFallbackFrames.end()) {
            outTex = menuFallbackTex;
            return &itFallback->second;
        }
        outTex = nullptr;
        return nullptr;
    };
    auto renderCenterLoopedFrame = [&](SDL_Texture* tex, const Frame& f, const SDL_Rect& dst) {
        if (!tex) return;
        if (f.rotated || f.rect.w < 3 || f.rect.h < 3 || dst.w <= 0 || dst.h <= 0) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }
        const int srcLeft = (f.rect.w - 1) / 2;
        const int srcRight = f.rect.w - srcLeft - 1;
        const int srcTop = (f.rect.h - 1) / 2;
        const int srcBottom = f.rect.h - srcTop - 1;
        if (srcLeft < 1 || srcRight < 1 || srcTop < 1 || srcBottom < 1) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }

        int dstLeft = std::min(srcLeft, std::max(0, dst.w / 2));
        int dstRight = std::min(srcRight, std::max(0, dst.w - dstLeft - 1));
        int dstTop = std::min(srcTop, std::max(0, dst.h / 2));
        int dstBottom = std::min(srcBottom, std::max(0, dst.h - dstTop - 1));
        int dstMidW = dst.w - dstLeft - dstRight;
        int dstMidH = dst.h - dstTop - dstBottom;
        if (dstMidW <= 0 || dstMidH <= 0) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }

        auto blit = [&](const SDL_Rect& src, const SDL_Rect& out) {
            if (src.w <= 0 || src.h <= 0 || out.w <= 0 || out.h <= 0) return;
            SDL_RenderCopy(ctx.ren, tex, &src, &out);
        };

        const int cx = f.rect.x + srcLeft;
        const int cy = f.rect.y + srcTop;
        SDL_Rect sTL{f.rect.x, f.rect.y, srcLeft, srcTop};
        SDL_Rect sT{cx, f.rect.y, 1, srcTop};
        SDL_Rect sTR{cx + 1, f.rect.y, srcRight, srcTop};
        SDL_Rect sL{f.rect.x, cy, srcLeft, 1};
        SDL_Rect sC{cx, cy, 1, 1};
        SDL_Rect sR{cx + 1, cy, srcRight, 1};
        SDL_Rect sBL{f.rect.x, cy + 1, srcLeft, srcBottom};
        SDL_Rect sB{cx, cy + 1, 1, srcBottom};
        SDL_Rect sBR{cx + 1, cy + 1, srcRight, srcBottom};

        SDL_Rect dTL{dst.x, dst.y, dstLeft, dstTop};
        SDL_Rect dT{dst.x + dstLeft, dst.y, dstMidW, dstTop};
        SDL_Rect dTR{dst.x + dst.w - dstRight, dst.y, dstRight, dstTop};
        SDL_Rect dL{dst.x, dst.y + dstTop, dstLeft, dstMidH};
        SDL_Rect dC{dst.x + dstLeft, dst.y + dstTop, dstMidW, dstMidH};
        SDL_Rect dR{dst.x + dst.w - dstRight, dst.y + dstTop, dstRight, dstMidH};
        SDL_Rect dBL{dst.x, dst.y + dst.h - dstBottom, dstLeft, dstBottom};
        SDL_Rect dB{dst.x + dstLeft, dst.y + dst.h - dstBottom, dstMidW, dstBottom};
        SDL_Rect dBR{dst.x + dst.w - dstRight, dst.y + dst.h - dstBottom, dstRight, dstBottom};

        blit(sTL, dTL);
        blit(sT, dT);
        blit(sTR, dTR);
        blit(sL, dL);
        blit(sC, dC);
        blit(sR, dR);
        blit(sBL, dBL);
        blit(sB, dB);
        blit(sBR, dBR);
    };
    auto renderOpaqueCenterLoopedFrame = [&](SDL_Texture* tex, const Frame& f, const SDL_Rect& dst) {
        if (!tex) return;
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        renderCenterLoopedFrame(tex, f, dst);
    };
    auto cleanupMenuAssets = [&]() {
        if (mainMenuTex) {
            SDL_DestroyTexture(mainMenuTex);
            mainMenuTex = nullptr;
        }
        if (menuFallbackTex) {
            SDL_DestroyTexture(menuFallbackTex);
            menuFallbackTex = nullptr;
        }
        if (menuBgTex) {
            SDL_DestroyTexture(menuBgTex);
            menuBgTex = nullptr;
        }
    };
    auto tryStartCustomLevel = [&]() -> bool {
        if (!ctx.selectedLevelPath) return false;
        std::string path = RunCustomLevelSelect(ctx.win, ctx.ren);
        if (path.empty()) return false;
        *ctx.selectedLevelPath = path;
        cleanupMenuAssets();
        return true;
    };

    // Apply persisted audio state before entering the menu loop.
    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
    InputSystem menuInput;
    menuInput.scanConnected();

    while (running) {
        applySettingsExitCleanup();
        int eventsProcessed = 0;
        while (eventsProcessed < 256 && SDL_PollEvent(&e)) {
            ++eventsProcessed;
            menuInput.handleEvent(e);
            const bool inputBlocked = SDL_GetTicks() < inputBlockUntilTicks;
            if (e.type == SDL_QUIT) {
                running = false;
                cleanupMenuAssets();
                return FrontendAction::Quit;
            }
            if (inputBlocked &&
                e.type != SDL_MOUSEBUTTONDOWN &&
                e.type != SDL_MOUSEBUTTONUP &&
                e.type != SDL_EVENT_FINGER_DOWN &&
                e.type != SDL_EVENT_FINGER_UP &&
                e.type != SDL_EVENT_FINGER_CANCELED) {
                continue;
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const bool isBackBtn =
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK ||
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
                const bool isAcceptBtn =
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_START;
                if (closeMenuOpen) {
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP ||
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) closeMenuSel = (closeMenuSel + 1) % 2;
                    if (isBackBtn) setCloseMenuOpen(false);
                    if (isAcceptBtn) {
                        if (closeMenuSel == 0) {
                            setCloseMenuOpen(false);
                        } else {
                            running = false;
                            cleanupMenuAssets();
                            return FrontendAction::Quit;
                        }
                    }
                    continue;
                }
                if (!inSettings) {
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) menuSel = (menuSel + 2) % 3;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ||
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) menuSel = (menuSel + 1) % 3;
                    if (isAcceptBtn) {
                        if (menuSel == 0) setInSettings(true);
                        if (menuSel == 1) {
                            cleanupMenuAssets();
                            return FrontendAction::StartGame;
                        }
                        if (menuSel == 2) {
                            if (tryStartCustomLevel()) return FrontendAction::StartGame;
                        }
                    }
                    if (isBackBtn) {
                        setCloseMenuOpen(true);
                        closeMenuSel = 0;
                    }
                    continue;
                }
                if (isBackBtn) {
                    setInSettings(false);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    settingsSel = (settingsSel + kSettingsCount - 1) % kSettingsCount;
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    settingsSel = (settingsSel + 1) % kSettingsCount;
                    continue;
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                const bool isBackKey = (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK);
                if (closeMenuOpen) {
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_w) closeMenuSel = (closeMenuSel + 1) % 2;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) closeMenuSel = (closeMenuSel + 1) % 2;
                    if (isBackKey) setCloseMenuOpen(false);
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (closeMenuSel == 0) {
                            setCloseMenuOpen(false);
                        } else {
                            running = false;
                            cleanupMenuAssets();
                            return FrontendAction::Quit;
                        }
                    }
                    continue;
                }
                if (!inSettings) {
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_a) menuSel = (menuSel + 2) % 3;
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_d) menuSel = (menuSel + 1) % 3;
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_w) menuSel = (menuSel + 2) % 3;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) menuSel = (menuSel + 1) % 3;
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (menuSel == 0) setInSettings(true);
                        if (menuSel == 1) {
                            cleanupMenuAssets();
                            return FrontendAction::StartGame;
                        }
                        if (menuSel == 2) {
                            if (tryStartCustomLevel()) return FrontendAction::StartGame;
                        }
                    }
                    if (isBackKey) {
                        setCloseMenuOpen(true);
                        closeMenuSel = 0;
                    }
                } else {
                    if (e.key.key == SDLK_TAB || e.key.key == SDLK_q || e.key.key == SDLK_e) {
                        setSettingsTab((settingsTab + 1) % kSettingsTabCount);
                        continue;
                    }
                    if (e.key.key == SDLK_1) { setSettingsTab(0); continue; }
                    if (e.key.key == SDLK_2) { setSettingsTab(1); continue; }
                    if (e.key.key == SDLK_3) { setSettingsTab(2); continue; }
                    if (e.key.key == SDLK_4) { setSettingsTab(3); continue; }
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_w) settingsSel = (settingsSel + kSettingsCount - 1) % kSettingsCount;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_s) settingsSel = (settingsSel + 1) % kSettingsCount;
                    if (e.key.key == SDLK_v) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    if (e.key.key == SDLK_c) clampCamX = !clampCamX;
                    if (e.key.key == SDLK_f) defaultShowFpsCounter = !defaultShowFpsCounter;
                    if (e.key.key == SDLK_g) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                    if (e.key.key == SDLK_h) defaultShowHitboxes = !defaultShowHitboxes;
                    if (e.key.key == SDLK_p) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                    if (e.key.key == SDLK_d) defaultShowDebugView = !defaultShowDebugView;
                    if (isBackKey) setInSettings(false);

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
                            else if (settingsSelAudio == 4) setInSettings(false);
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
                            else if (settingsSelDebug == 6) setInSettings(false);
                        }
                        continue;
                    }
                    if (settingsTab == 3) {
                        if (isBackKey || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) setInSettings(false);
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
                        else if (settingsSel == IDX_ABOUT) tryShowAboutPopup();
                        else setInSettings(false);
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
                        else if (settingsSel == IDX_ABOUT) tryShowAboutPopup();
                        else setInSettings(false);
#endif
                        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                    }
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) sliderDrag = SliderDragTarget::None;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (!activeTouchFingers.empty()) continue;
                // Some platforms emit synthetic mouse clicks for taps; ignore only near-immediate
                // events that occur at the same touch location to avoid dropping real mouse input.
                if (isLikelySyntheticMouseFromTouch(e.button.x, e.button.y)) continue;
                SDL_Point pt{};
                if (!mouseToGamePoint(e.button.x, e.button.y, pt)) continue;
                if (closeMenuOpen) {
                    SDL_Rect modal{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
                    SDL_Rect resumeBtn{modal.x + 26, modal.y + 94, 140, 56};
                    SDL_Rect closeBtn{modal.x + 194, modal.y + 94, 140, 56};
                    if (SDL_PointInRect(&pt, &resumeBtn)) {
                        closeMenuSel = 0;
                        setCloseMenuOpen(false);
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &closeBtn)) {
                        closeMenuSel = 1;
                        running = false;
                        cleanupMenuAssets();
                        return FrontendAction::Quit;
                        continue;
                    }
                    continue;
                }
                if (!inSettings) {
                    SDL_Rect settingsBtn = mainMenuBtnRect(0);
                    SDL_Rect playBtn = mainMenuBtnRect(1);
                    SDL_Rect editorBtn = mainMenuBtnRect(2);
                    if (SDL_PointInRect(&pt, &settingsBtn)) { menuSel = 0; setInSettings(true); continue; }
                    if (SDL_PointInRect(&pt, &playBtn)) {
                        menuSel = 1;
                        cleanupMenuAssets();
                        return FrontendAction::StartGame;
                    }
                    if (SDL_PointInRect(&pt, &editorBtn)) {
                        menuSel = 2;
                        if (tryStartCustomLevel()) return FrontendAction::StartGame;
                        continue;
                    }
                } else {
                    for (int ti = 0; ti < kSettingsTabCount; ++ti) {
                        SDL_Rect tr = settingsTabBtn(ti);
                        if (SDL_PointInRect(&pt, &tr)) {
                            setSettingsTab(ti);
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
                            setInSettings(false);
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
                        else if (SDL_PointInRect(&pt, &row6)) setInSettings(false);
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
                    else if (SDL_PointInRect(&pt, &aboutBtn)) tryShowAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
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
                    else if (SDL_PointInRect(&pt, &aboutBtn)) tryShowAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
#endif
                        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                }
            }
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                activeTouchFingers.insert(e.tfinger.fingerID);
                int winW = 0, winH = 0;
                SDL_GetWindowSize(ctx.win, &winW, &winH);
                int wx = (int)std::lround(e.tfinger.x * winW);
                int wy = (int)std::lround(e.tfinger.y * winH);
                lastTouchDownTicks = SDL_GetTicks();
                lastTouchDownWinX = wx;
                lastTouchDownWinY = wy;
                SDL_Point pt{};
                if (!mouseToGamePoint(wx, wy, pt)) continue;
                if (closeMenuOpen) {
                    SDL_Rect modal{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
                    SDL_Rect resumeBtn{modal.x + 26, modal.y + 94, 140, 56};
                    SDL_Rect closeBtn{modal.x + 194, modal.y + 94, 140, 56};
                    if (SDL_PointInRect(&pt, &resumeBtn)) {
                        closeMenuSel = 0;
                        setCloseMenuOpen(false);
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
                    SDL_Rect settingsBtn = mainMenuBtnRect(0);
                    SDL_Rect playBtn = mainMenuBtnRect(1);
                    SDL_Rect editorBtn = mainMenuBtnRect(2);
                    if (SDL_PointInRect(&pt, &settingsBtn)) {
                        menuSel = 0;
                        setInSettings(true);
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &playBtn)) {
                        menuSel = 1;
                        cleanupMenuAssets();
                        return FrontendAction::StartGame;
                    }
                    if (SDL_PointInRect(&pt, &editorBtn)) {
                        menuSel = 2;
                        if (tryStartCustomLevel()) return FrontendAction::StartGame;
                        continue;
                    }
                } else {
                    for (int ti = 0; ti < kSettingsTabCount; ++ti) {
                        SDL_Rect tr = settingsTabBtn(ti);
                        if (SDL_PointInRect(&pt, &tr)) {
                            setSettingsTab(ti);
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
                            setInSettings(false);
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
                        else if (SDL_PointInRect(&pt, &row6)) setInSettings(false);
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
                    else if (SDL_PointInRect(&pt, &aboutBtn)) tryShowAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
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
                    else if (SDL_PointInRect(&pt, &aboutBtn)) tryShowAboutPopup();
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
#endif
                    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                }
            }
            if (e.type == SDL_EVENT_FINGER_MOTION &&
                inSettings && sliderDrag != SliderDragTarget::None &&
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
            if (e.type == SDL_EVENT_FINGER_UP && e.tfinger.fingerID == sliderDragFinger) {
                sliderDrag = SliderDragTarget::None;
            }
            if (e.type == SDL_EVENT_FINGER_UP) {
                activeTouchFingers.erase(e.tfinger.fingerID);
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED && e.tfinger.fingerID == sliderDragFinger) {
                sliderDrag = SliderDragTarget::None;
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED) {
                activeTouchFingers.erase(e.tfinger.fingerID);
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
        if (menuBgTex && !menuBgFrames.empty()) {
            auto getBgFrame = [&](const char* name) -> const Frame* {
                auto it = menuBgFrames.find(name);
                if (it != menuBgFrames.end()) return &it->second;
                return nullptr;
            };
            const Frame* bgBack = getBgFrame("w1b");
            if (!bgBack) bgBack = getBgFrame("w1b.png");
            const Frame* bgFront = getBgFrame("w1f");
            if (!bgFront) bgFront = getBgFrame("w1f.png");

            struct MenuParallaxLayer {
                const Frame* frame;
                float speed;
                float yBias;
                float alpha;
            };
            MenuParallaxLayer layers[] = {
                {bgBack, 0.015f, 0.0f, 255.0f},
                {bgFront, 0.045f, 10.0f, 255.0f},
                {bgFront, 0.075f, 20.0f, 255.0f},
            };
            const Uint64 ticks = SDL_GetTicks();
            for (const auto& layer : layers) {
                if (!layer.frame) continue;
                int fw = layer.frame->rotated ? layer.frame->rect.h : layer.frame->rect.w;
                int fh = layer.frame->rotated ? layer.frame->rect.w : layer.frame->rect.h;
                if (fw <= 0 || fh <= 0) continue;
                float ox = std::fmod((float)ticks * layer.speed, (float)fw);
                if (ox < 0.0f) ox += (float)fw;
                int y = (int)std::lround((float)ctx.baseScreenH - (float)fh + layer.yBias);
                SDL_SetTextureAlphaMod(menuBgTex, (Uint8)std::clamp((int)std::lround(layer.alpha), 0, 255));
                for (int x = -1; x <= ctx.baseScreenW / fw + 1; ++x) {
                    SDL_Rect dst{(int)std::lround((float)(x * fw) - ox), y, fw, fh};
                    renderFrame(ctx.ren, menuBgTex, *layer.frame, dst);
                }
            }
            SDL_SetTextureAlphaMod(menuBgTex, 255);
        }
        if (!inSettings) {
            SDL_Texture* mainLogoTex = nullptr;
            SDL_Texture* btnSpriteTex = nullptr;
            SDL_Texture* playLogoTex = nullptr;
            SDL_Texture* settingsLogoTex = nullptr;
            SDL_Texture* editorLogoTex = nullptr;
            const Frame* mainLogo = getMenuFrame("Main_logo", mainLogoTex);
            const Frame* btnSprite = getMenuFrame("btn_sprite", btnSpriteTex);
            bool btnSpriteIsGeneric = false;
            if (!btnSprite) {
                btnSprite = getMenuFrame("button_genreaic", btnSpriteTex);
                btnSpriteIsGeneric = (btnSprite != nullptr);
            }
            const Frame* playLogo = getMenuFrame("play_btn_logo", playLogoTex);
            const Frame* settingsLogo = getMenuFrame("settings_btn_logo", settingsLogoTex);
            const Frame* editorLogo = getMenuFrame("Editor_logo", editorLogoTex);
            if (mainLogoTex && mainLogo) {
                SDL_Rect dst{ctx.baseScreenW / 2 - 320, 42, 640, 66};
                renderFrame(ctx.ren, mainLogoTex, *mainLogo, dst);
            }
            SDL_Rect menuBtns[3] = {mainMenuBtnRect(0), mainMenuBtnRect(1), mainMenuBtnRect(2)};
            if (btnSpriteTex && btnSprite) {
                for (int i = 0; i < 3; ++i) {
                    if (btnSpriteIsGeneric) renderOpaqueCenterLoopedFrame(btnSpriteTex, *btnSprite, menuBtns[i]);
                    else renderFrame(ctx.ren, btnSpriteTex, *btnSprite, menuBtns[i]);
                }
            }
            for (int i = 0; i < 3; ++i) {
                if (i == menuSel) {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_ADD);
                    SDL_SetRenderDrawColor(ctx.ren, 45, 45, 45, 255);
                    SDL_RenderFillRect(ctx.ren, &menuBtns[i]);
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(ctx.ren, 180, 200, 230, 255);
                    SDL_RenderDrawRect(ctx.ren, &menuBtns[i]);
                }
            }
            if (settingsLogoTex && settingsLogo) {
                SDL_Rect b = mainMenuBtnRect(0);
                SDL_Rect dst{b.x + 20, b.y + 16, 72, 72};
                renderFrame(ctx.ren, settingsLogoTex, *settingsLogo, dst);
            }
            if (playLogoTex && playLogo) {
                SDL_Rect b = mainMenuBtnRect(1);
                SDL_Rect dst{b.x + 30, b.y + 12, 52, 88};
                renderFrame(ctx.ren, playLogoTex, *playLogo, dst);
            }
            if (editorLogoTex && editorLogo) {
                SDL_Rect b = mainMenuBtnRect(2);
                SDL_Rect dst{b.x + 34, b.y + 16, 44, 80};
                renderFrame(ctx.ren, editorLogoTex, *editorLogo, dst);
            }
            if ((!settingsLogoTex || !settingsLogo) || (!playLogoTex || !playLogo) || (!editorLogoTex || !editorLogo)) {
                DrawText(ctx.ren, menuBtns[0].x + 14, menuBtns[0].y + 82, 2, "SETTINGS");
                DrawText(ctx.ren, menuBtns[1].x + 24, menuBtns[1].y + 82, 2, "PLAY");
                DrawText(ctx.ren, menuBtns[2].x + 18, menuBtns[2].y + 82, 2, "EDITOR");
            }
            if (!mainLogoTex || !mainLogo) {
                const std::string title = "Dorfplatformer Timetravel";
                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(3, title) / 2, 84, 3, title);
            }
            if (!closeMenuOpen) {
                const std::string versionText = std::string("v") + (ctx.versionString.empty() ? "dev" : ctx.versionString);
                const std::string copyrightText = "Copyright (c) Benno111 2024 - 2026";
                DrawText(ctx.ren, 12, ctx.baseScreenH - 28, 1, versionText);
                DrawText(ctx.ren, ctx.baseScreenW - 12 - MeasureTextWidth(1, copyrightText), ctx.baseScreenH - 28, 1, copyrightText);
            }
            if (closeMenuOpen) {
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, 255);
                SDL_Rect dim{0, 0, ctx.baseScreenW, ctx.baseScreenH};
                SDL_RenderFillRect(ctx.ren, &dim);

                SDL_Rect modal{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
                SDL_Texture* popupWindowTex = nullptr;
                const Frame* popupWindow = getMenuFrame("window", popupWindowTex);
                if (!popupWindow) popupWindow = getMenuFrame("window.png", popupWindowTex);
                if (popupWindow && popupWindowTex) {
                    renderFrame(ctx.ren, popupWindowTex, *popupWindow, modal);
                } else {
                    SDL_SetRenderDrawColor(ctx.ren, 26, 32, 42, 245);
                    SDL_RenderFillRect(ctx.ren, &modal);
                    SDL_SetRenderDrawColor(ctx.ren, 170, 190, 220, 255);
                    SDL_RenderDrawRect(ctx.ren, &modal);
                }
                DrawText(ctx.ren, modal.x + (modal.w - MeasureTextWidth(2, "CLOSE GAME?")) / 2, modal.y + 18, 2, "CLOSE GAME?");

                SDL_Rect resumeBtn{modal.x + 26, modal.y + 94, 140, 56};
                SDL_Rect closeBtn{modal.x + 194, modal.y + 94, 140, 56};
                SDL_Texture* popupBtnTex = nullptr;
                const Frame* popupBtnFrame = getMenuFrame("button_genreaic", popupBtnTex);
                if (!popupBtnFrame) popupBtnFrame = getMenuFrame("button_genreaic.png", popupBtnTex);
                if (popupBtnFrame && popupBtnTex) {
                    renderOpaqueCenterLoopedFrame(popupBtnTex, *popupBtnFrame, resumeBtn);
                    renderOpaqueCenterLoopedFrame(popupBtnTex, *popupBtnFrame, closeBtn);
                } else {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 0 ? 120 : 70, 95, 85, 255);
                    SDL_RenderFillRect(ctx.ren, &resumeBtn);
                    SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 1 ? 120 : 70, 70, 75, 255);
                    SDL_RenderFillRect(ctx.ren, &closeBtn);
                }
                SDL_SetRenderDrawColor(ctx.ren, 190, 200, 220, 255);
                SDL_RenderDrawRect(ctx.ren, &resumeBtn);
                SDL_RenderDrawRect(ctx.ren, &closeBtn);
                DrawText(ctx.ren, resumeBtn.x + (resumeBtn.w - MeasureTextWidth(2, "RESUME")) / 2, resumeBtn.y + 18, 2, "RESUME");
                DrawText(ctx.ren, closeBtn.x + (closeBtn.w - MeasureTextWidth(2, "CLOSE")) / 2, closeBtn.y + 18, 2, "CLOSE");
            }
        } else {
            const std::string title = "SETTINGS";
            DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(3, title) / 2, 84, 3, title);
            SDL_Texture* checkboxActiveTex = nullptr;
            SDL_Texture* checkboxInactiveTex = nullptr;
            SDL_Texture* toggleButtonTex = nullptr;
            const Frame* checkboxActive = getMenuFrame("checkbox_active", checkboxActiveTex);
            if (!checkboxActive) checkboxActive = getMenuFrame("checkbox_active.png", checkboxActiveTex);
            const Frame* checkboxInactive = getMenuFrame("checkbox_disabled", checkboxInactiveTex);
            if (!checkboxInactive) checkboxInactive = getMenuFrame("checkbox_disabled.png", checkboxInactiveTex);
            const Frame* toggleButtonFrame = getMenuFrame("button_genreaic", toggleButtonTex);
            if (!toggleButtonFrame) toggleButtonFrame = getMenuFrame("button_genreaic.png", toggleButtonTex);
            auto drawToggleHitbox = [&](int rowIdx, bool selected) {
                SDL_Rect r = settingsRowBtn(rowIdx);
                if (toggleButtonFrame && toggleButtonTex) {
                    renderOpaqueCenterLoopedFrame(toggleButtonTex, *toggleButtonFrame, r);
                } else {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(ctx.ren, selected ? 120 : 70, 95, 85, 255);
                    SDL_RenderFillRect(ctx.ren, &r);
                }
                SDL_SetRenderDrawColor(ctx.ren, selected ? 210 : 140, 190, 170, 255);
                SDL_RenderDrawRect(ctx.ren, &r);
            };
            auto drawToggleCheckbox = [&](int rowIdx, bool enabled) {
                SDL_Rect r = settingsRowBtn(rowIdx);
                const Frame* frame = enabled ? checkboxActive : checkboxInactive;
                SDL_Texture* tex = enabled ? checkboxActiveTex : checkboxInactiveTex;
                if (frame && tex) {
                    SDL_Rect dst{r.x + 8, r.y + 3, 24, 24};
                    renderFrame(ctx.ren, tex, *frame, dst);
                    return;
                }
                SDL_Rect box{r.x + 8, r.y + 6, 18, 18};
                SDL_SetRenderDrawColor(ctx.ren, 180, 200, 230, 255);
                SDL_RenderDrawRect(ctx.ren, &box);
                if (enabled) {
                    SDL_SetRenderDrawColor(ctx.ren, 190, 240, 170, 255);
                    SDL_Rect fill{box.x + 4, box.y + 4, box.w - 8, box.h - 8};
                    SDL_RenderFillRect(ctx.ren, &fill);
                }
            };
            const char* tabNames[kSettingsTabCount] = {"GENERAL", "AUDIO", "DEBUG", "CONTROLS"};
            for (int ti = 0; ti < kSettingsTabCount; ++ti) {
                SDL_Rect tr = settingsTabBtn(ti);
                SDL_SetRenderDrawColor(ctx.ren, settingsTab == ti ? 80 : 50, 90, 120, 255);
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
                    drawToggleHitbox(i, i == settingsSelAudio);
                    if (i == 0) drawToggleCheckbox(i, menuMusicEnabled);
                    if (i == 1) drawToggleCheckbox(i, muteAllAudio);
                    int y = settingsRowY(i);
                    if (i == settingsSelAudio) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 255);
                        SDL_Rect hl{ctx.baseScreenW / 2 - 140, y - 2, 280, 30};
                        SDL_RenderFillRect(ctx.ren, &hl);
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
                    drawToggleHitbox(i, i == settingsSelDebug);
                    if (i == 0) drawToggleCheckbox(i, defaultShowFpsCounter);
                    if (i == 1) drawToggleCheckbox(i, defaultShowDetailedDebugger);
                    if (i == 2) drawToggleCheckbox(i, defaultShowHitboxes);
                    if (i == 3) drawToggleCheckbox(i, defaultShowPlayerHitbox);
                    if (i == 4) drawToggleCheckbox(i, defaultShowDebugView);
                    if (i == 5) drawToggleCheckbox(i, defaultHideUnknownObjectTypes);
                    int y = settingsRowY(i);
                    if (i == settingsSelDebug) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 255);
                        SDL_Rect hl{ctx.baseScreenW / 2 - 140, y - 2, 280, 30};
                        SDL_RenderFillRect(ctx.ren, &hl);
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
                    drawToggleHitbox(i, i == settingsSel);
#if defined(__ANDROID__)
                    if (i == 0) drawToggleCheckbox(i, vsyncEnabled);
                    if (i == 1) drawToggleCheckbox(i, clampCamX);
                    if (i == 2) drawToggleCheckbox(i, defaultShowFpsCounter);
                    if (i == 3) drawToggleCheckbox(i, defaultShowDetailedDebugger);
                    if (i == 4) drawToggleCheckbox(i, defaultShowHitboxes);
                    if (i == 5) drawToggleCheckbox(i, defaultShowPlayerHitbox);
                    if (i == 6) drawToggleCheckbox(i, defaultShowDebugView);
#else
                    if (i == 0) drawToggleCheckbox(i, fullscreen);
                    if (i == 1) drawToggleCheckbox(i, vsyncEnabled);
                    if (i == 2) drawToggleCheckbox(i, clampCamX);
                    if (i == 3) drawToggleCheckbox(i, defaultShowFpsCounter);
                    if (i == 4) drawToggleCheckbox(i, defaultShowDetailedDebugger);
                    if (i == 5) drawToggleCheckbox(i, defaultShowHitboxes);
                    if (i == 6) drawToggleCheckbox(i, defaultShowPlayerHitbox);
                    if (i == 7) drawToggleCheckbox(i, defaultShowDebugView);
#endif
                    int y = settingsRowY(i);
                    if (i == settingsSel) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 255);
                        SDL_Rect hl{ctx.baseScreenW / 2 - 140, y - 2, 280, 30};
                        SDL_RenderFillRect(ctx.ren, &hl);
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
