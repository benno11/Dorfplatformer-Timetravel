#include "FrontendMenu.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <system_error>
#include <string>
#include <unordered_set>
#include <vector>
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif

#include "AssetPath.h"
#include "GameSupport.h"
#include "InputSystem.h"
#include "LevelSelect.h"
#include "TextRenderer.h"
#include "UiScale.h"

FrontendAction runFrontendMenu(FrontendMenuContext& ctx) {
    constexpr const char* kSavedGameSelectionToken = "__DF_SAVEGAME_CONTINUE__";

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
    bool debugModeEnabledLocal = true;
    bool& debugModeEnabled = ctx.debugModeEnabled ? *ctx.debugModeEnabled : debugModeEnabledLocal;
    bool devToolsEnabledLocal = true;
    bool& devToolsEnabled = ctx.devToolsEnabled ? *ctx.devToolsEnabled : devToolsEnabledLocal;
    bool powerManagementEnabledLocal = false;
    bool& powerManagementEnabled = ctx.powerManagementEnabled ? *ctx.powerManagementEnabled : powerManagementEnabledLocal;
    bool lowPowerModeEnabledLocal = false;
    bool& lowPowerModeEnabled = ctx.lowPowerModeEnabled ? *ctx.lowPowerModeEnabled : lowPowerModeEnabledLocal;
    bool showExperimentalFeaturesLocal = false;
    bool& showExperimentalFeatures = ctx.showExperimentalFeatures ? *ctx.showExperimentalFeatures : showExperimentalFeaturesLocal;
    bool levelSelectEnabledLocal = true;
    bool& levelSelectEnabled = ctx.levelSelectEnabled ? *ctx.levelSelectEnabled : levelSelectEnabledLocal;
    bool& menuMusicEnabled = *ctx.menuMusicEnabled;
    bool& muteAllAudio = *ctx.muteAllAudio;
    auto updaterStatusText = [&]() -> std::string {
        if (ctx.getUpdaterStatusText) {
            const std::string text = ctx.getUpdaterStatusText();
            if (!text.empty()) return text;
        }
        return ctx.windowsUpdateManifestUrl.empty() ? "NOT CONFIGURED" : "READY";
    };
    auto updaterStatusDetail = [&]() -> std::string {
        if (ctx.getUpdaterStatusDetail) {
            const std::string text = ctx.getUpdaterStatusDetail();
            if (!text.empty()) return text;
        }
        return std::string();
    };
    auto updaterLatestVersionText = [&]() -> std::string {
        if (ctx.getUpdaterLatestVersionText) {
            const std::string text = ctx.getUpdaterLatestVersionText();
            if (!text.empty()) return text;
        }
        return std::string();
    };
    auto updaterNotesText = [&]() -> std::string {
        if (ctx.getUpdaterNotesText) {
            const std::string text = ctx.getUpdaterNotesText();
            if (!text.empty()) return text;
        }
        return std::string();
    };
    auto updaterProgress01 = [&]() -> float {
        if (ctx.getUpdaterProgress01) {
            return std::clamp(ctx.getUpdaterProgress01(), 0.0f, 1.0f);
        }
        return 0.0f;
    };
    SDL_Scancode& keyMoveLeft = *ctx.keyMoveLeft;
    SDL_Scancode& keyMoveRight = *ctx.keyMoveRight;
    SDL_Scancode& keyMoveDown = *ctx.keyMoveDown;
    SDL_Scancode& keyJump = *ctx.keyJump;
    SDL_Scancode& keyPause = *ctx.keyPause;
    int& musicVolume = *ctx.musicVolume;
    int& sfxVolume = *ctx.sfxVolume;
    int& uiScalePercent = *ctx.uiScalePercent;
    int uiEdgePaddingLocal = 0;
    int& uiEdgePadding = ctx.uiEdgePadding ? *ctx.uiEdgePadding : uiEdgePaddingLocal;
    std::string networkServerUrlLocal;
    std::string networkAuthTokenLocal;
    std::string networkUsernameLocal;
    std::string accountManagerUrlLocal = "https://benno111.github.io/Dorfplatformer-API/";
    std::string firebaseApiKeyLocal;
    std::string& levelServerUrl = ctx.levelServerUrl ? *ctx.levelServerUrl : networkServerUrlLocal;
    std::string& levelServerAuthToken = ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : networkAuthTokenLocal;
    std::string& levelServerAccountUsername = ctx.levelServerAccountUsername ? *ctx.levelServerAccountUsername : networkUsernameLocal;
    std::string& accountManagerUrl = ctx.accountManagerUrl ? *ctx.accountManagerUrl : accountManagerUrlLocal;
    std::string& firebaseApiKey = ctx.firebaseApiKey ? *ctx.firebaseApiKey : firebaseApiKeyLocal;
    int activeSaveSlotIndexLocal = 0;
    int& activeSaveSlotIndex = ctx.activeSaveSlotIndex ? *ctx.activeSaveSlotIndex : activeSaveSlotIndexLocal;
    auto uiButtonScale = [&]() -> float {
        return UiScale::multiplier(uiScalePercent);
    };
    auto settingsMenuScale = [&]() -> float {
        return UiScale::settingsMultiplier(uiScalePercent);
    };
    std::vector<std::string> settingsTabLabels = {
        "GENERAL", "AUDIO", "DEBUG", "CONTROLS", "ABOUT",
        "FILE INFO", "SAVES", "GRAPHICS", "GAMEPLAY", "ACCESSIBILITY",
        "USER PROFILE", "PRIVACY", "UPDATER"
    };
    std::vector<std::string> controlsLabels = {
        "MOVE LEFT", "MOVE RIGHT", "MOVE DOWN", "JUMP", "PAUSE", "BACK"
    };
    std::vector<std::string> audioLabels = {
        "MENU MUSIC", "MUTE ALL", "MUSIC", "SFX", "BACK"
    };
    std::vector<std::string> debugLabels = {
        "DETAILED DEBUGGER", "SHOW HITBOXES", "PLAYER HITBOX",
        "DEBUG HUD", "HIDE UNKNOWN OBJECT TYPES", "BACK"
    };
    {
        const std::string menuJsonText = ReadTextFile("assets/menus/settings_menu.json");
        if (!menuJsonText.empty()) {
            try {
                nlohmann::json j = nlohmann::json::parse(menuJsonText);
                auto readStringArray = [&](const char* key, std::vector<std::string>& out, int expectedMin) {
                    if (!j.contains(key) || !j[key].is_array()) return;
                    std::vector<std::string> tmp;
                    for (const auto& v : j[key]) {
                        if (v.is_string()) tmp.push_back(v.get<std::string>());
                    }
                    if ((int)tmp.size() >= expectedMin) out = std::move(tmp);
                };
                readStringArray("tabs", settingsTabLabels, 13);
                readStringArray("controls_rows", controlsLabels, 6);
                readStringArray("audio_rows", audioLabels, 5);
                readStringArray("debug_rows", debugLabels, 6);
            } catch (...) {}
        }
    }
    if ((int)settingsTabLabels.size() < 13) settingsTabLabels.resize(13, "TAB");
    settingsTabLabels[6] = settingsTabLabels[6] == "TAB" ? "SAVES" : settingsTabLabels[6];
    settingsTabLabels[12] = settingsTabLabels[12] == "TAB" ? "UPDATER" : settingsTabLabels[12];
    constexpr float kMenuAuthorW = 960.0f;
    constexpr float kMenuAuthorH = 540.0f;
    auto menuCanvasScale = [&]() -> float {
        const float sx = (float)ctx.baseScreenW / kMenuAuthorW;
        const float sy = (float)ctx.baseScreenH / kMenuAuthorH;
        return std::max(0.5f, std::min(sx, sy));
    };
    auto menuCanvasOriginX = [&]() -> float {
        const float s = menuCanvasScale();
        return ((float)ctx.baseScreenW - kMenuAuthorW * s) * 0.5f;
    };
    auto menuCanvasOriginY = [&]() -> float {
        const float s = menuCanvasScale();
        return ((float)ctx.baseScreenH - kMenuAuthorH * s) * 0.5f;
    };
    auto mainMenuScale = [&]() -> float {
        const float canvas = menuCanvasScale();
        const float desired = uiButtonScale();
        const float desiredBtnW = 112.0f * canvas * desired;
        const float desiredGap = 44.0f * canvas * desired;
        const float desiredTotalW = desiredBtnW * 3.0f + desiredGap * 2.0f;
        const float maxTotalW = std::max(260.0f, (float)ctx.baseScreenW - 120.0f);
        const float fit = (desiredTotalW > 0.0f) ? std::min(1.0f, maxTotalW / desiredTotalW) : 1.0f;
        return std::clamp(desired * fit, 0.55f, 5.0f);
    };
    auto scaleRectCentered = [&](const SDL_Rect& in, float scale) -> SDL_Rect {
        return UiScale::scaleRectCentered(in, scale);
    };

    auto applyRenderVsync = [&]() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (SDL_RenderSetVSync(ctx.ren, vsyncEnabled ? 1 : 0) != 0) {
            SDL_Log("Could not set renderer VSync=%d: %s", vsyncEnabled ? 1 : 0, SDL_GetError());
        }
#endif
    };
    auto applyFullscreen = [&](bool enabled) -> bool {
#if defined(__ANDROID__)
        (void)enabled;
        return false;
#else
        static int restoreX = SDL_WINDOWPOS_CENTERED;
        static int restoreY = SDL_WINDOWPOS_CENTERED;
        static int restoreW = 1280;
        static int restoreH = 720;
        const bool currentlyFullscreen = (SDL_GetWindowFlags(ctx.win) & SDL_WINDOW_FULLSCREEN) != 0;
        if (enabled && !currentlyFullscreen) {
            SDL_GetWindowPosition(ctx.win, &restoreX, &restoreY);
            SDL_GetWindowSize(ctx.win, &restoreW, &restoreH);
        }
        if (!SDL_SetWindowFullscreen(ctx.win, enabled)) {
            SDL_Log("Could not set menu fullscreen=%d: %s", enabled ? 1 : 0, SDL_GetError());
            return false;
        }
        if (!enabled) {
            if (restoreW <= 0 || restoreH <= 0) {
                restoreW = std::max(960, ctx.baseScreenW / 2);
                restoreH = std::max(540, ctx.baseScreenH / 2);
            }
            SDL_SetWindowSize(ctx.win, restoreW, restoreH);
            SDL_SetWindowPosition(ctx.win, restoreX, restoreY);
        }
        fullscreen = enabled;
        return true;
#endif
    };

    bool inSettings = false;
    bool closeMenuOpen = false;
    int closeMenuSel = 0; // 0 Resume, 1 Close Game
    int menuSel = -1;    // 0 Settings, 1 Play, 2 Editor
    int settingsTab = 0; // General, Audio, Debug, Controls, About, Saves, Extra categories
    int settingsSelAudio = 0;
    int settingsSelDebug = 0;
    int settingsSelControls = 0;
    int settingsSelNetwork = 0;
    int settingsSelSaves = 0;
    bool startSavedGameRequested = false;
    std::string networkLoginEmail;
    std::string networkLoginPassword;
    std::size_t networkLoginEmailCursor = 0;
    std::size_t networkLoginPasswordCursor = 0;
    bool networkCursorPreset = false;
    std::string networkLoginStatus;
    bool waitingForControlKey = false;
    int waitingControlIndex = -1;
    enum class NetworkEditField {
        None,
        LoginEmail,
        LoginPassword
    };
    NetworkEditField networkEditField = NetworkEditField::None;
    bool pendingSettingsExitCleanup = false;
    Uint64 inputBlockUntilTicks = 0;
    constexpr Uint64 kMenuInputBlockMs = 1000;
    constexpr int kSettingsTabCount = 13;
    constexpr int IDX_SETTINGS_GENERAL = 0;
    constexpr int IDX_SETTINGS_AUDIO = 1;
    constexpr int IDX_SETTINGS_DEBUG = 2;
    constexpr int IDX_SETTINGS_CONTROLS = 3;
    constexpr int IDX_SETTINGS_ABOUT = 4;
    constexpr int IDX_SETTINGS_FILEINFO = 5;
    constexpr int IDX_SAVES_TAB = 6;
    constexpr int IDX_SETTINGS_GRAPHICS = 7;
    constexpr int IDX_SETTINGS_GAMEPLAY = 8;
    constexpr int IDX_SETTINGS_ACCESSIBILITY = 9;
    constexpr int IDX_SETTINGS_ACCOUNT = 10;
    constexpr int IDX_SETTINGS_PRIVACY = 11;
    constexpr int kExtraTabStart = 7;
    constexpr int kExtraTabCount = 5;
    constexpr int IDX_UPDATER_TAB = 12;
    constexpr int kExtraTabOptionCount = 11;
    bool localExtraTabValues[kExtraTabCount][kExtraTabOptionCount] = {};
    auto extraTabValueRef = [&](int tabIdx, int optIdx) -> bool& {
        const int t = std::clamp(tabIdx, 0, kExtraTabCount - 1);
        const int o = std::clamp(optIdx, 0, kExtraTabOptionCount - 1);
        const int flat = t * kExtraTabOptionCount + o;
        if (ctx.extraSettings && ctx.extraSettingsCount >= (kExtraTabCount * kExtraTabOptionCount)) {
            return ctx.extraSettings[flat];
        }
        return localExtraTabValues[t][o];
    };
    const char* extraTabNames[kExtraTabCount] = {"GRAPHICS", "GAMEPLAY", "ACCESSIBILITY", "USER PROFILE", "PRIVACY"};
    const char* extraTabOptionLabels[kExtraTabCount][kExtraTabOptionCount] = {
        {"BLOOM", "MOTION BLUR", "FILM GRAIN", "CHROMATIC ABERRATION", "SHADOW BOOST", "LIGHT SHAFTS", "WATER REFLECTIONS", "PARTICLE DENSITY+", "DISTANT DETAIL", "RETRO PIXEL FILTER", "SCREEN SHAKE+"},
        {"AUTO SAVE", "AUTO CHECKPOINT", "TUTORIAL HINTS", "SMART CAMERA", "MAGNET COINS", "SLOW-MO ON DEATH", "ASSISTED JUMPS", "EXTENDED INVULN FRAMES", "QUICK RESTART", "ENEMY AGGRO REDUCE", "DROP SAFE MODE"},
        {"HIGH CONTRAST UI", "COLORBLIND MODE", "LARGE TEXT", "OUTLINE PLAYER", "SCREEN READER CUES", "SUBTITLES+", "FLASH REDUCTION", "INPUT HOLD ASSIST", "AIM ASSIST", "MENU NARRATION", "CAPTION BACKGROUND"},
        {"AUTO RETRY CONNECTION", "LIMIT BACKGROUND FETCH", "SYNC CLOUD PROGRESS", "USE CDN MIRROR", "LOW BANDWIDTH MODE", "UPLOAD CRASH LOGS", "PING DIAGNOSTICS", "NET DEBUG OVERLAY", "FORCE IPV4", "TLS STRICT MODE", "CACHE REMOTE LEVELS"},
        {"SEND ANONYMOUS METRICS", "PERSONALIZED CONTENT", "SESSION TELEMETRY", "ERROR REPORT DETAILS", "LOCAL HISTORY LOG", "CROSS-DEVICE IDS", "ALLOW SOCIAL FEATURES", "FRIEND PRESENCE", "SHOW ONLINE STATUS", "DELETE TEMP DATA ON EXIT", "PRIVACY LOCKDOWN"}
    };
    auto isExtraTab = [&](int tab) -> bool { return tab >= kExtraTabStart && tab < IDX_UPDATER_TAB; };
    auto extraTabIndex = [&](int tab) -> int { return std::clamp(tab - kExtraTabStart, 0, kExtraTabCount - 1); };
    auto extraTabUsedOptionCount = [&](int tab) -> int {
        // Keep only wired options visible.
        // PRIVACY currently has one hooked option: SEND ANONYMOUS METRICS.
        if (tab == IDX_SETTINGS_PRIVACY) return 1;
        return 0;
    };
    auto extraTabOptionAtVisibleRow = [&](int tab, int row) -> int {
        if (tab == IDX_SETTINGS_PRIVACY && row == 0) return 0;
        return -1;
    };
    auto extraTabRowCountForTab = [&](int tab) -> int {
        const int used = extraTabUsedOptionCount(tab);
        if (used <= 0) return 0;
        return used + 1; // + BACK
    };
    auto tabIsVisible = [&](int tab) -> bool {
        if (tab == IDX_SETTINGS_DEBUG) return false;
        if (tab == IDX_SETTINGS_ACCOUNT) return true; // Account hosts account manager UI.
        if (tab == IDX_UPDATER_TAB) return true;
        if (!isExtraTab(tab)) return true;
        return showExperimentalFeatures && extraTabUsedOptionCount(tab) > 0;
    };
    auto visibleTabList = [&]() -> std::vector<int> {
        std::vector<int> tabs;
        for (int i = 0; i < kSettingsTabCount; ++i) {
            if (tabIsVisible(i)) tabs.push_back(i);
        }
        return tabs;
    };
    auto sidebarTabList = [&]() -> std::vector<int> {
        return visibleTabList();
    };
    auto firstVisibleTab = [&]() -> int {
        for (int i = 0; i < kSettingsTabCount; ++i) {
            if (tabIsVisible(i)) return i;
        }
        return 0;
    };
    auto normalizeSettingsTab = [&]() {
        if (!tabIsVisible(settingsTab)) settingsTab = firstVisibleTab();
    };
    auto blockMenuInput = [&]() {
        inputBlockUntilTicks = SDL_GetTicks() + kMenuInputBlockMs;
    };
    auto setInSettings = [&](bool value) {
        if (inSettings != value) {
            blockMenuInput();
            if (!value) pendingSettingsExitCleanup = true;
        }
        if (!value && networkEditField != NetworkEditField::None) {
            networkEditField = NetworkEditField::None;
            SDL_StopTextInput(ctx.win);
        }
        inSettings = value;
    };
    auto setCloseMenuOpen = [&](bool value) {
        if (closeMenuOpen != value) blockMenuInput();
        closeMenuOpen = value;
    };
    auto setSettingsTab = [&](int value) {
        settingsTab = std::clamp(value, 0, kSettingsTabCount - 1);
        normalizeSettingsTab();
    };
#if defined(__ANDROID__)
    constexpr int IDX_VSYNC = 1;
    constexpr int IDX_CAM_CLAMP = 1;
    constexpr int IDX_UI_SCALE = 2;
    constexpr int IDX_UI_EDGE_PADDING = 3;
    constexpr int IDX_DEBUG_MODE = 4;
    constexpr int IDX_SHOW_FPS = 5;
    constexpr int IDX_SHOW_DETAILED = 6;
    constexpr int IDX_SHOW_HITBOXES = 7;
    constexpr int IDX_SHOW_PLAYER_HITBOX = 8;
    constexpr int IDX_SHOW_DEBUG_VIEW = 9;
    constexpr int IDX_POWER_MANAGEMENT = 10;
    constexpr int IDX_LOW_POWER_MODE = 11;
    constexpr int IDX_MUSIC = 12;
    constexpr int IDX_SFX = 13;
    constexpr int IDX_SHOW_EXPERIMENTAL = 14;
    constexpr int IDX_LEVEL_SELECT = 15;
    constexpr int IDX_ABOUT = 16;
    constexpr int IDX_BACK = 17;
    constexpr int kSettingsCount = 18;
#else
#if defined(_WIN32)
    constexpr int IDX_FULLSCREEN = 0;
    constexpr int IDX_VSYNC = 0;
    constexpr int IDX_CAM_CLAMP = 2;
    constexpr int IDX_UI_SCALE = 3;
    constexpr int IDX_UI_EDGE_PADDING = 4;
    constexpr int IDX_DEBUG_MODE = 5;
    constexpr int IDX_SHOW_FPS = 6;
    constexpr int IDX_SHOW_DETAILED = 7;
    constexpr int IDX_SHOW_HITBOXES = 8;
    constexpr int IDX_SHOW_PLAYER_HITBOX = 9;
    constexpr int IDX_SHOW_DEBUG_VIEW = 10;
    constexpr int IDX_POWER_MANAGEMENT = 11;
    constexpr int IDX_LOW_POWER_MODE = 12;
    constexpr int IDX_MUSIC = 13;
    constexpr int IDX_SFX = 14;
    constexpr int IDX_SHOW_EXPERIMENTAL = 15;
    constexpr int IDX_LEVEL_SELECT = 16;
    constexpr int IDX_UPDATE = 17;
    constexpr int IDX_ABOUT = 18;
    constexpr int IDX_BACK = 19;
    constexpr int kSettingsCount = 20;
#else
    constexpr int IDX_FULLSCREEN = 0;
    constexpr int IDX_VSYNC = 0;
    constexpr int IDX_CAM_CLAMP = 2;
    constexpr int IDX_UI_SCALE = 3;
    constexpr int IDX_UI_EDGE_PADDING = 4;
    constexpr int IDX_DEBUG_MODE = 5;
    constexpr int IDX_SHOW_FPS = 6;
    constexpr int IDX_SHOW_DETAILED = 7;
    constexpr int IDX_SHOW_HITBOXES = 8;
    constexpr int IDX_SHOW_PLAYER_HITBOX = 9;
    constexpr int IDX_SHOW_DEBUG_VIEW = 10;
    constexpr int IDX_POWER_MANAGEMENT = 11;
    constexpr int IDX_LOW_POWER_MODE = 12;
    constexpr int IDX_MUSIC = 13;
    constexpr int IDX_SFX = 14;
    constexpr int IDX_SHOW_EXPERIMENTAL = 15;
    constexpr int IDX_LEVEL_SELECT = 16;
    constexpr int IDX_ABOUT = 17;
    constexpr int IDX_BACK = 18;
    constexpr int kSettingsCount = 19;
#endif
#endif
    int settingsSel = 0;
    enum class SliderDragTarget { None, Music, Sfx, UiScale, UiEdgePadding };
    SliderDragTarget sliderDrag = SliderDragTarget::None;
    SDL_FingerID sliderDragFinger = 0;
    enum class ScrollbarDragTarget { None, Settings, About };
    ScrollbarDragTarget scrollbarDrag = ScrollbarDragTarget::None;
    SDL_FingerID scrollbarDragFinger = 0;
    auto applySettingsExitCleanup = [&]() {
        if (!pendingSettingsExitCleanup) return;
        pendingSettingsExitCleanup = false;
        // Ensure main menu resumes in an interactive state after leaving settings.
        if (menuSel < 0 || menuSel > 2) menuSel = 1;
        sliderDrag = SliderDragTarget::None;
        sliderDragFinger = 0;
        scrollbarDrag = ScrollbarDragTarget::None;
        scrollbarDragFinger = 0;
        waitingForControlKey = false;
        waitingControlIndex = -1;
    };
    Uint64 lastTouchDownTicks = 0;
    int lastTouchDownWinX = -100000;
    int lastTouchDownWinY = -100000;
    constexpr Uint64 kSyntheticMouseSuppressMs = 220;
    constexpr int kSyntheticMouseSuppressDistPx = 36;
    std::unordered_set<SDL_FingerID> activeTouchFingers;
    SDL_Event e;
    const int settingsStartY = 201;
    const int settingsRowH = 36;
    const int settingsTabY = 118;
    const int settingsTabW = 120;
    const int settingsTabH = 30;
    const int settingsListTop = settingsStartY;
    const int settingsListBottom = ctx.baseScreenH - 24;
    int settingsScrollY = 0;
    int aboutScrollY = 0;
    bool showOptionalSidebar = ctx.showOptionalSidebar ? *ctx.showOptionalSidebar : true;
    int aboutContentBottomY = 558;
    auto setOptionalSidebar = [&](bool value) {
        if (showOptionalSidebar == value) return;
        showOptionalSidebar = value;
        if (ctx.showOptionalSidebar) *ctx.showOptionalSidebar = value;
        if (ctx.saveClientSettings) ctx.saveClientSettings();
    };
    auto aboutMaxScroll = [&]() -> int {
        return std::max(0, aboutContentBottomY - settingsListBottom);
    };
    auto clampAboutScroll = [&]() {
        aboutScrollY = std::clamp(aboutScrollY, 0, aboutMaxScroll());
    };
    auto scrollAboutBy = [&](int dy) {
        aboutScrollY += dy;
        clampAboutScroll();
    };
    auto generalSettingsRowHidden = [&](int idx) -> bool {
        return idx == IDX_DEBUG_MODE ||
               idx == IDX_SHOW_DETAILED ||
               idx == IDX_SHOW_HITBOXES ||
               idx == IDX_SHOW_PLAYER_HITBOX ||
               idx == IDX_SHOW_DEBUG_VIEW;
    };
    auto generalSettingsRowCount = [&]() -> int {
        int count = 0;
        for (int i = 0; i < kSettingsCount; ++i) {
            if (!generalSettingsRowHidden(i)) ++count;
        }
        return count;
    };
    auto settingsRowsForTab = [&](int tab) -> int {
        if (tab == IDX_SETTINGS_AUDIO) return 5;
        if (tab == IDX_SETTINGS_DEBUG) return 6;
        if (tab == IDX_SETTINGS_CONTROLS) return 6;
        if (tab == IDX_SETTINGS_FILEINFO) return 1;
        if (tab == IDX_SAVES_TAB) return 5; // slot 1-3, load active, back
        if (tab == IDX_SETTINGS_ACCOUNT) {
            const bool hasUser = !levelServerAccountUsername.empty();
            const bool hasToken = !levelServerAuthToken.empty();
            const bool invalidLogin = (hasUser && !hasToken);
            if (invalidLogin) return 4; // status, repair, open, back
            if (hasUser && hasToken) return 4; // username, logout, open, back
            return 5; // email, password, sign in, open, back
        }
        if (tab == IDX_UPDATER_TAB) return 3;
        if (tab == 4) return 0;
        if (isExtraTab(tab)) return extraTabRowCountForTab(tab);
        return generalSettingsRowCount();
    };
    auto visibleGeneralSettingsRowIndex = [&](int rawIdx) -> int {
        int visibleIdx = 0;
        for (int i = 0; i < rawIdx; ++i) {
            if (!generalSettingsRowHidden(i)) ++visibleIdx;
        }
        return visibleIdx;
    };
    auto generalSettingsRawIndexFromVisible = [&](int visibleIdx) -> int {
        int currentVisible = 0;
        for (int rawIdx = 0; rawIdx < kSettingsCount; ++rawIdx) {
            if (generalSettingsRowHidden(rawIdx)) continue;
            if (currentVisible == visibleIdx) return rawIdx;
            ++currentVisible;
        }
        return IDX_BACK;
    };
    auto activeSettingsSelectionRef = [&]() -> int& {
        if (settingsTab == IDX_SETTINGS_AUDIO) return settingsSelAudio;
        if (settingsTab == IDX_SETTINGS_DEBUG) return settingsSelDebug;
        if (settingsTab == IDX_SETTINGS_CONTROLS) return settingsSelControls;
        if (settingsTab == IDX_SAVES_TAB) return settingsSelSaves;
        if (settingsTab == IDX_SETTINGS_ACCOUNT) return settingsSelNetwork;
        return settingsSel;
    };
    auto keyNameForDisplay = [&](SDL_Scancode sc) -> std::string {
        const char* n = SDL_GetScancodeName(sc);
        if (!n || !*n) return "UNBOUND";
        return n;
    };
    auto sanitizeAccountUsername = [](const std::string& in) -> std::string {
        std::string out;
        out.reserve(in.size());
        for (char ch : in) {
            const bool alphaNum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
            if (alphaNum || ch == '_' || ch == '-') out.push_back(ch);
        }
        if (out.size() > 48) out.resize(48);
        return out;
    };
#if defined(__ANDROID__)
    auto showVirtualKeyboard = [&](int x, int y, int w, int h) {
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
    };
    auto hideVirtualKeyboard = [&]() {
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
    };
#endif
    auto stopNetworkEditing = [&]() {
        if (networkEditField == NetworkEditField::None) return;
        networkEditField = NetworkEditField::None;
        SDL_StopTextInput(ctx.win);
#if defined(__ANDROID__)
        hideVirtualKeyboard();
#endif
    };
    auto beginNetworkEditing = [&](NetworkEditField field, const SDL_Rect* focusGameRect = nullptr) {
        networkEditField = field;
        if (!networkCursorPreset) {
            if (field == NetworkEditField::LoginEmail) networkLoginEmailCursor = networkLoginEmail.size();
            if (field == NetworkEditField::LoginPassword) networkLoginPasswordCursor = networkLoginPassword.size();
        }
        networkCursorPreset = false;
        SDL_StartTextInput(ctx.win);
#if defined(__ANDROID__)
        int winW = 0;
        int winH = 0;
        getWindowSizeInPixelsCompat(ctx.win, winW, winH);
        if (focusGameRect && ctx.baseScreenW > 0 && ctx.baseScreenH > 0 && winW > 0 && winH > 0) {
            SDL_Rect focusRect = *focusGameRect;
            const int focusPad = 12;
            focusRect.x -= focusPad;
            focusRect.y -= focusPad;
            focusRect.w += focusPad * 2;
            focusRect.h += focusPad * 2;

            SDL_Rect present = computePresentRect(winW, winH, ctx.baseScreenW, ctx.baseScreenH, 1.0f);
            if (present.w > 0 && present.h > 0) {
                const float sx = (float)present.w / (float)ctx.baseScreenW;
                const float sy = (float)present.h / (float)ctx.baseScreenH;
                int wx = present.x + (int)std::floor((float)focusRect.x * sx);
                int wy = present.y + (int)std::floor((float)focusRect.y * sy);
                int ww = std::max(1, (int)std::ceil((float)focusRect.w * sx));
                int wh = std::max(1, (int)std::ceil((float)focusRect.h * sy));
                wx = std::clamp(wx, 0, std::max(0, winW - 1));
                wy = std::clamp(wy, 0, std::max(0, winH - 1));
                ww = std::min(ww, std::max(1, winW - wx));
                wh = std::min(wh, std::max(1, winH - wy));
                showVirtualKeyboard(wx, wy, ww, wh);
            } else {
                showVirtualKeyboard(0, 0, std::max(1, winW), std::max(1, winH));
            }
        } else {
            showVirtualKeyboard(0, 0, std::max(1, winW), std::max(1, winH));
        }
#endif
    };
    auto utf8Prev = [](const std::string& s, std::size_t pos) -> std::size_t {
        if (pos == 0) return 0;
        std::size_t i = pos - 1;
        while (i > 0 && ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)) --i;
        return i;
    };
    auto utf8Next = [](const std::string& s, std::size_t pos) -> std::size_t {
        if (pos >= s.size()) return s.size();
        std::size_t i = pos + 1;
        while (i < s.size() && ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)) ++i;
        return i;
    };
    auto appendNetworkInput = [&](const std::string& text) {
        if (text.empty()) return;
        std::string* target = nullptr;
        std::size_t* cursor = nullptr;
        if (networkEditField == NetworkEditField::LoginEmail) { target = &networkLoginEmail; cursor = &networkLoginEmailCursor; }
        if (networkEditField == NetworkEditField::LoginPassword) { target = &networkLoginPassword; cursor = &networkLoginPasswordCursor; }
        if (!target || !cursor) return;
        if (*cursor > target->size()) *cursor = target->size();
        target->insert(*cursor, text);
        *cursor += text.size();
        while (target->size() > 256) {
            const std::size_t cut = utf8Prev(*target, target->size());
            target->erase(cut);
            if (*cursor > target->size()) *cursor = target->size();
        }
    };
    auto popNetworkInput = [&]() {
        std::string* target = nullptr;
        std::size_t* cursor = nullptr;
        if (networkEditField == NetworkEditField::LoginEmail) { target = &networkLoginEmail; cursor = &networkLoginEmailCursor; }
        if (networkEditField == NetworkEditField::LoginPassword) { target = &networkLoginPassword; cursor = &networkLoginPasswordCursor; }
        if (!target || !cursor || target->empty()) return;
        if (*cursor > target->size()) *cursor = target->size();
        if (*cursor == 0) return;
        const std::size_t left = utf8Prev(*target, *cursor);
        target->erase(left, *cursor - left);
        *cursor = left;
    };
    auto deleteNetworkInputForward = [&]() {
        std::string* target = nullptr;
        std::size_t* cursor = nullptr;
        if (networkEditField == NetworkEditField::LoginEmail) { target = &networkLoginEmail; cursor = &networkLoginEmailCursor; }
        if (networkEditField == NetworkEditField::LoginPassword) { target = &networkLoginPassword; cursor = &networkLoginPasswordCursor; }
        if (!target || !cursor || target->empty()) return;
        if (*cursor > target->size()) *cursor = target->size();
        if (*cursor >= target->size()) return;
        const std::size_t right = utf8Next(*target, *cursor);
        target->erase(*cursor, right - *cursor);
    };
    auto moveNetworkCursor = [&](int dir) {
        std::string* target = nullptr;
        std::size_t* cursor = nullptr;
        if (networkEditField == NetworkEditField::LoginEmail) { target = &networkLoginEmail; cursor = &networkLoginEmailCursor; }
        if (networkEditField == NetworkEditField::LoginPassword) { target = &networkLoginPassword; cursor = &networkLoginPasswordCursor; }
        if (!target || !cursor) return;
        if (*cursor > target->size()) *cursor = target->size();
        if (dir < 0) *cursor = utf8Prev(*target, *cursor);
        if (dir > 0) *cursor = utf8Next(*target, *cursor);
    };
    auto moveNetworkCursorToEdge = [&](bool toEnd) {
        if (networkEditField == NetworkEditField::LoginEmail) networkLoginEmailCursor = toEnd ? networkLoginEmail.size() : 0;
        if (networkEditField == NetworkEditField::LoginPassword) networkLoginPasswordCursor = toEnd ? networkLoginPassword.size() : 0;
    };
    auto maskedPassword = [](const std::string& password) -> std::string {
        if (password.empty()) return "<empty>";
        return std::string(password.size(), '*');
    };
    auto loginWithFirebase = [&](std::string& errOut) -> bool {
        errOut.clear();
        const std::string email = networkLoginEmail;
        const std::string password = networkLoginPassword;
        SDL_Log("ACCOUNT: sign-in requested email=%s", email.c_str());
        if (email.empty() || password.empty()) {
            errOut = "Email/password required.";
            SDL_Log("ACCOUNT: sign-in rejected (missing email/password)");
            return false;
        }
        std::string effectiveApiKey = firebaseApiKey;
        if (effectiveApiKey.empty()) {
            const std::string cfgText = ReadTextFile("assets/config.json");
            if (!cfgText.empty()) {
                try {
                    const nlohmann::json cfgJson = nlohmann::json::parse(cfgText);
                    if (cfgJson.is_object() && cfgJson.contains("firebase_api_key") && cfgJson["firebase_api_key"].is_string()) {
                        effectiveApiKey = cfgJson["firebase_api_key"].get<std::string>();
                        firebaseApiKey = effectiveApiKey;
                    }
                } catch (...) {}
            }
        }
        if (effectiveApiKey.empty()) {
            errOut = "firebase_api_key missing in config.";
            SDL_Log("ACCOUNT: sign-in rejected (missing firebase_api_key after config fallback)");
            return false;
        }
#if defined(__ANDROID__)
        {
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            if (env) {
                jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
                if (!cls) {
                    if (env->ExceptionCheck()) env->ExceptionClear();
                } else {
                    jmethodID mid = env->GetStaticMethodID(
                        cls, "firebaseSignIn",
                        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
                    if (mid) {
                        jstring jApi = env->NewStringUTF(effectiveApiKey.c_str());
                        jstring jEmail = env->NewStringUTF(email.c_str());
                        jstring jPass = env->NewStringUTF(password.c_str());
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                        }
                        jobject jRespObj = env->CallStaticObjectMethod(cls, mid, jApi, jEmail, jPass, (jint)12000);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                        }
                        if (jApi) env->DeleteLocalRef(jApi);
                        if (jEmail) env->DeleteLocalRef(jEmail);
                        if (jPass) env->DeleteLocalRef(jPass);
                        std::string respBody;
                        if (jRespObj) {
                            jstring jResp = static_cast<jstring>(jRespObj);
                            const char* cResp = env->GetStringUTFChars(jResp, nullptr);
                            if (cResp) {
                                respBody = cResp;
                                env->ReleaseStringUTFChars(jResp, cResp);
                            }
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                            }
                            env->DeleteLocalRef(jRespObj);
                        }
                        env->DeleteLocalRef(cls);
                        if (!respBody.empty()) {
                            nlohmann::json resp;
                            try { resp = nlohmann::json::parse(respBody); } catch (...) { resp = nlohmann::json(); }
                            if (resp.is_object() && resp.contains("idToken") && resp["idToken"].is_string()) {
                                levelServerAuthToken = resp["idToken"].get<std::string>();
                                if (resp.contains("displayName") && resp["displayName"].is_string()) {
                                    levelServerAccountUsername = sanitizeAccountUsername(resp["displayName"].get<std::string>());
                                }
                                if (levelServerAccountUsername.empty()) {
                                    const std::size_t atPos = email.find('@');
                                    const std::string localName = (atPos == std::string::npos) ? email : email.substr(0, atPos);
                                    levelServerAccountUsername = sanitizeAccountUsername(localName);
                                }
                                SDL_Log("ACCOUNT: sign-in ok (Java) email=%s username=%s",
                                        email.c_str(), levelServerAccountUsername.c_str());
                                return true;
                            }
                            if (resp.is_object() && resp.contains("error") && resp["error"].is_object() &&
                                resp["error"].contains("message") && resp["error"]["message"].is_string()) {
                                errOut = resp["error"]["message"].get<std::string>();
                                SDL_Log("ACCOUNT: sign-in failed (Java) email=%s reason=%s", email.c_str(), errOut.c_str());
                                return false;
                            }
                        }
                    } else {
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        env->DeleteLocalRef(cls);
                    }
                }
            }
        }
#endif
#if defined(HAVE_CURL) && HAVE_CURL
        CURL* curl = curl_easy_init();
        if (!curl) {
            errOut = "curl init failed.";
            return false;
        }
        const std::string url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + effectiveApiKey;
        nlohmann::json req;
        req["email"] = email;
        req["password"] = password;
        req["returnSecureToken"] = true;
        const std::string body = req.dump();
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string respBody;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
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
        if (rc != CURLE_OK || code < 200 || code >= 300) {
            if (!respBody.empty()) {
                try {
                    const nlohmann::json errJson = nlohmann::json::parse(respBody);
                    if (errJson.is_object() && errJson.contains("error") && errJson["error"].is_object() &&
                        errJson["error"].contains("message") && errJson["error"]["message"].is_string()) {
                        errOut = errJson["error"]["message"].get<std::string>();
                    }
                } catch (...) {}
            }
            if (errOut.empty()) errOut = "Login failed.";
            SDL_Log("ACCOUNT: sign-in failed email=%s http=%ld curl=%d reason=%s",
                    email.c_str(), code, (int)rc, errOut.c_str());
            return false;
        }
        nlohmann::json resp;
        try { resp = nlohmann::json::parse(respBody); } catch (...) { resp = nlohmann::json(); }
        if (!resp.is_object() || !resp.contains("idToken") || !resp["idToken"].is_string()) {
            errOut = "Invalid login response.";
            SDL_Log("ACCOUNT: sign-in failed email=%s reason=%s", email.c_str(), errOut.c_str());
            return false;
        }
        levelServerAuthToken = resp["idToken"].get<std::string>();
        if (resp.contains("displayName") && resp["displayName"].is_string()) {
            levelServerAccountUsername = sanitizeAccountUsername(resp["displayName"].get<std::string>());
        }
        if (levelServerAccountUsername.empty()) {
            const std::size_t atPos = email.find('@');
            const std::string localName = (atPos == std::string::npos) ? email : email.substr(0, atPos);
            levelServerAccountUsername = sanitizeAccountUsername(localName);
        }
        SDL_Log("ACCOUNT: sign-in ok email=%s username=%s", email.c_str(), levelServerAccountUsername.c_str());
        return true;
#else
        errOut = "curl disabled in this build.";
        SDL_Log("ACCOUNT: sign-in unavailable (curl disabled)");
        return false;
#endif
    };
    auto controlBindingRef = [&](int idx) -> SDL_Scancode* {
        if (idx == 0) return &keyMoveLeft;
        if (idx == 1) return &keyMoveRight;
        if (idx == 2) return &keyMoveDown;
        if (idx == 3) return &keyJump;
        if (idx == 4) return &keyPause;
        return nullptr;
    };
    auto settingsRowWidthPx = [&]() -> int {
        const int scaled = std::max(120, (int)std::lround(280.0f * settingsMenuScale()));
        return std::min(scaled, std::max(120, (int)std::lround((float)ctx.baseScreenW * 0.62f)));
    };
    auto settingsRowHeightPx = [&]() -> int {
        return std::max(18, (int)std::lround(30.0f * settingsMenuScale()));
    };
    auto settingsRowStepPx = [&]() -> int {
        return std::max(settingsRowHeightPx() + 4, (int)std::lround((float)settingsRowH * settingsMenuScale()));
    };
    auto settingsListPadPx = [&]() -> int {
        return std::max(4, (int)std::lround(8.0f * settingsMenuScale()));
    };
    auto settingsTabWidthPx = [&]() -> int {
        const int scaled = std::max(84, (int)std::lround((float)settingsTabW * settingsMenuScale()));
        return std::min(scaled, std::max(84, (int)std::lround((float)ctx.baseScreenW * 0.24f)));
    };
    auto settingsTabHeightPx = [&]() -> int {
        const int desired = std::max(20, (int)std::lround((float)settingsTabH * settingsMenuScale()));
        const int gap = std::max(4, (int)std::lround(6.0f * settingsMenuScale()));
        const int visibleCount = std::max(1, (int)visibleTabList().size());
        const int availableH = std::max(visibleCount * 18, ctx.baseScreenH - settingsTabY - 24);
        const int fit = (availableH - gap * (visibleCount - 1)) / visibleCount;
        return std::clamp(std::min(desired, fit), 18, desired);
    };
    auto settingsTabGapPx = [&]() -> int {
        return std::max(4, (int)std::lround(6.0f * settingsMenuScale()));
    };
    auto settingsTabsToListGapPx = [&]() -> int {
        return std::max(10, (int)std::lround(18.0f * settingsMenuScale()));
    };
    auto settingsMaxScroll = [&](int tab) -> int {
        const int rows = settingsRowsForTab(tab);
        if (rows <= 0) return 0;
        const int contentH = (rows - 1) * settingsRowStepPx() + settingsRowHeightPx();
        const int viewH = std::max(1, settingsListBottom - settingsListTop);
        return std::max(0, contentH - viewH);
    };
    auto clampSettingsScroll = [&]() {
        settingsScrollY = std::clamp(settingsScrollY, 0, settingsMaxScroll(settingsTab));
    };
    auto scrollSettingsBy = [&](int dy) {
        settingsScrollY += dy;
        clampSettingsScroll();
    };
    auto settingsRowY = [&](int idx) -> int { return settingsStartY + idx * settingsRowStepPx() - settingsScrollY; };
    auto settingsRowBtn = [&](int idx) -> SDL_Rect {
        const int rowW = settingsRowWidthPx();
        const int rowHpx = settingsRowHeightPx();
        const int tabW = settingsTabWidthPx();
        const int pad = settingsListPadPx();
        const int tabsGap = settingsTabsToListGapPx();
        const int trackGap = std::max(6, (int)std::lround(8.0f * settingsMenuScale()));
        const int trackW = std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
        const int totalW = tabW + tabsGap + (rowW + pad * 2) + trackGap + trackW;
        const int left = ctx.baseScreenW / 2 - totalW / 2;
        const int rowLeft = left + tabW + tabsGap;
        return SDL_Rect{rowLeft + pad, settingsRowY(idx), rowW, rowHpx};
    };
    auto setNetworkCursorFromPoint = [&](int rowIdx, int px) {
        const bool hasUser = !levelServerAccountUsername.empty();
        const bool hasToken = !levelServerAuthToken.empty();
        if (hasUser || hasToken) return;
        if (rowIdx != 0 && rowIdx != 1) return;

        std::string* target = (rowIdx == 0) ? &networkLoginEmail : &networkLoginPassword;
        std::size_t* cursor = (rowIdx == 0) ? &networkLoginEmailCursor : &networkLoginPasswordCursor;
        if (!target || !cursor) return;
        if (target->empty()) {
            *cursor = 0;
            networkCursorPreset = true;
            return;
        }

        const int settingsRowTextScale = std::clamp((int)std::lround(2.0f + 0.5f * settingsMenuScale()), 2, 4);
        const std::string prefix = (rowIdx == 0) ? "LOGIN EMAIL: " : "LOGIN PASSWORD: ";
        const std::string shown = (rowIdx == 0) ? *target : maskedPassword(*target);
        SDL_Rect row = settingsRowBtn(rowIdx);
        const int leftInset = std::max(24, row.h);
        const int totalW = MeasureTextWidth(settingsRowTextScale, prefix + shown);
        const int tx = row.x + leftInset + std::max(0, (row.w - leftInset - totalW) / 2);
        const int valueStartX = tx + MeasureTextWidth(settingsRowTextScale, prefix);
        const int localX = std::max(0, px - valueStartX);

        std::size_t bytePos = 0;
        std::size_t lastBytePos = 0;
        int cpIndex = 0;
        while (bytePos < target->size()) {
            std::size_t next = bytePos + 1;
            while (next < target->size() && ((static_cast<unsigned char>((*target)[next]) & 0xC0) == 0x80)) ++next;
            const int leftW = MeasureTextWidth(settingsRowTextScale, shown.substr(0, cpIndex));
            const int rightW = MeasureTextWidth(settingsRowTextScale, shown.substr(0, cpIndex + 1));
            const int mid = leftW + (rightW - leftW) / 2;
            if (localX < mid) {
                *cursor = bytePos;
                networkCursorPreset = true;
                return;
            }
            lastBytePos = next;
            bytePos = next;
            ++cpIndex;
        }
        *cursor = lastBytePos;
        networkCursorPreset = true;
    };
    auto settingsListClipRect = [&]() -> SDL_Rect {
        const int pad = settingsListPadPx();
        SDL_Rect row0 = settingsRowBtn(0);
        return SDL_Rect{row0.x - pad, settingsListTop - pad / 2, row0.w + pad * 2, std::max(1, settingsListBottom - settingsListTop + pad)};
    };
    auto settingsScrollbarTrackRect = [&]() -> SDL_Rect {
        SDL_Rect clip = settingsListClipRect();
        const int gap = std::max(6, (int)std::lround(8.0f * settingsMenuScale()));
        const int trackW = std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
        return SDL_Rect{clip.x + clip.w + gap, settingsListTop, trackW, std::max(1, settingsListBottom - settingsListTop)};
    };
    auto settingsScrollbarThumbRect = [&]() -> SDL_Rect {
        SDL_Rect track = settingsScrollbarTrackRect();
        const int maxScroll = settingsMaxScroll(settingsTab);
        if (maxScroll <= 0) return SDL_Rect{track.x, track.y, track.w, track.h};
        const int rows = settingsRowsForTab(settingsTab);
        const int contentH = std::max(1, (rows - 1) * settingsRowStepPx() + settingsRowHeightPx());
        const int viewH = std::max(1, track.h);
        const int thumbH = std::clamp((viewH * viewH) / std::max(1, contentH), 18, viewH);
        const int travel = std::max(1, track.h - thumbH);
        const float t = std::clamp(settingsScrollY / (float)maxScroll, 0.0f, 1.0f);
        const int thumbY = track.y + (int)std::lround(t * travel);
        return SDL_Rect{track.x, thumbY, track.w, thumbH};
    };
    auto aboutScrollbarThumbRect = [&]() -> SDL_Rect {
        SDL_Rect track = settingsScrollbarTrackRect();
        const int maxScroll = aboutMaxScroll();
        if (maxScroll <= 0) return SDL_Rect{track.x, track.y, track.w, track.h};
        const int contentH = std::max(1, aboutContentBottomY - settingsListTop);
        const int viewH = std::max(1, track.h);
        const int thumbH = std::clamp((viewH * viewH) / std::max(1, contentH), 18, viewH);
        const int travel = std::max(1, track.h - thumbH);
        const float t = std::clamp(aboutScrollY / (float)maxScroll, 0.0f, 1.0f);
        const int thumbY = track.y + (int)std::lround(t * travel);
        return SDL_Rect{track.x, thumbY, track.w, thumbH};
    };
    auto setSettingsScrollFromY = [&](int y) {
        const int maxScroll = settingsMaxScroll(settingsTab);
        if (maxScroll <= 0) {
            settingsScrollY = 0;
            return;
        }
        SDL_Rect track = settingsScrollbarTrackRect();
        SDL_Rect thumb = settingsScrollbarThumbRect();
        const int travel = std::max(1, track.h - thumb.h);
        const int top = std::clamp(y - thumb.h / 2, track.y, track.y + travel);
        const float t = (top - track.y) / (float)travel;
        settingsScrollY = std::clamp((int)std::lround(t * maxScroll), 0, maxScroll);
    };
    auto pageSettingsScrollFromY = [&](int y) {
        SDL_Rect thumb = settingsScrollbarThumbRect();
        const int page = std::max(24, settingsListBottom - settingsListTop - 24);
        if (y < thumb.y) settingsScrollY -= page;
        else if (y > thumb.y + thumb.h) settingsScrollY += page;
        clampSettingsScroll();
    };
    auto setAboutScrollFromY = [&](int y) {
        const int maxScroll = aboutMaxScroll();
        if (maxScroll <= 0) {
            aboutScrollY = 0;
            return;
        }
        SDL_Rect track = settingsScrollbarTrackRect();
        SDL_Rect thumb = aboutScrollbarThumbRect();
        const int travel = std::max(1, track.h - thumb.h);
        const int top = std::clamp(y - thumb.h / 2, track.y, track.y + travel);
        const float t = (top - track.y) / (float)travel;
        aboutScrollY = std::clamp((int)std::lround(t * maxScroll), 0, maxScroll);
    };
    auto pageAboutScrollFromY = [&](int y) {
        SDL_Rect thumb = aboutScrollbarThumbRect();
        const int page = std::max(24, settingsListBottom - settingsListTop - 24);
        if (y < thumb.y) aboutScrollY -= page;
        else if (y > thumb.y + thumb.h) aboutScrollY += page;
        clampAboutScroll();
    };
    auto settingsTabBtn = [&](int idx) -> SDL_Rect {
        const int tabW = settingsTabWidthPx();
        const int tabH = settingsTabHeightPx();
        const int tabGap = settingsTabGapPx();
        const int rowW = settingsRowWidthPx();
        const int pad = settingsListPadPx();
        const int tabsGap = settingsTabsToListGapPx();
        const int trackGap = std::max(6, (int)std::lround(8.0f * settingsMenuScale()));
        const int trackW = std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
        const int totalW = tabW + tabsGap + (rowW + pad * 2) + trackGap + trackW;
        const int left = ctx.baseScreenW / 2 - totalW / 2;
        return SDL_Rect{left, settingsTabY + idx * (tabH + tabGap), tabW, tabH};
    };
    auto musicSliderRect = [&]() -> SDL_Rect {
        const SDL_Rect row = settingsRowBtn(2);
        const int inset = std::max(14, (int)std::lround(22.0f * settingsMenuScale()));
        const int sliderW = std::max(80, row.w - inset * 2);
        const int sliderH = std::clamp((int)std::lround(10.0f * settingsMenuScale()), 10, 22);
        const int sliderYPad = std::max(2, (int)std::lround(3.0f * settingsMenuScale()));
        return SDL_Rect{row.x + (row.w - sliderW) / 2, row.y + row.h - sliderH - sliderYPad, sliderW, sliderH};
    };
    auto sfxSliderRect = [&]() -> SDL_Rect {
        const SDL_Rect row = settingsRowBtn(3);
        const int inset = std::max(14, (int)std::lround(22.0f * settingsMenuScale()));
        const int sliderW = std::max(80, row.w - inset * 2);
        const int sliderH = std::clamp((int)std::lround(10.0f * settingsMenuScale()), 10, 22);
        const int sliderYPad = std::max(2, (int)std::lround(3.0f * settingsMenuScale()));
        return SDL_Rect{row.x + (row.w - sliderW) / 2, row.y + row.h - sliderH - sliderYPad, sliderW, sliderH};
    };
    auto uiScaleSliderRect = [&]() -> SDL_Rect {
        const SDL_Rect row = settingsRowBtn(IDX_UI_SCALE);
        const int inset = std::max(14, (int)std::lround(22.0f * settingsMenuScale()));
        const int sliderW = std::max(80, row.w - inset * 2);
        const int sliderH = std::clamp((int)std::lround(10.0f * settingsMenuScale()), 10, 22);
        const int sliderYPad = std::max(2, (int)std::lround(3.0f * settingsMenuScale()));
        return SDL_Rect{row.x + (row.w - sliderW) / 2, row.y + row.h - sliderH - sliderYPad, sliderW, sliderH};
    };
    auto uiEdgePaddingSliderRect = [&]() -> SDL_Rect {
        const SDL_Rect row = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_UI_EDGE_PADDING));
        const int inset = std::max(14, (int)std::lround(22.0f * settingsMenuScale()));
        const int sliderW = std::max(80, row.w - inset * 2);
        const int sliderH = std::clamp((int)std::lround(10.0f * settingsMenuScale()), 10, 22);
        const int sliderYPad = std::max(2, (int)std::lround(3.0f * settingsMenuScale()));
        return SDL_Rect{row.x + (row.w - sliderW) / 2, row.y + row.h - sliderH - sliderYPad, sliderW, sliderH};
    };
    auto sliderHitRect = [&](const SDL_Rect& slider) -> SDL_Rect {
        const int padX = std::max(4, (int)std::lround(5.0f * settingsMenuScale()));
        const int padY = std::max(6, (int)std::lround(8.0f * settingsMenuScale()));
        return SDL_Rect{slider.x - padX, slider.y - padY, slider.w + padX * 2, slider.h + padY * 2};
    };
    auto sliderValueFromPoint = [&](int x, const SDL_Rect& slider) -> int {
        int rel = x - slider.x;
        if (rel < 0) rel = 0;
        if (rel > slider.w) rel = slider.w;
        return (int)std::lround((rel / (double)std::max(1, slider.w)) * 128.0);
    };
    auto uiScalePercentFromPoint = [&](int x, const SDL_Rect& slider) -> int {
        return UiScale::percentFromSliderX(x, slider);
    };
    auto uiEdgePaddingFromPoint = [&](int x, const SDL_Rect& slider) -> int {
        return UiScale::edgePaddingFromSliderX(x, slider);
    };
    auto mouseToGamePoint = [&](int mx, int my, SDL_Point& pt) -> bool {
        int winW = 0, winH = 0, gx = 0, gy = 0;
        getWindowSizeInPixelsCompat(ctx.win, winW, winH);
        if (!windowToGamePoint(mx, my, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy, 1.0f)) return false;
        pt.x = gx;
        pt.y = gy;
        return true;
    };
    constexpr int kSaveSlotCount = 3;
    auto saveSlotPath = [&](int slotIndex) -> std::filesystem::path {
        const int slot = std::clamp(slotIndex, 0, kSaveSlotCount - 1);
        return std::filesystem::path(GetAppSaveRootPath()) / "saves" / ("save_slot_" + std::to_string(slot + 1) + ".json");
    };
    auto saveSlotExists = [&](int slotIndex) -> bool {
        return std::filesystem::exists(saveSlotPath(slotIndex));
    };
    auto activeSaveSlotPath = [&]() -> std::filesystem::path {
        return saveSlotPath(activeSaveSlotIndex);
    };
    auto hasSavedGame = [&]() -> bool {
        return saveSlotExists(activeSaveSlotIndex);
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
        const int count = levelSelectEnabled ? 3 : 2;
        const float canvas = menuCanvasScale();
        const float s = mainMenuScale();
        const int btnW = std::max(56, (int)std::lround(112.0f * canvas * s));
        const int btnH = std::max(56, (int)std::lround(112.0f * canvas * s));
        const int step = std::max(btnW + 8, (int)std::lround((112.0f + 44.0f) * canvas * s));
        const int centerY = (int)std::lround(menuCanvasOriginY() + 266.0f * canvas);
        const float groupOffset = (float)idx - (float)(count - 1) * 0.5f;
        const int centerX = (int)std::lround(menuCanvasOriginX() + 480.0f * canvas + groupOffset * (float)step);
        return SDL_Rect{centerX - btnW / 2, centerY - btnH / 2, btnW, btnH};
    };
    auto mainMenuButtonCount = [&]() -> int {
        return levelSelectEnabled ? 3 : 2;
    };
    auto quitModalRect = [&]() -> SDL_Rect {
        const float s = uiButtonScale();
        SDL_Rect base{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
        return scaleRectCentered(base, s);
    };
    auto quitResumeBtnRect = [&](const SDL_Rect& modal) -> SDL_Rect {
        const float s = uiButtonScale();
        return SDL_Rect{
            modal.x + (int)std::lround(26.0f * s),
            modal.y + (int)std::lround(94.0f * s),
            std::max(40, (int)std::lround(140.0f * s)),
            std::max(20, (int)std::lround(56.0f * s))
        };
    };
    auto quitCloseBtnRect = [&](const SDL_Rect& modal) -> SDL_Rect {
        const float s = uiButtonScale();
        return SDL_Rect{
            modal.x + (int)std::lround(194.0f * s),
            modal.y + (int)std::lround(94.0f * s),
            std::max(40, (int)std::lround(140.0f * s)),
            std::max(20, (int)std::lround(56.0f * s))
        };
    };
    auto ensureSettingsRowVisible = [&](int rowIdx) {
        if (rowIdx < 0) return;
        SDL_Rect row = settingsRowBtn(rowIdx);
        int y = row.y;
        if (y < settingsListTop) {
            settingsScrollY -= (settingsListTop - y);
        } else if (y + row.h > settingsListBottom) {
            settingsScrollY += (y + row.h - settingsListBottom);
        }
        clampSettingsScroll();
    };
    auto settingsPanelRect = [&]() -> SDL_Rect {
        const std::vector<int> tabs = sidebarTabList();
        SDL_Rect tabsTop = settingsTabBtn(0);
        SDL_Rect tabsBottom = settingsTabBtn(std::max(0, (int)tabs.size() - 1));
        SDL_Rect listClip = settingsListClipRect();
        SDL_Rect scrollbarTrack = settingsScrollbarTrackRect();
        const int panelPad = std::max(10, (int)std::lround(14.0f * settingsMenuScale()));
        return SDL_Rect{
            std::min(tabsTop.x, listClip.x) - panelPad,
            std::min(tabsTop.y, listClip.y) - panelPad,
            (std::max(tabsBottom.x + tabsBottom.w, scrollbarTrack.x + scrollbarTrack.w) - std::min(tabsTop.x, listClip.x)) + panelPad * 2,
            (std::max(scrollbarTrack.y + scrollbarTrack.h, listClip.y + listClip.h) - std::min(tabsTop.y, listClip.y)) + panelPad * 2
        };
    };
    auto settingsSidebarToggleRect = [&]() -> SDL_Rect {
        SDL_Rect panel = settingsPanelRect();
        const int btnW = std::max(96, (int)std::lround(138.0f * settingsMenuScale()));
        const int btnH = std::max(22, (int)std::lround(30.0f * settingsMenuScale()));
        return SDL_Rect{
            panel.x + panel.w - btnW - std::max(8, (int)std::lround(10.0f * settingsMenuScale())),
            panel.y + std::max(8, (int)std::lround(10.0f * settingsMenuScale())),
            btnW,
            btnH
        };
    };
    auto settingsSidebarRect = [&]() -> SDL_Rect {
        SDL_Rect panel = settingsPanelRect();
        const int sideW = std::max(150, (int)std::lround(200.0f * settingsMenuScale()));
        const int gap = std::max(10, (int)std::lround(14.0f * settingsMenuScale()));
        const int pad = std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
        const int topPad = std::max(44, (int)std::lround(50.0f * settingsMenuScale()));
        const int bottomPad = std::max(10, (int)std::lround(12.0f * settingsMenuScale()));
        int sideX = panel.x + panel.w + gap;
        if (sideX + sideW > ctx.baseScreenW - 8) {
            sideX = panel.x - gap - sideW;
        }
        sideX = std::clamp(sideX, 8, std::max(8, ctx.baseScreenW - sideW - 8));
        return SDL_Rect{
            sideX,
            panel.y + topPad,
            sideW,
            std::max(20, panel.h - topPad - bottomPad)
        };
    };
    auto aboutOpenMainLevelSelectRect = [&]() -> SDL_Rect {
        if (!showExperimentalFeatures) return SDL_Rect{0, 0, 0, 0};
        const int aboutHeadScale = std::clamp((int)std::lround(2.0f + 0.35f * settingsMenuScale()), 2, 3);
        const int aboutBodyScale = std::clamp((int)std::lround(1.3f + 0.35f * settingsMenuScale()), 2, 3);
        int y = settingsListTop + std::max(8, (int)std::lround(10.0f * settingsMenuScale())) - aboutScrollY;
        auto advanceAboutLine = [&](int scale, int gapMul) {
            y += std::max(14, (10 * scale) + gapMul);
        };
        advanceAboutLine(aboutHeadScale, 8);
        advanceAboutLine(aboutBodyScale, 6);
#if defined(_WIN32)
        advanceAboutLine(aboutBodyScale, 6);
#endif
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 2);
        advanceAboutLine(aboutBodyScale, 8);
        advanceAboutLine(aboutBodyScale, 2);
        advanceAboutLine(aboutBodyScale, 6);
#if defined(_WIN32)
        advanceAboutLine(aboutBodyScale, 6);
        advanceAboutLine(aboutBodyScale, 6);
#endif
        const std::string actionText = "OPEN LEVEL SELECT";
        const int textW = MeasureTextWidth(aboutBodyScale, actionText);
        const int textH = 10 * aboutBodyScale;
        const int buttonPadX = std::max(12, (int)std::lround(14.0f * settingsMenuScale()));
        const int buttonPadY = std::max(6, (int)std::lround(8.0f * settingsMenuScale()));
        y += std::max(64, (int)std::lround(64.0f * settingsMenuScale()));
        return SDL_Rect{
            ctx.baseScreenW / 2 - textW / 2 - buttonPadX,
            y - buttonPadY,
            textW + buttonPadX * 2,
            textH + buttonPadY * 2
        };
    };
    auto fileInfoBackRect = [&]() -> SDL_Rect {
        const int backScale = std::clamp((int)std::lround(1.3f + 0.35f * settingsMenuScale()), 2, 3);
        const std::string backLabel = "BACK";
        const int textW = MeasureTextWidth(backScale, backLabel);
        const int textH = 10 * backScale;
        const int buttonPadX = std::max(12, (int)std::lround(14.0f * settingsMenuScale()));
        const int buttonPadY = std::max(6, (int)std::lround(8.0f * settingsMenuScale()));
        const int buttonY = settingsListBottom - std::max(18, (int)std::lround(20.0f * settingsMenuScale()));
        return SDL_Rect{
            ctx.baseScreenW / 2 - textW / 2 - buttonPadX,
            buttonY - buttonPadY,
            textW + buttonPadX * 2,
            textH + buttonPadY * 2
        };
    };
    auto resetSelectionForTab = [&](int tab) {
        if (tab == IDX_SETTINGS_AUDIO) settingsSelAudio = 0;
        if (tab == IDX_SETTINGS_DEBUG) settingsSelDebug = 0;
        if (tab == IDX_SETTINGS_CONTROLS) settingsSelControls = 0;
        if (tab == IDX_SETTINGS_FILEINFO) settingsSel = 0;
        if (tab == IDX_SETTINGS_ACCOUNT) settingsSelNetwork = 0;
        if (tab == IDX_SAVES_TAB) {
            settingsSelSaves = std::clamp(activeSaveSlotIndex, 0, kSaveSlotCount - 1);
            settingsScrollY = 0;
        }
        if (isExtraTab(tab)) settingsSel = 0;
    };
    auto openSettingsTab = [&](int tab) {
        stopNetworkEditing();
        setSettingsTab(tab);
        resetSelectionForTab(settingsTab);
        waitingForControlKey = false;
        waitingControlIndex = -1;
        settingsScrollY = 0;
        aboutScrollY = 0;
    };
    auto cycleSettingsTab = [&](int delta) {
        const std::vector<int> tabs = visibleTabList();
        if (tabs.empty()) return;
        auto it = std::find(tabs.begin(), tabs.end(), settingsTab);
        const int idx = (it == tabs.end()) ? 0 : (int)(it - tabs.begin());
        const int next = (idx + delta + (int)tabs.size()) % (int)tabs.size();
        openSettingsTab(tabs[next]);
    };
    auto refreshAccountUi = [&]() {
        if (settingsTab != IDX_SETTINGS_ACCOUNT) return;
        const int rows = std::max(1, settingsRowsForTab(IDX_SETTINGS_ACCOUNT));
        settingsSelNetwork = std::clamp(settingsSelNetwork, 0, rows - 1);
        ensureSettingsRowVisible(settingsSelNetwork);
    };
    auto updaterCanStartInstall = [&]() -> bool {
        const std::string status = updaterStatusText();
        return status.find("UPDATE FOUND") != std::string::npos;
    };
    auto syncNetworkSessionState = [&]() {
        SetLevelServerAuthToken(levelServerAuthToken);
        SetLevelServerAccountUsername(levelServerAccountUsername);
    };
    auto navigateSettingsBy = [&](int delta) {
        if (settingsTab == IDX_SETTINGS_ABOUT) {
            scrollAboutBy(delta < 0 ? -24 : 24);
            return;
        }
        const int rows = settingsRowsForTab(settingsTab);
        if (rows <= 0) return;
        int& activeSel = activeSettingsSelectionRef();
        activeSel = (activeSel + delta + rows) % rows;
        ensureSettingsRowVisible(activeSel);
    };
    auto activateCurrentSettingsSelection = [&](int dir) {
        if (settingsTab == IDX_SETTINGS_AUDIO) {
            if (settingsSelAudio == 0) menuMusicEnabled = !menuMusicEnabled;
            else if (settingsSelAudio == 1) muteAllAudio = !muteAllAudio;
            else if (settingsSelAudio == 2 && dir != 0) musicVolume = std::clamp(musicVolume + dir * 8, 0, 128);
            else if (settingsSelAudio == 3 && dir != 0) sfxVolume = std::clamp(sfxVolume + dir * 8, 0, 128);
            else if (settingsSelAudio == 4) setInSettings(false);
            if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
            if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
            return;
        }
        if (settingsTab == IDX_SETTINGS_DEBUG) {
            if (settingsSelDebug == 0) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
            else if (settingsSelDebug == 1) defaultShowHitboxes = !defaultShowHitboxes;
            else if (settingsSelDebug == 2) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
            else if (settingsSelDebug == 3) defaultShowDebugView = !defaultShowDebugView;
            else if (settingsSelDebug == 4) defaultHideUnknownObjectTypes = !defaultHideUnknownObjectTypes;
            else if (settingsSelDebug == 5) setInSettings(false);
            return;
        }
        if (settingsTab == IDX_SETTINGS_CONTROLS) {
            if (settingsSelControls >= 0 && settingsSelControls <= 4) {
                waitingForControlKey = true;
                waitingControlIndex = settingsSelControls;
            } else {
                setInSettings(false);
            }
            return;
        }
        if (settingsTab == IDX_SAVES_TAB) {
            settingsSelSaves = std::clamp(settingsSelSaves, 0, 4);
            if (settingsSelSaves >= 0 && settingsSelSaves <= 2) {
                activeSaveSlotIndex = std::clamp(settingsSelSaves, 0, kSaveSlotCount - 1);
                if (ctx.saveClientSettings) ctx.saveClientSettings();
                return;
            }
            if (settingsSelSaves == 3) {
                startSavedGameRequested = true;
                return;
            }
            setInSettings(false);
            return;
        }
        if (settingsTab == IDX_SETTINGS_ACCOUNT) {
            const bool hasUser = !levelServerAccountUsername.empty();
            const bool hasToken = !levelServerAuthToken.empty();
            const bool invalidLogin = (hasUser && !hasToken);
            auto openAccountManager = [&]() {
                if (accountManagerUrl.empty()) {
                    SDL_Log("ACCOUNT: open account manager skipped (url empty)");
                    return;
                }
                SDL_Log("ACCOUNT: opening account manager url=%s", accountManagerUrl.c_str());
                if (!SDL_OpenURL(accountManagerUrl.c_str())) {
                    SDL_Log("ACCOUNT: open account manager failed err=%s", SDL_GetError());
                } else {
                    SDL_Log("ACCOUNT: open account manager dispatched");
                }
            };
            if (invalidLogin) {
                if (settingsSelNetwork == 1) {
                    levelServerAccountUsername.clear();
                    levelServerAuthToken.clear();
                    syncNetworkSessionState();
                    networkLoginStatus = "Login repaired. Enter credentials.";
                    SDL_Log("ACCOUNT: login repaired (cleared username/token)");
                    if (ctx.saveClientSettings) ctx.saveClientSettings();
                    refreshAccountUi();
                } else if (settingsSelNetwork == 2) {
                    openAccountManager();
                } else if (settingsSelNetwork == 3) {
                    setInSettings(false);
                }
                return;
            }
            if (hasUser && hasToken) {
                if (settingsSelNetwork == 1) {
                    levelServerAccountUsername.clear();
                    levelServerAuthToken.clear();
                    syncNetworkSessionState();
                    networkLoginStatus = "Logged out.";
                    SDL_Log("ACCOUNT: logged out");
                    if (ctx.saveClientSettings) ctx.saveClientSettings();
                    refreshAccountUi();
                } else if (settingsSelNetwork == 2) {
                    openAccountManager();
                } else if (settingsSelNetwork == 3) {
                    setInSettings(false);
                }
                return;
            }
            if (settingsSelNetwork == 0) {
                SDL_Rect row = settingsRowBtn(0);
                beginNetworkEditing(NetworkEditField::LoginEmail, &row);
            } else if (settingsSelNetwork == 1) {
                SDL_Rect row = settingsRowBtn(1);
                beginNetworkEditing(NetworkEditField::LoginPassword, &row);
            } else if (settingsSelNetwork == 2) {
                // Finalize IME composition before login so the latest touched text is committed.
                stopNetworkEditing();
                std::string err;
                if (loginWithFirebase(err)) {
                    syncNetworkSessionState();
                    networkLoginPassword.clear();
                    networkLoginPasswordCursor = 0;
                    networkLoginStatus = std::string("Logged in as ") + levelServerAccountUsername;
                    if (ctx.saveClientSettings) ctx.saveClientSettings();
                    refreshAccountUi();
                } else {
                    networkLoginStatus = err.empty() ? "Login failed." : err;
                }
            } else if (settingsSelNetwork == 3) {
                openAccountManager();
            } else if (settingsSelNetwork == 4) {
                setInSettings(false);
            }
            return;
        }
        if (settingsTab == IDX_UPDATER_TAB) {
            if (settingsSel == 1 && ctx.launchUpdater) {
                if (ctx.saveClientSettings) ctx.saveClientSettings();
                (void)ctx.launchUpdater();
            } else if (settingsSel == 2) {
                openSettingsTab(0);
            }
            return;
        }
        if (settingsTab == IDX_SETTINGS_ABOUT) {
            setInSettings(false);
            return;
        }
        if (settingsTab == IDX_SETTINGS_FILEINFO) {
            setInSettings(false);
            return;
        }
        if (isExtraTab(settingsTab)) {
            const int extraIdx = extraTabIndex(settingsTab);
            const int optionIdx = extraTabOptionAtVisibleRow(settingsTab, settingsSel);
            if (optionIdx >= 0) {
                bool& v = extraTabValueRef(extraIdx, optionIdx);
                v = !v;
            } else {
                setInSettings(false);
            }
            return;
        }
#if defined(__ANDROID__)
        const int rawGeneralSel = generalSettingsRawIndexFromVisible(settingsSel);
        if (rawGeneralSel == IDX_VSYNC) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
        else if (rawGeneralSel == IDX_CAM_CLAMP) clampCamX = !clampCamX;
        else if (rawGeneralSel == IDX_UI_SCALE && dir != 0) uiScalePercent = UiScale::stepPercent(uiScalePercent, dir);
        else if (rawGeneralSel == IDX_UI_EDGE_PADDING && dir != 0) uiEdgePadding = UiScale::stepEdgePadding(uiEdgePadding, dir);
        else if (rawGeneralSel == IDX_SHOW_FPS) defaultShowFpsCounter = !defaultShowFpsCounter;
        else if (rawGeneralSel == IDX_POWER_MANAGEMENT) powerManagementEnabled = !powerManagementEnabled;
        else if (rawGeneralSel == IDX_LOW_POWER_MODE) lowPowerModeEnabled = !lowPowerModeEnabled;
        else if (rawGeneralSel == IDX_MUSIC && dir != 0) musicVolume = std::clamp(musicVolume + dir * 8, 0, 128);
        else if (rawGeneralSel == IDX_SFX && dir != 0) sfxVolume = std::clamp(sfxVolume + dir * 8, 0, 128);
        else if (rawGeneralSel == IDX_SHOW_EXPERIMENTAL) showExperimentalFeatures = !showExperimentalFeatures;
        else if (rawGeneralSel == IDX_LEVEL_SELECT) levelSelectEnabled = !levelSelectEnabled;
        else if (rawGeneralSel == IDX_ABOUT) { openSettingsTab(IDX_SETTINGS_ABOUT); }
        else setInSettings(false);
#else
        const int rawGeneralSel = generalSettingsRawIndexFromVisible(settingsSel);
        if (rawGeneralSel == IDX_FULLSCREEN) { (void)applyFullscreen(!fullscreen); }
        else if (rawGeneralSel == IDX_VSYNC) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
        else if (rawGeneralSel == IDX_CAM_CLAMP) clampCamX = !clampCamX;
        else if (rawGeneralSel == IDX_UI_SCALE && dir != 0) uiScalePercent = UiScale::stepPercent(uiScalePercent, dir);
        else if (rawGeneralSel == IDX_UI_EDGE_PADDING && dir != 0) uiEdgePadding = UiScale::stepEdgePadding(uiEdgePadding, dir);
        else if (rawGeneralSel == IDX_SHOW_FPS) defaultShowFpsCounter = !defaultShowFpsCounter;
        else if (rawGeneralSel == IDX_POWER_MANAGEMENT) powerManagementEnabled = !powerManagementEnabled;
        else if (rawGeneralSel == IDX_LOW_POWER_MODE) lowPowerModeEnabled = !lowPowerModeEnabled;
        else if (rawGeneralSel == IDX_MUSIC && dir != 0) musicVolume = std::clamp(musicVolume + dir * 8, 0, 128);
        else if (rawGeneralSel == IDX_SFX && dir != 0) sfxVolume = std::clamp(sfxVolume + dir * 8, 0, 128);
        else if (rawGeneralSel == IDX_SHOW_EXPERIMENTAL) showExperimentalFeatures = !showExperimentalFeatures;
        else if (rawGeneralSel == IDX_LEVEL_SELECT) levelSelectEnabled = !levelSelectEnabled;
#if defined(_WIN32)
        else if (rawGeneralSel == IDX_UPDATE && ctx.launchUpdater) {
            openSettingsTab(IDX_UPDATER_TAB);
        }
#endif
        else if (rawGeneralSel == IDX_ABOUT) { openSettingsTab(IDX_SETTINGS_ABOUT); }
        else setInSettings(false);
#endif
        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    };
    SDL_Texture* mainMenuTex = loadTextureSafe(ctx.ren, "assets/Sheets/DF_Main_menu-uhd.png", nullptr);
    if (mainMenuTex) SDL_SetTextureScaleMode(mainMenuTex, SDL_SCALEMODE_NEAREST);
    auto mainMenuFrames = loadPlistFrames("assets/Sheets/DF_Main_menu-uhd.plist");
    SDL_Texture* menuFallbackTex = loadTextureSafe(ctx.ren, "assets/Sheets/DF_Menus-uhd.png", nullptr);
    if (menuFallbackTex) SDL_SetTextureScaleMode(menuFallbackTex, SDL_SCALEMODE_NEAREST);
    auto menuFallbackFrames = loadPlistFrames("assets/Sheets/DF_Menus-uhd.plist");
    std::string menuBgErr;
    SDL_Texture* menuBgTex = loadTextureSafe(ctx.ren, "assets/Sheets/DF_Back_1-uhd.png", &menuBgErr);
    auto menuBgFrames = loadPlistFrames("assets/Sheets/DF_Back_1-uhd.plist");
    if (!menuBgTex || menuBgFrames.empty()) {
        SDL_Log("Menu background load incomplete: texture=assets/Sheets/DF_Back_1-uhd.png loaded=%d plist frames=%d sdl_error='%s'",
                menuBgTex ? 1 : 0, (int)menuBgFrames.size(), menuBgErr.c_str());
    }
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
        // Large UI atlas frames (e.g. window/button panels) are authored as 3x3 regions.
        // Decide per-axis so tall/narrow or wide/short frames still avoid stretch on the long axis.
        const bool useThirdSplitX = (f.rect.w >= 48);
        const bool useThirdSplitY = (f.rect.h >= 48);
        const int srcLeft = useThirdSplitX ? std::max(1, f.rect.w / 3) : (f.rect.w - 1) / 2;
        const int srcRight = useThirdSplitX ? std::max(1, f.rect.w / 3) : (f.rect.w - srcLeft - 1);
        const int srcTop = useThirdSplitY ? std::max(1, f.rect.h / 3) : (f.rect.h - 1) / 2;
        const int srcBottom = useThirdSplitY ? std::max(1, f.rect.h / 3) : (f.rect.h - srcTop - 1);
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

        const int srcMidW = f.rect.w - srcLeft - srcRight;
        const int srcMidH = f.rect.h - srcTop - srcBottom;
        if (srcMidW <= 0 || srcMidH <= 0) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }

        auto blit = [&](const SDL_Rect& src, const SDL_Rect& out) {
            if (src.w <= 0 || src.h <= 0 || out.w <= 0 || out.h <= 0) return;
            SDL_RenderTexture(ctx.ren, tex, &src, &out);
        };
        auto blitTiled = [&](const SDL_Rect& src, const SDL_Rect& out, bool tileX, bool tileY) {
            if (src.w <= 0 || src.h <= 0 || out.w <= 0 || out.h <= 0) return;
            const int stepX = tileX ? src.w : out.w;
            const int stepY = tileY ? src.h : out.h;
            for (int y = out.y; y < out.y + out.h; y += std::max(1, stepY)) {
                const int remH = out.y + out.h - y;
                const int h = tileY ? std::min(src.h, remH) : out.h;
                for (int x = out.x; x < out.x + out.w; x += std::max(1, stepX)) {
                    const int remW = out.x + out.w - x;
                    const int w = tileX ? std::min(src.w, remW) : out.w;
                    SDL_Rect s{src.x, src.y, w, h};
                    SDL_Rect d{x, y, w, h};
                    SDL_RenderTexture(ctx.ren, tex, &s, &d);
                    if (!tileX) break;
                }
                if (!tileY) break;
            }
        };

        const int cx = f.rect.x + srcLeft;
        const int cy = f.rect.y + srcTop;
        SDL_Rect sTL{f.rect.x, f.rect.y, srcLeft, srcTop};
        SDL_Rect sT{cx, f.rect.y, srcMidW, srcTop};
        SDL_Rect sTR{cx + srcMidW, f.rect.y, srcRight, srcTop};
        SDL_Rect sL{f.rect.x, cy, srcLeft, srcMidH};
        SDL_Rect sC{cx, cy, srcMidW, srcMidH};
        SDL_Rect sR{cx + srcMidW, cy, srcRight, srcMidH};
        SDL_Rect sBL{f.rect.x, cy + srcMidH, srcLeft, srcBottom};
        SDL_Rect sB{cx, cy + srcMidH, srcMidW, srcBottom};
        SDL_Rect sBR{cx + srcMidW, cy + srcMidH, srcRight, srcBottom};

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
        blitTiled(sT, dT, true, false);
        blit(sTR, dTR);
        blitTiled(sL, dL, false, true);
        blitTiled(sC, dC, true, true);
        blitTiled(sR, dR, false, true);
        blit(sBL, dBL);
        blitTiled(sB, dB, true, false);
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
        if (!levelSelectEnabled) return false;
        std::string path = RunCustomLevelSelect(ctx.win, ctx.ren);
        if (path.empty()) return false;
        *ctx.selectedLevelPath = path;
        cleanupMenuAssets();
        return true;
    };
    auto tryStartLevelSelect = [&]() -> bool {
        if (!ctx.selectedLevelPath) return false;
        if (!levelSelectEnabled) return false;
        std::string path = RunLevelSelect(ctx.win, ctx.ren);
        if (path.empty()) return false;
        *ctx.selectedLevelPath = path;
        cleanupMenuAssets();
        return true;
    };
    auto mainMenuSelectionAction = [&](int sel) -> bool {
        if (!levelSelectEnabled) {
            if (sel == 0) return false;
            if (sel == 1) {
                cleanupMenuAssets();
                return true;
            }
            return false;
        }
        if (sel == 0) return false;
        if (sel == 1) {
            return tryStartLevelSelect();
        }
        if (sel == 2) {
            return tryStartCustomLevel();
        }
        return false;
    };
    auto mainMenuLabelForIndex = [&](int idx) -> std::string {
        if (!levelSelectEnabled) {
            if (idx == 0) return "SETTINGS";
            if (idx == 1) return "PLAY";
            return "BUTTON";
        }
        if (idx == 0) return "SETTINGS";
        if (idx == 1) return "PLAY";
        if (idx == 2) return "EDITOR";
        return "BUTTON";
    };
    // Apply persisted audio state before entering the menu loop.
    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
    normalizeSettingsTab();
    InputSystem menuInput;
    menuInput.scanConnected();

    Uint64 fpsLastSampleTicks = SDL_GetTicks();
    int fpsFramesSinceSample = 0;
    int menuFps = 0;

    while (running) {
        const Uint64 frameTicks = SDL_GetTicks();
        ++fpsFramesSinceSample;
        const Uint64 sampleElapsed = frameTicks - fpsLastSampleTicks;
        if (sampleElapsed >= 250) {
            menuFps = (int)std::lround((double)fpsFramesSinceSample * 1000.0 / (double)std::max<Uint64>(1, sampleElapsed));
            fpsFramesSinceSample = 0;
            fpsLastSampleTicks = frameTicks;
        }
        if (ctx.pollUpdaterAutoRelaunch) {
            ctx.pollUpdaterAutoRelaunch();
            if (!running) {
                cleanupMenuAssets();
                return FrontendAction::Quit;
            }
        }
        SDL_Texture* gameTarget = (ctx.gameTargetRef && *ctx.gameTargetRef) ? *ctx.gameTargetRef : ctx.gameTarget;
        if (!gameTarget) {
            SDL_Delay(1);
            continue;
        }
        SetTextScaleMultiplier(UiScale::multiplier(uiScalePercent));
        applySettingsExitCleanup();
        // Keep menu music alive even when no audio-settings input occurs.
        if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
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
            if ((e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
                e.window.windowID == SDL_GetWindowID(ctx.win)) {
                if (ctx.updateDynamicResolution) ctx.updateDynamicResolution();
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
                    const int mainMenuCount = std::max(1, mainMenuButtonCount());
                    menuSel = std::clamp(menuSel, 0, mainMenuCount - 1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) menuSel = (menuSel + mainMenuCount - 1) % mainMenuCount;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ||
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) menuSel = (menuSel + 1) % mainMenuCount;
                    if (isAcceptBtn) {
                        if (menuSel == 0) {
                            openSettingsTab(0);
                            setInSettings(true);
                        } else if (mainMenuSelectionAction(menuSel)) {
                            return FrontendAction::StartGame;
                        }
                    }
                    if (isBackBtn) {
                        setCloseMenuOpen(true);
                        closeMenuSel = 0;
                    }
                    continue;
                }
                if (isBackBtn) {
                    if (waitingForControlKey) {
                        waitingForControlKey = false;
                        waitingControlIndex = -1;
                        continue;
                    }
                    setInSettings(false);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST ||
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                    setOptionalSidebar(!showOptionalSidebar);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    navigateSettingsBy(-1);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    navigateSettingsBy(1);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
                    cycleSettingsTab(-1);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                    cycleSettingsTab(1);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                    activateCurrentSettingsSelection(-1);
                    continue;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                    activateCurrentSettingsSelection(1);
                    continue;
                }
                if (isAcceptBtn) {
                    activateCurrentSettingsSelection(0);
                    continue;
                }
            }
            const bool isTextInputEvent =
#if defined(SDL_EVENT_TEXT_INPUT)
                (e.type == SDL_EVENT_TEXT_INPUT) ||
#endif
                (e.type == SDL_TEXTINPUT);
            if (isTextInputEvent &&
                inSettings &&
                settingsTab == 9 &&
                networkEditField != NetworkEditField::None) {
                appendNetworkInput(e.text.text ? e.text.text : "");
                if (ctx.saveClientSettings) ctx.saveClientSettings();
                continue;
            }
            const bool isKeyDownEvent =
#if defined(SDL_EVENT_KEY_DOWN)
                (e.type == SDL_EVENT_KEY_DOWN) ||
#endif
                (e.type == SDL_KEYDOWN);
            if (isKeyDownEvent && e.key.repeat == 0) {
                if (e.key.key == SDLK_F11) {
#if !defined(__ANDROID__)
                    (void)applyFullscreen(!fullscreen);
#endif
                    continue;
                }
                const bool navLeft = (e.key.key == SDLK_LEFT || e.key.scancode == keyMoveLeft);
                const bool navRight = (e.key.key == SDLK_RIGHT || e.key.scancode == keyMoveRight);
                const bool navUp = (e.key.key == SDLK_UP || e.key.scancode == keyJump);
                const bool navDown = (e.key.key == SDLK_DOWN || e.key.scancode == keyMoveDown);
                const bool isBackKey = (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK);
                if (closeMenuOpen) {
                    if (navUp) closeMenuSel = (closeMenuSel + 1) % 2;
                    if (navDown) closeMenuSel = (closeMenuSel + 1) % 2;
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
                    const int mainMenuCount = std::max(1, mainMenuButtonCount());
                    menuSel = std::clamp(menuSel, 0, mainMenuCount - 1);
                    if (navLeft) menuSel = (menuSel + mainMenuCount - 1) % mainMenuCount;
                    if (navRight) menuSel = (menuSel + 1) % mainMenuCount;
                    if (navUp) menuSel = (menuSel + mainMenuCount - 1) % mainMenuCount;
                    if (navDown) menuSel = (menuSel + 1) % mainMenuCount;
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (menuSel == 0) {
                            openSettingsTab(0);
                            setInSettings(true);
                        } else if (mainMenuSelectionAction(menuSel)) {
                            return FrontendAction::StartGame;
                        }
                    }
                    if (isBackKey) {
                        setCloseMenuOpen(true);
                        closeMenuSel = 0;
                    }
                } else {
                    if (devToolsEnabled &&
                        (e.key.key == SDLK_d) &&
                        (e.key.mod & SDL_KMOD_CTRL) &&
                        (e.key.mod & SDL_KMOD_SHIFT)) {
                        debugModeEnabled = !debugModeEnabled;
                        if (!debugModeEnabled) {
                            defaultShowFpsCounter = false;
                            defaultShowDetailedDebugger = false;
                            defaultShowHitboxes = false;
                            defaultShowPlayerHitbox = false;
                            defaultShowDebugView = false;
                            defaultHideUnknownObjectTypes = true;
                            if (settingsTab == 2) openSettingsTab(0);
                        }
                        normalizeSettingsTab();
                        if (ctx.saveClientSettings) ctx.saveClientSettings();
                        continue;
                    }
                    if (settingsTab == IDX_SETTINGS_ACCOUNT && networkEditField != NetworkEditField::None) {
                        if (e.key.key == SDLK_ESCAPE) {
                            stopNetworkEditing();
                        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            if (networkEditField == NetworkEditField::LoginPassword) {
                                // Finalize IME composition before login so the latest touched text is committed.
                                stopNetworkEditing();
                                std::string err;
                                if (loginWithFirebase(err)) {
                                    syncNetworkSessionState();
                                    networkLoginPassword.clear();
                                    networkLoginPasswordCursor = 0;
                                    networkLoginStatus = std::string("Logged in as ") + levelServerAccountUsername;
                                    if (ctx.saveClientSettings) ctx.saveClientSettings();
                                    refreshAccountUi();
                                } else {
                                    networkLoginStatus = err.empty() ? "Login failed." : err;
                                }
                            } else {
                                stopNetworkEditing();
                            }
                        } else if (e.key.key == SDLK_BACKSPACE) {
                            popNetworkInput();
                            if (ctx.saveClientSettings) ctx.saveClientSettings();
                        } else if (e.key.key == SDLK_DELETE) {
                            deleteNetworkInputForward();
                            if (ctx.saveClientSettings) ctx.saveClientSettings();
                        } else if (e.key.key == SDLK_LEFT) {
                            moveNetworkCursor(-1);
                        } else if (e.key.key == SDLK_RIGHT) {
                            moveNetworkCursor(1);
                        } else if (e.key.key == SDLK_HOME) {
                            moveNetworkCursorToEdge(false);
                        } else if (e.key.key == SDLK_END) {
                            moveNetworkCursorToEdge(true);
                        } else if ((e.key.mod & SDL_KMOD_CTRL) && e.key.key == SDLK_v) {
                            const char* clip = SDL_GetClipboardText();
                            if (clip && *clip) {
                                appendNetworkInput(clip);
                                if (ctx.saveClientSettings) ctx.saveClientSettings();
                            }
                            if (clip) SDL_free((void*)clip);
                        }
                        continue;
                    }
                    if (waitingForControlKey) {
                        if (isBackKey) {
                            waitingForControlKey = false;
                            waitingControlIndex = -1;
                        } else {
                            SDL_Scancode* bind = controlBindingRef(waitingControlIndex);
                            if (bind && e.key.scancode > SDL_SCANCODE_UNKNOWN && e.key.scancode < SDL_SCANCODE_COUNT) {
                                *bind = e.key.scancode;
                                if (ctx.saveClientSettings) ctx.saveClientSettings();
                            }
                            waitingForControlKey = false;
                            waitingControlIndex = -1;
                        }
                        continue;
                    }
                    if (e.key.key == SDLK_TAB || e.key.key == SDLK_q || e.key.key == SDLK_e) {
                        cycleSettingsTab(1);
                        continue;
                    }
                    if (e.key.key == SDLK_b) {
                        setOptionalSidebar(!showOptionalSidebar);
                        continue;
                    }
                    {
                        int digitIdx = -1;
                        if (e.key.key == SDLK_1) digitIdx = 0;
                        else if (e.key.key == SDLK_2) digitIdx = 1;
                        else if (e.key.key == SDLK_3) digitIdx = 2;
                        else if (e.key.key == SDLK_4) digitIdx = 3;
                        else if (e.key.key == SDLK_5) digitIdx = 4;
                        else if (e.key.key == SDLK_6) digitIdx = 5;
                        else if (e.key.key == SDLK_7) digitIdx = 6;
                        else if (e.key.key == SDLK_8) digitIdx = 7;
                        else if (e.key.key == SDLK_9) digitIdx = 8;
                        else if (e.key.key == SDLK_0) digitIdx = 9;
                        if (digitIdx >= 0) {
                            const std::vector<int> tabs = visibleTabList();
                            if (digitIdx < (int)tabs.size()) {
                                openSettingsTab(tabs[digitIdx]);
                            }
                            continue;
                        }
                    }
                    if (settingsTab == IDX_SETTINGS_ACCOUNT) {
                        const int kNetworkCount = std::max(1, settingsRowsForTab(settingsTab));
                        if (navUp) {
                            settingsSelNetwork = (settingsSelNetwork + kNetworkCount - 1) % kNetworkCount;
                            ensureSettingsRowVisible(settingsSelNetwork);
                        }
                        if (navDown) {
                            settingsSelNetwork = (settingsSelNetwork + 1) % kNetworkCount;
                            ensureSettingsRowVisible(settingsSelNetwork);
                        }
                        if (isBackKey) {
                            setInSettings(false);
                            continue;
                        }
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || navLeft || navRight) {
                            activateCurrentSettingsSelection(0);
                        }
                        continue;
                    }
                    if (settingsTab == IDX_SAVES_TAB) {
                        const int kSaveCount = std::max(1, settingsRowsForTab(settingsTab));
                        if (navUp) {
                            settingsSelSaves = (settingsSelSaves + kSaveCount - 1) % kSaveCount;
                            ensureSettingsRowVisible(settingsSelSaves);
                        }
                        if (navDown) {
                            settingsSelSaves = (settingsSelSaves + 1) % kSaveCount;
                            ensureSettingsRowVisible(settingsSelSaves);
                        }
                        if (isBackKey) {
                            setInSettings(false);
                            continue;
                        }
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
                            navLeft || navRight) {
                            activateCurrentSettingsSelection(0);
                        }
                        continue;
                    }
                    if (settingsTab == IDX_SETTINGS_FILEINFO) {
                        if (isBackKey || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            setInSettings(false);
                        }
                        continue;
                    }
                    if (isExtraTab(settingsTab)) {
                        const int extraRows = settingsRowsForTab(settingsTab);
                        if (navUp) {
                            settingsSel = (settingsSel + extraRows - 1) % extraRows;
                            ensureSettingsRowVisible(settingsSel);
                        }
                        if (navDown) {
                            settingsSel = (settingsSel + 1) % extraRows;
                            ensureSettingsRowVisible(settingsSel);
                        }
                        if (isBackKey) {
                            setInSettings(false);
                            continue;
                        }
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
                            navLeft || navRight) {
                            activateCurrentSettingsSelection(0);
                        }
                        continue;
                    }
                    if (navUp) {
                        settingsSel = (settingsSel + kSettingsCount - 1) % kSettingsCount;
                        ensureSettingsRowVisible(settingsSel);
                    }
                    if (navDown) {
                        settingsSel = (settingsSel + 1) % kSettingsCount;
                        ensureSettingsRowVisible(settingsSel);
                    }
                    if (e.key.key == SDLK_v) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    if (e.key.key == SDLK_c) clampCamX = !clampCamX;
                    if (e.key.key == SDLK_f) defaultShowFpsCounter = !defaultShowFpsCounter;
                    if (e.key.key == SDLK_g) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                    if (e.key.key == SDLK_h) defaultShowHitboxes = !defaultShowHitboxes;
                    if (e.key.key == SDLK_p) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                    if (e.key.key == SDLK_d) defaultShowDebugView = !defaultShowDebugView;
#if defined(_WIN32)
                    if (e.key.key == SDLK_u && ctx.launchUpdater) {
                        openSettingsTab(IDX_UPDATER_TAB);
                        continue;
                    }
#endif
                    if (isBackKey) setInSettings(false);

                    if (settingsTab == 1) {
                        constexpr int kAudioCount = 5;
                        if (navUp) settingsSelAudio = (settingsSelAudio + kAudioCount - 1) % kAudioCount;
                        if (navDown) settingsSelAudio = (settingsSelAudio + 1) % kAudioCount;
                        if (navUp || navDown) {
                            ensureSettingsRowVisible(settingsSelAudio);
                        }
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
                            navLeft || navRight) {
                            const int dir = navLeft ? -1 : (navRight ? 1 : 0);
                            activateCurrentSettingsSelection(dir);
                        }
                        continue;
                    }
                    if (settingsTab == 2) {
                        constexpr int kDebugCount = 9;
                        if (navUp) settingsSelDebug = (settingsSelDebug + kDebugCount - 1) % kDebugCount;
                        if (navDown) settingsSelDebug = (settingsSelDebug + 1) % kDebugCount;
                        if (navUp || navDown) {
                            ensureSettingsRowVisible(settingsSelDebug);
                        }
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            activateCurrentSettingsSelection(0);
                        }
                        continue;
                    }
                    if (settingsTab == IDX_SETTINGS_CONTROLS) {
                        constexpr int kControlsCount = 6;
                        if (navUp) {
                            settingsSelControls = (settingsSelControls + kControlsCount - 1) % kControlsCount;
                            ensureSettingsRowVisible(settingsSelControls);
                        }
                        if (navDown) {
                            settingsSelControls = (settingsSelControls + 1) % kControlsCount;
                            ensureSettingsRowVisible(settingsSelControls);
                        }
                        if (isBackKey) {
                            setInSettings(false);
                            continue;
                        }
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            activateCurrentSettingsSelection(0);
                        }
                        continue;
                    }
                    if (settingsTab == IDX_SETTINGS_ABOUT) {
                        if (navUp) scrollAboutBy(-24);
                        if (navDown) scrollAboutBy(24);
                        if (e.key.key == SDLK_PAGEUP) scrollAboutBy(-96);
                        if (e.key.key == SDLK_PAGEDOWN) scrollAboutBy(96);
                        if (isBackKey || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) setInSettings(false);
                        continue;
                    }

                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
                        navLeft || navRight) {
                        const int dir = navLeft ? -1 : (navRight ? 1 : 0);
                        activateCurrentSettingsSelection(dir);
                    }
                }
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                if (inSettings && !closeMenuOpen) {
                    if (settingsTab == IDX_SETTINGS_ABOUT) scrollAboutBy(-e.wheel.y * 24);
                    else scrollSettingsBy(-e.wheel.y * 24);
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                sliderDrag = SliderDragTarget::None;
                scrollbarDrag = ScrollbarDragTarget::None;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                // Some platforms emit synthetic mouse clicks for taps; ignore only near-immediate
                // events that occur at the same touch location to avoid dropping real mouse input.
                if (isLikelySyntheticMouseFromTouch(e.button.x, e.button.y)) continue;
                SDL_Point pt{};
                if (!mouseToGamePoint(e.button.x, e.button.y, pt)) continue;
                if (closeMenuOpen) {
                    SDL_Rect modal = quitModalRect();
                    SDL_Rect resumeBtn = quitResumeBtnRect(modal);
                    SDL_Rect closeBtn = quitCloseBtnRect(modal);
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
                    if (SDL_PointInRect(&pt, &settingsBtn)) {
                        menuSel = 0;
                        openSettingsTab(0);
                        setInSettings(true);
                        continue;
                    }
                    SDL_Rect playBtn = mainMenuBtnRect(1);
                    SDL_Rect editorBtn = mainMenuBtnRect(2);
                    if (SDL_PointInRect(&pt, &playBtn)) { menuSel = 1; if (mainMenuSelectionAction(menuSel)) return FrontendAction::StartGame; continue; }
                    if (SDL_PointInRect(&pt, &editorBtn)) { menuSel = 2; if (mainMenuSelectionAction(menuSel)) return FrontendAction::StartGame; continue; }
                } else {
                    SDL_Rect sidebarToggleBtn = settingsSidebarToggleRect();
                    if (SDL_PointInRect(&pt, &sidebarToggleBtn)) {
                        setOptionalSidebar(!showOptionalSidebar);
                        continue;
                    }
                    const std::vector<int> tabs = sidebarTabList();
                    for (int vi = 0; vi < (int)tabs.size(); ++vi) {
                        const int ti = tabs[vi];
                        SDL_Rect tr = settingsTabBtn(vi);
                        if (SDL_PointInRect(&pt, &tr)) {
                            openSettingsTab(ti);
                            continue;
                        }
                    }
                    if (settingsTab == IDX_SETTINGS_ABOUT && aboutMaxScroll() > 0) {
                        SDL_Rect scrollbarTrack = settingsScrollbarTrackRect();
                        SDL_Rect scrollbarThumb = aboutScrollbarThumbRect();
                        if (SDL_PointInRect(&pt, &scrollbarTrack)) {
                            if (SDL_PointInRect(&pt, &scrollbarThumb)) {
                                scrollbarDrag = ScrollbarDragTarget::About;
                                setAboutScrollFromY(pt.y);
                            } else {
                                pageAboutScrollFromY(pt.y);
                            }
                            continue;
                        }
                    }
                    if (settingsTab == IDX_SETTINGS_ABOUT) {
                        if (showExperimentalFeatures) {
                            SDL_Rect mainLevelSelectRect = aboutOpenMainLevelSelectRect();
                            if (mainLevelSelectRect.w > 0 && mainLevelSelectRect.h > 0 &&
                                SDL_PointInRect(&pt, &mainLevelSelectRect)) {
                                if (tryStartLevelSelect()) return FrontendAction::StartGame;
                                continue;
                            }
                        }
                        continue;
                    }
                    if (settingsTab == IDX_SETTINGS_FILEINFO) {
                        SDL_Rect backRect = fileInfoBackRect();
                        if (SDL_PointInRect(&pt, &backRect)) {
                            setInSettings(false);
                        }
                        continue;
                    }
                    if (settingsTab != IDX_SETTINGS_CONTROLS && settingsTab != IDX_SETTINGS_ABOUT && settingsMaxScroll(settingsTab) > 0) {
                        SDL_Rect scrollbarTrack = settingsScrollbarTrackRect();
                        SDL_Rect scrollbarThumb = settingsScrollbarThumbRect();
                        if (SDL_PointInRect(&pt, &scrollbarTrack)) {
                            if (SDL_PointInRect(&pt, &scrollbarThumb)) {
                                scrollbarDrag = ScrollbarDragTarget::Settings;
                                setSettingsScrollFromY(pt.y);
                            } else {
                                pageSettingsScrollFromY(pt.y);
                            }
                            continue;
                        }
                    }
                    if (settingsTab == IDX_SETTINGS_CONTROLS) {
                        for (int i = 0; i < 6; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            if (!SDL_PointInRect(&pt, &row)) continue;
                            settingsSelControls = i;
                            if (i >= 0 && i <= 4) {
                                waitingForControlKey = true;
                                waitingControlIndex = i;
                            } else {
                                setInSettings(false);
                            }
                            break;
                        }
                        continue;
                    }
                    if (settingsTab == IDX_SAVES_TAB) {
                        const int rows = std::max(1, settingsRowsForTab(settingsTab));
                        settingsSelSaves = std::clamp(settingsSelSaves, 0, rows - 1);
                        for (int i = 0; i < rows; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            if (!SDL_PointInRect(&pt, &row)) continue;
                            settingsSelSaves = i;
                            if (i >= 0 && i <= 2) {
                                activeSaveSlotIndex = i;
                                if (ctx.saveClientSettings) ctx.saveClientSettings();
                            } else if (i == 3) {
                                if (saveSlotExists(activeSaveSlotIndex)) {
                                    activateCurrentSettingsSelection(0);
                                }
                            } else {
                                setInSettings(false);
                            }
                            break;
                        }
                        continue;
                    }
                    if (settingsTab == 9) {
                        auto pointInPaddedRect = [&](const SDL_Rect& r, int pad = 10) -> bool {
                            SDL_Rect rr{r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2};
                            return SDL_PointInRect(&pt, &rr);
                        };
                        const bool hasUser = !levelServerAccountUsername.empty();
                        const bool hasToken = !levelServerAuthToken.empty();
                        const int rows = settingsRowsForTab(settingsTab);
                        for (int i = 0; i < rows; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            const int pad = (i <= 1) ? 14 : 10; // Email/password rows get extra touch room.
                            if (!pointInPaddedRect(row, pad)) continue;
                            if (!hasUser && !hasToken && (i == 0 || i == 1)) {
                                setNetworkCursorFromPoint(i, pt.x);
                            }
                            settingsSelNetwork = i;
                            activateCurrentSettingsSelection(0);
                            if (ctx.saveClientSettings) ctx.saveClientSettings();
                            break;
                        }
                        continue;
                    }
                    if (settingsTab == IDX_UPDATER_TAB) {
                        const int rows = settingsRowsForTab(settingsTab);
                        for (int i = 0; i < rows; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            if (!SDL_PointInRect(&pt, &row)) continue;
                            settingsSel = i;
                            activateCurrentSettingsSelection(0);
                            break;
                        }
                        continue;
                    }
                    if (isExtraTab(settingsTab)) {
                        const int extraIdx = extraTabIndex(settingsTab);
                        const int extraRows = settingsRowsForTab(settingsTab);
                        for (int i = 0; i < extraRows; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            if (!SDL_PointInRect(&pt, &row)) continue;
                            settingsSel = i;
                            const int optionIdx = extraTabOptionAtVisibleRow(settingsTab, i);
                            if (optionIdx >= 0) {
                                bool& v = extraTabValueRef(extraIdx, optionIdx);
                                v = !v;
                            }
                            else setInSettings(false);
                            break;
                        }
                        continue;
                    }
                    if (settingsTab == 1) {
                        SDL_Rect row0 = settingsRowBtn(0);
                        SDL_Rect row1 = settingsRowBtn(1);
                        SDL_Rect row2 = settingsRowBtn(2);
                        SDL_Rect row3 = settingsRowBtn(3);
                        SDL_Rect row4 = settingsRowBtn(4);
                        SDL_Rect musicSlider = musicSliderRect();
                        SDL_Rect sfxSlider = sfxSliderRect();
                        SDL_Rect musicSliderHit = sliderHitRect(musicSlider);
                        SDL_Rect sfxSliderHit = sliderHitRect(sfxSlider);
                        if (SDL_PointInRect(&pt, &musicSliderHit)) {
                            musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                            sliderDrag = SliderDragTarget::Music;
                        } else if (SDL_PointInRect(&pt, &sfxSliderHit)) {
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
                        if (SDL_PointInRect(&pt, &row0)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                        else if (SDL_PointInRect(&pt, &row1)) defaultShowHitboxes = !defaultShowHitboxes;
                        else if (SDL_PointInRect(&pt, &row2)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                        else if (SDL_PointInRect(&pt, &row3)) defaultShowDebugView = !defaultShowDebugView;
                        else if (SDL_PointInRect(&pt, &row4)) defaultHideUnknownObjectTypes = !defaultHideUnknownObjectTypes;
                        else if (SDL_PointInRect(&pt, &row5)) setInSettings(false);
                        settingsSelDebug = SDL_PointInRect(&pt, &row0) ? 0 :
                                           SDL_PointInRect(&pt, &row1) ? 1 :
                                           SDL_PointInRect(&pt, &row2) ? 2 :
                                           SDL_PointInRect(&pt, &row3) ? 3 :
                                           SDL_PointInRect(&pt, &row4) ? 4 :
                                           SDL_PointInRect(&pt, &row5) ? 5 : settingsSelDebug;
                        continue;
                    }
                    SDL_Rect aboutBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_ABOUT));
                    SDL_Rect backBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_BACK));
#if defined(__ANDROID__)
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect uiScaleBtn = settingsRowBtn(IDX_UI_SCALE);
                    SDL_Rect uiScaleSlider = uiScaleSliderRect();
                    SDL_Rect uiScaleSliderHit = sliderHitRect(uiScaleSlider);
                    SDL_Rect uiEdgePaddingBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_UI_EDGE_PADDING));
                    SDL_Rect uiEdgePaddingSlider = uiEdgePaddingSliderRect();
                    SDL_Rect uiEdgePaddingSliderHit = sliderHitRect(uiEdgePaddingSlider);
                    SDL_Rect fpsBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_FPS));
                    SDL_Rect powerMgmtBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_POWER_MANAGEMENT));
                    SDL_Rect lowPowerBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LOW_POWER_MODE));
                    SDL_Rect experimentalBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_EXPERIMENTAL));
                    SDL_Rect levelSelectBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LEVEL_SELECT));
                    if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &uiScaleSliderHit) || SDL_PointInRect(&pt, &uiScaleBtn)) {
                        uiScalePercent = uiScalePercentFromPoint(pt.x, uiScaleSlider);
                        sliderDrag = SliderDragTarget::UiScale;
                    }
                    else if (SDL_PointInRect(&pt, &uiEdgePaddingSliderHit) || SDL_PointInRect(&pt, &uiEdgePaddingBtn)) {
                        uiEdgePadding = uiEdgePaddingFromPoint(pt.x, uiEdgePaddingSlider);
                        sliderDrag = SliderDragTarget::UiEdgePadding;
                    }
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &powerMgmtBtn)) powerManagementEnabled = !powerManagementEnabled;
                    else if (SDL_PointInRect(&pt, &lowPowerBtn)) lowPowerModeEnabled = !lowPowerModeEnabled;
                    else if (SDL_PointInRect(&pt, &experimentalBtn)) showExperimentalFeatures = !showExperimentalFeatures;
                    else if (SDL_PointInRect(&pt, &levelSelectBtn)) levelSelectEnabled = !levelSelectEnabled;
                    else if (SDL_PointInRect(&pt, &aboutBtn)) { openSettingsTab(IDX_SETTINGS_ABOUT); }
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
#else
                    SDL_Rect fullBtn = settingsRowBtn(IDX_FULLSCREEN);
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect uiScaleBtn = settingsRowBtn(IDX_UI_SCALE);
                    SDL_Rect uiScaleSlider = uiScaleSliderRect();
                    SDL_Rect uiScaleSliderHit = sliderHitRect(uiScaleSlider);
                    SDL_Rect uiEdgePaddingBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_UI_EDGE_PADDING));
                    SDL_Rect uiEdgePaddingSlider = uiEdgePaddingSliderRect();
                    SDL_Rect uiEdgePaddingSliderHit = sliderHitRect(uiEdgePaddingSlider);
                    SDL_Rect fpsBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_FPS));
                    SDL_Rect powerMgmtBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_POWER_MANAGEMENT));
                    SDL_Rect lowPowerBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LOW_POWER_MODE));
                    SDL_Rect experimentalBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_EXPERIMENTAL));
                    SDL_Rect levelSelectBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LEVEL_SELECT));
#if defined(_WIN32)
                    SDL_Rect updateBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_UPDATE));
#endif
                    if (SDL_PointInRect(&pt, &fullBtn)) { (void)applyFullscreen(!fullscreen); }
                    else if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &uiScaleSliderHit) || SDL_PointInRect(&pt, &uiScaleBtn)) {
                        uiScalePercent = uiScalePercentFromPoint(pt.x, uiScaleSlider);
                        sliderDrag = SliderDragTarget::UiScale;
                    }
                    else if (SDL_PointInRect(&pt, &uiEdgePaddingSliderHit) || SDL_PointInRect(&pt, &uiEdgePaddingBtn)) {
                        uiEdgePadding = uiEdgePaddingFromPoint(pt.x, uiEdgePaddingSlider);
                        sliderDrag = SliderDragTarget::UiEdgePadding;
                    }
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &powerMgmtBtn)) powerManagementEnabled = !powerManagementEnabled;
                    else if (SDL_PointInRect(&pt, &lowPowerBtn)) lowPowerModeEnabled = !lowPowerModeEnabled;
                    else if (SDL_PointInRect(&pt, &experimentalBtn)) showExperimentalFeatures = !showExperimentalFeatures;
                    else if (SDL_PointInRect(&pt, &levelSelectBtn)) levelSelectEnabled = !levelSelectEnabled;
#if defined(_WIN32)
                    else if (SDL_PointInRect(&pt, &updateBtn) && ctx.launchUpdater) {
                        openSettingsTab(IDX_UPDATER_TAB);
                    }
#endif
                    else if (SDL_PointInRect(&pt, &aboutBtn)) { openSettingsTab(IDX_SETTINGS_ABOUT); }
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
#endif
                        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                }
            }
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                activeTouchFingers.insert(e.tfinger.fingerID);
                int winW = 0, winH = 0;
                getWindowSizeInPixelsCompat(ctx.win, winW, winH);
                int wx = (int)std::lround(e.tfinger.x * winW);
                int wy = (int)std::lround(e.tfinger.y * winH);
                lastTouchDownTicks = SDL_GetTicks();
                lastTouchDownWinX = wx;
                lastTouchDownWinY = wy;
                SDL_Point pt{};
                if (!mouseToGamePoint(wx, wy, pt)) continue;
                if (closeMenuOpen) {
                    SDL_Rect modal = quitModalRect();
                    SDL_Rect resumeBtn = quitResumeBtnRect(modal);
                    SDL_Rect closeBtn = quitCloseBtnRect(modal);
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
                        openSettingsTab(0);
                        setInSettings(true);
                        continue;
                    }
                    if (SDL_PointInRect(&pt, &playBtn)) { menuSel = 1; if (mainMenuSelectionAction(menuSel)) return FrontendAction::StartGame; continue; }
                    if (SDL_PointInRect(&pt, &editorBtn)) { menuSel = 2; if (mainMenuSelectionAction(menuSel)) return FrontendAction::StartGame; continue; }
                } else {
                    SDL_Rect sidebarToggleBtn = settingsSidebarToggleRect();
                    if (SDL_PointInRect(&pt, &sidebarToggleBtn)) {
                        setOptionalSidebar(!showOptionalSidebar);
                        continue;
                    }
                    const std::vector<int> tabs = sidebarTabList();
                    for (int vi = 0; vi < (int)tabs.size(); ++vi) {
                        const int ti = tabs[vi];
                        SDL_Rect tr = settingsTabBtn(vi);
                        if (SDL_PointInRect(&pt, &tr)) {
                            openSettingsTab(ti);
                            continue;
                        }
                    }
                    if (settingsTab == IDX_SETTINGS_ABOUT && aboutMaxScroll() > 0) {
                        SDL_Rect scrollbarTrack = settingsScrollbarTrackRect();
                        SDL_Rect scrollbarThumb = aboutScrollbarThumbRect();
                        if (SDL_PointInRect(&pt, &scrollbarTrack)) {
                            if (SDL_PointInRect(&pt, &scrollbarThumb)) {
                                scrollbarDrag = ScrollbarDragTarget::About;
                                scrollbarDragFinger = e.tfinger.fingerID;
                                setAboutScrollFromY(pt.y);
                            } else {
                                pageAboutScrollFromY(pt.y);
                            }
                            continue;
                        }
                    }
                    if (settingsTab == IDX_SETTINGS_FILEINFO) {
                        SDL_Rect backRect = fileInfoBackRect();
                        if (SDL_PointInRect(&pt, &backRect)) {
                            setInSettings(false);
                        }
                        continue;
                    }
                    if (settingsTab != IDX_SETTINGS_CONTROLS && settingsTab != IDX_SETTINGS_ABOUT && settingsMaxScroll(settingsTab) > 0) {
                        SDL_Rect scrollbarTrack = settingsScrollbarTrackRect();
                        SDL_Rect scrollbarThumb = settingsScrollbarThumbRect();
                        if (SDL_PointInRect(&pt, &scrollbarTrack)) {
                            if (SDL_PointInRect(&pt, &scrollbarThumb)) {
                                scrollbarDrag = ScrollbarDragTarget::Settings;
                                scrollbarDragFinger = e.tfinger.fingerID;
                                setSettingsScrollFromY(pt.y);
                            } else {
                                pageSettingsScrollFromY(pt.y);
                            }
                            continue;
                        }
                    }
                    if (settingsTab == IDX_SETTINGS_ABOUT) continue;
                    if (settingsTab == IDX_SETTINGS_CONTROLS) {
                        for (int i = 0; i < 6; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            if (!SDL_PointInRect(&pt, &row)) continue;
                            settingsSelControls = i;
                            if (i >= 0 && i <= 4) {
                                waitingForControlKey = true;
                                waitingControlIndex = i;
                            } else {
                                setInSettings(false);
                            }
                            break;
                        }
                        continue;
                    }
                    if (settingsTab == 9) {
                        auto pointInPaddedRect = [&](const SDL_Rect& r, int pad = 10) -> bool {
                            SDL_Rect rr{r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2};
                            return SDL_PointInRect(&pt, &rr);
                        };
                        const bool hasUser = !levelServerAccountUsername.empty();
                        const bool hasToken = !levelServerAuthToken.empty();
                        const int rows = settingsRowsForTab(settingsTab);
                        for (int i = 0; i < rows; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            const int pad = (i <= 1) ? 14 : 10;
                            if (!pointInPaddedRect(row, pad)) continue;
                            if (!hasUser && !hasToken && (i == 0 || i == 1)) {
                                setNetworkCursorFromPoint(i, pt.x);
                            }
                            settingsSelNetwork = i;
                            activateCurrentSettingsSelection(0);
                            if (ctx.saveClientSettings) ctx.saveClientSettings();
                            break;
                        }
                        continue;
                    }
                    if (isExtraTab(settingsTab)) {
                        const int extraIdx = extraTabIndex(settingsTab);
                        const int extraRows = settingsRowsForTab(settingsTab);
                        for (int i = 0; i < extraRows; ++i) {
                            SDL_Rect row = settingsRowBtn(i);
                            if (!SDL_PointInRect(&pt, &row)) continue;
                            settingsSel = i;
                            const int optionIdx = extraTabOptionAtVisibleRow(settingsTab, i);
                            if (optionIdx >= 0) {
                                bool& v = extraTabValueRef(extraIdx, optionIdx);
                                v = !v;
                            }
                            else setInSettings(false);
                            break;
                        }
                        continue;
                    }
                    if (settingsTab == 1) {
                        SDL_Rect row0 = settingsRowBtn(0);
                        SDL_Rect row1 = settingsRowBtn(1);
                        SDL_Rect row2 = settingsRowBtn(2);
                        SDL_Rect row3 = settingsRowBtn(3);
                        SDL_Rect row4 = settingsRowBtn(4);
                        SDL_Rect musicSlider = musicSliderRect();
                        SDL_Rect sfxSlider = sfxSliderRect();
                        SDL_Rect musicSliderHit = sliderHitRect(musicSlider);
                        SDL_Rect sfxSliderHit = sliderHitRect(sfxSlider);
                        if (SDL_PointInRect(&pt, &musicSliderHit)) {
                            musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                            sliderDrag = SliderDragTarget::Music;
                            sliderDragFinger = e.tfinger.fingerID;
                        } else if (SDL_PointInRect(&pt, &sfxSliderHit)) {
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
                        if (SDL_PointInRect(&pt, &row0)) defaultShowDetailedDebugger = !defaultShowDetailedDebugger;
                        else if (SDL_PointInRect(&pt, &row1)) defaultShowHitboxes = !defaultShowHitboxes;
                        else if (SDL_PointInRect(&pt, &row2)) defaultShowPlayerHitbox = !defaultShowPlayerHitbox;
                        else if (SDL_PointInRect(&pt, &row3)) defaultShowDebugView = !defaultShowDebugView;
                        else if (SDL_PointInRect(&pt, &row4)) defaultHideUnknownObjectTypes = !defaultHideUnknownObjectTypes;
                        else if (SDL_PointInRect(&pt, &row5)) setInSettings(false);
                        settingsSelDebug = SDL_PointInRect(&pt, &row0) ? 0 :
                                           SDL_PointInRect(&pt, &row1) ? 1 :
                                           SDL_PointInRect(&pt, &row2) ? 2 :
                                           SDL_PointInRect(&pt, &row3) ? 3 :
                                           SDL_PointInRect(&pt, &row4) ? 4 :
                                           SDL_PointInRect(&pt, &row5) ? 5 : settingsSelDebug;
                        continue;
                    }
                    SDL_Rect aboutBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_ABOUT));
                    SDL_Rect backBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_BACK));
#if defined(__ANDROID__)
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect uiScaleBtn = settingsRowBtn(IDX_UI_SCALE);
                    SDL_Rect uiScaleSlider = uiScaleSliderRect();
                    SDL_Rect uiScaleSliderHit = sliderHitRect(uiScaleSlider);
                    SDL_Rect uiEdgePaddingBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_UI_EDGE_PADDING));
                    SDL_Rect uiEdgePaddingSlider = uiEdgePaddingSliderRect();
                    SDL_Rect uiEdgePaddingSliderHit = sliderHitRect(uiEdgePaddingSlider);
                    SDL_Rect fpsBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_FPS));
                    SDL_Rect powerMgmtBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_POWER_MANAGEMENT));
                    SDL_Rect lowPowerBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LOW_POWER_MODE));
                    SDL_Rect experimentalBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_EXPERIMENTAL));
                    SDL_Rect levelSelectBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LEVEL_SELECT));
                    if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &uiScaleSliderHit) || SDL_PointInRect(&pt, &uiScaleBtn)) {
                        uiScalePercent = uiScalePercentFromPoint(pt.x, uiScaleSlider);
                        sliderDrag = SliderDragTarget::UiScale;
                        sliderDragFinger = e.tfinger.fingerID;
                    }
                    else if (SDL_PointInRect(&pt, &uiEdgePaddingSliderHit) || SDL_PointInRect(&pt, &uiEdgePaddingBtn)) {
                        uiEdgePadding = uiEdgePaddingFromPoint(pt.x, uiEdgePaddingSlider);
                        sliderDrag = SliderDragTarget::UiEdgePadding;
                        sliderDragFinger = e.tfinger.fingerID;
                    }
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &powerMgmtBtn)) powerManagementEnabled = !powerManagementEnabled;
                    else if (SDL_PointInRect(&pt, &lowPowerBtn)) lowPowerModeEnabled = !lowPowerModeEnabled;
                    else if (SDL_PointInRect(&pt, &experimentalBtn)) showExperimentalFeatures = !showExperimentalFeatures;
                    else if (SDL_PointInRect(&pt, &levelSelectBtn)) levelSelectEnabled = !levelSelectEnabled;
                    else if (SDL_PointInRect(&pt, &aboutBtn)) { openSettingsTab(IDX_SETTINGS_ABOUT); }
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
#else
                    SDL_Rect fullBtn = settingsRowBtn(IDX_FULLSCREEN);
                    SDL_Rect vsyncBtn = settingsRowBtn(IDX_VSYNC);
                    SDL_Rect camBtn = settingsRowBtn(IDX_CAM_CLAMP);
                    SDL_Rect uiScaleBtn = settingsRowBtn(IDX_UI_SCALE);
                    SDL_Rect uiScaleSlider = uiScaleSliderRect();
                    SDL_Rect uiScaleSliderHit = sliderHitRect(uiScaleSlider);
                    SDL_Rect uiEdgePaddingBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_UI_EDGE_PADDING));
                    SDL_Rect uiEdgePaddingSlider = uiEdgePaddingSliderRect();
                    SDL_Rect uiEdgePaddingSliderHit = sliderHitRect(uiEdgePaddingSlider);
                    SDL_Rect fpsBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_FPS));
                    SDL_Rect powerMgmtBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_POWER_MANAGEMENT));
                    SDL_Rect lowPowerBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LOW_POWER_MODE));
                    SDL_Rect experimentalBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_SHOW_EXPERIMENTAL));
                    SDL_Rect levelSelectBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_LEVEL_SELECT));
#if defined(_WIN32)
                    SDL_Rect updateBtn = settingsRowBtn(visibleGeneralSettingsRowIndex(IDX_UPDATE));
#endif
                    if (SDL_PointInRect(&pt, &fullBtn)) { (void)applyFullscreen(!fullscreen); }
                    else if (SDL_PointInRect(&pt, &vsyncBtn)) { vsyncEnabled = !vsyncEnabled; applyRenderVsync(); }
                    else if (SDL_PointInRect(&pt, &camBtn)) clampCamX = !clampCamX;
                    else if (SDL_PointInRect(&pt, &uiScaleSliderHit) || SDL_PointInRect(&pt, &uiScaleBtn)) {
                        uiScalePercent = uiScalePercentFromPoint(pt.x, uiScaleSlider);
                        sliderDrag = SliderDragTarget::UiScale;
                        sliderDragFinger = e.tfinger.fingerID;
                    }
                    else if (SDL_PointInRect(&pt, &uiEdgePaddingSliderHit) || SDL_PointInRect(&pt, &uiEdgePaddingBtn)) {
                        uiEdgePadding = uiEdgePaddingFromPoint(pt.x, uiEdgePaddingSlider);
                        sliderDrag = SliderDragTarget::UiEdgePadding;
                        sliderDragFinger = e.tfinger.fingerID;
                    }
                    else if (SDL_PointInRect(&pt, &fpsBtn)) defaultShowFpsCounter = !defaultShowFpsCounter;
                    else if (SDL_PointInRect(&pt, &powerMgmtBtn)) powerManagementEnabled = !powerManagementEnabled;
                    else if (SDL_PointInRect(&pt, &lowPowerBtn)) lowPowerModeEnabled = !lowPowerModeEnabled;
                    else if (SDL_PointInRect(&pt, &experimentalBtn)) showExperimentalFeatures = !showExperimentalFeatures;
                    else if (SDL_PointInRect(&pt, &levelSelectBtn)) levelSelectEnabled = !levelSelectEnabled;
#if defined(_WIN32)
                    else if (SDL_PointInRect(&pt, &updateBtn) && ctx.launchUpdater) {
                        if (ctx.saveClientSettings) ctx.saveClientSettings();
                        (void)ctx.launchUpdater();
                    }
#endif
                    else if (SDL_PointInRect(&pt, &aboutBtn)) { openSettingsTab(IDX_SETTINGS_ABOUT); }
                    else if (SDL_PointInRect(&pt, &backBtn)) setInSettings(false);
#endif
                    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
                }
            }
            if (e.type == SDL_EVENT_FINGER_MOTION &&
                inSettings && sliderDrag != SliderDragTarget::None &&
                e.tfinger.fingerID == sliderDragFinger) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                getWindowSizeInPixelsCompat(ctx.win, winW, winH);
                int wx = (int)std::lround(e.tfinger.x * winW);
                int wy = (int)std::lround(e.tfinger.y * winH);
                if (!windowToGamePoint(wx, wy, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy, 1.0f)) continue;
                SDL_Point pt{gx, gy};
                SDL_Rect musicSlider = musicSliderRect();
                SDL_Rect sfxSlider = sfxSliderRect();
                SDL_Rect uiSlider = uiScaleSliderRect();
                SDL_Rect uiEdgeSlider = uiEdgePaddingSliderRect();
                if (sliderDrag == SliderDragTarget::Music) musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                if (sliderDrag == SliderDragTarget::Sfx) sfxVolume = sliderValueFromPoint(pt.x, sfxSlider);
                if (sliderDrag == SliderDragTarget::UiScale) uiScalePercent = uiScalePercentFromPoint(pt.x, uiSlider);
                if (sliderDrag == SliderDragTarget::UiEdgePadding) uiEdgePadding = uiEdgePaddingFromPoint(pt.x, uiEdgeSlider);
                if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
            }
            if (e.type == SDL_EVENT_FINGER_MOTION &&
                inSettings && scrollbarDrag != ScrollbarDragTarget::None &&
                e.tfinger.fingerID == scrollbarDragFinger) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                getWindowSizeInPixelsCompat(ctx.win, winW, winH);
                int wx = (int)std::lround(e.tfinger.x * winW);
                int wy = (int)std::lround(e.tfinger.y * winH);
                if (!windowToGamePoint(wx, wy, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy, 1.0f)) continue;
                if (scrollbarDrag == ScrollbarDragTarget::Settings) setSettingsScrollFromY(gy);
                if (scrollbarDrag == ScrollbarDragTarget::About) setAboutScrollFromY(gy);
            }
            if (e.type == SDL_EVENT_FINGER_UP && e.tfinger.fingerID == sliderDragFinger) {
                sliderDrag = SliderDragTarget::None;
            }
            if (e.type == SDL_EVENT_FINGER_UP && e.tfinger.fingerID == scrollbarDragFinger) {
                scrollbarDrag = ScrollbarDragTarget::None;
            }
            if (e.type == SDL_EVENT_FINGER_UP) {
                activeTouchFingers.erase(e.tfinger.fingerID);
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED && e.tfinger.fingerID == sliderDragFinger) {
                sliderDrag = SliderDragTarget::None;
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED && e.tfinger.fingerID == scrollbarDragFinger) {
                scrollbarDrag = ScrollbarDragTarget::None;
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED) {
                activeTouchFingers.erase(e.tfinger.fingerID);
            }
            if (e.type == SDL_MOUSEMOTION && inSettings && sliderDrag != SliderDragTarget::None) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                getWindowSizeInPixelsCompat(ctx.win, winW, winH);
                if (!windowToGamePoint(e.motion.x, e.motion.y, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy, 1.0f)) continue;
                SDL_Point pt{gx, gy};
                SDL_Rect musicSlider = musicSliderRect();
                SDL_Rect sfxSlider = sfxSliderRect();
                SDL_Rect uiSlider = uiScaleSliderRect();
                SDL_Rect uiEdgeSlider = uiEdgePaddingSliderRect();
                if (sliderDrag == SliderDragTarget::Music) musicVolume = sliderValueFromPoint(pt.x, musicSlider);
                if (sliderDrag == SliderDragTarget::Sfx) sfxVolume = sliderValueFromPoint(pt.x, sfxSlider);
                if (sliderDrag == SliderDragTarget::UiScale) uiScalePercent = uiScalePercentFromPoint(pt.x, uiSlider);
                if (sliderDrag == SliderDragTarget::UiEdgePadding) uiEdgePadding = uiEdgePaddingFromPoint(pt.x, uiEdgeSlider);
                if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
            }
            if (e.type == SDL_MOUSEMOTION && inSettings && scrollbarDrag != ScrollbarDragTarget::None) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                getWindowSizeInPixelsCompat(ctx.win, winW, winH);
                if (!windowToGamePoint(e.motion.x, e.motion.y, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy, 1.0f)) continue;
                if (scrollbarDrag == ScrollbarDragTarget::Settings) setSettingsScrollFromY(gy);
                if (scrollbarDrag == ScrollbarDragTarget::About) setAboutScrollFromY(gy);
            }
            if (startSavedGameRequested) {
                if (ctx.selectedLevelPath) {
                    *ctx.selectedLevelPath = kSavedGameSelectionToken;
                }
                cleanupMenuAssets();
                return FrontendAction::StartGame;
            }
            // Keep mouse/touch handling behavior in main for now to minimize risk.
        }

        SDL_SetRenderTarget(ctx.ren, gameTarget);
        SDL_SetRenderDrawColor(ctx.ren, 118, 225, 255, 255); // #76e1ff
        SDL_RenderClear(ctx.ren);
        if (menuBgTex && !menuBgFrames.empty()) {
            auto getBgFrame = [&](const char* name) -> const Frame* {
                auto it = menuBgFrames.find(name);
                if (it != menuBgFrames.end()) return &it->second;
                std::string withPng = std::string(name) + ".png";
                auto itPng = menuBgFrames.find(withPng);
                if (itPng != menuBgFrames.end()) return &itPng->second;
                return nullptr;
            };
            const Frame* bgBack = getBgFrame("far");
            const Frame* bgMiddle = getBgFrame("middle");
            if (!bgMiddle) bgMiddle = getBgFrame("mid");

            struct MenuParallaxLayer {
                const Frame* frame;
                float speed;
                float yBias;
                float alpha;
                float scaleMul;
            };
            MenuParallaxLayer layers[] = {
                {bgBack, 0.015f, 0.0f, 255.0f, 1.0f},
                {bgMiddle, 0.045f, 10.0f, 255.0f, 1.0f},
                {bgMiddle, 0.075f, 20.0f, 255.0f, 1.0f},
            };
            const Uint64 ticks = SDL_GetTicks();
            const float bgScale = std::clamp(menuCanvasScale() * mainMenuScale() * 0.80f, 0.5f, 3.0f);
            const float canvasScale = menuCanvasScale();
            const float canvasBottomY = menuCanvasOriginY() + kMenuAuthorH * canvasScale;
            for (const auto& layer : layers) {
                if (!layer.frame) continue;
                const int srcW = layer.frame->rotated ? layer.frame->rect.h : layer.frame->rect.w;
                const int srcH = layer.frame->rotated ? layer.frame->rect.w : layer.frame->rect.h;
                const float layerScale = std::clamp(bgScale * layer.scaleMul, 0.5f, 3.0f);
                int fw = std::max(1, (int)std::lround((float)srcW * layerScale));
                int fh = std::max(1, (int)std::lround((float)srcH * layerScale));
                float ox = std::fmod((float)ticks * layer.speed * layerScale, (float)fw);
                if (ox < 0.0f) ox += (float)fw;
                int y = (int)std::lround(canvasBottomY - (float)fh + layer.yBias * canvasScale);
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
            const char* mainLogoFrameName = levelSelectEnabled ? "Main_logo" : "lite_logo";
            const Frame* mainLogo = getMenuFrame(mainLogoFrameName, mainLogoTex);
            const Frame* btnSprite = getMenuFrame("btn_sprite", btnSpriteTex);
            bool btnSpriteIsGeneric = false;
            if (!btnSprite) {
                btnSprite = getMenuFrame("button_generic", btnSpriteTex);
                btnSpriteIsGeneric = (btnSprite != nullptr);
            }
            const Frame* playLogo = getMenuFrame("play_btn_logo", playLogoTex);
            const Frame* settingsLogo = getMenuFrame("settings_btn_logo", settingsLogoTex);
            const Frame* editorLogo = getMenuFrame("Editor_logo", editorLogoTex);
            if (mainLogoTex && mainLogo) {
                const float canvas = menuCanvasScale();
                const float s = mainMenuScale();
                const int logoW = std::max(120, (int)std::lround(640.0f * canvas * s));
                const int logoH = std::max(24, (int)std::lround(66.0f * canvas * s));
                if (logoW > mainLogo->rect.w || logoH > mainLogo->rect.h) {
                    // Don't upscale if it would cause significant blurring. Instead, just center the original texture.
                    SDL_Rect dst{(ctx.baseScreenW - mainLogo->rect.w) / 2, (int)std::lround(menuCanvasOriginY() + 75.0f * canvas) - mainLogo->rect.h / 2, mainLogo->rect.w, mainLogo->rect.h};
                    renderFrame(ctx.ren, mainLogoTex, *mainLogo, dst);
                } else {
                const int logoCX = (int)std::lround(menuCanvasOriginX() + 480.0f * canvas);
                const int logoCY = (int)std::lround(menuCanvasOriginY() + 75.0f * canvas);
                SDL_Rect dst{logoCX - logoW / 2, logoCY - logoH / 2, logoW, logoH};
                renderFrame(ctx.ren, mainLogoTex, *mainLogo, dst);
                }
            }
            int menubuttoncount = 0;
            SDL_Rect menuBtns[4];
            if (levelSelectEnabled) {
                menuBtns[0] = mainMenuBtnRect(0);
                menuBtns[1] = mainMenuBtnRect(1);
                menuBtns[2] = mainMenuBtnRect(2);
                menubuttoncount = 3;
            } else {
                menuBtns[0] = mainMenuBtnRect(0);
                menuBtns[1] = mainMenuBtnRect(1);
                menubuttoncount = 2;
            }
            if (btnSpriteTex && btnSprite) {
                for (int i = 0; i < menubuttoncount; ++i) {
                    if (btnSpriteIsGeneric) renderOpaqueCenterLoopedFrame(btnSpriteTex, *btnSprite, menuBtns[i]);
                    else renderFrame(ctx.ren, btnSpriteTex, *btnSprite, menuBtns[i]);
                }
            }
            for (int i = 0; i < menubuttoncount; ++i) {
                if (i == menuSel) {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_ADD);
                    SDL_SetRenderDrawColor(ctx.ren, 45, 45, 45, 255);
                    SDL_RenderFillRect(ctx.ren, &menuBtns[i]);
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(ctx.ren, 180, 200, 230, 255);
                    SDL_RenderRect(ctx.ren, &menuBtns[i]);
                    }
            }
            if (settingsLogoTex && settingsLogo) {
                SDL_Rect b = mainMenuBtnRect(0);
                SDL_Rect dst{
                    b.x + (int)std::lround((float)b.w * 0.18f),
                    b.y + (int)std::lround((float)b.h * 0.14f),
                    (int)std::lround((float)b.w * 0.64f),
                    (int)std::lround((float)b.h * 0.64f)
                };
                renderFrame(ctx.ren, settingsLogoTex, *settingsLogo, dst);
            }
            const int playButtonIndex = 1;
            const int editorButtonIndex = 2;
            if (playLogoTex && playLogo) {
                SDL_Rect b = mainMenuBtnRect(playButtonIndex);
                SDL_Rect dst{
                    b.x + (int)std::lround((float)b.w * 0.27f),
                    b.y + (int)std::lround((float)b.h * 0.11f),
                    (int)std::lround((float)b.w * 0.46f),
                    (int)std::lround((float)b.h * 0.79f)
                };
                renderFrame(ctx.ren, playLogoTex, *playLogo, dst);
            }
            if (editorLogoTex && editorLogo && levelSelectEnabled) {
                SDL_Rect b = mainMenuBtnRect(editorButtonIndex);
                SDL_Rect dst{
                    b.x + (int)std::lround((float)b.w * 0.30f),
                    b.y + (int)std::lround((float)b.h * 0.14f),
                    (int)std::lround((float)b.w * 0.39f),
                    (int)std::lround((float)b.h * 0.71f)
                };
                if (levelSelectEnabled){
                    renderFrame(ctx.ren, editorLogoTex, *editorLogo, dst);
                }
            }
            if ((!settingsLogoTex || !settingsLogo) || (!playLogoTex || !playLogo) || (!editorLogoTex || !editorLogo)) {
                for (int i = 0; i < menubuttoncount; ++i) {
                    const std::string label = mainMenuLabelForIndex(i);
                    const int tw = MeasureTextWidth(2, label);
                    const int ty = menuBtns[i].y + (int)std::lround((float)menuBtns[i].h * 0.73f);
                    DrawText(ctx.ren, menuBtns[i].x + (menuBtns[i].w - tw) / 2, ty, 2, label);
                }
            }
            if (!mainLogoTex || !mainLogo) {
                const std::string title = levelSelectEnabled ? "Dorfplatformer Timetravel" : "Dorfplatformer Timetravel Lite";
                const int titleY = (int)std::lround(menuCanvasOriginY() + 84.0f * menuCanvasScale());
                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(3, title) / 2, titleY, 3, title);
            }
            std::string versionText = std::string("v") + (ctx.versionString.empty() ? "dev" : ctx.versionString);
            const std::string copyrightText = "Copyright (c) Benno111 2024 - 2026";
            const int footerScale = std::max(2, (int)std::lround(2.0f * menuCanvasScale() * mainMenuScale()));
            const int footerY = (int)std::lround(menuCanvasOriginY() + 496.0f * menuCanvasScale());
            const int edgePad = std::max(8, (int)std::lround(12.0f * menuCanvasScale()));
            DrawText(ctx.ren, edgePad, footerY, footerScale, versionText);
            DrawText(ctx.ren, ctx.baseScreenW - edgePad - MeasureTextWidth(footerScale, copyrightText), footerY, footerScale, copyrightText);
            if (closeMenuOpen) {
                constexpr Uint8 kQuitDimAlpha = 150;
                constexpr Uint8 kQuitWindowAlpha = 236;
                constexpr Uint8 kQuitButtonAlpha = 228;
                constexpr Uint8 kQuitSelectAlpha = 56;
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, kQuitDimAlpha);
                SDL_Rect dim{0, 0, ctx.baseScreenW, ctx.baseScreenH};
                SDL_RenderFillRect(ctx.ren, &dim);

                SDL_Rect modal = quitModalRect();
                SDL_Texture* popupWindowTex = nullptr;
                const Frame* popupWindow = getMenuFrame("window", popupWindowTex);
                if (!popupWindow) popupWindow = getMenuFrame("window.png", popupWindowTex);
                if (popupWindow && popupWindowTex) {
                    SDL_SetTextureBlendMode(popupWindowTex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(popupWindowTex, kQuitWindowAlpha);
                    renderFrame(ctx.ren, popupWindowTex, *popupWindow, modal);
                    SDL_SetTextureAlphaMod(popupWindowTex, 255);
                } else {
                    SDL_SetRenderDrawColor(ctx.ren, 26, 32, 42, kQuitWindowAlpha);
                    SDL_RenderFillRect(ctx.ren, &modal);
                    SDL_SetRenderDrawColor(ctx.ren, 170, 190, 220, 255);
                    SDL_RenderDrawRect(ctx.ren, &modal);
                }
                DrawText(ctx.ren, modal.x + (modal.w - MeasureTextWidth(2, "CLOSE GAME?")) / 2, modal.y + (int)std::lround(18.0f * uiButtonScale()), 2, "CLOSE GAME?");

                SDL_Rect resumeBtn = quitResumeBtnRect(modal);
                SDL_Rect closeBtn = quitCloseBtnRect(modal);
                SDL_Texture* popupBtnTex = nullptr;
                const Frame* popupBtnFrame = getMenuFrame("button_genreaic", popupBtnTex);
                if (!popupBtnFrame) popupBtnFrame = getMenuFrame("button_genreaic.png", popupBtnTex);
                if (popupBtnFrame && popupBtnTex) {
                    SDL_SetTextureBlendMode(popupBtnTex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(popupBtnTex, kQuitButtonAlpha);
                    renderFrame(ctx.ren, popupBtnTex, *popupBtnFrame, resumeBtn);
                    renderFrame(ctx.ren, popupBtnTex, *popupBtnFrame, closeBtn);
                    SDL_SetTextureAlphaMod(popupBtnTex, 255);
                } else {
                    SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 0 ? 120 : 70, 95, 85, kQuitButtonAlpha);
                    SDL_RenderFillRect(ctx.ren, &resumeBtn);
                    SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 1 ? 120 : 70, 70, 75, kQuitButtonAlpha);
                    SDL_RenderFillRect(ctx.ren, &closeBtn);
                }
                // Keep selection opacity consistent regardless of textured/fallback button path.
                SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, kQuitSelectAlpha);
                SDL_Rect selectedBtn = (closeMenuSel == 0) ? resumeBtn : closeBtn;
                SDL_RenderFillRect(ctx.ren, &selectedBtn);
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ctx.ren, 190, 200, 220, 255);
                SDL_RenderRect(ctx.ren, &resumeBtn);
                SDL_RenderRect(ctx.ren, &closeBtn);
                const int labelLineH = std::max(10, (int)std::lround(16.0f * uiButtonScale()));
                DrawText(ctx.ren, resumeBtn.x + (resumeBtn.w - MeasureTextWidth(2, "RESUME")) / 2, resumeBtn.y + (resumeBtn.h - labelLineH) / 2, 2, "RESUME");
                DrawText(ctx.ren, closeBtn.x + (closeBtn.w - MeasureTextWidth(2, "CLOSE")) / 2, closeBtn.y + (closeBtn.h - labelLineH) / 2, 2, "CLOSE");
            }
        } else {
            const std::vector<int> tabs = sidebarTabList();
            SDL_Rect settingsPanel = settingsPanelRect();
            const Frame* settingsWindowFrame = nullptr;
            SDL_Texture* settingsWindowTex = menuFallbackTex;
            if (settingsWindowTex) {
                auto itWindow = menuFallbackFrames.find("window");
                if (itWindow != menuFallbackFrames.end()) settingsWindowFrame = &itWindow->second;
                if (!settingsWindowFrame) {
                    auto itWindowPng = menuFallbackFrames.find("window.png");
                    if (itWindowPng != menuFallbackFrames.end()) settingsWindowFrame = &itWindowPng->second;
                }
            }
            if (settingsWindowFrame && settingsWindowTex) {
                SDL_SetTextureBlendMode(settingsWindowTex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(settingsWindowTex, 236);
                renderCenterLoopedFrame(settingsWindowTex, *settingsWindowFrame, settingsPanel);
                SDL_SetTextureAlphaMod(settingsWindowTex, 255);
            } else {
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx.ren, 26, 32, 42, 236);
                SDL_RenderFillRect(ctx.ren, &settingsPanel);
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ctx.ren, 170, 190, 220, 255);
                SDL_RenderRect(ctx.ren, &settingsPanel);
            }
            const std::string title = "SETTINGS";
            const int titleScale = std::clamp((int)std::lround(2.0f + 0.6f * settingsMenuScale()), 3, 5);
            const int titleX = ctx.baseScreenW / 2 - MeasureTextWidth(titleScale, title) / 2;
            const int titleY = settingsPanel.y + std::max(10, (int)std::lround(12.0f * settingsMenuScale()));
            DrawText(ctx.ren, titleX, titleY, titleScale, title);
            SDL_Texture* checkboxActiveTex = nullptr;
            SDL_Texture* checkboxInactiveTex = nullptr;
            SDL_Texture* toggleButtonTex = nullptr;
            const Frame* checkboxActive = getMenuFrame("checkbox_active", checkboxActiveTex);
            if (!checkboxActive) checkboxActive = getMenuFrame("checkbox_active.png", checkboxActiveTex);
            const Frame* checkboxInactive = getMenuFrame("checkbox_disabled", checkboxInactiveTex);
            if (!checkboxInactive) checkboxInactive = getMenuFrame("checkbox_disabled.png", checkboxInactiveTex);
            const Frame* toggleButtonFrame = getMenuFrame("button_genreaic", toggleButtonTex);
            if (!toggleButtonFrame) toggleButtonFrame = getMenuFrame("button_genreaic.png", toggleButtonTex);
            auto drawChromeButton = [&](const SDL_Rect& r, bool selected) {
                if (toggleButtonFrame && toggleButtonTex) {
                    renderOpaqueCenterLoopedFrame(toggleButtonTex, *toggleButtonFrame, r);
                } else {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(ctx.ren, selected ? 88 : 60, 92, 110, 255);
                    SDL_RenderFillRect(ctx.ren, &r);
                }
                SDL_SetRenderDrawColor(ctx.ren, selected ? 210 : 180, 200, 230, 255);
                SDL_RenderDrawRect(ctx.ren, &r);
            };
            SDL_Rect sidebarToggleBtn = settingsSidebarToggleRect();
            drawChromeButton(sidebarToggleBtn, showOptionalSidebar);
            const std::string sidebarLabel = std::string("SIDEBAR: ") + (showOptionalSidebar ? "ON" : "OFF");
            DrawText(ctx.ren,
                     sidebarToggleBtn.x + (sidebarToggleBtn.w - MeasureTextWidth(2, sidebarLabel)) / 2,
                     sidebarToggleBtn.y + std::max(3, (sidebarToggleBtn.h - std::max(10, (int)std::lround(16.0f * uiButtonScale()))) / 2),
                     2,
                     sidebarLabel);
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
                const int boxSize = std::clamp(r.h - std::max(8, (int)std::lround(10.0f * settingsMenuScale())), 18, 42);
                const int boxPadX = std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
                const int boxY = r.y + (r.h - boxSize) / 2;
                if (frame && tex) {
                    SDL_Rect dst{r.x + boxPadX, boxY, boxSize, boxSize};
                    renderFrame(ctx.ren, tex, *frame, dst);
                    return;
                }
                SDL_Rect box{r.x + boxPadX, boxY, boxSize, boxSize};
                SDL_SetRenderDrawColor(ctx.ren, 180, 200, 230, 255);
                SDL_RenderRect(ctx.ren, &box);
                if (enabled) {
                    SDL_SetRenderDrawColor(ctx.ren, 190, 240, 170, 255);
                    const int inset = std::max(3, box.w / 5);
                    SDL_Rect fill{box.x + inset, box.y + inset, std::max(1, box.w - inset * 2), std::max(1, box.h - inset * 2)};
                    SDL_RenderFillRect(ctx.ren, &fill);
                }
            };
            const int settingsRowTextScale = std::clamp((int)std::lround(2.0f + 0.5f * settingsMenuScale()), 2, 4);
            const int settingsTabTextScale = std::clamp((int)std::lround(2.0f + 0.35f * settingsMenuScale()), 2, 3);
            auto drawRowText = [&](int rowIdx, const std::string& text) {
                SDL_Rect row = settingsRowBtn(rowIdx);
                const int leftInset = std::max(24, row.h);
                const int textW = MeasureTextWidth(settingsRowTextScale, text);
                const int tx = row.x + leftInset + std::max(0, (row.w - leftInset - textW) / 2);
                const int ty = row.y + std::max(0, (row.h - 10 * settingsRowTextScale) / 2);
                DrawText(ctx.ren, tx, ty, settingsRowTextScale, text);
            };
            for (int vi = 0; vi < (int)tabs.size(); ++vi) {
                const int ti = tabs[vi];
                SDL_Rect tr = settingsTabBtn(vi);
                drawChromeButton(tr, settingsTab == ti);
                const std::string& tabLabel = (ti >= 0 && ti < (int)settingsTabLabels.size()) ? settingsTabLabels[ti] : std::string("TAB");
                const int ttx = tr.x + (tr.w - MeasureTextWidth(settingsTabTextScale, tabLabel)) / 2;
                const int tty = tr.y + std::max(0, (tr.h - 10 * settingsTabTextScale) / 2);
                DrawText(ctx.ren, ttx, tty, settingsTabTextScale, tabLabel);
            }
            if (showOptionalSidebar) {
                SDL_Rect side = settingsSidebarRect();
                if (settingsWindowFrame && settingsWindowTex) {
                    SDL_SetTextureBlendMode(settingsWindowTex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(settingsWindowTex, 220);
                    renderCenterLoopedFrame(settingsWindowTex, *settingsWindowFrame, side);
                    SDL_SetTextureAlphaMod(settingsWindowTex, 255);
                } else {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ctx.ren, 36, 45, 58, 220);
                    SDL_RenderFillRect(ctx.ren, &side);
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(ctx.ren, 160, 182, 212, 255);
                    SDL_RenderRect(ctx.ren, &side);
                }
                const std::string tabName = (settingsTab >= 0 && settingsTab < (int)settingsTabLabels.size()) ? settingsTabLabels[settingsTab] : std::string("UNKNOWN");
                auto selectedRowIndex = [&]() -> int {
                    if (settingsTab == IDX_SETTINGS_AUDIO) return settingsSelAudio;
                    if (settingsTab == IDX_SETTINGS_DEBUG) return settingsSelDebug;
                    if (settingsTab == IDX_SETTINGS_CONTROLS) return settingsSelControls;
                    if (settingsTab == IDX_SAVES_TAB) return settingsSelSaves;
                    if (settingsTab == IDX_SETTINGS_ACCOUNT) return settingsSelNetwork;
                    return settingsSel;
                };
                auto selectedRowHelp = [&]() -> std::string {
                    if (settingsTab == IDX_SETTINGS_AUDIO) {
                        if (settingsSelAudio == 0) return "Toggle menu music playback.";
                        if (settingsSelAudio == 1) return "Silence all music and sound effects.";
                        if (settingsSelAudio == 2) return "Drag or press left/right to set music volume.";
                        if (settingsSelAudio == 3) return "Drag or press left/right to set effects volume.";
                    }
                    if (settingsTab == IDX_SETTINGS_CONTROLS) return "Press Enter to rebind the selected action.";
                    if (settingsTab == IDX_SETTINGS_ACCOUNT) {
                        if (settingsSelNetwork <= 1) return "Enter account credentials or manage them online.";
                        return "Sign in, repair, log out, or open account tools.";
                    }
                    if (settingsTab == IDX_SAVES_TAB) return "Choose a save slot or load the active slot.";
                    if (settingsTab == IDX_UPDATER_TAB) return "Check for updates and install releases.";
                    if (settingsTab == IDX_SETTINGS_ABOUT) return "Review build, platform, and runtime details.";
                    if (settingsTab == IDX_SETTINGS_FILEINFO) return "Review save and config file locations.";
                    if (settingsTab == IDX_SETTINGS_PRIVACY) return "Control privacy and telemetry options.";
                    const int rawGeneralSel = generalSettingsRawIndexFromVisible(settingsSel);
                    if (rawGeneralSel == IDX_UI_SCALE) return "Adjust menu and HUD scale.";
                    if (rawGeneralSel == IDX_UI_EDGE_PADDING) return "Move UI away from screen edges.";
                    if (rawGeneralSel == IDX_SHOW_FPS) return "Show a live FPS badge in the menu interface.";
                    if (rawGeneralSel == IDX_VSYNC) return "Reduce tearing by syncing to display refresh.";
                    if (rawGeneralSel == IDX_LEVEL_SELECT) return "Show or hide level select from the main menu.";
                    return "Use Enter to toggle, arrows to move, Q/E for tabs.";
                };
                DrawText(ctx.ren, side.x + 10, side.y + 10, 2, "QUICK INFO");
                DrawText(ctx.ren, side.x + 10, side.y + 34, 2, std::string("TAB: ") + tabName);
                DrawText(ctx.ren, side.x + 10, side.y + 58, 2, std::string("ROW: ") + std::to_string(selectedRowIndex() + 1));
                DrawText(ctx.ren, side.x + 10, side.y + 82, 2, selectedRowHelp());
                DrawText(ctx.ren, side.x + 10, side.y + side.h - 82, 2, std::string("B: TOGGLE SIDEBAR"));
                DrawText(ctx.ren, side.x + 10, side.y + side.h - 58, 2, std::string("Q/E: NEXT TAB"));
                DrawText(ctx.ren, side.x + 10, side.y + side.h - 34, 2, std::string("ESC: BACK"));
                if (settingsTab == IDX_SETTINGS_ACCOUNT) {
                    const bool hasUser = !levelServerAccountUsername.empty();
                    const bool hasToken = !levelServerAuthToken.empty();
                    const bool invalidLogin = (hasUser && !hasToken);
                    std::string status = "LOGGED OUT";
                    if (invalidLogin) status = "LOGIN NEEDS REPAIR";
                    else if (hasUser && hasToken) status = "LOGGED IN";
                    DrawText(ctx.ren, side.x + 10, side.y + 106, 2, std::string("STATUS: ") + status);
                }
            }
            aboutContentBottomY = settingsListBottom;
            if (settingsTab == IDX_SETTINGS_FILEINFO) {
                auto fsTimeToString = [&](const std::filesystem::file_time_type& ft) -> std::string {
                    using namespace std::chrono;
                    const auto systemNow = system_clock::now();
                    const auto fileNow = std::filesystem::file_time_type::clock::now();
                    const auto systemTime = time_point_cast<system_clock::duration>(ft - fileNow + systemNow);
                    const std::time_t tt = system_clock::to_time_t(systemTime);
                    std::tm tm{};
#if defined(_WIN32)
                    localtime_s(&tm, &tt);
#else
                    localtime_r(&tt, &tm);
#endif
                    std::ostringstream oss;
                    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
                    return oss.str();
                };
                auto describeFile = [&](const std::filesystem::path& path) -> std::vector<std::string> {
                    std::error_code ec;
                    std::vector<std::string> lines;
                    lines.push_back(std::string("PATH: ") + path.string());
                    if (!std::filesystem::exists(path, ec)) {
                        lines.push_back("STATE: MISSING");
                        return lines;
                    }
                    const auto size = std::filesystem::file_size(path, ec);
                    if (!ec) lines.push_back(std::string("SIZE: ") + std::to_string(static_cast<unsigned long long>(size)) + " B");
                    else lines.push_back("SIZE: UNKNOWN");
                    const auto ftime = std::filesystem::last_write_time(path, ec);
                    if (!ec) lines.push_back(std::string("MODIFIED: ") + fsTimeToString(ftime));
                    else lines.push_back("MODIFIED: UNKNOWN");
                    return lines;
                };
                const std::filesystem::path saveRoot = std::filesystem::path(GetAppSaveRootPath());
                const std::filesystem::path clientSettingsPath = saveRoot / "client_settings.json";
                const int activeSlot = std::clamp(activeSaveSlotIndex, 0, std::max(0, kSaveSlotCount - 1));
                const std::filesystem::path activeSavePath = saveSlotPath(activeSlot);
                const std::filesystem::path configPath = std::filesystem::path("assets") / "config.json";
                const int infoHeadScale = std::clamp((int)std::lround(2.0f + 0.35f * settingsMenuScale()), 2, 3);
                const int infoBodyScale = std::clamp((int)std::lround(1.3f + 0.35f * settingsMenuScale()), 2, 3);
                int y = settingsListTop + std::max(8, (int)std::lround(10.0f * settingsMenuScale())) - settingsScrollY;
                const int infoX = settingsRowBtn(0).x + 14;
                auto drawInfoLine = [&](int scale, const std::string& text, int gapMul) {
                    DrawText(ctx.ren, infoX, y, scale, text);
                    y += std::max(14, (10 * scale) + gapMul);
                };
                drawInfoLine(infoHeadScale, "FILE INFO", 8);
                drawInfoLine(infoBodyScale, std::string("ACTIVE SAVE SLOT: ") + std::to_string(activeSlot + 1), 6);
                drawInfoLine(infoBodyScale, std::string("SAVE ROOT: ") + saveRoot.string(), 6);
                for (const std::string& line : describeFile(activeSavePath)) drawInfoLine(infoBodyScale, line, 4);
                for (const std::string& line : describeFile(clientSettingsPath)) drawInfoLine(infoBodyScale, line, 4);
                for (const std::string& line : describeFile(configPath)) drawInfoLine(infoBodyScale, line, 4);
                drawInfoLine(infoBodyScale, std::string("CONFIG VERSION: ") + (ctx.versionString.empty() ? "dev" : ctx.versionString), 6);
                SDL_Rect fileInfoBtn = fileInfoBackRect();
                const std::string backLabel = "BACK";
                const int backScale = infoBodyScale;
                const int backTextW = MeasureTextWidth(backScale, backLabel);
                const int backTextH = 10 * backScale;
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, settingsSel == 0 ? 52 : 28);
                SDL_RenderFillRect(ctx.ren, &fileInfoBtn);
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                DrawText(ctx.ren,
                         fileInfoBtn.x + (fileInfoBtn.w - backTextW) / 2,
                         fileInfoBtn.y + std::max(2, (fileInfoBtn.h - backTextH) / 2),
                         backScale,
                         backLabel);
                aboutContentBottomY = y + aboutScrollY;
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else if (settingsTab == IDX_SAVES_TAB) {
                const int slotCount = std::max(1, kSaveSlotCount);
                const int selectedSaveRow = std::clamp(settingsSelSaves, 0, 4);
                auto saveSlotStatus = [&](int slotIdx) -> std::string {
                    if (slotIdx < 0 || slotIdx >= slotCount) return "UNKNOWN";
                    if (activeSaveSlotIndex == slotIdx) return "ACTIVE";
                    return saveSlotExists(slotIdx) ? "READY" : "EMPTY";
                };
                auto drawSaveSlotRow = [&](int rowIdx, int slotIdx) {
                    SDL_Rect row = settingsRowBtn(rowIdx);
                    drawChromeButton(row, selectedSaveRow == rowIdx);
                    const std::string label = std::string("SLOT ") + std::to_string(slotIdx + 1) + ": " + saveSlotStatus(slotIdx);
                    DrawText(ctx.ren, row.x + 18, row.y + std::max(0, (row.h - 10 * settingsRowTextScale) / 2), settingsRowTextScale, label);
                };
                drawSaveSlotRow(0, 0);
                drawSaveSlotRow(1, 1);
                drawSaveSlotRow(2, 2);
                drawChromeButton(settingsRowBtn(3), selectedSaveRow == 3);
                DrawText(ctx.ren, settingsRowBtn(3).x + 18, settingsRowBtn(3).y + std::max(0, (settingsRowBtn(3).h - 10 * settingsRowTextScale) / 2), settingsRowTextScale, "LOAD ACTIVE SLOT");
                drawChromeButton(settingsRowBtn(4), selectedSaveRow == 4);
                DrawText(ctx.ren, settingsRowBtn(4).x + 18, settingsRowBtn(4).y + std::max(0, (settingsRowBtn(4).h - 10 * settingsRowTextScale) / 2), settingsRowTextScale, "BACK");
            } else if (settingsTab == IDX_SETTINGS_ABOUT) {
                const int sdlVer = SDL_GetVersion();
                const int sdlMajor = SDL_VERSIONNUM_MAJOR(sdlVer);
                const int sdlMinor = SDL_VERSIONNUM_MINOR(sdlVer);
                const int sdlPatch = SDL_VERSIONNUM_MICRO(sdlVer);
                const char* platform = SDL_GetPlatform();
                const char* rendererName = SDL_GetRendererName(ctx.ren);
                const char* videoDriver = SDL_GetCurrentVideoDriver();
                const char* audioDriver = SDL_GetCurrentAudioDriver();
                const char* sdlRevision = SDL_GetRevision();
                const int logicalCpuCores = SDL_GetNumLogicalCPUCores();
                const int systemRamMiB = SDL_GetSystemRAM();
                int winW = 0, winH = 0;
                getWindowSizeInPixelsCompat(ctx.win, winW, winH);
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
                const int aboutHeadScale = std::clamp((int)std::lround(2.0f + 0.35f * settingsMenuScale()), 2, 3);
                const int aboutBodyScale = std::clamp((int)std::lround(1.3f + 0.35f * settingsMenuScale()), 2, 3);
                int y = settingsListTop + std::max(8, (int)std::lround(10.0f * settingsMenuScale())) - aboutScrollY;
                auto drawAboutLine = [&](int scale, const std::string& text, int gapMul) {
                    DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(scale, text) / 2, y, scale, text);
                    y += std::max(14, (10 * scale) + gapMul);
                };
                drawAboutLine(aboutHeadScale, "DORFPLATFORMER TIMETRAVEL", 8);
                drawAboutLine(aboutBodyScale, std::string("VERSION: ") + (ctx.versionString.empty() ? "dev" : ctx.versionString), 6);
                drawAboutLine(aboutBodyScale, std::string("SDL: ") + std::to_string(sdlMajor) + "." + std::to_string(sdlMinor) + "." + std::to_string(sdlPatch), 6);
                drawAboutLine(aboutBodyScale, std::string("PLATFORM: ") + (platform ? platform : "unknown"), 6);
                drawAboutLine(aboutBodyScale, std::string("RENDERER: ") + (rendererName ? rendererName : "unknown"), 6);
                drawAboutLine(aboutBodyScale, std::string("WINDOW: ") + std::to_string(winW) + "x" + std::to_string(winH), 6);
                drawAboutLine(aboutBodyScale, std::string("BASE: ") + std::to_string(ctx.baseScreenW) + "x" + std::to_string(ctx.baseScreenH), 6);
                drawAboutLine(aboutBodyScale, std::string("UI SCALE: ") + std::to_string(uiScalePercent) + "%", 6);
                drawAboutLine(aboutBodyScale, std::string("UI EDGE PAD: ") + std::to_string(uiEdgePadding) + " PX", 6);
                drawAboutLine(aboutBodyScale, std::string("VIDEO DRIVER: ") + (videoDriver ? videoDriver : "unknown"), 6);
                drawAboutLine(aboutBodyScale, std::string("AUDIO DRIVER: ") + (audioDriver ? audioDriver : "unknown"), 6);
                drawAboutLine(aboutBodyScale, std::string("CPU CORES: ") + std::to_string(logicalCpuCores), 6);
                drawAboutLine(aboutBodyScale, std::string("SYSTEM RAM: ") + std::to_string(systemRamMiB) + " MiB", 6);
                drawAboutLine(aboutBodyScale, "SDL REVISION:", 2);
                drawAboutLine(aboutBodyScale, sdlRevision ? sdlRevision : "unknown", 8);
                drawAboutLine(aboutBodyScale, "BUILD UUID:", 2);
                drawAboutLine(aboutBodyScale, ctx.buildUuid, 6);
                drawAboutLine(aboutBodyScale, "BUILD TIME:", 2);
                drawAboutLine(aboutBodyScale, ctx.buildTimestamp.empty() ? "unknown" : ctx.buildTimestamp, 6);
                drawAboutLine(aboutBodyScale, "BUILD TIMEZONE:", 2);
                drawAboutLine(aboutBodyScale, ctx.buildTimezone.empty() ? "unknown" : ctx.buildTimezone, 6);
#if defined(_WIN32)
                drawAboutLine(aboutBodyScale,
                              std::string("UPDATER: ") + updaterStatusText(),
                              6);
                drawAboutLine(aboutBodyScale, "PRESS U OR USE SETTINGS > CHECK FOR UPDATES", 6);
#endif
                if (showExperimentalFeatures) {
                    const std::string actionText = "OPEN LEVEL SELECT";
                    const int actionTextH = 10 * aboutBodyScale;
                    SDL_Rect actionRect = aboutOpenMainLevelSelectRect();
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 36);
                    SDL_RenderFillRect(ctx.ren, &actionRect);
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    DrawText(ctx.ren,
                             ctx.baseScreenW / 2 - MeasureTextWidth(aboutBodyScale, actionText) / 2,
                             actionRect.y + std::max(2, (actionRect.h - actionTextH) / 2),
                             aboutBodyScale,
                             actionText);
                    y = actionRect.y + actionRect.h + std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
                }
                aboutContentBottomY = y + aboutScrollY;
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else if (settingsTab == IDX_SETTINGS_CONTROLS) {
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
                std::vector<std::string> rows = {
                    controlsLabels[0] + std::string(": ") + keyNameForDisplay(keyMoveLeft),
                    controlsLabels[1] + std::string(": ") + keyNameForDisplay(keyMoveRight),
                    controlsLabels[2] + std::string(": ") + keyNameForDisplay(keyMoveDown),
                    controlsLabels[3] + std::string(": ") + keyNameForDisplay(keyJump),
                    controlsLabels[4] + std::string(": ") + keyNameForDisplay(keyPause),
                    controlsLabels[5]
                };
                for (int i = 0; i < (int)rows.size(); ++i) {
                    drawToggleHitbox(i, i == settingsSelControls);
                    int y = settingsRowY(i);
                    if (i == settingsSelControls) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 76);
                        SDL_Rect hl = settingsRowBtn(i);
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    drawRowText(i, rows[i]);
                }
                if (waitingForControlKey) {
                    const std::string waitMsg = "PRESS A KEY (ESC TO CANCEL)";
                    DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, waitMsg) / 2, settingsListTop - 26, 2, waitMsg);
                }
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else if (settingsTab == IDX_SETTINGS_AUDIO) {
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
                std::vector<std::string> rows = {
                    audioLabels[0] + std::string(": ") + (menuMusicEnabled ? "ON" : "OFF"),
                    audioLabels[1] + std::string(": ") + (muteAllAudio ? "ON" : "OFF"),
                    audioLabels[2] + std::string(": ") + std::to_string((musicVolume * 100) / 128) + "%",
                    audioLabels[3] + std::string(": ") + std::to_string((sfxVolume * 100) / 128) + "%",
                    audioLabels[4]
                };
                for (int i = 0; i < (int)rows.size(); ++i) {
                    drawToggleHitbox(i, i == settingsSelAudio);
                    if (i == 0) drawToggleCheckbox(i, menuMusicEnabled);
                    if (i == 1) drawToggleCheckbox(i, muteAllAudio);
                    int y = settingsRowY(i);
                    if (i == settingsSelAudio) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 76);
                        SDL_Rect hl = settingsRowBtn(i);
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    drawRowText(i, rows[i]);
                }
                auto drawSlider = [&](const SDL_Rect& slider, int value) {
                    SDL_SetRenderDrawColor(ctx.ren, 70, 80, 95, 255);
                    SDL_RenderFillRect(ctx.ren, &slider);
                    SDL_SetRenderDrawColor(ctx.ren, 130, 150, 180, 255);
                    SDL_RenderRect(ctx.ren, &slider);
                    int fillW = (int)std::lround((value / 128.0f) * slider.w);
                    SDL_Rect fill{slider.x, slider.y, std::clamp(fillW, 0, slider.w), slider.h};
                    SDL_SetRenderDrawColor(ctx.ren, 180, 220, 255, 255);
                    SDL_RenderFillRect(ctx.ren, &fill);
                };
                drawSlider(musicSliderRect(), musicVolume);
                drawSlider(sfxSliderRect(), sfxVolume);
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else if (settingsTab == IDX_SETTINGS_DEBUG) {
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
                std::vector<std::string> rows = {
                    debugLabels[0] + std::string(": ") + (defaultShowDetailedDebugger ? "ON" : "OFF"),
                    debugLabels[1] + std::string(": ") + (defaultShowHitboxes ? "ON" : "OFF"),
                    debugLabels[2] + std::string(": ") + (defaultShowPlayerHitbox ? "ON" : "OFF"),
                    debugLabels[3] + std::string(": ") + (defaultShowDebugView ? "ON" : "OFF"),
                    debugLabels[4] + std::string(": ") + (defaultHideUnknownObjectTypes ? "ON" : "OFF"),
                    debugLabels[5]
                };
                for (int i = 0; i < (int)rows.size(); ++i) {
                    drawToggleHitbox(i, i == settingsSelDebug);
                    if (i == 0) drawToggleCheckbox(i, defaultShowDetailedDebugger);
                    if (i == 1) drawToggleCheckbox(i, defaultShowHitboxes);
                    if (i == 2) drawToggleCheckbox(i, defaultShowPlayerHitbox);
                    if (i == 3) drawToggleCheckbox(i, defaultShowDebugView);
                    if (i == 4) drawToggleCheckbox(i, defaultHideUnknownObjectTypes);
                    int y = settingsRowY(i);
                    if (i == settingsSelDebug) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 76);
                        SDL_Rect hl = settingsRowBtn(i);
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    drawRowText(i, rows[i]);
                }
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else if (settingsTab == IDX_SETTINGS_ACCOUNT) {
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
                const bool hasUser = !levelServerAccountUsername.empty();
                const bool hasToken = !levelServerAuthToken.empty();
                const bool invalidLogin = (hasUser && !hasToken);
                std::vector<std::string> rows;
                if (invalidLogin) {
                    rows = {
                        "ACCOUNT: INVALID LOGIN",
                        "REPAIR LOGIN",
                        "OPEN ACCOUNT MANAGER",
                        "BACK"
                    };
                } else if (hasUser && hasToken) {
                    rows = {
                        std::string("ACCOUNT: ") + levelServerAccountUsername,
                        "LOG OUT",
                        "OPEN ACCOUNT MANAGER",
                        "BACK"
                    };
                } else {
                    auto utf8CodepointCount = [](const std::string& s, std::size_t byteEnd = std::string::npos) -> int {
                        const std::size_t end = (byteEnd == std::string::npos) ? s.size() : std::min(byteEnd, s.size());
                        int count = 0;
                        for (std::size_t i = 0; i < end;) {
                            ++count;
                            ++i;
                            while (i < end && ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)) ++i;
                        }
                        return count;
                    };
                    auto insertCaret = [](const std::string& s, int caretPos) -> std::string {
                        const int clampedPos = std::max(0, std::min((int)s.size(), caretPos));
                        return s.substr(0, (std::size_t)clampedPos) + "|" + s.substr((std::size_t)clampedPos);
                    };
                    std::string emailDisplay = networkLoginEmail.empty() ? "<empty>" : networkLoginEmail;
                    std::string passwordDisplay = networkLoginPassword.empty() ? "<empty>" : maskedPassword(networkLoginPassword);
                    if (networkEditField == NetworkEditField::LoginEmail) {
                        const std::size_t cur = std::min(networkLoginEmailCursor, networkLoginEmail.size());
                        const int pos = (int)cur;
                        emailDisplay = insertCaret(emailDisplay, pos);
                    } else if (networkEditField == NetworkEditField::LoginPassword) {
                        const std::size_t cur = std::min(networkLoginPasswordCursor, networkLoginPassword.size());
                        const int pos = utf8CodepointCount(networkLoginPassword, cur);
                        passwordDisplay = insertCaret(passwordDisplay, pos);
                    }
                    rows = {
                        std::string("LOGIN EMAIL: ") + emailDisplay,
                        std::string("LOGIN PASSWORD: ") + passwordDisplay,
                        "SIGN IN",
                        "OPEN ACCOUNT MANAGER",
                        "BACK"
                    };
                }
                for (int i = 0; i < (int)rows.size(); ++i) {
                    drawToggleHitbox(i, i == settingsSelNetwork);
                    int y = settingsRowY(i);
                    if (i == settingsSelNetwork) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 76);
                        SDL_Rect hl = settingsRowBtn(i);
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    std::string rowText = rows[i];
                    if (!hasUser && !hasToken && networkEditField != NetworkEditField::None) {
                        if ((i == 0 && networkEditField == NetworkEditField::LoginEmail) ||
                            (i == 1 && networkEditField == NetworkEditField::LoginPassword)) {
                            rowText += "  [EDITING]";
                        }
                    }
                    drawRowText(i, rowText);
                }
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else if (settingsTab == IDX_UPDATER_TAB) {
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
                const std::string status = updaterStatusText();
                const std::string detail = updaterStatusDetail();
                const std::string latest = updaterLatestVersionText();
                const std::string notes = updaterNotesText();
                const float progress01 = updaterProgress01();
                const std::string actionLabel =
                    updaterCanStartInstall() ? "START UPDATE" :
                    (status == "NOT CONFIGURED" ? "UPDATER NOT CONFIGURED" : "CHECK FOR UPDATES");
                std::vector<std::string> rows = {
                    std::string("STATUS: ") + status,
                    actionLabel,
                    "BACK"
                };
                for (int i = 0; i < (int)rows.size(); ++i) {
                    drawToggleHitbox(i, i == settingsSel);
                    if (i == settingsSel) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 76);
                        SDL_Rect hl = settingsRowBtn(i);
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    drawRowText(i, rows[i]);
                }
                int infoY = settingsRowY((int)rows.size()) + std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
                const int infoScale = std::clamp((int)std::lround(1.3f + 0.35f * settingsMenuScale()), 2, 3);
                const int infoX = settingsRowBtn(0).x + std::max(6, (int)std::lround(8.0f * settingsMenuScale()));
                auto drawInfo = [&](const std::string& line) {
                    if (line.empty()) return;
                    DrawText(ctx.ren, infoX, infoY, infoScale, line);
                    infoY += std::max(16, 10 * infoScale + 6);
                };
                drawInfo(std::string("CURRENT: ") + (ctx.versionString.empty() ? "dev" : ctx.versionString));
                if (!latest.empty()) drawInfo(std::string("LATEST: ") + latest);
                if (!detail.empty()) drawInfo(std::string("DETAIL: ") + detail);
                if (status.find("DOWNLOADING") != std::string::npos || progress01 > 0.0f) {
                    SDL_Rect bar{
                        infoX,
                        infoY + 2,
                        std::max(80, settingsRowBtn(0).w - std::max(12, (int)std::lround(16.0f * settingsMenuScale()))),
                        std::clamp((int)std::lround(12.0f * settingsMenuScale()), 10, 18)
                    };
                    SDL_SetRenderDrawColor(ctx.ren, 70, 80, 95, 255);
                    SDL_RenderFillRect(ctx.ren, &bar);
                    SDL_SetRenderDrawColor(ctx.ren, 130, 150, 180, 255);
                    SDL_RenderDrawRect(ctx.ren, &bar);
                    SDL_Rect fill = bar;
                    fill.w = std::clamp((int)std::lround(progress01 * bar.w), 0, bar.w);
                    SDL_SetRenderDrawColor(ctx.ren, 180, 220, 255, 255);
                    SDL_RenderFillRect(ctx.ren, &fill);
                    infoY += bar.h + std::max(10, (int)std::lround(12.0f * settingsMenuScale()));
                    drawInfo(std::string("PROGRESS: ") + std::to_string((int)std::lround(progress01 * 100.0f)) + "%");
                }
                if (!notes.empty()) {
                    drawInfo("NOTES:");
                    drawInfo(notes);
                }
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else if (isExtraTab(settingsTab)) {
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
                const int extraIdx = extraTabIndex(settingsTab);
                std::vector<std::string> rows;
                const int used = extraTabUsedOptionCount(settingsTab);
                rows.reserve(used + 1);
                for (int i = 0; i < used; ++i) {
                    const int optionIdx = extraTabOptionAtVisibleRow(settingsTab, i);
                    if (optionIdx < 0) continue;
                    rows.push_back(std::string(extraTabOptionLabels[extraIdx][optionIdx]) + ": " +
                                   (extraTabValueRef(extraIdx, optionIdx) ? "ON" : "OFF"));
                }
                rows.push_back("BACK");
                for (int i = 0; i < (int)rows.size(); ++i) {
                    drawToggleHitbox(i, i == settingsSel);
                    const int optionIdx = extraTabOptionAtVisibleRow(settingsTab, i);
                    if (optionIdx >= 0) drawToggleCheckbox(i, extraTabValueRef(extraIdx, optionIdx));
                    int y = settingsRowY(i);
                    if (i == settingsSel) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 76);
                        SDL_Rect hl = settingsRowBtn(i);
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    drawRowText(i, rows[i]);
                }
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            } else {
                SDL_Rect listClip = settingsListClipRect();
                SDL_SetRenderClipRect(ctx.ren, &listClip);
#if defined(__ANDROID__)
                std::vector<std::string> rows = {
                    std::string("VSYNC: ") + (vsyncEnabled ? "ON" : "OFF"),
                    std::string("CAM CLAMP: ") + (clampCamX ? "ON" : "OFF"),
                    std::string("UI SCALE: ") + std::to_string(uiScalePercent) + "%",
                    std::string("UI EDGE PAD: ") + std::to_string(uiEdgePadding) + " PX",
                    std::string("DEBUG MODE: ") + (debugModeEnabled ? "ON" : "OFF"),
                    std::string("FPS COUNTER: ") + (defaultShowFpsCounter ? "ON" : "OFF"),
                    std::string("DETAILED DEBUGGER: ") + (defaultShowDetailedDebugger ? "ON" : "OFF"),
                    std::string("SHOW HITBOXES: ") + (defaultShowHitboxes ? "ON" : "OFF"),
                    std::string("PLAYER HITBOX: ") + (defaultShowPlayerHitbox ? "ON" : "OFF"),
                    std::string("DEBUG HUD: ") + (defaultShowDebugView ? "ON" : "OFF"),
                    std::string("POWER MANAGEMENT: ") + (powerManagementEnabled ? "ON" : "OFF"),
                    std::string("LOW POWER MODE: ") + (lowPowerModeEnabled ? "ON" : "OFF"),
                    std::string("MUSIC: ") + std::to_string((musicVolume * 100) / 128) + "%",
                    std::string("SFX: ") + std::to_string((sfxVolume * 100) / 128) + "%",
                    std::string("SHOW EXPERIMENTAL FEATURES: ") + (showExperimentalFeatures ? "ON" : "OFF"),
                    std::string("LEVEL SELECT: ") + (levelSelectEnabled ? "ON" : "OFF"),
                    "ABOUT",
                    "BACK"
                };
#else
                std::vector<std::string> rows = {
                    std::string("FULLSCREEN: ") + (fullscreen ? "ON" : "OFF"),
                    std::string("VSYNC: ") + (vsyncEnabled ? "ON" : "OFF"),
                    std::string("CAM CLAMP: ") + (clampCamX ? "ON" : "OFF"),
                    std::string("UI SCALE: ") + std::to_string(uiScalePercent) + "%",
                    std::string("UI EDGE PAD: ") + std::to_string(uiEdgePadding) + " PX",
                    std::string("DEBUG MODE: ") + (debugModeEnabled ? "ON" : "OFF"),
                    std::string("FPS COUNTER: ") + (defaultShowFpsCounter ? "ON" : "OFF"),
                    std::string("DETAILED DEBUGGER: ") + (defaultShowDetailedDebugger ? "ON" : "OFF"),
                    std::string("SHOW HITBOXES: ") + (defaultShowHitboxes ? "ON" : "OFF"),
                    std::string("PLAYER HITBOX: ") + (defaultShowPlayerHitbox ? "ON" : "OFF"),
                    std::string("DEBUG HUD: ") + (defaultShowDebugView ? "ON" : "OFF"),
                    std::string("POWER MANAGEMENT: ") + (powerManagementEnabled ? "ON" : "OFF"),
                    std::string("LOW POWER MODE: ") + (lowPowerModeEnabled ? "ON" : "OFF"),
                    std::string("MUSIC: ") + std::to_string((musicVolume * 100) / 128) + "%",
                    std::string("SFX: ") + std::to_string((sfxVolume * 100) / 128) + "%",
                    std::string("SHOW EXPERIMENTAL FEATURES: ") + (showExperimentalFeatures ? "ON" : "OFF"),
                    std::string("LEVEL SELECT: ") + (levelSelectEnabled ? "ON" : "OFF"),
#if defined(_WIN32)
                    std::string("UPDATER: ") + updaterStatusText(),
#endif
                    "ABOUT",
                    "BACK"
                };
#endif
                int drawIdx = 0;
                for (int i = 0; i < (int)rows.size(); ++i) {
                    if (generalSettingsRowHidden(i)) continue;
                    drawToggleHitbox(drawIdx, drawIdx == settingsSel);
#if defined(__ANDROID__)
                    if (i == 0) drawToggleCheckbox(drawIdx, vsyncEnabled);
                    if (i == 1) drawToggleCheckbox(drawIdx, clampCamX);
                    if (i == IDX_SHOW_FPS) drawToggleCheckbox(drawIdx, defaultShowFpsCounter);
                    if (i == IDX_POWER_MANAGEMENT) drawToggleCheckbox(drawIdx, powerManagementEnabled);
                    if (i == IDX_LOW_POWER_MODE) drawToggleCheckbox(drawIdx, lowPowerModeEnabled);
                    if (i == IDX_SHOW_EXPERIMENTAL) drawToggleCheckbox(drawIdx, showExperimentalFeatures);
                    if (i == IDX_LEVEL_SELECT) drawToggleCheckbox(drawIdx, levelSelectEnabled);
#else
                    if (i == 0) drawToggleCheckbox(drawIdx, fullscreen);
                    if (i == 1) drawToggleCheckbox(drawIdx, vsyncEnabled);
                    if (i == 2) drawToggleCheckbox(drawIdx, clampCamX);
                    if (i == IDX_SHOW_FPS) drawToggleCheckbox(drawIdx, defaultShowFpsCounter);
                    if (i == IDX_POWER_MANAGEMENT) drawToggleCheckbox(drawIdx, powerManagementEnabled);
                    if (i == IDX_LOW_POWER_MODE) drawToggleCheckbox(drawIdx, lowPowerModeEnabled);
                    if (i == IDX_SHOW_EXPERIMENTAL) drawToggleCheckbox(drawIdx, showExperimentalFeatures);
                    if (i == IDX_LEVEL_SELECT) drawToggleCheckbox(drawIdx, levelSelectEnabled);
#endif
                    int y = settingsRowY(drawIdx);
                    if (drawIdx == settingsSel) {
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, 76);
                        SDL_Rect hl = settingsRowBtn(drawIdx);
                        SDL_RenderFillRect(ctx.ren, &hl);
                        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    }
                    drawRowText(drawIdx, rows[i]);
                    drawIdx++;
                }
                auto drawGeneralSlider = [&](const SDL_Rect& slider, float t) {
                    SDL_SetRenderDrawColor(ctx.ren, 70, 80, 95, 255);
                    SDL_RenderFillRect(ctx.ren, &slider);
                    SDL_SetRenderDrawColor(ctx.ren, 130, 150, 180, 255);
                    SDL_RenderDrawRect(ctx.ren, &slider);
                    int fillW = (int)std::lround(std::clamp(t, 0.0f, 1.0f) * slider.w);
                    SDL_Rect fill{slider.x, slider.y, std::clamp(fillW, 0, slider.w), slider.h};
                    SDL_SetRenderDrawColor(ctx.ren, 180, 220, 255, 255);
                    SDL_RenderFillRect(ctx.ren, &fill);
                };
                drawGeneralSlider(uiScaleSliderRect(), UiScale::normalizedPercent(uiScalePercent));
                drawGeneralSlider(uiEdgePaddingSliderRect(), UiScale::normalizedEdgePadding(uiEdgePadding));
                SDL_SetRenderClipRect(ctx.ren, nullptr);
            }
            if ((settingsTab != IDX_SETTINGS_ABOUT && settingsMaxScroll(settingsTab) > 0) ||
                (settingsTab == IDX_SETTINGS_ABOUT && aboutMaxScroll() > 0)) {
                SDL_Rect track = settingsScrollbarTrackRect();
                SDL_Rect thumb = (settingsTab == IDX_SETTINGS_ABOUT) ? aboutScrollbarThumbRect() : settingsScrollbarThumbRect();
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ctx.ren, 42, 52, 68, 255);
                SDL_RenderFillRect(ctx.ren, &track);
                SDL_SetRenderDrawColor(ctx.ren, 92, 110, 138, 255);
                SDL_RenderDrawRect(ctx.ren, &track);
                SDL_SetRenderDrawColor(ctx.ren, 176, 198, 230, 255);
                SDL_RenderFillRect(ctx.ren, &thumb);
            }
        }

        if (defaultShowFpsCounter) {
            const std::string fpsText = std::string("FPS: ") + std::to_string(menuFps);
            const int fpsScale = std::max(2, (int)std::lround(2.0f * settingsMenuScale()));
            const int fpsPadX = std::max(8, (int)std::lround(10.0f * settingsMenuScale()));
            const int fpsPadY = std::max(5, (int)std::lround(6.0f * settingsMenuScale()));
            const int fpsTextW = MeasureTextWidth(fpsScale, fpsText);
            const int fpsTextH = 10 * fpsScale;
            SDL_Rect fpsBadge{
                ctx.baseScreenW - fpsTextW - fpsPadX * 2 - std::max(8, uiEdgePadding),
                std::max(8, uiEdgePadding),
                fpsTextW + fpsPadX * 2,
                fpsTextH + fpsPadY * 2
            };
            SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ctx.ren, 18, 25, 36, 205);
            SDL_RenderFillRect(ctx.ren, &fpsBadge);
            SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ctx.ren, 176, 198, 230, 255);
            SDL_RenderRect(ctx.ren, &fpsBadge);
            DrawText(ctx.ren, fpsBadge.x + fpsPadX, fpsBadge.y + fpsPadY, fpsScale, fpsText);
        }

        SDL_SetRenderTarget(ctx.ren, nullptr);
        int winW = 0, winH = 0;
        getWindowSizeInPixelsCompat(ctx.win, winW, winH);
        SDL_Rect presentDst = computePresentRect(winW, winH, ctx.baseScreenW, ctx.baseScreenH, 1.0f);
        SDL_SetRenderDrawColor(ctx.ren, 118, 225, 255, 255); // #76e1ff
        SDL_RenderClear(ctx.ren);
        SDL_RenderCopy(ctx.ren, gameTarget, nullptr, &presentDst);
        SDL_RenderPresent(ctx.ren);
    }
    cleanupMenuAssets();
    return FrontendAction::Quit;
}

