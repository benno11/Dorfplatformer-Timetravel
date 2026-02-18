#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <cstdlib>
#include <vector>
#include <fstream>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif
#include "TileMap.h"
#include "LevelLoader.h"
#include "Player.h"
#include "PlayerController.h"
#include "TextRenderer.h"
#include "LevelSelect.h"
#include "AssetPath.h"
#include "LevelManager.h"
#include "FrontendMenu.h"
#include "GameSupport.h"
#include "ParallaxRenderer.h"
#include "CrashReporter.h"
#include "AudioSystem.h"
#include "InputSystem.h"
#include "World3PatternBackground.h"

namespace {
static void setEnvCompat(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    setenv(name, value ? value : "", 1);
#endif
}

static void unsetEnvCompat(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
} // namespace

int RunGameApp(int argc, char** argv) {
    CrashReporter::start();
    // Keep existing terminal output (e.g. compile script logs) visible.
    const std::string buildUuid = makeBuildUuid();
    auto reportStartupError = [](const char* title, const std::string& msg, SDL_Window* parent) {
#if defined(__ANDROID__)
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s: %s", title, msg.c_str());
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg.c_str(), parent);
#endif
    };
    SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    auto sdlErr = []() -> std::string {
        const char* e = SDL_GetError();
        if (e && *e) return std::string(e);
        return "unknown error";
    };
    std::ofstream startupLog("build/startup.log", std::ios::app);
    auto logStartup = [&](const std::string& line) {
        SDL_Log("%s", line.c_str());
        if (startupLog.is_open()) startupLog << line << "\n";
    };
    logStartup("=== startup begin ===");
    auto envOrUnset = [](const char* name) -> std::string {
        const char* v = std::getenv(name);
        return (v && *v) ? std::string(v) : std::string("<unset>");
    };
    auto configuredAudioDriver = [&]() -> std::string {
        const char* hintDriver = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
        if (hintDriver && *hintDriver) return std::string(hintDriver);
        const std::string modernEnv = envOrUnset("SDL_AUDIO_DRIVER");
        if (modernEnv != "<unset>") return modernEnv;
        const std::string legacyEnv = envOrUnset("SDL_AUDIODRIVER");
        if (legacyEnv != "<unset>") return legacyEnv;
        return "<unset>";
    };
    auto applyAudioDriverSelection = [&](const char* driver) {
        if (driver && *driver) {
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
            setEnvCompat("SDL_AUDIO_DRIVER", driver);
            setEnvCompat("SDL_AUDIODRIVER", driver); // Legacy compatibility.
            return;
        }
        SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
        unsetEnvCompat("SDL_AUDIO_DRIVER");
        unsetEnvCompat("SDL_AUDIODRIVER");
    };
    struct InitAttempt {
        const char* label;
        const char* videoDriver; // nullptr means keep current env
        Uint32 flags;
    };
    auto hasVideoDriver = [](const char* name) -> bool {
        const int n = SDL_GetNumVideoDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetVideoDriver(i);
            if (d && std::strcmp(d, name) == 0) return true;
        }
        return false;
    };
    auto hasAudioDriver = [](const char* name) -> bool {
        const int n = SDL_GetNumAudioDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetAudioDriver(i);
            if (d && std::strcmp(d, name) == 0) return true;
        }
        return false;
    };
    auto listVideoDrivers = []() -> std::string {
        std::string out;
        const int n = SDL_GetNumVideoDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetVideoDriver(i);
            if (!d) continue;
            if (!out.empty()) out += ", ";
            out += d;
        }
        if (out.empty()) out = "<none>";
        return out;
    };
    auto listAudioDrivers = []() -> std::string {
        std::string out;
        const int n = SDL_GetNumAudioDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetAudioDriver(i);
            if (!d) continue;
            if (!out.empty()) out += ", ";
            out += d;
        }
        if (out.empty()) out = "<none>";
        return out;
    };
    auto listConnectedGamepads = []() -> std::string {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids || count <= 0) {
            if (ids) SDL_free(ids);
            return "<none>";
        }
        std::string out;
        for (int i = 0; i < count; ++i) {
            SDL_Gamepad* pad = SDL_OpenGamepad(ids[i]);
            const char* name = pad ? SDL_GetGamepadName(pad) : nullptr;
            if (!out.empty()) out += ", ";
            out += name ? name : "<unnamed>";
            if (pad) SDL_CloseGamepad(pad);
        }
        SDL_free(ids);
        return out.empty() ? "<none>" : out;
    };
    std::vector<InitAttempt> attempts;
    const bool hasExplicitAudioEnv = configuredAudioDriver() != "<unset>";
    if (!hasExplicitAudioEnv) {
        const char* preselectedAudio = nullptr;
#if defined(__ANDROID__)
        if (hasAudioDriver("aaudio")) preselectedAudio = "aaudio";
        else if (hasAudioDriver("openslES")) preselectedAudio = "openslES";
#else
        if (hasAudioDriver("pulseaudio")) preselectedAudio = "pulseaudio";
        else if (hasAudioDriver("dummy")) preselectedAudio = "dummy";
        else if (hasAudioDriver("alsa")) preselectedAudio = "alsa";
        else if (hasAudioDriver("pipewire")) preselectedAudio = "pipewire";
#endif
        if (preselectedAudio) {
            applyAudioDriverSelection(preselectedAudio);
            logStartup(std::string("audio preselect: ") + preselectedAudio);
        }
    }
    attempts.push_back({"video+gamepad (env defaults)", nullptr, SDL_INIT_VIDEO | SDL_INIT_GAMEPAD});
    attempts.push_back({"video only (env defaults)", nullptr, SDL_INIT_VIDEO});
    const bool hasX11 = !envOrUnset("DISPLAY").empty() && envOrUnset("DISPLAY") != "<unset>";
    const bool hasWayland = !envOrUnset("WAYLAND_DISPLAY").empty() && envOrUnset("WAYLAND_DISPLAY") != "<unset>";
    const bool canX11 = hasX11 && hasVideoDriver("x11");
    const bool canWayland = hasWayland && hasVideoDriver("wayland");
    const std::string sessionType = envOrUnset("XDG_SESSION_TYPE");
    const bool preferWayland = canWayland && (sessionType == "wayland" || !canX11);
    auto pushWaylandAttempts = [&attempts]() {
        attempts.push_back({"video+gamepad (wayland)", "wayland", SDL_INIT_VIDEO | SDL_INIT_GAMEPAD});
        attempts.push_back({"video only (wayland)", "wayland", SDL_INIT_VIDEO});
    };
    auto pushX11Attempts = [&attempts]() {
        attempts.push_back({"video+gamepad (x11)", "x11", SDL_INIT_VIDEO | SDL_INIT_GAMEPAD});
        attempts.push_back({"video only (x11)", "x11", SDL_INIT_VIDEO});
    };
    if (preferWayland) {
        if (canWayland) pushWaylandAttempts();
        if (canX11) pushX11Attempts();
    } else {
        if (canX11) pushX11Attempts();
        if (canWayland) pushWaylandAttempts();
    }
    if (hasVideoDriver("offscreen")) {
        attempts.push_back({"video only (offscreen)", "offscreen", SDL_INIT_VIDEO});
    }
    if (hasVideoDriver("dummy")) {
        attempts.push_back({"video only (dummy)", "dummy", SDL_INIT_VIDEO});
    }

    bool sdlOk = false;
    std::string initTrace;
    const std::string initialVideoEnv = envOrUnset("SDL_VIDEODRIVER");
    const std::string initialAudioEnv = configuredAudioDriver();
    for (const auto& a : attempts) {
        if (a.videoDriver) setEnvCompat("SDL_VIDEODRIVER", a.videoDriver);
        else if (initialVideoEnv != "<unset>") setEnvCompat("SDL_VIDEODRIVER", initialVideoEnv.c_str());
        else unsetEnvCompat("SDL_VIDEODRIVER");
        if (initialAudioEnv != "<unset>") applyAudioDriverSelection(initialAudioEnv.c_str());
        else applyAudioDriverSelection(nullptr);
        SDL_Quit();
        if (SDL_Init(a.flags)) {
            initTrace += std::string(a.label) + ": ok\n";
            logStartup(std::string("init attempt: ") + a.label + " -> ok");
            sdlOk = true;
            break;
        }
        const std::string err = sdlErr();
        initTrace += std::string(a.label) + ": " + err + "\n";
        logStartup(std::string("init attempt: ") + a.label + " -> " + err);
    }
    if (!sdlOk) {
        std::string msg = "SDL_Init failed.\n";
        msg += initTrace;
        msg += "DISPLAY=" + envOrUnset("DISPLAY") + "\n";
        msg += "WAYLAND_DISPLAY=" + envOrUnset("WAYLAND_DISPLAY") + "\n";
        msg += "XDG_RUNTIME_DIR=" + envOrUnset("XDG_RUNTIME_DIR") + "\n";
        msg += "available_video_drivers=" + listVideoDrivers() + "\n";
        msg += "SDL_VIDEODRIVER=" + envOrUnset("SDL_VIDEODRIVER") + "\n";
        msg += "SDL_AUDIO_DRIVER=" + envOrUnset("SDL_AUDIO_DRIVER") + "\n";
        msg += "SDL_AUDIODRIVER=" + envOrUnset("SDL_AUDIODRIVER") + "\n";
        msg += "SDL_HINT_AUDIO_DRIVER=" + configuredAudioDriver();
        logStartup("init failed");
        logStartup(msg);
        reportStartupError("SDL Init Error", msg, nullptr);
        CrashReporter::stop();
        return 1;
    }
    logStartup("SDL_Init completed");
    auto tryAudioInit = [&](const char* label, const char* forcedDriver) -> bool {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        if (forcedDriver && *forcedDriver) applyAudioDriverSelection(forcedDriver);
        else if (initialAudioEnv != "<unset>") applyAudioDriverSelection(initialAudioEnv.c_str());
        else applyAudioDriverSelection(nullptr);
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const char* active = SDL_GetCurrentAudioDriver();
            if (forcedDriver && *forcedDriver && active && std::strcmp(active, forcedDriver) != 0) {
                logStartup(std::string("audio init attempt (") + label + "): mismatch active=" + active + " expected=" + forcedDriver);
                return false;
            }
            logStartup(std::string("audio init attempt (") + label + "): ok");
            return true;
        }
        const std::string err = sdlErr();
        logStartup(std::string("audio init attempt (") + label + ") failed: " + err);
        if ((forcedDriver && *forcedDriver && std::strcmp(forcedDriver, "dummy") != 0) &&
            err.find("Pipewire: Failed to connect hotplug detection context") != std::string::npos) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            applyAudioDriverSelection("dummy");
            if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                const char* active = SDL_GetCurrentAudioDriver();
                if (active && std::strcmp(active, "dummy") == 0) {
                    logStartup(std::string("audio init fallback (") + label + " -> dummy): ok");
                    return true;
                }
                logStartup(std::string("audio init fallback (") + label + " -> dummy): mismatch active=" +
                           (active ? active : "<none>") + " expected=dummy");
            } else {
                logStartup(std::string("audio init fallback (") + label + " -> dummy) failed: " + sdlErr());
            }
        }
        return false;
    };
    auto isGoodAudioDriver = [](const char* driver) -> bool {
        if (!driver) return false;
#if defined(__ANDROID__)
        return std::strcmp(driver, "dummy") != 0;
#else
        return std::strcmp(driver, "pipewire") == 0 ||
               std::strcmp(driver, "pulseaudio") == 0 ||
               std::strcmp(driver, "dummy") == 0;
#endif
    };
    bool audioReady = false;
    if (hasExplicitAudioEnv) {
        audioReady = tryAudioInit("env default", nullptr);
    } else {
#if defined(__ANDROID__)
        if (!audioReady) audioReady = tryAudioInit("env default", nullptr);
        if (!audioReady && hasAudioDriver("aaudio")) audioReady = tryAudioInit("aaudio", "aaudio");
        if (!audioReady && hasAudioDriver("openslES")) audioReady = tryAudioInit("openslES", "openslES");
        if (!audioReady && hasAudioDriver("dummy")) audioReady = tryAudioInit("dummy", "dummy");
#else
        if (hasAudioDriver("pipewire")) audioReady = tryAudioInit("pipewire", "pipewire");
        if (!audioReady && hasAudioDriver("pulseaudio")) audioReady = tryAudioInit("pulseaudio", "pulseaudio");
        if (!audioReady) audioReady = tryAudioInit("env default", nullptr);
        if (!audioReady && hasAudioDriver("alsa")) audioReady = tryAudioInit("alsa", "alsa");
        if (!audioReady && hasAudioDriver("sndio")) audioReady = tryAudioInit("sndio", "sndio");
        if (!audioReady && hasAudioDriver("dummy")) audioReady = tryAudioInit("dummy", "dummy");
#endif
    }
    if (!audioReady) {
        logStartup("audio init failed for all attempted drivers");
    } else {
        const char* active = SDL_GetCurrentAudioDriver();
        if (!isGoodAudioDriver(active)) {
            logStartup(std::string("audio override: active driver '") + (active ? active : "<none>") + "' is not preferred");
            bool overridden = false;
#if defined(__ANDROID__)
            if (hasAudioDriver("aaudio")) overridden = tryAudioInit("override aaudio", "aaudio");
            if (!overridden && hasAudioDriver("openslES")) overridden = tryAudioInit("override openslES", "openslES");
#else
            if (hasAudioDriver("pipewire")) overridden = tryAudioInit("override pipewire", "pipewire");
            if (!overridden && hasAudioDriver("pulseaudio")) overridden = tryAudioInit("override pulseaudio", "pulseaudio");
            if (!overridden && hasAudioDriver("dummy")) overridden = tryAudioInit("override dummy", "dummy");
#endif
            if (!overridden) {
                logStartup("audio override: no preferred driver override succeeded");
                (void)tryAudioInit("restore env default", nullptr);
            } else {
                logStartup(std::string("audio override: switched to ") +
                           (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "<none>"));
            }
        }
    }
    logStartup(std::string("SDL drivers: video=") +
               (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "<none>") +
               " audio=" +
               (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "<none>"));
    logStartup(std::string("audio drivers available: ") + listAudioDrivers());
    logStartup(std::string("active audio driver: ") +
               (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "<none>"));
    if ((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) == 0) {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            logStartup(std::string("gamepad init attempt failed: ") + sdlErr());
        } else {
            logStartup("gamepad init attempt: ok");
        }
    }
    logStartup(std::string("connected gamepads: ") + listConnectedGamepads());
    InitTextRenderer(ResolveAssetPath("assets/Fonts/Main.ttf"));
    AudioSystem audio;
    audio.setLoopingEnabled(true);
    bool audioRecoveryEnabled = true;
    int audioRecoverFailures = 0;
    constexpr int kMaxAudioRecoverFailures = 3;
    const bool audioInitializedAtStartup = audio.initialize();
    if (!audioInitializedAtStartup) {
        const char* startupAudioDriver = SDL_GetCurrentAudioDriver();
        if (!isGoodAudioDriver(startupAudioDriver)) {
            SDL_Log("audio: disabling runtime recovery because no preferred audio backend is active");
            audioRecoveryEnabled = false;
        }
    }
    InputSystem input;
    input.scanConnected();
    {
        InputSystem::DetectionEvent ev;
        while (input.pollDetectionEvent(ev)) {
            const char* typeStr = (ev.type == InputSystem::DetectionEvent::Type::Connected) ? "connected" : "disconnected";
            SDL_Log("controller %s: id=%d name=\"%s\" connected=%d", typeStr, (int)ev.id, ev.name.c_str(), ev.connectedCount);
        }
        if (input.hasGamepad()) {
            const char* name = input.activeGamepadName();
            SDL_Log("active controller: %s", name ? name : "<unnamed>");
        } else {
            SDL_Log("active controller: <none>");
        }
    }

    int kBaseScreenW = 1920;
    int kBaseScreenH = 1080;
    {
        const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* displayMode = nullptr;
        if (primaryDisplay != 0) {
            displayMode = SDL_GetCurrentDisplayMode(primaryDisplay);
            if (!displayMode) displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
        }
        if (displayMode && displayMode->w > 0 && displayMode->h > 0) {
            float aspect = (float)displayMode->w / (float)displayMode->h;
            // Clamp to practical gameplay targets: 4:3 .. 32:9.
            aspect = std::clamp(aspect, 4.0f / 3.0f, 32.0f / 9.0f);
            kBaseScreenH = 1080;
            kBaseScreenW = std::max(1280, (int)std::lround((float)kBaseScreenH * aspect));
            if ((kBaseScreenW & 1) != 0) ++kBaseScreenW;
            SDL_Log("Adaptive aspect target: %dx%d (display %dx%d)", kBaseScreenW, kBaseScreenH, displayMode->w, displayMode->h);
        }
    }
    // Base resolution is 2x legacy (1920x1080 vs 960x540), so zoom must also be
    // doubled to preserve the original gameplay/background framing.
    constexpr float kGameplayZoom = 3.0f;
    int kGameplayViewW = std::max(1, (int)std::lround((float)kBaseScreenW / kGameplayZoom));
    int kGameplayViewH = std::max(1, (int)std::lround((float)kBaseScreenH / kGameplayZoom));

    SDL_Window* win = SDL_CreateWindow("Dorfplatformer Timetravel", kBaseScreenW, kBaseScreenH, SDL_WINDOW_RESIZABLE);
    if (!win) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow(resizable) failed: %s", SDL_GetError());
        win = SDL_CreateWindow("Dorfplatformer Timetravel", kBaseScreenW, kBaseScreenH, 0);
    }
    if (!win) {
        reportStartupError("Window Error", std::string("SDL_CreateWindow failed: ") + SDL_GetError(), nullptr);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (!ren) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer(default) failed: %s", SDL_GetError());
        ren = SDL_CreateRenderer(win, "software");
    }
    if (!ren) {
        reportStartupError("Renderer Error", std::string("SDL_CreateRenderer failed: ") + SDL_GetError(), win);
        SDL_DestroyWindow(win);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }
    SDL_Log("Window and renderer created");
    SDL_Texture* gameTarget = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        kBaseScreenW, kBaseScreenH
    );
    SDL_Texture* worldTarget = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        kGameplayViewW, kGameplayViewH
    );
    if (!gameTarget || !worldTarget) {
        reportStartupError("Render Target Error", std::string("SDL_CreateTexture failed: ") + SDL_GetError(), win);
        if (worldTarget) SDL_DestroyTexture(worldTarget);
        if (gameTarget) SDL_DestroyTexture(gameTarget);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(worldTarget, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(gameTarget, SDL_SCALEMODE_NEAREST);
    SDL_Window* debugWin = nullptr;
    SDL_Renderer* debugRen = nullptr;
    bool showDetailedDebugger = false;
    std::array<float, 240> frameMsHistory{};
    std::array<float, 240> memRssHistory{};
    int frameMsHistoryHead = 0;

    nlohmann::json texJson;
    {
        const std::string text = ReadTextFile("assets/textures.json");
        if (!text.empty()) {
            try { texJson = nlohmann::json::parse(text); } catch (...) { texJson = nlohmann::json(); }
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
    SDL_Texture* pauseTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "pause", "assets/Sheets/DF_Pause-uhd.png")).c_str());
    if (pauseTex) SDL_SetTextureScaleMode(pauseTex, SDL_SCALEMODE_NEAREST);
    auto pauseFrames = loadPlistFrames(pausePlist);

    std::string blocksPlist = texPath("plists", "blocks", "assets/Sheets/DF_Blocks-uhd.plist");
    SDL_Texture* blocksTex = loadTextureWithColorKey(ren, texPath("textures", "blocks", "assets/Sheets/DF_Blocks-uhd.png"), 0x9f, 0x61, 0xff);
    auto blocksFrameList = loadPlistFrameList(blocksPlist);
    std::unordered_map<std::string, Frame> blocksFrameByName;
    blocksFrameByName.reserve(blocksFrameList.size());
    for (const auto& e : blocksFrameList) blocksFrameByName[e.name] = e.frame;
    std::vector<const Frame*> blocksFrameById(65536, nullptr);
    for (const auto& e : blocksFrameList) {
        bool numeric = !e.name.empty();
        for (char ch : e.name) {
            if (!std::isdigit((unsigned char)ch)) {
                numeric = false;
                break;
            }
        }
        if (!numeric) continue;
        int id = 0;
        try { id = std::stoi(e.name); } catch (...) { continue; }
        if (id < 0 || id >= (int)blocksFrameById.size()) continue;
        blocksFrameById[id] = &e.frame;
    }
    const Frame* cycleFrames[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 8; ++i) {
        const std::string key = std::string("c") + std::to_string(i + 1);
        auto it = blocksFrameByName.find(key);
        if (it != blocksFrameByName.end()) cycleFrames[i] = &it->second;
    }
    const Frame* world3PatternFrames[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 10; ++i) {
        const std::string key = std::string("3.") + std::to_string(i + 1);
        auto it = blocksFrameByName.find(key);
        if (it != blocksFrameByName.end()) world3PatternFrames[i] = &it->second;
        else {
            auto itPng = blocksFrameByName.find(key + ".png");
            if (itPng != blocksFrameByName.end()) world3PatternFrames[i] = &itPng->second;
            else {
                const std::string fallbackKey = std::string("c") + std::to_string(i + 1);
                auto itFallback = blocksFrameByName.find(fallbackKey);
                if (itFallback != blocksFrameByName.end()) world3PatternFrames[i] = &itFallback->second;
                else {
                    auto itFallbackPng = blocksFrameByName.find(fallbackKey + ".png");
                    if (itFallbackPng != blocksFrameByName.end()) world3PatternFrames[i] = &itFallbackPng->second;
                }
            }
        }
    }

    SDL_Texture* bgTexWorld1 = nullptr;
    SDL_Texture* bgTexWorld2 = nullptr;
    SDL_Texture* bgTexWorld4 = nullptr;
    SDL_Texture* bgTexWorld5 = nullptr;
    SDL_Texture* bgTexWorld6 = nullptr;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld1;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld2;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld4;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld5;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld6;
    std::vector<Frame> bgFrameListWorld1;
    std::vector<Frame> bgFrameListWorld2;
    std::vector<Frame> bgFrameListWorld4;
    std::vector<Frame> bgFrameListWorld5;
    std::vector<Frame> bgAnimFramesWorld6;
    auto loadBgSheet = [&](const std::string& texPathPrimary,
                           const std::string& plistPrimary,
                           SDL_Texture*& outTex,
                           std::unordered_map<std::string, Frame>& outFrames,
                           std::vector<Frame>* outAnimFrames = nullptr) {
        outTex = IMG_LoadTexture(ren, ResolveAssetPath(texPathPrimary).c_str());
        if (outTex) SDL_SetTextureScaleMode(outTex, SDL_SCALEMODE_NEAREST);
        auto bgFrameList = loadPlistFrameList(plistPrimary);
        if (!outTex || bgFrameList.empty()) {
            SDL_Log("Background sheet load incomplete: texture='%s' loaded=%d plist='%s' frames=%d",
                    texPathPrimary.c_str(), outTex ? 1 : 0, plistPrimary.c_str(), (int)bgFrameList.size());
        }
        outFrames.clear();
        outFrames.reserve(bgFrameList.size());
        for (const auto& e : bgFrameList) outFrames[e.name] = e.frame;
        if (outAnimFrames) {
            outAnimFrames->clear();
            outAnimFrames->reserve(bgFrameList.size());
            for (const auto& e : bgFrameList) outAnimFrames->push_back(e.frame);
        }
    };
    loadBgSheet(
        texPath("textures", "background_world1", "assets/Sheets/DF_Back_1-uhd.png"),
        texPath("plists", "background_world1", "assets/Sheets/DF_Back_1-uhd.plist"),
        bgTexWorld1,
        bgFrameByNameWorld1,
        &bgFrameListWorld1
    );
    loadBgSheet(
        texPath("textures", "background_world2", "assets/Sheets/DF_Back_2-uhd.png"),
        texPath("plists", "background_world2", "assets/Sheets/DF_Back_2-uhd.plist"),
        bgTexWorld2,
        bgFrameByNameWorld2,
        &bgFrameListWorld2
    );
    loadBgSheet(
        texPath("textures", "background_world4", "assets/Sheets/DF_Back_4-uhd.png"),
        texPath("plists", "background_world4", "assets/Sheets/DF_Back_4-uhd.plist"),
        bgTexWorld4,
        bgFrameByNameWorld4,
        &bgFrameListWorld4
    );
    loadBgSheet(
        texPath("textures", "background_world5", "assets/Sheets/DF_Back_5-uhd.png"),
        texPath("plists", "background_world5", "assets/Sheets/DF_Back_5-uhd.plist"),
        bgTexWorld5,
        bgFrameByNameWorld5,
        &bgFrameListWorld5
    );
    loadBgSheet(
        texPath("textures", "background_world6", "assets/Sheets/DF_Back_6-uhd.png"),
        texPath("plists", "background_world6", "assets/Sheets/DF_Back_6-uhd.plist"),
        bgTexWorld6,
        bgFrameByNameWorld6,
        &bgAnimFramesWorld6
    );
    SDL_Texture* introCardTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/Introcard-uhd.png").c_str());
    if (introCardTex) SDL_SetTextureScaleMode(introCardTex, SDL_SCALEMODE_NEAREST);
    auto introCardFrames = loadPlistFrames("assets/Sheets/Introcard-uhd.plist");
    SDL_Texture* endSignTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/end_sign-uhd.png").c_str());
    if (endSignTex) SDL_SetTextureScaleMode(endSignTex, SDL_SCALEMODE_NEAREST);
    auto endSignFrames = loadPlistFrames("assets/Sheets/end_sign-uhd.plist");
    const Frame* defaultEndSignFrame = nullptr;
    {
        auto it = endSignFrames.find("SignPost9");
        if (it == endSignFrames.end()) it = endSignFrames.find("SignPost9.png");
        if (it != endSignFrames.end()) defaultEndSignFrame = &it->second;
    }
    SDL_Texture* bossesTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/DF_Bosses-uhd.png").c_str());
    if (bossesTex) SDL_SetTextureScaleMode(bossesTex, SDL_SCALEMODE_NEAREST);
    auto bossesFrames = loadPlistFrames("assets/Sheets/DF_Bosses-uhd.plist");
    const Frame* bossNormalFrame = nullptr;
    const Frame* bossHurtFrame = nullptr;
    const Frame* bossFinalNormalFrame = nullptr;
    {
        auto it = bossesFrames.find("Normal-1");
        if (it == bossesFrames.end()) it = bossesFrames.find("Normal-1.png");
        if (it != bossesFrames.end()) bossNormalFrame = &it->second;
        it = bossesFrames.find("Hurt-1");
        if (it == bossesFrames.end()) it = bossesFrames.find("Hurt-1.png");
        if (it != bossesFrames.end()) bossHurtFrame = &it->second;
        it = bossesFrames.find("Final-Normal");
        if (it == bossesFrames.end()) it = bossesFrames.find("Final-Normal.png");
        if (it != bossesFrames.end()) bossFinalNormalFrame = &it->second;
    }

    const std::string entitiesPlist = "assets/Sheets/DF_Enitys-uhd.plist";
    SDL_Texture* entitiesTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/DF_Enitys-uhd.png").c_str());
    if (entitiesTex) SDL_SetTextureScaleMode(entitiesTex, SDL_SCALEMODE_NEAREST);
    auto entitiesFrameList = loadPlistFrameList(entitiesPlist);
    std::unordered_map<std::string, Frame> entitiesFrameByName;
    entitiesFrameByName.reserve(entitiesFrameList.size());
    for (const auto& e : entitiesFrameList) entitiesFrameByName[e.name] = e.frame;
    std::unordered_map<std::string, std::string> entityFrameKeyByObjectId;
    entityFrameKeyByObjectId["31"] = "Spring";
    const Frame* defaultEntityFrame = !entitiesFrameList.empty() ? &entitiesFrameList[0].frame : nullptr;

    std::unordered_map<int, std::string> tileFrameById;
    {
        const std::string text = ReadTextFile("assets/tile_defs.json");
        if (!text.empty()) {
            nlohmann::json j;
            try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
            auto readSection = [&](const char* key) {
                if (!j.contains(key) || !j[key].is_object()) return;
                for (auto it = j[key].begin(); it != j[key].end(); ++it) {
                    if (!it.value().is_object()) continue;
                    if (!it.value().contains("frame") || !it.value()["frame"].is_string()) continue;
                    int id = 0;
                    try { id = std::stoi(it.key()); } catch (...) { continue; }
                    tileFrameById[id] = it.value()["frame"].get<std::string>();
                }
            };
            readSection("bg");
            readSection("fg");
        }
    }

    std::string playerPlist = texPath("plists", "player", "assets/Sheets/DF_Player1-uhd.plist");
    SDL_Texture* playerTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "player", "assets/Sheets/DF_Player1-uhd.png")).c_str());
    if (playerTex) SDL_SetTextureScaleMode(playerTex, SDL_SCALEMODE_NEAREST);
    auto playerFrameList = loadPlistFrameList(playerPlist);
    std::unordered_map<std::string, Frame> playerFramesByName;
    playerFramesByName.reserve(playerFrameList.size());
    for (const auto& e : playerFrameList) playerFramesByName[e.name] = e.frame;
    const Frame* fallbackPlayerFrame = !playerFrameList.empty() ? &playerFrameList[0].frame : nullptr;
    audio.loadGlobalAssets();
    std::unordered_map<int, std::string> worldNamesById;
    struct IntroTextStyle {
        int x = 150;
        int y = 332;
        int scale = 4;
    };
    IntroTextStyle introWorldNameStyle;
    IntroTextStyle introAreaIdStyle{545, 322, 8};
    {
        const std::string worldNameText = ReadTextFile("assets/world_names.json");
        if (!worldNameText.empty()) {
            try {
                const nlohmann::json j = nlohmann::json::parse(worldNameText);
                auto parseIntMap = [](const nlohmann::json& obj, std::unordered_map<int, std::string>& out) {
                    if (!obj.is_object()) return;
                    for (auto it = obj.begin(); it != obj.end(); ++it) {
                        if (!it.value().is_string()) continue;
                        try {
                            out[std::stoi(it.key())] = it.value().get<std::string>();
                        } catch (...) {}
                    }
                };
                auto parseStyle = [](const nlohmann::json& obj, IntroTextStyle& style) {
                    if (!obj.is_object()) return;
                    if (obj.contains("x") && obj["x"].is_number_integer()) style.x = obj["x"].get<int>();
                    if (obj.contains("y") && obj["y"].is_number_integer()) style.y = obj["y"].get<int>();
                    if (obj.contains("scale") && obj["scale"].is_number_integer()) style.scale = std::max(1, obj["scale"].get<int>());
                };
                if (j.is_object()) {
                    if (j.contains("world_names")) parseIntMap(j["world_names"], worldNamesById);
                    if (worldNamesById.empty()) parseIntMap(j, worldNamesById);
                    if (j.contains("intro_layout") && j["intro_layout"].is_object()) {
                        const auto& layout = j["intro_layout"];
                        if (layout.contains("world_name")) parseStyle(layout["world_name"], introWorldNameStyle);
                        if (layout.contains("area_id")) parseStyle(layout["area_id"], introAreaIdStyle);
                    }
                }
            } catch (...) {}
        }
    }

    auto getPlayerFrame = [&](const std::string& name) -> const Frame* {
        auto it = playerFramesByName.find(name);
        if (it == playerFramesByName.end()) return fallbackPlayerFrame;
        return &it->second;
    };

    struct AnimConfig {
        float fps = 20.0f;
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
        const std::string text = ReadTextFile(path);
        if (text.empty()) return cfg;
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(text);
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
    auto framesFromNames = [&](const std::vector<std::string>& names) {
        std::vector<const Frame*> out;
        out.reserve(names.size());
        for (const auto& n : names) out.push_back(getPlayerFrame(n));
        return out;
    };
    const std::vector<const Frame*> animIdleFrames = framesFromNames(animCfg.idle);
    const std::vector<const Frame*> animWalkFrames = framesFromNames(animCfg.walk);
    const std::vector<const Frame*> animJumpFrames = framesFromNames(animCfg.jump);
    const std::vector<const Frame*> animFallFrames = framesFromNames(animCfg.fall);
    const std::vector<const Frame*> animCrouchFrames = framesFromNames(animCfg.crouch);
    const std::vector<const Frame*> animSkidFrames = framesFromNames(animCfg.skid);
    const std::vector<const Frame*> animHurtFrames = framesFromNames(animCfg.hurt);
    const std::vector<const Frame*> animDeathFrames = framesFromNames(animCfg.death);
    float jumpBufferMax = 0.12f;
    std::string levelServerUrl;
    std::string levelServerAuthToken;
    std::string appVersion = "dev";
    MovementConfig movementCfg{};
    float bossGravity = 0.0f;
    std::array<float, 3> parallaxLayerScales{{0.80f, 0.80f, 0.80f}};
    {
        const std::string text = ReadTextFile("assets/config.json");
        if (!text.empty()) {
            nlohmann::json cfg;
            try { cfg = nlohmann::json::parse(text); } catch (...) { cfg = nlohmann::json(); }
            if (cfg.contains("version") && cfg["version"].is_string()) {
                appVersion = cfg["version"].get<std::string>();
            }
            if (cfg.contains("level_server_url") && cfg["level_server_url"].is_string()) {
                levelServerUrl = cfg["level_server_url"].get<std::string>();
            }
            if (cfg.contains("level_server_auth_token") && cfg["level_server_auth_token"].is_string()) {
                levelServerAuthToken = cfg["level_server_auth_token"].get<std::string>();
            }
            if (cfg.contains("jump_buffer_seconds") && cfg["jump_buffer_seconds"].is_number()) {
                jumpBufferMax = (float)cfg["jump_buffer_seconds"].get<double>();
                if (jumpBufferMax < 0.0f) jumpBufferMax = 0.0f;
            }
            if (cfg.contains("movement") && cfg["movement"].is_object()) {
                const auto& m = cfg["movement"];
                auto readMove = [&](const char* key, float& out) {
                    if (m.contains(key) && m[key].is_number()) out = (float)m[key].get<double>();
                };
                readMove("accel_in_water", movementCfg.accelInWater);
                readMove("accel_ground", movementCfg.accelGround);
                readMove("max_speed_in_water", movementCfg.maxSpeedInWater);
                readMove("max_speed_ground", movementCfg.maxSpeedGround);
                readMove("friction_in_water", movementCfg.frictionInWater);
                readMove("friction_ground", movementCfg.frictionGround);
                readMove("gravity_in_water", movementCfg.gravityInWater);
                readMove("gravity_ground", movementCfg.gravityGround);
                readMove("jump_speed", movementCfg.jumpSpeed);
                readMove("jump_hold_gravity", movementCfg.jumpHoldGravity);
                readMove("jump_hold_max", movementCfg.jumpHoldMax);
                readMove("jump_cut_speed", movementCfg.jumpCutSpeed);
                readMove("swim_up_speed", movementCfg.swimUpSpeed);
                readMove("swim_rise", movementCfg.swimRise);
                readMove("boss_gravity", bossGravity);
            }
            if (cfg.contains("background") && cfg["background"].is_object()) {
                const auto& bg = cfg["background"];
                if (bg.contains("parallax_layer_scales") && bg["parallax_layer_scales"].is_array()) {
                    const auto& a = bg["parallax_layer_scales"];
                    for (int i = 0; i < 3 && i < (int)a.size(); ++i) {
                        if (a[i].is_number()) {
                            parallaxLayerScales[i] = std::clamp((float)a[i].get<double>(), 0.1f, 4.0f);
                        }
                    }
                }
            }
        }
    }
    SetLevelServerUrl(levelServerUrl);
    SetLevelServerAuthToken(levelServerAuthToken);
    if (!levelServerUrl.empty()) {
        SDL_Log("Level server: %s", levelServerUrl.c_str());
    }
    if (!levelServerAuthToken.empty()) {
        SDL_Log("Level server auth token configured");
    }
    {
        const std::string text = ReadTextFile("assets/log_settings.json");
        if (!text.empty()) {
            nlohmann::json logCfg;
            try { logCfg = nlohmann::json::parse(text); } catch (...) { logCfg = nlohmann::json(); }
            auto parsePriority = [](const nlohmann::json& v, SDL_LogPriority fallback) -> SDL_LogPriority {
                if (v.is_number_integer()) {
                    int p = v.get<int>();
                    if (p >= SDL_LOG_PRIORITY_VERBOSE && p <= SDL_LOG_PRIORITY_CRITICAL) {
                        return (SDL_LogPriority)p;
                    }
                    return fallback;
                }
                if (!v.is_string()) return fallback;
                std::string s = v.get<std::string>();
                for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
                if (s == "verbose") return SDL_LOG_PRIORITY_VERBOSE;
                if (s == "debug") return SDL_LOG_PRIORITY_DEBUG;
                if (s == "info") return SDL_LOG_PRIORITY_INFO;
                if (s == "warn" || s == "warning") return SDL_LOG_PRIORITY_WARN;
                if (s == "error") return SDL_LOG_PRIORITY_ERROR;
                if (s == "critical" || s == "fatal") return SDL_LOG_PRIORITY_CRITICAL;
                return fallback;
            };
            auto categoryFromName = [](const std::string& name) -> int {
                if (name == "application" || name == "app") return SDL_LOG_CATEGORY_APPLICATION;
                if (name == "error") return SDL_LOG_CATEGORY_ERROR;
                if (name == "assert") return SDL_LOG_CATEGORY_ASSERT;
                if (name == "system") return SDL_LOG_CATEGORY_SYSTEM;
                if (name == "audio") return SDL_LOG_CATEGORY_AUDIO;
                if (name == "video") return SDL_LOG_CATEGORY_VIDEO;
                if (name == "render") return SDL_LOG_CATEGORY_RENDER;
                if (name == "input") return SDL_LOG_CATEGORY_INPUT;
                if (name == "test") return SDL_LOG_CATEGORY_TEST;
                return SDL_LOG_CATEGORY_CUSTOM;
            };

            SDL_LogPriority defaultPriority = SDL_LOG_PRIORITY_INFO;
            if (logCfg.contains("global_priority")) {
                defaultPriority = parsePriority(logCfg["global_priority"], defaultPriority);
            }
            SDL_LogSetAllPriority(defaultPriority);

            if (logCfg.contains("categories") && logCfg["categories"].is_object()) {
                for (auto it = logCfg["categories"].begin(); it != logCfg["categories"].end(); ++it) {
                    std::string key = it.key();
                    for (char& ch : key) ch = (char)std::tolower((unsigned char)ch);
                    int category = categoryFromName(key);
                    SDL_LogSetPriority(category, parsePriority(it.value(), defaultPriority));
                }
            }
        }
    }

    if (!pauseTex || !blocksTex || !playerTex || pauseFrames.empty() || blocksFrameList.empty() || playerFrameList.empty()) {
        std::string msg = "Failed to load assets:";
        if (!pauseTex) msg += "\n- pause texture";
        if (pauseFrames.empty()) msg += "\n- pause plist";
        if (!blocksTex) msg += "\n- blocks texture";
        if (blocksFrameList.empty()) msg += "\n- blocks plist";
        if (!playerTex) msg += "\n- player texture";
        if (playerFrameList.empty()) msg += "\n- player plist";
        reportStartupError("Asset Load Error", msg, win);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        audio.shutdown();
        ShutdownTextRenderer();
        SDL_Quit();
        return 1;
    }
    if (!gameTarget) {
        reportStartupError("Render Target Error", "Failed to create game render target.", win);
        if (playerTex) SDL_DestroyTexture(playerTex);
        if (bgTexWorld1) SDL_DestroyTexture(bgTexWorld1);
        if (bgTexWorld2) SDL_DestroyTexture(bgTexWorld2);
        if (bgTexWorld4) SDL_DestroyTexture(bgTexWorld4);
        if (bgTexWorld5) SDL_DestroyTexture(bgTexWorld5);
        if (bgTexWorld6) SDL_DestroyTexture(bgTexWorld6);
        if (introCardTex) SDL_DestroyTexture(introCardTex);
        if (blocksTex) SDL_DestroyTexture(blocksTex);
        if (entitiesTex) SDL_DestroyTexture(entitiesTex);
        if (bossesTex) SDL_DestroyTexture(bossesTex);
        if (endSignTex) SDL_DestroyTexture(endSignTex);
        if (pauseTex) SDL_DestroyTexture(pauseTex);
        if (worldTarget) SDL_DestroyTexture(worldTarget);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        audio.shutdown();
        ShutdownTextRenderer();
        SDL_Quit();
        return 1;
    }

    bool fullscreen = false;
    bool vsyncEnabled = false;
    bool clampCamX = true;
    bool defaultShowFpsCounter = false;
    bool defaultShowDetailedDebugger = false;
    bool defaultShowHitboxes = false;
    bool defaultShowPlayerHitbox = false;
    bool defaultShowDebugView = false;
    bool defaultHideUnknownObjectTypes = false;
    bool powerManagementEnabled = true;
    bool menuMusicEnabled = true;
    bool muteAllAudio = false;
    KeyboardBindings keybinds{};
    constexpr int kUiScaleMinPercent = 50;
    constexpr int kUiScaleMaxPercent = 200;
    int uiScalePercent = kUiScaleMaxPercent;
    std::array<bool, 55> extraSettings{};
    extraSettings[44] = true; // PRIVACY+ -> SEND ANONYMOUS METRICS
    const std::string defaultTelemetryWebhook = "https://discord.com/api/webhooks/1471610164829356085/at2iXFzt7euIGzvIaN8iQEgNS6m1RfKUShwq6RPyIUUefIO7Id-uWxdB9Mo4wP1WKVWj";
    std::string telemetryWebhookUrl = defaultTelemetryWebhook;
    if (const char* envHook = std::getenv("DF_DISCORD_WEBHOOK"); envHook && *envHook) {
        telemetryWebhookUrl = envHook;
    }
    float fastTravelChangeDelay = 0.0f;
    int musicVolume = 96; // 0..128
    int sfxVolume = 96;   // 0..128
    const std::string localClientSettingsPath = "client_settings.json";
    const std::string appSaveRootPath = GetAppSaveRootPath();
    const std::filesystem::path replayDirPath = std::filesystem::path(appSaveRootPath) / "replays";
    std::string clientSettingsPath = localClientSettingsPath;
    if (!appSaveRootPath.empty()) {
        clientSettingsPath = (std::filesystem::path(appSaveRootPath) / "client_settings.json").string();
    }
    auto saveClientSettings = [&]() {
        nlohmann::json j;
        j["build_uuid"] = buildUuid;
        nlohmann::json settings;
        settings["display"] = {
            {"fullscreen", fullscreen},
            {"vsync", vsyncEnabled},
            {"ui_scale_percent", uiScalePercent}
        };
        settings["camera"] = {
            {"clamp_cam_x", clampCamX}
        };
        settings["debug"] = {
            {"show_fps_counter", defaultShowFpsCounter},
            {"show_detailed_debugger", defaultShowDetailedDebugger},
            {"show_hitboxes", defaultShowHitboxes},
            {"show_player_hitbox", defaultShowPlayerHitbox},
            {"show_debug_view", defaultShowDebugView},
            {"hide_unknown_object_types", defaultHideUnknownObjectTypes}
        };
        settings["audio"] = {
            {"menu_music_enabled", menuMusicEnabled},
            {"mute_all_audio", muteAllAudio},
            {"music_volume", musicVolume},
            {"sfx_volume", sfxVolume}
        };
        settings["controls"] = {
            {"move_left", (int)keybinds.moveLeft},
            {"move_right", (int)keybinds.moveRight},
            {"move_down", (int)keybinds.moveDown},
            {"jump", (int)keybinds.jump},
            {"pause", (int)keybinds.pause}
        };
        settings["gameplay"] = {
            {"power_management", powerManagementEnabled},
            {"fast_travel_delay", fastTravelChangeDelay}
        };
        settings["telemetry"] = {
            {"telemetry_webhook_url", telemetryWebhookUrl}
        };
        {
            nlohmann::json extra = nlohmann::json::array();
            for (bool v : extraSettings) extra.push_back(v);
            settings["extra_settings"] = std::move(extra);
        }
        j["settings"] = std::move(settings);

        // Legacy flat keys kept for backward compatibility.
        j["fullscreen"] = fullscreen;
        j["vsync"] = vsyncEnabled;
        j["clamp_cam_x"] = clampCamX;
        j["show_fps_counter"] = defaultShowFpsCounter;
        j["show_detailed_debugger"] = defaultShowDetailedDebugger;
        j["show_hitboxes"] = defaultShowHitboxes;
        j["show_player_hitbox"] = defaultShowPlayerHitbox;
        j["show_debug_view"] = defaultShowDebugView;
        j["hide_unknown_object_types"] = defaultHideUnknownObjectTypes;
        j["power_management"] = powerManagementEnabled;
        j["menu_music_enabled"] = menuMusicEnabled;
        j["mute_all_audio"] = muteAllAudio;
        j["key_move_left"] = (int)keybinds.moveLeft;
        j["key_move_right"] = (int)keybinds.moveRight;
        j["key_move_down"] = (int)keybinds.moveDown;
        j["key_jump"] = (int)keybinds.jump;
        j["key_pause"] = (int)keybinds.pause;
        j["ui_scale_percent"] = uiScalePercent;
        j["extra_settings"] = j["settings"]["extra_settings"];
        j["telemetry_webhook_url"] = telemetryWebhookUrl;
        j["fast_travel_delay"] = fastTravelChangeDelay;
        j["music_volume"] = musicVolume;
        j["sfx_volume"] = sfxVolume;
        auto tryWriteSettings = [&](const std::string& path) -> bool {
            try {
                std::filesystem::path p(path);
                if (p.has_parent_path()) {
                    std::error_code ec;
                    std::filesystem::create_directories(p.parent_path(), ec);
                }
                const std::filesystem::path tmp = p.string() + ".tmp";
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) return false;
                out << j.dump(2);
                out.flush();
                if (!out.good()) return false;
                out.close();
                std::error_code ec;
                std::filesystem::rename(tmp, p, ec);
                if (ec) {
                    // Replace existing target if rename can't overwrite.
                    std::filesystem::remove(p, ec);
                    ec.clear();
                    std::filesystem::rename(tmp, p, ec);
                    if (ec) return false;
                }
                return true;
            } catch (...) {
                return false;
            }
        };
        if (!tryWriteSettings(clientSettingsPath)) {
            (void)tryWriteSettings(localClientSettingsPath);
        }
    };
    {
        auto applyClientSettingsJson = [&](const nlohmann::json& j) {
            auto parseScancode = [](const nlohmann::json& v, SDL_Scancode fallback) -> SDL_Scancode {
                if (!v.is_number_integer()) return fallback;
                const int raw = v.get<int>();
                if (raw <= (int)SDL_SCANCODE_UNKNOWN || raw >= (int)SDL_SCANCODE_COUNT) return fallback;
                return (SDL_Scancode)raw;
            };
            const bool hasStructured = j.contains("settings") && j["settings"].is_object();
            if (hasStructured) {
                const auto& s = j["settings"];
                if (s.contains("display") && s["display"].is_object()) {
                    const auto& d = s["display"];
                    if (d.contains("fullscreen") && d["fullscreen"].is_boolean()) fullscreen = d["fullscreen"].get<bool>();
                    if (d.contains("vsync") && d["vsync"].is_boolean()) vsyncEnabled = d["vsync"].get<bool>();
                    if (d.contains("ui_scale_percent") && d["ui_scale_percent"].is_number_integer()) {
                        uiScalePercent = std::clamp(d["ui_scale_percent"].get<int>(), kUiScaleMinPercent, kUiScaleMaxPercent);
                    }
                }
                if (s.contains("camera") && s["camera"].is_object()) {
                    const auto& c = s["camera"];
                    if (c.contains("clamp_cam_x") && c["clamp_cam_x"].is_boolean()) clampCamX = c["clamp_cam_x"].get<bool>();
                }
                if (s.contains("debug") && s["debug"].is_object()) {
                    const auto& d = s["debug"];
                    if (d.contains("show_fps_counter") && d["show_fps_counter"].is_boolean()) defaultShowFpsCounter = d["show_fps_counter"].get<bool>();
                    if (d.contains("show_detailed_debugger") && d["show_detailed_debugger"].is_boolean()) defaultShowDetailedDebugger = d["show_detailed_debugger"].get<bool>();
                    if (d.contains("show_hitboxes") && d["show_hitboxes"].is_boolean()) defaultShowHitboxes = d["show_hitboxes"].get<bool>();
                    if (d.contains("show_player_hitbox") && d["show_player_hitbox"].is_boolean()) defaultShowPlayerHitbox = d["show_player_hitbox"].get<bool>();
                    if (d.contains("show_debug_view") && d["show_debug_view"].is_boolean()) defaultShowDebugView = d["show_debug_view"].get<bool>();
                    if (d.contains("hide_unknown_object_types") && d["hide_unknown_object_types"].is_boolean()) defaultHideUnknownObjectTypes = d["hide_unknown_object_types"].get<bool>();
                }
                if (s.contains("audio") && s["audio"].is_object()) {
                    const auto& a = s["audio"];
                    if (a.contains("menu_music_enabled") && a["menu_music_enabled"].is_boolean()) menuMusicEnabled = a["menu_music_enabled"].get<bool>();
                    if (a.contains("mute_all_audio") && a["mute_all_audio"].is_boolean()) muteAllAudio = a["mute_all_audio"].get<bool>();
                    if (a.contains("music_volume") && a["music_volume"].is_number_integer()) musicVolume = std::clamp(a["music_volume"].get<int>(), 0, 128);
                    if (a.contains("sfx_volume") && a["sfx_volume"].is_number_integer()) sfxVolume = std::clamp(a["sfx_volume"].get<int>(), 0, 128);
                }
                if (s.contains("controls") && s["controls"].is_object()) {
                    const auto& c = s["controls"];
                    if (c.contains("move_left")) keybinds.moveLeft = parseScancode(c["move_left"], keybinds.moveLeft);
                    if (c.contains("move_right")) keybinds.moveRight = parseScancode(c["move_right"], keybinds.moveRight);
                    if (c.contains("move_down")) keybinds.moveDown = parseScancode(c["move_down"], keybinds.moveDown);
                    if (c.contains("jump")) keybinds.jump = parseScancode(c["jump"], keybinds.jump);
                    if (c.contains("pause")) keybinds.pause = parseScancode(c["pause"], keybinds.pause);
                }
                if (s.contains("gameplay") && s["gameplay"].is_object()) {
                    const auto& g = s["gameplay"];
                    if (g.contains("power_management") && g["power_management"].is_boolean()) powerManagementEnabled = g["power_management"].get<bool>();
                    if (g.contains("fast_travel_delay") && g["fast_travel_delay"].is_number()) {
                        // Deprecated: delay removed in favor of immediate smooth transitions.
                        fastTravelChangeDelay = 0.0f;
                    }
                }
                if (s.contains("telemetry") && s["telemetry"].is_object()) {
                    const auto& t = s["telemetry"];
                    if (t.contains("telemetry_webhook_url") && t["telemetry_webhook_url"].is_string()) {
                        telemetryWebhookUrl = t["telemetry_webhook_url"].get<std::string>();
                    }
                }
                if (s.contains("extra_settings") && s["extra_settings"].is_array()) {
                    const auto& a = s["extra_settings"];
                    for (size_t i = 0; i < extraSettings.size() && i < a.size(); ++i) {
                        if (a[i].is_boolean()) extraSettings[i] = a[i].get<bool>();
                    }
                }
                return;
            }

            // Legacy flat format fallback.
            if (j.contains("fullscreen") && j["fullscreen"].is_boolean()) fullscreen = j["fullscreen"].get<bool>();
            if (j.contains("vsync") && j["vsync"].is_boolean()) vsyncEnabled = j["vsync"].get<bool>();
            if (j.contains("clamp_cam_x") && j["clamp_cam_x"].is_boolean()) clampCamX = j["clamp_cam_x"].get<bool>();
            if (j.contains("show_fps_counter") && j["show_fps_counter"].is_boolean()) defaultShowFpsCounter = j["show_fps_counter"].get<bool>();
            if (j.contains("show_detailed_debugger") && j["show_detailed_debugger"].is_boolean()) defaultShowDetailedDebugger = j["show_detailed_debugger"].get<bool>();
            if (j.contains("show_hitboxes") && j["show_hitboxes"].is_boolean()) defaultShowHitboxes = j["show_hitboxes"].get<bool>();
            if (j.contains("show_player_hitbox") && j["show_player_hitbox"].is_boolean()) defaultShowPlayerHitbox = j["show_player_hitbox"].get<bool>();
            if (j.contains("show_debug_view") && j["show_debug_view"].is_boolean()) defaultShowDebugView = j["show_debug_view"].get<bool>();
            if (j.contains("hide_unknown_object_types") && j["hide_unknown_object_types"].is_boolean()) defaultHideUnknownObjectTypes = j["hide_unknown_object_types"].get<bool>();
            if (j.contains("power_management") && j["power_management"].is_boolean()) powerManagementEnabled = j["power_management"].get<bool>();
            if (j.contains("menu_music_enabled") && j["menu_music_enabled"].is_boolean()) menuMusicEnabled = j["menu_music_enabled"].get<bool>();
            if (j.contains("mute_all_audio") && j["mute_all_audio"].is_boolean()) muteAllAudio = j["mute_all_audio"].get<bool>();
            if (j.contains("key_move_left")) keybinds.moveLeft = parseScancode(j["key_move_left"], keybinds.moveLeft);
            if (j.contains("key_move_right")) keybinds.moveRight = parseScancode(j["key_move_right"], keybinds.moveRight);
            if (j.contains("key_move_down")) keybinds.moveDown = parseScancode(j["key_move_down"], keybinds.moveDown);
            if (j.contains("key_jump")) keybinds.jump = parseScancode(j["key_jump"], keybinds.jump);
            if (j.contains("key_pause")) keybinds.pause = parseScancode(j["key_pause"], keybinds.pause);
            if (j.contains("ui_scale_percent") && j["ui_scale_percent"].is_number_integer()) {
                uiScalePercent = std::clamp(j["ui_scale_percent"].get<int>(), kUiScaleMinPercent, kUiScaleMaxPercent);
            }
            if (j.contains("extra_settings") && j["extra_settings"].is_array()) {
                const auto& a = j["extra_settings"];
                for (size_t i = 0; i < extraSettings.size() && i < a.size(); ++i) {
                    if (a[i].is_boolean()) extraSettings[i] = a[i].get<bool>();
                }
            }
            if (j.contains("telemetry_webhook_url") && j["telemetry_webhook_url"].is_string()) {
                telemetryWebhookUrl = j["telemetry_webhook_url"].get<std::string>();
            }
            if (j.contains("fast_travel_delay") && j["fast_travel_delay"].is_number()) {
                // Deprecated: delay removed in favor of immediate smooth transitions.
                fastTravelChangeDelay = 0.0f;
            }
            if (j.contains("music_volume") && j["music_volume"].is_number_integer()) musicVolume = std::clamp(j["music_volume"].get<int>(), 0, 128);
            if (j.contains("sfx_volume") && j["sfx_volume"].is_number_integer()) sfxVolume = std::clamp(j["sfx_volume"].get<int>(), 0, 128);
        };

        std::string text = ReadTextFile(clientSettingsPath);
        if (text.empty() && clientSettingsPath != localClientSettingsPath) {
            text = ReadTextFile(localClientSettingsPath);
        }
        if (!text.empty()) {
            nlohmann::json j;
            try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
            applyClientSettingsJson(j);
        } else {
            const std::string placeholderPath = "assets/client_settings.json";
            const std::string placeholderText = ReadTextFile(placeholderPath);
            bool usedPlaceholder = false;
            if (!placeholderText.empty()) {
                nlohmann::json j;
                try { j = nlohmann::json::parse(placeholderText); } catch (...) { j = nlohmann::json(); }
                if (!j.is_null()) {
                    applyClientSettingsJson(j);
                    usedPlaceholder = true;
                }
            }
            saveClientSettings();
            if (usedPlaceholder) {
                SDL_Log("Initialized client settings from placeholder: %s", placeholderPath.c_str());
            }
        }
    }
    auto applyRenderVsync = [&]() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (SDL_RenderSetVSync(ren, vsyncEnabled ? 1 : 0) != 0) {
            SDL_Log("Could not set renderer VSync=%d: %s", vsyncEnabled ? 1 : 0, SDL_GetError());
        }
#else
        SDL_Log("Renderer VSync toggle unsupported on this SDL version.");
#endif
    };
#if !defined(__ANDROID__)
    if (fullscreen) {
        if (!SDL_SetWindowFullscreen(win, true)) {
            SDL_Log("Could not apply startup fullscreen setting: %s", SDL_GetError());
            fullscreen = false;
        }
    }
#endif
    applyRenderVsync();
    SDL_Log("Build UUID: %s", buildUuid.c_str());
    auto applyAudioVolumes = [&]() {
        audio.applyVolumes(muteAllAudio, musicVolume, sfxVolume);
    };
    applyAudioVolumes();
    auto applyMenuMusicToggle = [&]() {
        audio.applyMenuMusicToggle(menuMusicEnabled);
    };
    auto telemetryEnabled = [&]() -> bool {
        return extraSettings.size() > 44 ? extraSettings[44] : false;
    };
    auto sendDiscordTelemetry = [&](const std::string& event, const nlohmann::json& details) {
        try {
        if (!telemetryEnabled()) return;
        if (telemetryWebhookUrl.empty()) return;
#if defined(HAVE_CURL) && HAVE_CURL
        CURL* curl = curl_easy_init();
        if (!curl) return;
        nlohmann::json embed;
        embed["title"] = "DF-New Telemetry";
        embed["description"] = event;
        embed["fields"] = nlohmann::json::array();
        for (auto it = details.begin(); it != details.end(); ++it) {
            nlohmann::json field;
            field["name"] = it.key();
            field["value"] = it->is_string() ? it->get<std::string>() : it->dump();
            field["inline"] = true;
            embed["fields"].push_back(std::move(field));
        }
        nlohmann::json payload;
        payload["username"] = "DF-New Telemetry";
        payload["embeds"] = nlohmann::json::array({embed});
        const std::string body = payload.dump();
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, telemetryWebhookUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        const CURLcode rc = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (rc != CURLE_OK || code < 200 || code >= 300) {
            SDL_Log("telemetry: webhook failed code=%ld curl=%d", code, (int)rc);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
#else
        (void)event;
        (void)details;
#endif
        } catch (const std::exception& e) {
            SDL_Log("telemetry: exception: %s", e.what());
        } catch (...) {
            SDL_Log("telemetry: unknown exception");
        }
    };
    sendDiscordTelemetry("startup", {
        {"build_uuid", buildUuid},
        {"version", appVersion},
        {"platform", SDL_GetPlatform() ? SDL_GetPlatform() : "unknown"},
        {"video_driver", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown"},
        {"audio_driver", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown"}
    });
    LevelManager levelManager;
    Uint64 nextAudioRecoverTick = 0;
    auto recoverAudioIfNeeded = [&](bool inLevel) {
        if (!audioRecoveryEnabled) return;
        if (audio.isReady()) return;
        const Uint64 nowTick = SDL_GetTicks();
        if (nowTick < nextAudioRecoverTick) return;
        nextAudioRecoverTick = nowTick + 1500;
        SDL_Log("audio.recover: restarting audio backend (inLevel=%d)", inLevel ? 1 : 0);
        audio.shutdown();
        if (!audio.initialize()) {
            ++audioRecoverFailures;
            SDL_Log("audio.recover: initialize failed");
            if (audioRecoverFailures >= kMaxAudioRecoverFailures) {
                audioRecoveryEnabled = false;
                SDL_Log("audio.recover: disabled after %d consecutive failures", audioRecoverFailures);
            }
            return;
        }
        audioRecoverFailures = 0;
        audio.loadGlobalAssets();
        applyAudioVolumes();
        if (inLevel) {
            audio.loadLevelMusic(levelManager.musicPath());
        } else {
            applyMenuMusicToggle();
        }
        SDL_Log("audio.recover: restart ok");
    };
    bool running = true;
    std::string currentFancyLevelName = "LEVEL";
    std::string currentAreaIdText = "1";
    auto buildFancyLevelName = [&]() -> std::string {
        const int worldId = levelManager.worldId();
        if (worldId <= 0) {
            return "LEVEL";
        }
        auto worldIt = worldNamesById.find(worldId);
        std::string worldName = (worldIt != worldNamesById.end()) ? worldIt->second : (std::string("WORLD ") + std::to_string(worldId));
        return worldName;
    };
    auto buildAreaIdText = [&]() -> std::string {
        const int areaId = levelManager.levelPartId();
        return std::to_string(std::max(1, areaId));
    };
    auto playSceneIntroCard = [&]() {
        if (!introCardTex) return;
        auto bgIt = introCardFrames.find("background");
        auto leftIt = introCardFrames.find("leftbar");
        auto topIt = introCardFrames.find("topsecion");
        if (bgIt == introCardFrames.end() || leftIt == introCardFrames.end() || topIt == introCardFrames.end()) return;

        const Frame& bgFrame = bgIt->second;
        const Frame& leftFrame = leftIt->second;
        const Frame& topFrame = topIt->second;
        const Uint64 introStartNs = SDL_GetTicksNS();
        const double introDurationSec = 0.78;
        auto lerp = [](double a, double b, double t) {
            return a + (b - a) * t;
        };
        auto stage = [&](double t, double start, double span) { return std::clamp((t - start) / span, 0.0, 1.0); };

        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_EVENT_QUIT) {
                    running = false;
                    return;
                }
                if (ev.type == SDL_EVENT_GAMEPAD_ADDED || ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    input.handleEvent(ev);
                    InputSystem::DetectionEvent det;
                    while (input.pollDetectionEvent(det)) {
                        const char* typeStr = (det.type == InputSystem::DetectionEvent::Type::Connected) ? "connected" : "disconnected";
                        SDL_Log("controller %s: id=%d name=\"%s\" connected=%d", typeStr, (int)det.id, det.name.c_str(), det.connectedCount);
                    }
                }
            }

            const double elapsedSec = (double)(SDL_GetTicksNS() - introStartNs) / 1000000000.0;
            const double t = std::clamp(elapsedSec / introDurationSec, 0.0, 1.0);
            const double leftIn = stage(t, 0.00, 0.60);
            const double topIn = stage(t, 0.08, 0.60);
            const double bgIn = stage(t, 0.16, 0.68);
            const int fadeAlpha = (int)std::lround(255.0 * (1.0 - stage(t, 0.0, 0.42)));

            SDL_SetRenderTarget(ren, gameTarget);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);

            auto frameW = [](const Frame& f) { return f.rotated ? f.rect.h : f.rect.w; };
            auto frameH = [](const Frame& f) { return f.rotated ? f.rect.w : f.rect.h; };
            const int bgSrcW = std::max(1, frameW(bgFrame));
            const int bgSrcH = std::max(1, frameH(bgFrame));
            const int leftSrcW = std::max(1, frameW(leftFrame));
            const int leftSrcH = std::max(1, frameH(leftFrame));
            const int topSrcW = std::max(1, frameW(topFrame));
            const int topSrcH = std::max(1, frameH(topFrame));

            const int bgH = kBaseScreenH;
            const int bgW = (int)std::lround((double)bgSrcW * ((double)bgH / (double)bgSrcH));
            const int leftH = kBaseScreenH;
            const int leftW = (int)std::lround((double)leftSrcW * ((double)leftH / (double)leftSrcH));
            const int topW = kBaseScreenW;
            const int topH = (int)std::lround((double)topSrcH * ((double)topW / (double)topSrcW));
            const int bgTargetX = std::max(0, kBaseScreenW - bgW);

            SDL_Rect bgDst{
                (int)std::lround(lerp((double)kBaseScreenW, (double)bgTargetX, bgIn)),
                0,
                bgW,
                bgH
            };
            SDL_Rect leftDst{
                (int)std::lround(lerp(-(double)leftW, 0.0, leftIn)),
                0,
                leftW,
                leftH
            };
            SDL_Rect topDst{
                0,
                (int)std::lround(lerp(-(double)topH, 0.0, topIn)),
                topW,
                topH
            };

            renderFrame(ren, introCardTex, bgFrame, bgDst);
            renderFrame(ren, introCardTex, leftFrame, leftDst);
            renderFrame(ren, introCardTex, topFrame, topDst);
            if (t > 0.30) {
                const int worldW = MeasureTextWidth(introWorldNameStyle.scale, currentFancyLevelName);
                const int worldX = introWorldNameStyle.x - worldW / 2;
                const int areaW = MeasureTextWidth(introAreaIdStyle.scale, currentAreaIdText);
                const int areaX = introAreaIdStyle.x - areaW / 2;
                DrawText(ren, worldX, introWorldNameStyle.y, introWorldNameStyle.scale, currentFancyLevelName);
                DrawTextColored(ren, areaX, introAreaIdStyle.y, introAreaIdStyle.scale, currentAreaIdText, SDL_Color{50, 210, 70, 255});
            }
            if (fadeAlpha > 0) {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)std::clamp(fadeAlpha, 0, 255));
                SDL_FRect fadeRect{0.0f, 0.0f, (float)kBaseScreenW, (float)kBaseScreenH};
                SDL_RenderFillRect(ren, &fadeRect);
            }

            SDL_SetRenderTarget(ren, nullptr);
            int ww = 0, wh = 0;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_Rect presentRect = computePresentRect(ww, wh, kBaseScreenW, kBaseScreenH, 1.0f);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, gameTarget, nullptr, &presentRect);
            SDL_RenderPresent(ren);

            if (t >= 1.0) break;
            SDL_Delay(1);
        }
    };
    bool reopenUserLevelMenu = false;
    FrontendMenuContext frontendCtx{};
    frontendCtx.win = win;
    frontendCtx.ren = ren;
    frontendCtx.gameTarget = gameTarget;
    frontendCtx.gameTargetRef = &gameTarget;
    frontendCtx.baseScreenW = kBaseScreenW;
    frontendCtx.baseScreenH = kBaseScreenH;
    frontendCtx.buildUuid = buildUuid;
    frontendCtx.versionString = appVersion;
    frontendCtx.running = &running;
    frontendCtx.fullscreen = &fullscreen;
    frontendCtx.vsyncEnabled = &vsyncEnabled;
    frontendCtx.clampCamX = &clampCamX;
    frontendCtx.defaultShowFpsCounter = &defaultShowFpsCounter;
    frontendCtx.defaultShowDetailedDebugger = &defaultShowDetailedDebugger;
    frontendCtx.defaultShowHitboxes = &defaultShowHitboxes;
    frontendCtx.defaultShowPlayerHitbox = &defaultShowPlayerHitbox;
    frontendCtx.defaultShowDebugView = &defaultShowDebugView;
    frontendCtx.defaultHideUnknownObjectTypes = &defaultHideUnknownObjectTypes;
    frontendCtx.powerManagementEnabled = &powerManagementEnabled;
    frontendCtx.menuMusicEnabled = &menuMusicEnabled;
    frontendCtx.muteAllAudio = &muteAllAudio;
    frontendCtx.keyMoveLeft = &keybinds.moveLeft;
    frontendCtx.keyMoveRight = &keybinds.moveRight;
    frontendCtx.keyMoveDown = &keybinds.moveDown;
    frontendCtx.keyJump = &keybinds.jump;
    frontendCtx.keyPause = &keybinds.pause;
    frontendCtx.musicVolume = &musicVolume;
    frontendCtx.sfxVolume = &sfxVolume;
    frontendCtx.uiScalePercent = &uiScalePercent;
    frontendCtx.extraSettings = extraSettings.data();
    frontendCtx.extraSettingsCount = (int)extraSettings.size();
    std::string frontendSelectedLevelPath;
    frontendCtx.selectedLevelPath = &frontendSelectedLevelPath;
    frontendCtx.applyAudioVolumes = applyAudioVolumes;
    frontendCtx.applyMenuMusicToggle = applyMenuMusicToggle;
    frontendCtx.saveClientSettings = saveClientSettings;
    auto applyDynamicResolutionFromWindow = [&](bool force) -> bool {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        if (winW <= 0 || winH <= 0) return true;

        float aspect = (float)winW / (float)winH;
        aspect = std::clamp(aspect, 4.0f / 3.0f, 32.0f / 9.0f);
        const int newBaseH = 1080;
        int newBaseW = std::max(1280, (int)std::lround((float)newBaseH * aspect));
        if ((newBaseW & 1) != 0) ++newBaseW;
        const int newGameplayW = std::max(1, (int)std::lround((float)newBaseW / kGameplayZoom));
        const int newGameplayH = std::max(1, (int)std::lround((float)newBaseH / kGameplayZoom));

        if (!force &&
            newBaseW == kBaseScreenW && newBaseH == kBaseScreenH &&
            newGameplayW == kGameplayViewW && newGameplayH == kGameplayViewH) {
            return true;
        }

        SDL_Texture* newGameTarget = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, newBaseW, newBaseH);
        SDL_Texture* newWorldTarget = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, newGameplayW, newGameplayH);
        if (!newGameTarget || !newWorldTarget) {
            if (newWorldTarget) SDL_DestroyTexture(newWorldTarget);
            if (newGameTarget) SDL_DestroyTexture(newGameTarget);
            SDL_Log("Dynamic resolution update failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureScaleMode(newWorldTarget, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureScaleMode(newGameTarget, SDL_SCALEMODE_NEAREST);
        SDL_DestroyTexture(worldTarget);
        SDL_DestroyTexture(gameTarget);
        worldTarget = newWorldTarget;
        gameTarget = newGameTarget;
        kBaseScreenW = newBaseW;
        kBaseScreenH = newBaseH;
        kGameplayViewW = newGameplayW;
        kGameplayViewH = newGameplayH;
        frontendCtx.gameTarget = gameTarget;
        frontendCtx.baseScreenW = kBaseScreenW;
        frontendCtx.baseScreenH = kBaseScreenH;
        SDL_Log("Dynamic resolution updated: base=%dx%d gameplay=%dx%d window=%dx%d",
            kBaseScreenW, kBaseScreenH, kGameplayViewW, kGameplayViewH, winW, winH);
        return true;
    };
    frontendCtx.updateDynamicResolution = [&]() { (void)applyDynamicResolutionFromWindow(false); };
    applyDynamicResolutionFromWindow(true);
    // Enforce persisted startup audio state immediately.
    applyMenuMusicToggle();
    while (running) {
        recoverAudioIfNeeded(false);
        std::string selectedLevelPath;
        bool selectedFromUserMenu = false;
        if (reopenUserLevelMenu) {
            selectedLevelPath = RunLevelSelect(win, ren);
            selectedFromUserMenu = true;
            reopenUserLevelMenu = false;
        } else {
            frontendSelectedLevelPath.clear();
            FrontendAction action = runFrontendMenu(frontendCtx);
            saveClientSettings();
            if (!running || action == FrontendAction::Quit) break;

            selectedLevelPath = frontendSelectedLevelPath;
            if (!selectedLevelPath.empty()) {
                selectedFromUserMenu = true;
            } else {
                selectedLevelPath = RunLevelSelect(win, ren);
            }
        }
        if (selectedLevelPath.empty()) {
            continue;
        }
        const bool allowNextLevelProgression = !selectedFromUserMenu;
        audio.haltMusic();
        levelManager.setLevelPath(selectedLevelPath);

        TileMap map;
        std::vector<ObjectInstance> objects;
        LevelMeta meta;
        Player player;
        int livesCount = 5;
        int scoreCount = 0;
        bool levelCompleteActive = false;
        bool levelCompleteCounting = false;
        std::string levelCompleteNextPath;
        int levelCompleteAudioChannel = -1;
        int levelCompleteAreaId = 0;
        int levelCompleteCoinBonus = 0;
        int levelCompleteTimeScore = 0;
        int levelCompleteAccountedScore = 0;
        int levelCompleteSnapshotSeconds = 0;
        float levelCompleteUiLerp = 0.0f;
        enum class EndSignPhase {
            Idle,
            SignForward,
            SignBackward,
            PlayerForward,
            PlayerBackward,
            TriggerComplete,
            Done
        };
        struct EndSignRuntimeState {
            bool present = false;
            bool triggered = false;
            int objectIndex = -1;
            float objectX = 0.0f;
            float objectY = 0.0f;
            EndSignPhase phase = EndSignPhase::Idle;
            float frameTimer = 0.0f;
            int signFrame = 9;
            int signLoopCount = 0;
            int playerFrame = 1;
            int playerLoopCount = 0;
            bool lockPlayerRight = false;
        };
        EndSignRuntimeState endSignState;
        struct BossRuntimeState {
            bool active = false;
            int world = 0;
            int sourceWorld = 0;
            float activationCooldown = 0.0f;
            int health = 0;
            int maxHealth = 0;
            float x = 0.0f;
            float y = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            float hurtFlash = 0.0f;
            int phase = 0; // world3: 0 wait_near_start, 1 replay_playback, 2 wait_near_fight, 3 fight
            std::vector<SDL_FPoint> replayPath;
            size_t replayIndex = 0;
            float replayFrameAcc = 0.0f;
            float rainbowTimer = 0.0f; // world4: teleport/invuln flash window
            float secretShotTimer = 0.0f; // level59: fireball cadence
            float secretTouchDamageCooldown = 0.0f; // level59: boss damage debounce on touch
        };
        struct SecretFireball {
            float x = 0.0f;
            float y = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            float timer = 0.0f;
            int phase = 0; // 0 = launch up, 1 = scatter + fall
        };
        struct SecretExplosion {
            float x = 0.0f;
            float y = 0.0f;
            float radius = 36.0f;
            float life = 0.24f;
            bool hitPlayer = false;
            bool hitBoss = false;
        };
        BossRuntimeState bossState;
        std::vector<SecretFireball> secretFireballs;
        std::vector<SecretExplosion> secretExplosions;
        auto resetBossStateForLoadedLevel = [&]() {
            bossState = BossRuntimeState{};
            secretFireballs.clear();
            secretExplosions.clear();
            const int world = levelManager.worldId();
            const int bossProfileWorld = (world == 2 || world == 6) ? 1 : std::max(1, world);
            const int levelId = parseLevelIdFromLevelPath(levelManager.levelPath());
            bossState.active = false;
            bossState.world = bossProfileWorld;
            bossState.sourceWorld = std::max(1, world);
            bossState.activationCooldown = 3.0f;
            bossState.maxHealth = 4;
            bossState.health = 4;

            auto loadBossReplayPathPositions = [&](const std::string& replayFile) -> std::vector<SDL_FPoint> {
                std::vector<SDL_FPoint> out;
                std::vector<std::string> candidates;
                candidates.push_back((replayDirPath / replayFile).string());
                candidates.push_back((replayDirPath / std::filesystem::path(replayFile).filename()).string());
                candidates.push_back(replayFile);
                for (const auto& p : candidates) {
                    const std::string text = ReadTextFile(p);
                    if (text.empty()) continue;
                    std::istringstream in(text);
                    std::string line;
                    while (std::getline(in, line)) {
                        if (line.empty()) continue;
                        nlohmann::json j;
                        try { j = nlohmann::json::parse(line); } catch (...) { continue; }
                        if (!j.is_object()) continue;
                        if (j.value("type", std::string()) != "frame") continue;
                        if (!j.contains("player") || !j["player"].is_object()) continue;
                        const auto& pstate = j["player"];
                        SDL_FPoint pt{};
                        pt.x = pstate.value("x", 0.0f) + pstate.value("w", 32) * 0.5f;
                        pt.y = pstate.value("y", 0.0f) + pstate.value("h", 32) * 0.5f;
                        out.push_back(pt);
                    }
                    if (!out.empty()) {
                        SDL_Log("boss.w3: loaded replay positions=%d from %s", (int)out.size(), p.c_str());
                        break;
                    }
                }
                if (out.empty()) {
                    SDL_Log("boss.w3: replay load failed for %s", replayFile.c_str());
                }
                return out;
            };

            const float mapW = (float)(map.w * map.tileSize);
            const float mapH = (float)(map.h * map.tileSize);
            if (levelId == 59) {
                bossState.active = true;
                bossState.activationCooldown = 0.0f;
                bossState.maxHealth = 32;
                bossState.health = 32;
                bossState.x = mapW * 0.5f;
                bossState.y = mapH * 0.5f;
                bossState.vx = 0.0f;
                bossState.vy = 0.0f;
                bossState.secretShotTimer = 0.60f;
            }
            if (bossProfileWorld == 1) {
                bossState.x = std::clamp(mapW * 0.72f, 96.0f, std::max(96.0f, mapW - 96.0f));
                bossState.y = std::clamp(mapH * 0.38f, 96.0f, std::max(96.0f, mapH - 96.0f));
                bossState.vx = 280.0f;
                bossState.vy = 220.0f;
            } else if (bossProfileWorld == 2) {
                bossState.x = std::clamp(mapW * 0.60f, 96.0f, std::max(96.0f, mapW - 96.0f));
                bossState.y = std::clamp(mapH * 0.42f, 96.0f, std::max(96.0f, mapH - 96.0f));
                bossState.vx = -260.0f;
                bossState.vy = 240.0f;
            } else {
                const float phase = (float)(bossProfileWorld % 6) / 6.0f;
                bossState.x = std::clamp(mapW * (0.32f + phase * 0.36f), 96.0f, std::max(96.0f, mapW - 96.0f));
                bossState.y = std::clamp(mapH * (0.30f + (1.0f - phase) * 0.28f), 96.0f, std::max(96.0f, mapH - 96.0f));
                bossState.vx = ((bossProfileWorld % 2) == 0) ? -250.0f : 250.0f;
                bossState.vy = 220.0f + (float)((bossProfileWorld % 3) * 20);
            }
            if (bossState.sourceWorld == 7) {
                const float world7MinX = 96.0f + 28.0f;
                const float world7MaxX = 96.0f + (float)kGameplayViewW - 28.0f;
                const float minX = std::min(world7MinX, world7MaxX);
                const float maxX = std::max(world7MinX, world7MaxX);
                const float rx = (float)std::rand() / (float)RAND_MAX;
                bossState.x = minX + (maxX - minX) * rx;
                bossState.y = 32.0f + (float)kGameplayViewH + 28.0f;
                bossState.vx = 0.0f;
                bossState.vy = -320.0f;
            }
            if (levelId == 59) {
                bossState.x = mapW * 0.5f;
                bossState.y = mapH * 0.5f;
                bossState.vx = 0.0f;
                bossState.vy = 0.0f;
            }
            if (bossState.sourceWorld == 3) {
                bossState.maxHealth = 8;
                bossState.health = 8;
                bossState.phase = 0;
                bossState.replayPath = loadBossReplayPathPositions("assets/boss3.replay");
                bossState.replayIndex = 0;
                bossState.replayFrameAcc = 0.0f;
                if (!bossState.replayPath.empty()) {
                    bossState.x = bossState.replayPath[0].x;
                    bossState.y = bossState.replayPath[0].y;
                }
                bossState.vx = 280.0f;
                bossState.vy = 220.0f;
                bossState.active = true;
            }
            if (bossState.sourceWorld == 4) {
                bossState.rainbowTimer = 0.0f;
            }
        };
        struct DemoRuntimeState {
            bool enabled = false;
            float dir = 1.0f;
            float jumpCooldown = 0.0f;
            float jumpHoldTimer = 0.0f;
            float stuckTime = 0.0f;
            float lastX = 0.0f;
            float repathTimer = 0.0f;
            size_t waypointIndex = 0;
            std::vector<SDL_Point> pathTiles;
            bool startTileSet = false;
            SDL_Point startTile{0, 0};
        };
        DemoRuntimeState demoState;
        float levelReloadTitleTimer = 0.0f;
        float levelTimerSeconds = 0.0f;
        float playerInvincibleTimer = 0.0f;
        constexpr float kPlayerInvincibleDuration = 5.00f;
        constexpr float kPlayerSpawnInvincibleDuration = 1.25f;
        float levelLoadDeathGraceTimer = 0.0f;
        constexpr float kLevelLoadDeathGraceDuration = 1.0f;
        constexpr int kPlayerSpawnLockFrames = 6;
        int playerSpawnLockFrames = 0;
        float playerSpawnLockX = 0.0f;
        float playerSpawnLockY = 0.0f;
        float cameraSmoothingSuppressTimer = 0.0f;
        bool levelCompleteCameraLocked = false;
        float levelCompleteCameraX = 0.0f;
        float levelCompleteCameraY = 0.0f;
        bool endSignCameraLocked = false;
        float endSignCameraX = 0.0f;
        enum FastTravelDir {
            FT_UP = 0,
            FT_DOWN = 1,
            FT_LEFT = 2,
            FT_RIGHT = 3,
            FT_EXIT = 4
        };
        int fastTravelActiveDir = -1;
        bool fastTravelOverlapWasActive = false;
        float fastTravelBlendVx = 0.0f;
        float fastTravelBlendVy = 0.0f;
        auto logFastTravelFlags = [&](const char* reason) {
            SDL_Log("fastTravel (%s): activeDir=%d", reason, fastTravelActiveDir);
        };
        auto fastTravelDirForObjectId = [&](int objId) -> int {
            if (objId == 57) return FT_UP;
            if (objId == 58) return FT_DOWN;
            if (objId == 59) return FT_LEFT;
            if (objId == 60) return FT_RIGHT;
            if (objId == 61) return FT_EXIT;
            return -1;
        };
        auto setFastTravelActiveDir = [&](int dir, const char* reason) {
            if (dir == fastTravelActiveDir) return;
            fastTravelActiveDir = dir;
            if (!(reason && std::strcmp(reason, "noop") == 0)) {
                logFastTravelFlags(reason);
            }
        };
        float timeTravelTriggerCooldown = 0.0f;
        int currentLevelId = parseLevelIdFromLevelPath(levelManager.levelPath());
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
        auto updatePlayerAnimState = [&](float moveInput, bool downHeld, float stepDt) {
            int newAnim = ANIM_IDLE;
            const float vxAbs = std::abs(player.vx);
            if (player.freeMove) {
                newAnim = ANIM_IDLE;
            } else if (!player.onGround) {
                newAnim = (player.vy < 0.0f) ? ANIM_JUMP : ANIM_FALL;
            } else if (downHeld) {
                newAnim = ANIM_CROUCH;
            } else if (vxAbs > 8.0f) {
                const bool opposing = (player.vx > 20.0f && moveInput < -0.1f) || (player.vx < -20.0f && moveInput > 0.1f);
                newAnim = opposing ? ANIM_SKID : ANIM_WALK;
            }
            if (moveInput < -0.1f) player.facing = -1;
            if (moveInput > 0.1f) player.facing = 1;
            if (newAnim != player.anim) {
                player.anim = newAnim;
                player.animTime = 0.0f;
            } else {
                const float animStepDt = std::clamp(stepDt, 0.0f, 0.05f);
                player.animTime += animStepDt;
            }
        };

        auto reloadLevel = [&]() {
            levelManager.reloadLevel(map, objects, meta, player);
            levelTimerSeconds = 0.0f;
            levelCompleteActive = false;
            levelCompleteCounting = false;
            levelCompleteNextPath.clear();
            levelCompleteAudioChannel = -1;
            levelCompleteAreaId = 0;
            levelCompleteCoinBonus = 0;
            levelCompleteTimeScore = 0;
            levelCompleteAccountedScore = 0;
            levelCompleteSnapshotSeconds = 0;
            levelCompleteUiLerp = 0.0f;
            levelCompleteCameraLocked = false;
            levelCompleteCameraX = 0.0f;
            levelCompleteCameraY = 0.0f;
            endSignCameraLocked = false;
            endSignCameraX = 0.0f;
            endSignState = EndSignRuntimeState{};
            float nearestAheadDist = 1e30f;
            int fallbackSignIndex = -1;
            for (int i = 0; i < (int)objects.size(); ++i) {
                if (objects[i].id != "67") continue;
                if (fallbackSignIndex < 0) fallbackSignIndex = i;
                const float dx = objects[i].x - player.x;
                if (dx >= 0.0f && dx < nearestAheadDist) {
                    nearestAheadDist = dx;
                    endSignState.objectIndex = i;
                }
            }
            if (endSignState.objectIndex < 0) endSignState.objectIndex = fallbackSignIndex;
            if (endSignState.objectIndex >= 0 && endSignState.objectIndex < (int)objects.size()) {
                endSignState.present = true;
                endSignState.objectX = objects[endSignState.objectIndex].x;
                endSignState.objectY = objects[endSignState.objectIndex].y;
            }
            resetBossStateForLoadedLevel();
            demoState.jumpCooldown = 0.0f;
            demoState.jumpHoldTimer = 0.0f;
            demoState.stuckTime = 0.0f;
            demoState.lastX = player.x;
            demoState.repathTimer = 0.0f;
            demoState.waypointIndex = 0;
            demoState.pathTiles.clear();
            demoState.startTileSet = false;
            demoState.startTile = SDL_Point{0, 0};
            cameraSmoothingSuppressTimer = 0.20f;
            setFastTravelActiveDir(-1, "reload");
            fastTravelOverlapWasActive = false;
            fastTravelBlendVx = 0.0f;
            fastTravelBlendVy = 0.0f;
            timeTravelTriggerCooldown = 0.35f;

            audio.loadLevelMusic(levelManager.musicPath());
            currentFancyLevelName = buildFancyLevelName();
            currentAreaIdText = buildAreaIdText();
            levelReloadTitleTimer = 2.0f;
            currentLevelId = parseLevelIdFromLevelPath(levelManager.levelPath());
            playerInvincibleTimer = std::max(playerInvincibleTimer, kPlayerSpawnInvincibleDuration);
            levelLoadDeathGraceTimer = kLevelLoadDeathGraceDuration;
            playerSpawnLockX = player.x;
            playerSpawnLockY = player.y;
            playerSpawnLockFrames = kPlayerSpawnLockFrames;
            playSceneIntroCard();
        };
        auto startLevelCompleteSequence = [&]() {
            if (levelCompleteActive) return;
            levelCompleteActive = true;
            levelCompleteCounting = false;
            levelCompleteNextPath = allowNextLevelProgression ? levelManager.nextLevelPath() : "";
            levelCompleteAreaId = levelManager.levelPartId();
            levelCompleteSnapshotSeconds = (int)levelTimerSeconds;
            levelCompleteCoinBonus = levelManager.coinCount() * 10;
            const int elapsedMinutes = (int)std::floor(levelTimerSeconds / 60.0f);
            if (elapsedMinutes > 3) {
                levelCompleteTimeScore = 0;
            } else {
                int denom = 1;
                for (int i = 0; i < elapsedMinutes; ++i) denom *= 5;
                levelCompleteTimeScore = 10000 / std::max(1, denom);
            }
            levelCompleteAccountedScore = 0;
            if (audio.isReady()) {
                audio.haltMusic();
                levelCompleteAudioChannel = audio.playVictorySfx();
                if (levelCompleteAudioChannel < 0) {
                    levelCompleteCounting = true;
                }
            } else {
                levelCompleteCounting = true;
            }
        };

        reloadLevel();
        if (!running) break;

        bool levelRunning = true;
        bool paused = false;
        bool deathSequenceActive = false;
        bool deathLifeDeducted = false;
        float deathTimer = 0.0f;
        struct DroppedCoin {
            float x = 0.0f;
            float y = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            float life = 12.0f;
            float noPickupTimer = 0.0f;
            int value = 1;
        };
        std::vector<DroppedCoin> droppedCoins;
        float fastTravelCooldown = 0.0f;
        bool showHitboxes = defaultShowHitboxes;
        bool showPlayerHitbox = defaultShowPlayerHitbox;
        bool showDebugView = defaultShowDebugView;
        bool showDemoPath = false;
        bool hideUnknownObjectTypes = defaultHideUnknownObjectTypes;
        bool showFpsCounter = defaultShowFpsCounter;
        showDetailedDebugger = defaultShowDetailedDebugger;
        if (showDetailedDebugger && !debugWin) {
#if defined(__ANDROID__)
            debugWin = win;
            debugRen = ren;
#else
            debugWin = SDL_CreateWindow("Detailed Debugger",
                                        560, 760,
                                        SDL_WINDOW_RESIZABLE);
            if (debugWin) {
                debugRen = SDL_CreateRenderer(debugWin, nullptr);
                if (!debugRen) {
                    SDL_DestroyWindow(debugWin);
                    debugWin = nullptr;
                    showDetailedDebugger = false;
                }
            } else {
                showDetailedDebugger = false;
            }
#endif
        } else if (debugWin) {
#if !defined(__ANDROID__)
            if (showDetailedDebugger) SDL_ShowWindow(debugWin);
            else SDL_HideWindow(debugWin);
#endif
        }
        bool verticalWrapActive = false;
        float camXClampBlend = 1.0f; // 1 = fully clamped, 0 = free-follow.
        float camYClampBlend = 1.0f; // 1 = fully clamped, 0 = free-follow.
        int detailedDebugSubmenu = 0; // 0 Overview, 1 Objects, 2 Performance, 3 Player Status
        int detailedDebugObjectIndex = 0;
        std::vector<SDL_FRect> solidHitboxes;
        std::vector<SDL_FRect> semiHitboxes;
        std::vector<SDL_FRect> waterHitboxes;
        std::vector<SDL_FRect> airDebugHitboxes;
        std::unordered_set<int> activeBumperIndices;
        int pauseSelection = 0; // 0 = Resume, 1 = Restart, 2 = Quit
        bool returnToSelect = false;
        std::unordered_map<SDL_FingerID, SDL_FPoint> activeTouches;
        struct ReplayInputSnapshot {
            float touchMove = 0.0f;
            bool touchDown = false;
            bool touchJump = false;
            float gamepadMove = 0.0f;
            bool gamepadDown = false;
            bool gamepadJump = false;
            bool gamepadFreeMove = false;
            float inputMove = 0.0f;
            bool inputDown = false;
            bool forceRightMovement = false;
            bool fastTravelEnabled = false;
            bool demoEnabled = false;
        };
        ReplayInputSnapshot replayInput{};
        struct ReplayRecorder {
            bool enabled = false;
            uint64_t frameIndex = 0;
            Uint64 startTicksNs = 0;
            std::string path;
            std::ofstream out;
        };
        struct ReplayFrameSample {
            float x = 0.0f;
            float y = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            int w = 0;
            int h = 0;
            bool onGround = false;
            bool inWater = false;
            int facing = 1;
            bool freeMove = false;
            int anim = 0;
            float animTime = 0.0f;
            bool jumpHeld = false;
            float jumpHoldTime = 0.0f;
            bool jumpWasDown = false;
            float jumpBufferTime = 0.0f;
            float inputMove = 0.0f;
            bool inputDown = false;
        };
        struct ReplayPlaybackState {
            bool active = false;
            std::string sourcePath;
            std::vector<ReplayFrameSample> frames;
            size_t nextFrame = 0;
        };
        ReplayRecorder replayRecorder{};
        ReplayPlaybackState replayPlayback{};
        auto stopReplayRecording = [&](const char* reason) {
            if (!replayRecorder.enabled || !replayRecorder.out.is_open()) return;
            nlohmann::json end;
            end["type"] = "end";
            end["reason"] = reason ? reason : "stop";
            end["frames"] = replayRecorder.frameIndex;
            end["level_timer"] = levelTimerSeconds;
            replayRecorder.out << end.dump() << "\n";
            replayRecorder.out.flush();
            replayRecorder.out.close();
            replayRecorder.enabled = false;
        };
        auto startReplayRecording = [&]() {
            stopReplayRecording("restart");
            replayRecorder = ReplayRecorder{};
            try {
                std::error_code ec;
                std::filesystem::create_directories(replayDirPath, ec);
                const Uint64 stamp = SDL_GetTicksNS();
                replayRecorder.path = (replayDirPath / ("replay-" + std::to_string((unsigned long long)stamp) + ".jsonl")).string();
                replayRecorder.out.open(replayRecorder.path, std::ios::binary | std::ios::trunc);
                if (replayRecorder.out.is_open()) {
                    replayRecorder.enabled = true;
                    replayRecorder.startTicksNs = stamp;
                    nlohmann::json meta;
                    meta["type"] = "meta";
                    meta["build_uuid"] = buildUuid;
                    meta["version"] = appVersion;
                    meta["level_path"] = levelManager.levelPath();
                    meta["world"] = levelManager.worldId();
                    meta["area"] = levelManager.levelPartId();
                    meta["time_id"] = levelManager.timeId();
                    meta["tile_size"] = map.tileSize;
                    meta["map_w"] = map.w;
                    meta["map_h"] = map.h;
                    meta["start_ticks_ns"] = (uint64_t)replayRecorder.startTicksNs;
                    replayRecorder.out << meta.dump() << "\n";
                    replayRecorder.out.flush();
                    std::ofstream latest((replayDirPath / "latest_replay.txt").string(), std::ios::binary | std::ios::trunc);
                    if (latest.is_open()) latest << replayRecorder.path << "\n";
                    SDL_Log("Replay recording: %s", replayRecorder.path.c_str());
                }
            } catch (...) {
                replayRecorder.enabled = false;
            }
        };
        auto stopReplayPlayback = [&]() {
            replayPlayback.active = false;
            replayPlayback.nextFrame = 0;
            replayPlayback.frames.clear();
            replayPlayback.sourcePath.clear();
        };
        auto loadReplayForPlayback = [&](const std::string& path, std::string& outLevelPath) -> bool {
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open()) return false;
            std::vector<ReplayFrameSample> loaded;
            loaded.reserve(8192);
            outLevelPath.clear();
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                nlohmann::json j;
                try { j = nlohmann::json::parse(line); } catch (...) { continue; }
                if (!j.is_object()) continue;
                const std::string type = j.value("type", std::string());
                if (type == "meta") {
                    if (j.contains("level_path") && j["level_path"].is_string()) {
                        outLevelPath = j["level_path"].get<std::string>();
                    }
                    continue;
                }
                if (type != "frame") continue;
                if (!j.contains("player") || !j["player"].is_object()) continue;
                const auto& p = j["player"];
                ReplayFrameSample s{};
                s.x = p.value("x", 0.0f);
                s.y = p.value("y", 0.0f);
                s.vx = p.value("vx", 0.0f);
                s.vy = p.value("vy", 0.0f);
                s.w = p.value("w", 32);
                s.h = p.value("h", 32);
                s.onGround = p.value("on_ground", false);
                s.inWater = p.value("in_water", false);
                s.facing = p.value("facing", 1);
                s.freeMove = p.value("free_move", false);
                s.anim = p.value("anim", 0);
                s.animTime = p.value("anim_time", 0.0f);
                s.jumpHeld = p.value("jump_held", false);
                s.jumpHoldTime = p.value("jump_hold_time", 0.0f);
                s.jumpWasDown = p.value("jump_was_down", false);
                s.jumpBufferTime = p.value("jump_buffer_time", 0.0f);
                if (j.contains("input_map") && j["input_map"].is_object()) {
                    const auto& im = j["input_map"];
                    s.inputMove = im.value("input_move", 0.0f);
                    s.inputDown = im.value("input_down", false);
                }
                loaded.push_back(s);
            }
            if (loaded.empty()) return false;
            replayPlayback.frames = std::move(loaded);
            replayPlayback.nextFrame = 0;
            replayPlayback.sourcePath = path;
            replayPlayback.active = true;
            return true;
        };
        // Replay recording is opt-in; keep disabled until user toggles it.
        SDL_Event e;
        Uint32 lastTicks = SDL_GetTicks();
        Uint32 lastPresentTicks = lastTicks;
        Uint32 nextPresentTicks = lastTicks;
        bool mainWindowFocused = true;
        bool mainWindowMinimized = false;
        constexpr int kFpsDisplayMax = 99999999;
        float updateFpsSmoothed = 0.0f;
        float renderFpsSmoothed = 0.0f;
        int updateFpsDisplay = 0;
        int renderFpsDisplay = 0;

        SDL_Rect pauseBtnContinue{0,0,0,0};
        SDL_Rect pauseBtnRestart{0,0,0,0};
        SDL_Rect pauseBtnExit{0,0,0,0};

        auto handlePauseSelect = [&](int sel) {
            pauseSelection = sel;
            if (pauseSelection == 0) {
                paused = false;
            } else if (pauseSelection == 1) {
                droppedCoins.clear();
                reloadLevel();
                paused = false;
            } else {
                returnToSelect = true;
                levelRunning = false;
            }
        };
        auto toggleDetailedDebugger = [&]() {
            showDetailedDebugger = !showDetailedDebugger;
            if (showDetailedDebugger && !debugWin) {
#if defined(__ANDROID__)
                debugWin = win;
                debugRen = ren;
#else
                debugWin = SDL_CreateWindow("Detailed Debugger",
                                            560, 760,
                                            SDL_WINDOW_RESIZABLE);
                if (debugWin) {
                    debugRen = SDL_CreateRenderer(debugWin, nullptr);
                    if (!debugRen) {
                        SDL_DestroyWindow(debugWin);
                        debugWin = nullptr;
                        showDetailedDebugger = false;
                    }
                } else {
                    showDetailedDebugger = false;
                }
#endif
            } else if (!showDetailedDebugger) {
#if defined(__ANDROID__)
                debugWin = nullptr;
                debugRen = nullptr;
#endif
            }
            if (debugWin) {
#if !defined(__ANDROID__)
                if (showDetailedDebugger) SDL_ShowWindow(debugWin);
                else SDL_HideWindow(debugWin);
#endif
            }
        };
        auto handleDetailedDebuggerTap = [&](int mx, int my) -> bool {
            if (!showDetailedDebugger) return false;
            SDL_Rect tab0{12, 38, 130, 36};
            SDL_Rect tab1{152, 38, 130, 36};
            SDL_Rect tab2{292, 38, 130, 36};
            SDL_Rect tab3{432, 38, 130, 36};
            SDL_Rect closeBtn{500, 8, 52, 24};
            SDL_Point pt{mx, my};
            bool handled = false;
            if (SDL_PointInRect(&pt, &tab0)) { detailedDebugSubmenu = 0; handled = true; }
            else if (SDL_PointInRect(&pt, &tab1)) { detailedDebugSubmenu = 1; handled = true; }
            else if (SDL_PointInRect(&pt, &tab2)) { detailedDebugSubmenu = 2; handled = true; }
            else if (SDL_PointInRect(&pt, &tab3)) { detailedDebugSubmenu = 3; handled = true; }
            else if (SDL_PointInRect(&pt, &closeBtn)) { toggleDetailedDebugger(); handled = true; }
            if (detailedDebugSubmenu == 1) {
                SDL_Rect prevBtn{12, 92, 120, 34};
                SDL_Rect nextBtn{142, 92, 120, 34};
                if (SDL_PointInRect(&pt, &prevBtn)) { detailedDebugObjectIndex--; handled = true; }
                if (SDL_PointInRect(&pt, &nextBtn)) { detailedDebugObjectIndex++; handled = true; }
            }
            return handled;
        };
        auto dropPlayerCoins = [&](float originX, float originY) {
            const int owned = levelManager.coinCount();
            if (owned <= 0) return;
            levelManager.resetCoinCount();
            const int spawnCount = std::min(owned, 120);
            droppedCoins.reserve(droppedCoins.size() + spawnCount);
            int remaining = owned;
            for (int i = 0; i < spawnCount; ++i) {
                DroppedCoin c{};
                const float t = (spawnCount <= 1) ? 0.0f : (i / (float)(spawnCount - 1)) * 2.0f - 1.0f;
                c.x = originX;
                c.y = originY;
                c.vx = t * 280.0f;
                c.vy = -430.0f - 90.0f * std::fabs(t);
                c.life = 12.0f;
                // Short grace period so freshly dropped coins don't instantly re-collect.
                c.noPickupTimer = 0.45f;
                droppedCoins.push_back(c);
                remaining--;
            }
            int idx = 0;
            while (remaining > 0 && !droppedCoins.empty()) {
                droppedCoins[idx % droppedCoins.size()].value++;
                remaining--;
                idx++;
            }
        };
        auto removeTimewarpObjectsAndExit = [&]() {
            objects.erase(std::remove_if(objects.begin(), objects.end(), [](const ObjectInstance& obj) {
                int objId = 0;
                try { objId = std::stoi(obj.id); } catch (...) { return false; }
                return objId >= 57 && objId <= 61;
            }), objects.end());
            setFastTravelActiveDir(-1, "boss_start_disable_timewarp");
            fastTravelOverlapWasActive = false;
            fastTravelBlendVx = 0.0f;
            fastTravelBlendVy = 0.0f;
            fastTravelCooldown = std::max(fastTravelCooldown, 0.30f);
            timeTravelTriggerCooldown = std::max(timeTravelTriggerCooldown, 0.50f);
        };

        while (levelRunning) {
            recoverAudioIfNeeded(true);
            applyDynamicResolutionFromWindow(false);
            Uint32 now = SDL_GetTicks();
            float dt = (now - lastTicks) / 1000.0f;
            lastTicks = now;
            SetTextScaleMultiplier(std::clamp((float)uiScalePercent / 100.0f, 0.5f, 2.0f));
            auto isVerticalWrapEnabledAtX = [&](float x) -> bool {
                if (((currentLevelId == 29 && x > 1250.0f) ||
                     (currentLevelId == 30 && x > 1250.0f) ||
                     currentLevelId == 39 ||
                     currentLevelId == 40 ||
                     currentLevelId == 53 ||
                     currentLevelId == 54)) {
                    return true;
                }
                if ((currentLevelId == 21 || currentLevelId == 22 || currentLevelId == 23 || currentLevelId == 24) &&
                    x > 3211.0f && x < 4559.0f) {
                    return true;
                }
                return false;
            };
            if (levelCompleteActive) {
                paused = false;
            }
            const int updateFpsInstant = std::clamp((dt > 0.0f) ? (int)(1.0f / dt) : 0, 0, kFpsDisplayMax);
            if (updateFpsSmoothed <= 0.0f) updateFpsSmoothed = (float)updateFpsInstant;
            else updateFpsSmoothed += ((float)updateFpsInstant - updateFpsSmoothed) * 0.16f;
            updateFpsDisplay = std::clamp((int)std::lround(updateFpsSmoothed), 0, kFpsDisplayMax);
            bool temp1TouchedThisFrame = false;
            const bool gameplayWrapX = (currentLevelId == 39 || currentLevelId == 40);
            const bool gameplayWrapY = isVerticalWrapEnabledAtX(player.x);
            verticalWrapActive = gameplayWrapY;
            activeBumperIndices.clear();
            SetHorizontalWrapCollision(gameplayWrapX);
            SetVerticalWrapCollision(gameplayWrapY);
            frameMsHistory[frameMsHistoryHead] = dt * 1000.0f;
            {
                long rssKB = -1, vmKB = -1;
                if (readProcessMemoryKB(rssKB, vmKB) && rssKB >= 0) {
                    memRssHistory[frameMsHistoryHead] = (float)rssKB / 1024.0f;
                } else {
                    memRssHistory[frameMsHistoryHead] = 0.0f;
                }
            }
            frameMsHistoryHead = (frameMsHistoryHead + 1) % (int)frameMsHistory.size();
            if (!paused && !deathSequenceActive) {
                levelTimerSeconds += dt;
                levelReloadTitleTimer = std::max(0.0f, levelReloadTitleTimer - dt);
            }
            if (fastTravelCooldown > 0.0f) {
                fastTravelCooldown = std::max(0.0f, fastTravelCooldown - dt);
            }
            if (timeTravelTriggerCooldown > 0.0f) {
                timeTravelTriggerCooldown = std::max(0.0f, timeTravelTriggerCooldown - dt);
            }
            if (cameraSmoothingSuppressTimer > 0.0f) {
                cameraSmoothingSuppressTimer = std::max(0.0f, cameraSmoothingSuppressTimer - dt);
            }
            if (bossState.hurtFlash > 0.0f) {
                bossState.hurtFlash = std::max(0.0f, bossState.hurtFlash - dt);
            }
            if (bossState.rainbowTimer > 0.0f) {
                bossState.rainbowTimer = std::max(0.0f, bossState.rainbowTimer - dt);
            }
            if (bossState.activationCooldown > 0.0f) {
                bossState.activationCooldown = std::max(0.0f, bossState.activationCooldown - dt);
            }
            if (bossState.secretTouchDamageCooldown > 0.0f) {
                bossState.secretTouchDamageCooldown = std::max(0.0f, bossState.secretTouchDamageCooldown - dt);
            }
            if (playerInvincibleTimer > 0.0f) {
                playerInvincibleTimer = std::max(0.0f, playerInvincibleTimer - dt);
            }
            if (levelLoadDeathGraceTimer > 0.0f) {
                levelLoadDeathGraceTimer = std::max(0.0f, levelLoadDeathGraceTimer - dt);
            }
            if (!paused && !deathSequenceActive && !droppedCoins.empty()) {
                auto tileSolidAt = [&](float wx, float wy) -> bool {
                    const int tx = (int)std::floor(wx / map.tileSize);
                    const int ty = (int)std::floor(wy / map.tileSize);
                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return true;
                    const int tidx = ty * map.w + tx;
                    return map.solid[tidx] != 0 || map.semisolid[tidx] != 0;
                };
                const float coinR = 8.0f;
                for (auto& c : droppedCoins) {
                    const float prevX = c.x;
                    const float prevY = c.y;
                    c.life -= dt;
                    c.noPickupTimer = std::max(0.0f, c.noPickupTimer - dt);
                    c.vy += 1900.0f * dt;
                    c.vy = std::min(c.vy, 1300.0f);
                    c.x += c.vx * dt;
                    c.y += c.vy * dt;
                    if (tileSolidAt(c.x - coinR, c.y)) {
                        const int gx = (int)std::floor((c.x - coinR) / map.tileSize);
                        c.x = (gx + 1) * map.tileSize + coinR;
                        c.vx = std::fabs(c.vx) * 0.35f;
                    }
                    if (tileSolidAt(c.x + coinR, c.y)) {
                        const int gx = (int)std::floor((c.x + coinR) / map.tileSize);
                        c.x = gx * map.tileSize - coinR;
                        c.vx = -std::fabs(c.vx) * 0.35f;
                    }
                    if (tileSolidAt(c.x, c.y - coinR)) {
                        const int gy = (int)std::floor((c.y - coinR) / map.tileSize);
                        c.y = (gy + 1) * map.tileSize + coinR;
                        c.vy = std::fabs(c.vy) * 0.25f;
                    }
                    if (tileSolidAt(c.x, c.y + coinR)) {
                        int gy = (int)std::floor((c.y + coinR) / map.tileSize);
                        c.y = gy * map.tileSize - coinR;
                        c.vy *= -0.28f;
                        if (std::fabs(c.vy) < 28.0f) c.vy = 0.0f;
                        c.vx *= 0.78f;
                    }
                    // If a coin is still embedded in geometry (e.g. spawned inside a wall),
                    // roll back to the nearest valid previous axis and dampen velocity.
                    if (tileSolidAt(c.x, c.y)) {
                        if (!tileSolidAt(prevX, c.y)) c.x = prevX;
                        else if (!tileSolidAt(c.x, prevY)) c.y = prevY;
                        else {
                            c.x = prevX;
                            c.y = prevY;
                        }
                        c.vx *= -0.25f;
                        c.vy *= -0.25f;
                    }
                    c.vx *= std::exp(-2.0f * dt);
                }
                const float px1 = player.x;
                const float px2 = player.x + player.w;
                const float py1 = player.y;
                const float py2 = player.y + player.h;
                droppedCoins.erase(std::remove_if(droppedCoins.begin(), droppedCoins.end(), [&](const DroppedCoin& c) {
                    if (c.life <= 0.0f) return true;
                    const bool touched = (c.noPickupTimer <= 0.0f) &&
                                         (c.x + coinR > px1 && c.x - coinR < px2 && c.y + coinR > py1 && c.y - coinR < py2);
                    if (!touched) return false;
                    levelManager.addCoins(std::max(1, c.value));
                    audio.playCoinSfx();
                    return true;
                }), droppedCoins.end());
            }
            // Keep level music looping even if backend loop handling stops unexpectedly.
            audio.ensureLevelMusic(paused, deathSequenceActive, levelCompleteActive);
            {
                const float target = levelCompleteActive ? 1.0f : 0.0f;
                const float speed = 4.5f;
                if (levelCompleteUiLerp < target) {
                    levelCompleteUiLerp = std::min(target, levelCompleteUiLerp + speed * dt);
                } else if (levelCompleteUiLerp > target) {
                    levelCompleteUiLerp = std::max(target, levelCompleteUiLerp - speed * dt);
                }
            }
            if (levelCompleteActive && !levelCompleteCounting) {
                if (!audio.isReady() || levelCompleteAudioChannel < 0 || !audio.isChannelPlaying(levelCompleteAudioChannel)) {
                    levelCompleteCounting = true;
                }
            }
            if (levelCompleteActive && levelCompleteCounting && !paused) {
                // Uncapped payout during level complete: process full remaining bonus immediately.
                int payoutPerFrame = std::max(1, levelCompleteCoinBonus + levelCompleteTimeScore);
                int coinStep = std::min(levelCompleteCoinBonus, payoutPerFrame);
                if (coinStep > 0) {
                    levelCompleteCoinBonus -= coinStep;
                    scoreCount += coinStep;
                    levelCompleteAccountedScore += coinStep;
                    audio.playMessageSfx();
                }
                int timeStep = std::min(levelCompleteTimeScore, payoutPerFrame);
                if (timeStep > 0) {
                    levelCompleteTimeScore -= timeStep;
                    scoreCount += timeStep;
                    levelCompleteAccountedScore += timeStep;
                    audio.playMessageSfx();
                }
                if (levelCompleteCoinBonus <= 0 && levelCompleteTimeScore <= 0) {
                    if (!levelCompleteNextPath.empty()) {
                        levelManager.setLevelPath(levelCompleteNextPath);
                        droppedCoins.clear();
                        reloadLevel();
                        continue;
                    }
                    returnToSelect = true;
                    levelRunning = false;
                    continue;
                }
            }

        float inputMove = 0.0f;
        bool inputDown = false;
        int screenW = kBaseScreenW;
        int screenH = kBaseScreenH;
        float uiSize = std::clamp(std::min((float)screenW, (float)screenH) * 0.16f, 110.0f, 190.0f);
        float uiPad = std::clamp(uiSize * 0.22f, 20.0f, 44.0f);
        float uiGap = std::clamp(uiSize * 0.18f, 12.0f, 34.0f);
        SDL_FRect touchLeftBtn{uiPad, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchRightBtn{uiPad + uiSize + uiGap, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchDownBtn{uiPad + (uiSize + uiGap) * 2.0f, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchJumpBtn{screenW - uiPad - uiSize, screenH - uiPad - uiSize, uiSize, uiSize};
        bool touchLeft = false;
        bool touchRight = false;
        bool touchDown = false;
        bool touchJump = false;

        auto computeTouchButtons = [&]() {
            touchLeft = false;
            touchRight = false;
            touchDown = false;
            touchJump = false;
            if (paused) return;
            int winW = 0, winH = 0;
            SDL_GetWindowSize(win, &winW, &winH);
            auto expandRect = [](const SDL_FRect& r, float pad) {
                return SDL_FRect{r.x - pad, r.y - pad, r.w + pad * 2.0f, r.h + pad * 2.0f};
            };
            float hitPad = std::max(8.0f, uiSize * 0.12f);
            SDL_FRect leftHit = expandRect(touchLeftBtn, hitPad);
            SDL_FRect rightHit = expandRect(touchRightBtn, hitPad);
            SDL_FRect downHit = expandRect(touchDownBtn, hitPad);
            SDL_FRect jumpHit = expandRect(touchJumpBtn, hitPad);
            for (const auto& kv : activeTouches) {
                int wx = (int)std::lround(kv.second.x * winW);
                int wy = (int)std::lround(kv.second.y * winH);
                int gx = 0, gy = 0;
                if (!windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) continue;
                float px = (float)gx;
                float py = (float)gy;
                if (pointInRectF(px, py, leftHit)) touchLeft = true;
                if (pointInRectF(px, py, rightHit)) touchRight = true;
                if (pointInRectF(px, py, downHit)) touchDown = true;
                if (pointInRectF(px, py, jumpHit)) touchJump = true;
            }
        };
        computeTouchButtons();

        const SDL_WindowID mainWindowId = SDL_GetWindowID(win);
        const bool embeddedDetailedDebugger = (showDetailedDebugger && debugWin == win && debugRen == ren);
        while (SDL_PollEvent(&e)) {
            input.handleEvent(e);
            {
                InputSystem::DetectionEvent ev;
                while (input.pollDetectionEvent(ev)) {
                    const char* typeStr = (ev.type == InputSystem::DetectionEvent::Type::Connected) ? "connected" : "disconnected";
                    SDL_Log("controller %s: id=%d name=\"%s\" connected=%d", typeStr, (int)ev.id, ev.name.c_str(), ev.connectedCount);
                }
            }
            if (e.type == SDL_QUIT) { running = false; levelRunning = false; }
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && debugWin &&
                e.window.windowID == SDL_GetWindowID(debugWin)) {
                SDL_HideWindow(debugWin);
                showDetailedDebugger = false;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (embeddedDetailedDebugger && e.button.windowID == mainWindowId) {
                    int winW = 0, winH = 0, gx = 0, gy = 0;
                    SDL_GetWindowSize(win, &winW, &winH);
                    if (windowToGamePoint(e.button.x, e.button.y, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                        if (handleDetailedDebuggerTap(gx, gy)) continue;
                    }
                } else if (debugWin && debugRen &&
                           e.button.windowID == SDL_GetWindowID(debugWin)) {
                    const int mx = e.button.x;
                    const int my = e.button.y;
                    (void)handleDetailedDebuggerTap(mx, my);
                }
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowFocused = false;
                    paused = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowFocused = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_MINIMIZED) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowMinimized = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_RESTORED) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowMinimized = false;
                }
            }
            if ((e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
                e.window.windowID == mainWindowId) {
                applyDynamicResolutionFromWindow(false);
            }
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                bool consumedByDebugger = false;
                if (embeddedDetailedDebugger &&
                    (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0)) {
                    int winW = 0, winH = 0;
                    SDL_GetWindowSize(win, &winW, &winH);
                    int wx = (int)std::lround(e.tfinger.x * winW);
                    int wy = (int)std::lround(e.tfinger.y * winH);
                    int gx = 0, gy = 0;
                    if (windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                        consumedByDebugger = handleDetailedDebuggerTap(gx, gy);
                    }
                } else if (debugWin && e.tfinger.windowID == SDL_GetWindowID(debugWin)) {
                    int dbgW = 0, dbgH = 0;
                    SDL_GetWindowSize(debugWin, &dbgW, &dbgH);
                    const int mx = (int)std::lround(e.tfinger.x * dbgW);
                    const int my = (int)std::lround(e.tfinger.y * dbgH);
                    consumedByDebugger = handleDetailedDebuggerTap(mx, my);
                }
                if (!consumedByDebugger && e.tfinger.windowID == mainWindowId) {
                    // Touch hotspot (top-right) to toggle detailed debugger on mobile.
                    if (e.tfinger.x >= 0.92f && e.tfinger.y <= 0.10f) {
                        toggleDetailedDebugger();
                        consumedByDebugger = true;
                    }
                }
                if (!consumedByDebugger &&
                    (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0)) {
                    activeTouches[e.tfinger.fingerID] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
                }
            }
            if (e.type == SDL_EVENT_FINGER_MOTION) {
                if (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0 ||
                    activeTouches.find(e.tfinger.fingerID) != activeTouches.end()) {
                    activeTouches[e.tfinger.fingerID] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
                }
            }
            if (e.type == SDL_EVENT_FINGER_UP) {
                if (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0 ||
                    activeTouches.find(e.tfinger.fingerID) != activeTouches.end()) {
                    activeTouches.erase(e.tfinger.fingerID);
                }
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED) {
                if (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0 ||
                    activeTouches.find(e.tfinger.fingerID) != activeTouches.end()) {
                    activeTouches.erase(e.tfinger.fingerID);
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.key == SDLK_F11) {
#if !defined(__ANDROID__)
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen);
#endif
                }
                if (e.key.key == SDLK_F12) {
                    showHitboxes = !showHitboxes;
                }
                if (e.key.key == SDLK_F8) {
                    const bool next = !(showHitboxes && showPlayerHitbox && showDebugView);
                    showHitboxes = next;
                    showPlayerHitbox = next;
                    showDebugView = next;
                }
                if (e.key.key == SDLK_F7) {
                    showDebugView = !showDebugView;
                }
                if (e.key.key == SDLK_p) {
                    showDemoPath = !showDemoPath;
                }
                if (e.key.key == SDLK_F6) {
                    showFpsCounter = !showFpsCounter;
                }
                if (e.key.key == SDLK_F10) {
                    clampCamX = !clampCamX;
                }
                if (e.key.key == SDLK_F5) {
                    toggleDetailedDebugger();
                }
                if (e.key.key == SDLK_F4) {
                    detailedDebugSubmenu = (detailedDebugSubmenu + 1) % 4;
                }
                if (showDetailedDebugger && detailedDebugSubmenu == 1) {
                    if (e.key.key == SDLK_UP) detailedDebugObjectIndex--;
                    if (e.key.key == SDLK_DOWN) detailedDebugObjectIndex++;
                }
                if (e.key.key == SDLK_F9) {
                    if (allowNextLevelProgression) {
                        std::string nextPath = levelManager.nextLevelPath();
                        if (!nextPath.empty()) {
                            levelManager.setLevelPath(nextPath);
                            droppedCoins.clear();
                            reloadLevel();
                        }
                    }
                }
                if (e.key.key == SDLK_F3) {
                    demoState.enabled = !demoState.enabled;
                    demoState.jumpCooldown = 0.0f;
                    demoState.jumpHoldTimer = 0.0f;
                    demoState.stuckTime = 0.0f;
                    demoState.lastX = player.x;
                    demoState.repathTimer = 0.0f;
                    demoState.waypointIndex = 0;
                    demoState.pathTiles.clear();
                    demoState.startTileSet = false;
                    SDL_Log("demo autoplay: %s", demoState.enabled ? "enabled" : "disabled");
                }
                if (e.key.key == SDLK_F2) {
                    if (replayRecorder.enabled) {
                        stopReplayRecording("user_toggle_off");
                        SDL_Log("replay recording: disabled");
                    } else {
                        startReplayRecording();
                        SDL_Log("replay recording: enabled (%s)", replayRecorder.path.c_str());
                    }
                }
                if (e.key.key == SDLK_F1) {
                    if (replayPlayback.active) {
                        stopReplayPlayback();
                        SDL_Log("replay playback: disabled");
                    } else {
                        std::string replayPath;
                        {
                            std::ifstream latest((replayDirPath / "latest_replay.txt").string(), std::ios::binary);
                            if (latest.is_open()) std::getline(latest, replayPath);
                        }
                        if (!replayPath.empty()) {
                            std::string replayLevelPath;
                            if (loadReplayForPlayback(replayPath, replayLevelPath)) {
                                stopReplayRecording("playback_started");
                                if (!replayLevelPath.empty() && replayLevelPath != levelManager.levelPath()) {
                                    levelManager.setLevelPath(replayLevelPath);
                                    droppedCoins.clear();
                                    reloadLevel();
                                }
                                SDL_Log("replay playback: enabled (%s, frames=%d)",
                                        replayPath.c_str(), (int)replayPlayback.frames.size());
                            } else {
                                SDL_Log("replay playback: failed to load %s", replayPath.c_str());
                            }
                        } else {
                            SDL_Log("replay playback: latest replay path not found");
                        }
                    }
                }
                if (!levelCompleteActive && (e.key.key == SDLK_AC_BACK || e.key.key == SDL_GetKeyFromScancode(keybinds.pause, SDL_KMOD_NONE, false))) {
                    paused = !paused;
                }
                if (paused) {
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_a) {
                        pauseSelection = std::max(0, pauseSelection - 1);
                    }
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_d) {
                        pauseSelection = std::min(2, pauseSelection + 1);
                    }
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        handlePauseSelect(pauseSelection);
                    }
                }
            }
            if (!levelCompleteActive && InputSystem::isPauseToggleEvent(e)) {
                paused = !paused;
            }
            if (paused && InputSystem::isLeftEvent(e)) {
                pauseSelection = std::max(0, pauseSelection - 1);
            }
            if (paused && InputSystem::isRightEvent(e)) {
                pauseSelection = std::min(2, pauseSelection + 1);
            }
            if (paused && InputSystem::isAcceptEvent(e)) {
                handlePauseSelect(pauseSelection);
            }
            if (paused && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                if (windowToGamePoint(e.button.x, e.button.y, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                    SDL_Point pt{gx, gy};
                    if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                    else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                    else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
                }
            }
            if (paused && e.type == SDL_EVENT_FINGER_DOWN &&
                (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0)) {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                int wx = (int)(e.tfinger.x * winW);
                int wy = (int)(e.tfinger.y * winH);
                int gx = 0, gy = 0;
                if (windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                    SDL_Point pt{gx, gy};
                    if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                    else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                    else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
                }
            }
        }
        computeTouchButtons();

        if (deathSequenceActive && !paused) {
            deathTimer += dt;
            if (!deathLifeDeducted && deathTimer >= 0.12f) {
                livesCount = std::max(0, livesCount - 1);
                deathLifeDeducted = true;
            }
            if (deathLifeDeducted && deathTimer >= 0.90f) {
                deathSequenceActive = false;
                deathTimer = 0.0f;
                if (livesCount > 0) {
                    droppedCoins.clear();
                    reloadLevel();
                    continue;
                } else {
                    returnToSelect = true;
                    levelRunning = false;
                    continue;
                }
            }
        }

            bool replayPlaybackDrivingThisFrame = false;
            if (replayPlayback.active && !paused && !deathSequenceActive && !levelCompleteActive) {
                if (replayPlayback.nextFrame < replayPlayback.frames.size()) {
                    const ReplayFrameSample& s = replayPlayback.frames[replayPlayback.nextFrame++];
                    player.x = s.x;
                    player.y = s.y;
                    player.vx = s.vx;
                    player.vy = s.vy;
                    player.w = s.w;
                    player.h = s.h;
                    player.onGround = s.onGround;
                    player.inWater = s.inWater;
                    player.facing = s.facing;
                    player.freeMove = s.freeMove;
                    player.anim = s.anim;
                    player.animTime = s.animTime;
                    player.jumpHeld = s.jumpHeld;
                    player.jumpHoldTime = s.jumpHoldTime;
                    player.jumpWasDown = s.jumpWasDown;
                    player.jumpBufferTime = s.jumpBufferTime;
                    inputMove = s.inputMove;
                    inputDown = s.inputDown;
                    replayPlaybackDrivingThisFrame = true;
                } else {
                    stopReplayPlayback();
                }
            }

            if (!paused && !deathSequenceActive && !replayPlaybackDrivingThisFrame) {
            const float frameStartX = player.x;
            const float frameStartY = player.y;
            enum MovementReasonMask : unsigned int {
                MR_FAST_TRAVEL = 1u << 0,
                MR_NORMAL_MOVEMENT = 1u << 1,
                MR_WORLD_WRAP = 1u << 2,
                MR_SPRING = 1u << 3,
                MR_BUMPER = 1u << 4,
            };
            unsigned int movementReasons = 0u;
            auto addMovementReason = [&](unsigned int reasonBit) {
                movementReasons |= reasonBit;
            };
            const float mapWrapW = (float)(map.w * map.tileSize);
            const float mapWrapH = (float)(map.h * map.tileSize);
            auto wrapCoordNear = [&](float value, float size, float anchor, float wrapSize, bool wrapEnabled) -> float {
                if (!wrapEnabled || wrapSize <= 0.0f) return value;
                float wrapped = value;
                const float centerOffset = size * 0.5f;
                while ((wrapped + centerOffset - anchor) < -wrapSize * 0.5f) wrapped += wrapSize;
                while ((wrapped + centerOffset - anchor) > wrapSize * 0.5f) wrapped -= wrapSize;
                return wrapped;
            };
            auto overlapPlayerWithWrappedRect = [&](float rx, float ry, float rw, float rh, float& outRx, float& outRy) -> bool {
                const float playerCX = player.x + (float)player.w * 0.5f;
                const float playerCY = player.y + (float)player.h * 0.5f;
                outRx = wrapCoordNear(rx, rw, playerCX, mapWrapW, gameplayWrapX);
                outRy = wrapCoordNear(ry, rh, playerCY, mapWrapH, gameplayWrapY);
                const float px1 = player.x;
                const float px2 = player.x + (float)player.w;
                const float py1 = player.y;
                const float py2 = player.y + (float)player.h;
                return (px2 > outRx) && (px1 < outRx + rw) && (py2 > outRy) && (py1 < outRy + rh);
            };
            bool fastTravelReload = false;
            bool fastTravelTriggered = false;
            auto updateEndSignState = [&]() {
                if (!endSignState.present || levelCompleteActive) return;
                const float kFrameTime = 0.0166667f;
                if (endSignState.phase == EndSignPhase::Idle) return;
                const float animStepDt = std::clamp(dt, 0.0f, 0.05f);
                endSignState.frameTimer += animStepDt;
                while (endSignState.frameTimer >= kFrameTime) {
                    endSignState.frameTimer -= kFrameTime;
                    if (endSignState.phase == EndSignPhase::SignForward) {
                        if (endSignState.signFrame < 15) {
                            endSignState.signFrame++;
                        } else {
                            endSignState.phase = EndSignPhase::SignBackward;
                        }
                    } else if (endSignState.phase == EndSignPhase::SignBackward) {
                        if (endSignState.signFrame > 9) {
                            endSignState.signFrame--;
                        } else {
                            endSignState.signLoopCount++;
                            if (endSignState.signLoopCount >= 3) {
                                endSignState.phase = EndSignPhase::PlayerForward;
                                endSignState.lockPlayerRight = true;
                            } else {
                                endSignState.phase = EndSignPhase::SignForward;
                            }
                        }
                    } else if (endSignState.phase == EndSignPhase::PlayerForward) {
                        if (endSignState.playerFrame < 7) {
                            endSignState.playerFrame++;
                        } else {
                            endSignState.phase = EndSignPhase::PlayerBackward;
                        }
                    } else if (endSignState.phase == EndSignPhase::PlayerBackward) {
                        if (endSignState.playerFrame > 1) {
                            endSignState.playerFrame--;
                        } else {
                            endSignState.playerLoopCount++;
                            if (endSignState.playerLoopCount >= 3) {
                                endSignState.phase = EndSignPhase::TriggerComplete;
                            } else {
                                endSignState.phase = EndSignPhase::PlayerForward;
                            }
                        }
                    } else if (endSignState.phase == EndSignPhase::TriggerComplete) {
                        endSignState.phase = EndSignPhase::Done;
                        startLevelCompleteSequence();
                    }
                }
            };
            updateEndSignState();
            auto ejectFromFastTravel = [&](int dirHint) {
                const bool startedInsideSolid = RectHitsSolid(map, player.x, player.y, player.w, player.h);
                float ex = 0.0f;
                if (dirHint == FT_LEFT) ex = -1.0f;
                else if (dirHint == FT_RIGHT) ex = 1.0f;
                const float horizontalRange = 28.0f;
                const float upwardRange = startedInsideSolid ? 96.0f : 30.0f;
                const float stepSize = 2.0f;
                const float startX = player.x;
                const float startY = player.y;

                auto sweepMoveNoClip = [&](float dx, float dy) {
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const int steps = std::max(1, (int)std::ceil(dist / stepSize));
                    const float sx = dx / (float)steps;
                    const float sy = dy / (float)steps;
                    for (int i = 0; i < steps; ++i) {
                        const float nx = player.x + sx;
                        const float ny = player.y + sy;
                        if (RectHitsSolid(map, nx, ny, player.w, player.h)) break;
                        player.x = nx;
                        player.y = ny;
                    }
                };

                if (startedInsideSolid) {
                    bool foundEscape = false;
                    for (int up = 4; up <= (int)upwardRange && !foundEscape; up += 4) {
                        for (int side = 0; side <= (int)horizontalRange; side += 4) {
                            const float nxA = startX + (ex * side);
                            const float nyA = startY - (float)up;
                            if (!RectHitsSolid(map, nxA, nyA, player.w, player.h)) {
                                player.x = nxA;
                                player.y = nyA;
                                foundEscape = true;
                                break;
                            }
                            if (side == 0) continue;
                            const float nxB = startX - (ex * side);
                            const float nyB = startY - (float)up;
                            if (!RectHitsSolid(map, nxB, nyB, player.w, player.h)) {
                                player.x = nxB;
                                player.y = nyB;
                                foundEscape = true;
                                break;
                            }
                        }
                    }
                    if (!foundEscape) {
                        player.x = startX;
                        player.y = startY;
                    }
                } else {
                    // Keep ejection collision-safe when already in free space.
                    sweepMoveNoClip(ex * horizontalRange, 0.0f);
                    sweepMoveNoClip(0.0f, -upwardRange);
                }

                const float ejectSpeed = 620.0f;
                const float verticalDir = startedInsideSolid ? -1.0f : -0.55f;
                player.vx = ex * ejectSpeed;
                player.vy = verticalDir * ejectSpeed;
                player.onGround = false;
            };
            if (timeTravelTriggerCooldown <= 0.0f) {
                int overlapDir = -1;
                for (const auto& obj : objects) {
                    int objId = 0;
                    try { objId = std::stoi(obj.id); } catch (...) { continue; }
                    if (objId < 57 || objId > 61) continue;
                    if (fastTravelCooldown > 0.0f) break;

                    const float ox = obj.x - 16.0f;
                    const float oy = obj.y - 16.0f;
                    const float ow = 32.0f;
                    const float oh = 32.0f;
                    float testOx = ox;
                    float testOy = oy;
                    const bool overlap = overlapPlayerWithWrappedRect(ox, oy, ow, oh, testOx, testOy);
                    if (!overlap) continue;

                    const int dir = fastTravelDirForObjectId(objId);
                    if (dir < 0) continue;
                    if (dir == FT_EXIT && fastTravelActiveDir < 0) continue;
                    const bool isHighestPriority = (objId == 61);
                    if (isHighestPriority) {
                        temp1TouchedThisFrame = true;
                        overlapDir = dir;
                        break;
                    }
                    if (overlapDir < 0) {
                        overlapDir = dir;
                    }
                }
                if (overlapDir >= 0) {
                    // No delay; velocity blending provides smooth transitions.
                    setFastTravelActiveDir(overlapDir, "set_overlap");
                }
                fastTravelOverlapWasActive = (overlapDir >= 0);
            }
            if (fastTravelActiveDir >= 0) {
                const float eps = 0.01f;
                const float mapW = (float)(map.w * map.tileSize);
                const float mapH = (float)(map.h * map.tileSize);
                const bool insideSolid = RectHitsSolid(map, player.x, player.y, player.w, player.h);
                const float oldX = player.x;
                const float oldY = player.y;
                bool positionChanged = false;
                const float fastTravelSpeed = 900.0f;
                const float fastTravelBlendSpeed = 21.0f;
                float targetVx = 0.0f;
                float targetVy = 0.0f;

                if (fastTravelActiveDir == FT_UP) {
                    targetVy = -fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_DOWN) {
                    targetVy = fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_LEFT) {
                    targetVx = -fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_RIGHT) {
                    targetVx = fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_EXIT && fastTravelCooldown <= 0.0f) {
                    // Keep fast travel active while embedded in solids.
                    if (!insideSolid) {
                        const int previousDir = fastTravelActiveDir;
                        ejectFromFastTravel(previousDir);
                        setFastTravelActiveDir(-1, "exit_mode");
                        fastTravelOverlapWasActive = false;
                        fastTravelBlendVx = 0.0f;
                        fastTravelBlendVy = 0.0f;
                        fastTravelCooldown = 0.2f;
                    }
                }

                // Frame-rate independent smoothing for consistent response.
                const float blendT = 1.0f - std::exp(-fastTravelBlendSpeed * std::max(0.0f, dt));
                fastTravelBlendVx += (targetVx - fastTravelBlendVx) * blendT;
                fastTravelBlendVy += (targetVy - fastTravelBlendVy) * blendT;

                if (fastTravelActiveDir != FT_EXIT) {
                    player.x += fastTravelBlendVx * dt;
                    player.y += fastTravelBlendVy * dt;
                    while (player.y + (float)player.h < 0.0f) player.y += mapH;
                    while (player.y >= mapH) player.y -= mapH;
                    while (player.x + (float)player.w < 0.0f) player.x += mapW;
                    while (player.x >= mapW) player.x -= mapW;
                }

                positionChanged = (std::fabs(player.x - oldX) > eps) || (std::fabs(player.y - oldY) > eps);
                if (positionChanged) addMovementReason(MR_FAST_TRAVEL);
                player.vx = fastTravelBlendVx;
                player.vy = fastTravelBlendVy;
                player.onGround = false;
                if (positionChanged || fastTravelReload) {
                    fastTravelTriggered = true;
                }
                if (positionChanged && showDebugView) {
                    SDL_Log("fastTravel move: dir=%d from=(%.2f, %.2f) to=(%.2f, %.2f) delta=(%.2f, %.2f)",
                            fastTravelActiveDir,
                            oldX, oldY,
                            player.x, player.y,
                            player.x - oldX, player.y - oldY);
                }
            }
            if (temp1TouchedThisFrame && fastTravelActiveDir >= 0 &&
                !RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                const int previousDir = fastTravelActiveDir;
                ejectFromFastTravel(previousDir);
                setFastTravelActiveDir(-1, "temp1_disable");
                fastTravelOverlapWasActive = false;
                fastTravelBlendVx = 0.0f;
                fastTravelBlendVy = 0.0f;
                fastTravelCooldown = std::max(fastTravelCooldown, 0.2f);
            }
            if (fastTravelReload) {
                droppedCoins.clear();
                reloadLevel();
                continue;
            }

            float touchMove = 0.0f;
            if (touchLeft) touchMove -= 1.0f;
            if (touchRight) touchMove += 1.0f;
            const bool demoControlsActive = demoState.enabled && !paused && !deathSequenceActive && !levelCompleteActive;
            bool demoDown = false;
            bool demoJump = false;
            if (demoControlsActive) {
                demoState.jumpCooldown = std::max(0.0f, demoState.jumpCooldown - dt);
                demoState.jumpHoldTimer = std::max(0.0f, demoState.jumpHoldTimer - dt);
                demoState.repathTimer = std::max(0.0f, demoState.repathTimer - dt);
                const float mapWidthPx = (float)(map.w * map.tileSize);
                const float edgePad = (float)map.tileSize * 1.5f;
                const int t = map.tileSize;
                const int playerTilesH = std::max(1, (player.h + t - 1) / t);
                if (player.x <= edgePad) demoState.dir = 1.0f;
                if (player.x + player.w >= mapWidthPx - edgePad) demoState.dir = -1.0f;

                auto passableTile = [&](int tx, int ty) -> bool {
                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return false;
                    return !map.getSolid(tx, ty) && !map.getSemiSolid(tx, ty);
                };
                auto standableTile = [&](int tx, int footTy) -> bool {
                    if (tx < 0 || tx >= map.w) return false;
                    if (footTy + 1 < 0 || footTy + 1 >= map.h) return false;
                    const int headTy = footTy - (playerTilesH - 1);
                    if (headTy < 0) return false;
                    for (int ty = headTy; ty <= footTy; ++ty) {
                        if (!passableTile(tx, ty)) return false;
                    }
                    return map.getSolid(tx, footTy + 1) || map.getSemiSolid(tx, footTy + 1);
                };
                auto findNearestStandable = [&](int cx, int cy, int radius) -> SDL_Point {
                    SDL_Point best{-1, -1};
                    int bestDist = 1e9;
                    for (int r = 0; r <= radius; ++r) {
                        for (int dy = -r; dy <= r; ++dy) {
                            for (int dx = -r; dx <= r; ++dx) {
                                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                                const int tx = cx + dx;
                                const int ty = cy + dy;
                                if (!standableTile(tx, ty)) continue;
                                const int dist = std::abs(dx) + std::abs(dy);
                                if (dist < bestDist) {
                                    bestDist = dist;
                                    best = SDL_Point{tx, ty};
                                }
                            }
                        }
                        if (best.x >= 0) break;
                    }
                    return best;
                };
                auto rebuildDemoPath = [&]() {
                    SDL_Log("demo.path: rebuild start");
                    if (!demoState.startTileSet) {
                        const int startTx = (int)std::floor((player.x + player.w * 0.5f) / (float)t);
                        const int startTy = (int)std::floor((player.y + player.h - 1.0f) / (float)t);
                        SDL_Point seeded = findNearestStandable(startTx, startTy, 24);
                        if (seeded.x >= 0) {
                            demoState.startTile = seeded;
                            demoState.startTileSet = true;
                        }
                    }
                    SDL_Point start = demoState.startTileSet ? demoState.startTile : SDL_Point{-1, -1};
                    if (start.x < 0 || !standableTile(start.x, start.y)) {
                        const int startTx = (int)std::floor((player.x + player.w * 0.5f) / (float)t);
                        const int startTy = (int)std::floor((player.y + player.h - 1.0f) / (float)t);
                        start = findNearestStandable(startTx, startTy, 24);
                        if (start.x >= 0) {
                            demoState.startTile = start;
                            demoState.startTileSet = true;
                        }
                    }
                    if (start.x < 0) {
                        SDL_Log("demo.path: rebuild failed (no valid start)");
                        demoState.pathTiles.clear();
                        demoState.waypointIndex = 0;
                        return;
                    }

                    SDL_Point target{-1, -1};
                    int bestTargetX = std::numeric_limits<int>::min();
                    for (const auto& obj : objects) {
                        if (obj.id != "67") continue;
                        const int ex = (int)std::floor(obj.x / (float)t);
                        const int ey = (int)std::floor((obj.y + 12.0f) / (float)t);
                        SDL_Point candidate = findNearestStandable(ex, ey, 10);
                        if (candidate.x < 0) continue;
                        if (candidate.x > bestTargetX) {
                            bestTargetX = candidate.x;
                            target = candidate;
                        }
                    }
                    if (target.x < 0) {
                        for (int x = map.w - 2; x >= 1 && target.x < 0; --x) {
                            for (int y = 1; y < map.h - 2; ++y) {
                                if (standableTile(x, y)) {
                                    target = SDL_Point{x, y};
                                    break;
                                }
                            }
                        }
                    }
                    if (target.x < 0) {
                        SDL_Log("demo.path: rebuild failed (no valid target)");
                        demoState.pathTiles.clear();
                        demoState.waypointIndex = 0;
                        return;
                    }
                    SDL_Log("demo.path: start=(%d,%d) target=(%d,%d)", start.x, start.y, target.x, target.y);
                    auto indexOf = [&](int tx, int ty) { return ty * map.w + tx; };
                    // Hazard-aware costs: steer the route away from bumpers/time-warp objects.
                    std::vector<int> tileRisk((size_t)map.w * (size_t)map.h, 0);
                    for (const auto& obj : objects) {
                        int objId = 0;
                        try { objId = std::stoi(obj.id); } catch (...) { continue; }
                        int basePenalty = 0;
                        int radiusX = 0;
                        int radiusY = 0;
                        if (objId == 46) { // bumper
                            basePenalty = 26;
                            radiusX = 2;
                            radiusY = 1;
                        } else if (objId >= 57 && objId <= 61) { // fast-travel trigger
                            basePenalty = 16;
                            radiusX = 2;
                            radiusY = 2;
                        } else {
                            continue;
                        }
                        const int ox = (int)std::floor(obj.x / (float)t);
                        const int oy = (int)std::floor(obj.y / (float)t);
                        for (int dy = -radiusY; dy <= radiusY; ++dy) {
                            for (int dx = -radiusX; dx <= radiusX; ++dx) {
                                const int tx = ox + dx;
                                const int ty = oy + dy;
                                if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) continue;
                                const int manhattan = std::abs(dx) + std::abs(dy);
                                const int risk = std::max(1, basePenalty - manhattan * 6);
                                tileRisk[indexOf(tx, ty)] = std::max(tileRisk[indexOf(tx, ty)], risk);
                            }
                        }
                    }

                    struct Node {
                        int g = std::numeric_limits<int>::max();
                        int f = std::numeric_limits<int>::max();
                        int parent = -1;
                        bool closed = false;
                    };
                    int foundIdx = -1;
                    int expanded = 0;
                    int guard = 0;
                    int usedAttempt = -1;
                    const int attemptCount = 3;
                    for (int attempt = 0; attempt < attemptCount && foundIdx < 0; ++attempt) {
                        const int heuristicXWeight = (attempt == 0) ? 3 : ((attempt == 1) ? 2 : 1);
                        const int forwardCost = (attempt == 0) ? 6 : ((attempt == 1) ? 8 : 10);
                        const int backCost = (attempt == 0) ? 14 : ((attempt == 1) ? 11 : 9);
                        const int downMax = (attempt == 0) ? 3 : ((attempt == 1) ? 4 : 5);
                        const int jumpMax = (attempt == 0) ? 3 : ((attempt == 1) ? 4 : 5);
                        const int jumpForwardBase = (attempt == 0) ? 20 : ((attempt == 1) ? 17 : 14);
                        const int jumpBackBase = (attempt == 0) ? 36 : ((attempt == 1) ? 26 : 20);
                        const int guardLimit = (attempt == 0) ? 30000 : 60000;
                        const float runMul = (attempt == 0) ? 0.78f : ((attempt == 1) ? 0.92f : 1.0f);
                        SDL_Log("demo.path: attempt %d (hX=%d jumpMax=%d downMax=%d)", attempt, heuristicXWeight, jumpMax, downMax);

                        const int total = map.w * map.h;
                        std::vector<Node> nodes(total);
                        std::vector<int> open;
                        open.reserve(4096);
                        auto heuristic = [&](int tx, int ty) {
                            return std::abs(tx - target.x) * heuristicXWeight + std::abs(ty - target.y);
                        };
                        auto simulateJumpReachable = [&](int fromX, int fromY, int toX, int toY) -> bool {
                        const float tile = (float)t;
                        const float startFootX = (fromX + 0.5f) * tile;
                        const float startFootY = (fromY + 1.0f) * tile;
                        const float targetFootX = (toX + 0.5f) * tile;
                        const float targetFootY = (toY + 1.0f) * tile;
                        const float dir = (targetFootX >= startFootX) ? 1.0f : -1.0f;
                        const float runSpeed = std::max(220.0f, movementCfg.maxSpeedGround * runMul);
                        float px = startFootX - (float)player.w * 0.5f;
                        float py = startFootY - (float)player.h;
                        float vx = dir * runSpeed;
                        float vy = -movementCfg.jumpSpeed;
                        const float g = movementCfg.gravityGround;
                        const float dtSim = 1.0f / 90.0f;
                        const float maxSimT = 1.35f;

                        auto rectHitsSolidOnly = [&](float x, float y, int w, int h) -> bool {
                            int left = (int)std::floor(x / tile);
                            int right = (int)std::floor((x + w - 1) / tile);
                            int top = (int)std::floor(y / tile);
                            int bottom = (int)std::floor((y + h - 1) / tile);
                            for (int ty = top; ty <= bottom; ++ty) {
                                for (int tx = left; tx <= right; ++tx) {
                                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return true;
                                    if (map.getSolid(tx, ty)) return true;
                                }
                            }
                            return false;
                        };

                        for (float simT = 0.0f; simT <= maxSimT; simT += dtSim) {
                            float nx = px + vx * dtSim;
                            float ny = py + vy * dtSim;
                            vy += g * dtSim;

                            if (!rectHitsSolidOnly(nx, py, player.w, player.h)) px = nx;
                            else vx = 0.0f;
                            if (!rectHitsSolidOnly(px, ny, player.w, player.h)) py = ny;
                            else {
                                if (vy > 0.0f) {
                                    const float footY = py + (float)player.h;
                                    const float dX = (px + player.w * 0.5f) - targetFootX;
                                    const float dY = footY - targetFootY;
                                    if (std::fabs(dX) <= tile * 0.70f && std::fabs(dY) <= tile * 0.60f &&
                                        standableTile(toX, toY)) {
                                        return true;
                                    }
                                }
                                break;
                            }

                            if (vy > 0.0f) {
                                const float footY = py + (float)player.h;
                                const float dX = (px + player.w * 0.5f) - targetFootX;
                                const float dY = footY - targetFootY;
                                if (std::fabs(dX) <= tile * 0.65f && std::fabs(dY) <= tile * 0.55f &&
                                    standableTile(toX, toY)) {
                                    return true;
                                }
                            }
                        }
                        return false;
                        };
                        auto tryRelax = [&](int fromIdx, int nx, int ny, int stepCost) {
                        if (nx < 0 || ny < 0 || nx >= map.w || ny >= map.h) return;
                        if (!standableTile(nx, ny)) return;
                        const int ni = indexOf(nx, ny);
                        if (nodes[ni].closed) return;
                        const int ng = nodes[fromIdx].g + stepCost + tileRisk[ni];
                        if (ng >= nodes[ni].g) return;
                        nodes[ni].g = ng;
                        nodes[ni].f = ng + heuristic(nx, ny);
                        nodes[ni].parent = fromIdx;
                        open.push_back(ni);
                        };

                        const int startIdx = indexOf(start.x, start.y);
                        nodes[startIdx].g = 0;
                        nodes[startIdx].f = heuristic(start.x, start.y);
                        open.push_back(startIdx);
                        int expandedAttempt = 0;
                        int guardAttempt = 0;
                        while (!open.empty() && guardAttempt++ < guardLimit) {
                        int bestOpenPos = 0;
                        int bestNode = open[0];
                        for (int i = 1; i < (int)open.size(); ++i) {
                            if (nodes[open[i]].f < nodes[bestNode].f) {
                                bestOpenPos = i;
                                bestNode = open[i];
                            }
                        }
                        open[bestOpenPos] = open.back();
                        open.pop_back();
                        if (nodes[bestNode].closed) continue;
                        nodes[bestNode].closed = true;
                        expandedAttempt++;
                        const int cx = bestNode % map.w;
                        const int cy = bestNode / map.w;
                        if (cx == target.x && cy == target.y) {
                            foundIdx = bestNode;
                            expanded = expandedAttempt;
                            guard = guardAttempt;
                            usedAttempt = attempt;
                            break;
                        }

                        tryRelax(bestNode, cx + 1, cy, forwardCost);
                        tryRelax(bestNode, cx - 1, cy, backCost);
                        // Prefer longer stable strides when ground permits.
                        tryRelax(bestNode, cx + 2, cy, forwardCost + 4);
                        tryRelax(bestNode, cx - 2, cy, backCost + 6);
                        // Allow straight drops when directly above a safe standable tile.
                        for (int dy = 1; dy <= downMax + 1; ++dy) {
                            tryRelax(bestNode, cx, cy + dy, 10 + dy * 3);
                        }
                        for (int dy = 1; dy <= downMax; ++dy) {
                            // Descents are allowed but carry slight risk cost.
                            tryRelax(bestNode, cx + 1, cy + dy, forwardCost + 4 + dy * 3);
                            tryRelax(bestNode, cx - 1, cy + dy, backCost + 8 + dy * 4);
                            tryRelax(bestNode, cx + 2, cy + dy, forwardCost + 7 + dy * 4);
                            tryRelax(bestNode, cx - 2, cy + dy, backCost + 11 + dy * 5);
                        }
                        for (int jx = 1; jx <= jumpMax; ++jx) {
                            for (int jy = 1; jy <= jumpMax; ++jy) {
                                int nx = cx + jx;
                                int ny = cy - jy;
                                if (simulateJumpReachable(cx, cy, nx, ny)) tryRelax(bestNode, nx, ny, jumpForwardBase + jx * 2 + jy * 4);
                                nx = cx - jx;
                                if (simulateJumpReachable(cx, cy, nx, ny)) tryRelax(bestNode, nx, ny, jumpBackBase + jx * 4 + jy * 6);
                            }
                        }
                    }
                        if (foundIdx < 0) {
                            SDL_Log("demo.path: attempt %d failed (expanded=%d guard=%d)", attempt, expandedAttempt, guardAttempt);
                        } else {
                            // Reconstruct using nodes from successful attempt.
                            demoState.pathTiles.clear();
                            demoState.waypointIndex = 0;
                            std::vector<SDL_Point> reversePath;
                            for (int at = foundIdx; at >= 0; at = nodes[at].parent) {
                                reversePath.push_back(SDL_Point{at % map.w, at / map.w});
                                if (at == startIdx) break;
                            }
                            for (int i = (int)reversePath.size() - 1; i >= 0; --i) {
                                demoState.pathTiles.push_back(reversePath[(size_t)i]);
                            }
                        }
                    }

                    if (foundIdx < 0) {
                        SDL_Log("demo.path: rebuild failed (unreachable target, expanded=%d, guard=%d)", expanded, guard);
                        demoState.pathTiles.clear();
                        demoState.waypointIndex = 0;
                        return;
                    }
                    if (demoState.pathTiles.size() >= 3) {
                        std::vector<SDL_Point> smoothed;
                        smoothed.reserve(demoState.pathTiles.size());
                        smoothed.push_back(demoState.pathTiles.front());
                        for (size_t i = 1; i + 1 < demoState.pathTiles.size(); ++i) {
                            const SDL_Point& a = smoothed.back();
                            const SDL_Point& b = demoState.pathTiles[i];
                            const SDL_Point& c = demoState.pathTiles[i + 1];
                            const int abx = b.x - a.x;
                            const int aby = b.y - a.y;
                            const int bcx = c.x - b.x;
                            const int bcy = c.y - b.y;
                            const bool sameDirection = (abx == bcx) && (aby == bcy);
                            if (!sameDirection) smoothed.push_back(b);
                        }
                        smoothed.push_back(demoState.pathTiles.back());
                        demoState.pathTiles.swap(smoothed);
                    }
                    SDL_Log("demo.path: rebuild ok (attempt=%d expanded=%d, guard=%d, path_len=%d)", usedAttempt, expanded, guard, (int)demoState.pathTiles.size());
                };

                if (demoState.repathTimer <= 0.0f || demoState.pathTiles.empty() || demoState.waypointIndex >= demoState.pathTiles.size()) {
                    rebuildDemoPath();
                    demoState.repathTimer = 0.85f;
                }

                const float probeNearX = (demoState.dir > 0.0f) ? (player.x + player.w + 6.0f) : (player.x - 6.0f);
                const float probeFarX = (demoState.dir > 0.0f) ? (player.x + player.w + t * 1.3f) : (player.x - t * 1.3f);
                const int txNear = (int)std::floor(probeNearX / (float)t);
                const int txFar = (int)std::floor(probeFarX / (float)t);
                const int topTy = (int)std::floor((player.y + 2.0f) / (float)t);
                const int footTy = (int)std::floor((player.y + player.h + 1.0f) / (float)t);
                bool wallAhead = false;
                for (int ty = topTy; ty <= footTy - 1; ++ty) {
                    if (map.getSolid(txNear, ty) || map.getSolid(txFar, ty)) {
                        wallAhead = true;
                        break;
                    }
                }
                auto hasGroundAt = [&](int tx, int ty) -> bool {
                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return false;
                    return map.getSolid(tx, ty) || map.getSemiSolid(tx, ty);
                };
                auto holeDepthAt = [&](int tx, int startTy, int maxDepth) -> int {
                    for (int d = 0; d <= maxDepth; ++d) {
                        const int ty = startTy + d;
                        if (ty >= map.h) return maxDepth + 1;
                        if (hasGroundAt(tx, ty)) return d;
                    }
                    return maxDepth + 1;
                };
                const int txFar2 = (int)std::floor(((demoState.dir > 0.0f) ? (player.x + player.w + t * 2.1f) : (player.x - t * 2.1f)) / (float)t);
                const int txFar3 = (int)std::floor(((demoState.dir > 0.0f) ? (player.x + player.w + t * 2.8f) : (player.x - t * 2.8f)) / (float)t);
                const int probeCols[4] = {txNear, txFar, txFar2, txFar3};
                int longestGapRun = 0;
                int gapRun = 0;
                int minLandingDepth = 9999;
                for (int i = 0; i < 4; ++i) {
                    const int depth = holeDepthAt(probeCols[i], footTy, 6);
                    if (depth == 0) {
                        gapRun = 0;
                    } else {
                        gapRun++;
                        longestGapRun = std::max(longestGapRun, gapRun);
                    }
                    minLandingDepth = std::min(minLandingDepth, depth);
                }
                const bool gapAhead = (longestGapRun >= 2) || (minLandingDepth >= 3);
                // Consider landing "safe" when one of the forward probes can be reached
                // with at most a short drop.
                const bool safeLandingAhead =
                    (holeDepthAt(txFar, footTy, 6) <= 2) ||
                    (holeDepthAt(txFar2, footTy, 6) <= 2) ||
                    (holeDepthAt(txFar3, footTy, 6) <= 2);

                if (!demoState.pathTiles.empty() && demoState.waypointIndex < demoState.pathTiles.size()) {
                    // Track progress on a fixed start->finish path by snapping to nearest upcoming node.
                    const float playerCenterX = player.x + player.w * 0.5f;
                    const float playerCenterY = player.y + player.h * 0.5f;
                    const size_t prevWaypointIndex = demoState.waypointIndex;
                    size_t nearestIdx = demoState.waypointIndex;
                    float nearestD2 = std::numeric_limits<float>::max();
                    const size_t lookEnd = std::min(demoState.pathTiles.size(), demoState.waypointIndex + 28);
                    for (size_t i = demoState.waypointIndex; i < lookEnd; ++i) {
                        const SDL_Point p = demoState.pathTiles[i];
                        const float px = (p.x + 0.5f) * (float)t;
                        const float py = (p.y + 0.5f) * (float)t;
                        const float dx = px - playerCenterX;
                        const float dy = py - playerCenterY;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < nearestD2) {
                            nearestD2 = d2;
                            nearestIdx = i;
                        }
                    }
                    demoState.waypointIndex = nearestIdx;
                    if (demoState.waypointIndex != prevWaypointIndex) {
                        SDL_Log("demo.path: waypoint %d -> %d", (int)prevWaypointIndex, (int)demoState.waypointIndex);
                    }
                }
                if (!demoState.pathTiles.empty() && demoState.waypointIndex < demoState.pathTiles.size()) {
                    while (demoState.waypointIndex < demoState.pathTiles.size() &&
                           !standableTile(demoState.pathTiles[demoState.waypointIndex].x,
                                          demoState.pathTiles[demoState.waypointIndex].y)) {
                        demoState.waypointIndex++;
                    }
                    if (demoState.waypointIndex >= demoState.pathTiles.size()) {
                        demoState.repathTimer = 0.0f;
                    }
                }
                if (!demoState.pathTiles.empty() && demoState.waypointIndex < demoState.pathTiles.size()) {
                    const SDL_Point wp = demoState.pathTiles[demoState.waypointIndex];
                    const float waypointX = (wp.x + 0.5f) * (float)t;
                    const float waypointY = (wp.y + 0.5f) * (float)t;
                    const float playerCenterX = player.x + player.w * 0.5f;
                    const float playerCenterY = player.y + player.h * 0.5f;
                    const float dxWp = waypointX - playerCenterX;
                    const float dyWp = waypointY - playerCenterY;
                    if (std::fabs(dxWp) < 8.0f && std::fabs(dyWp) < (float)t * 0.55f) {
                        if (demoState.waypointIndex + 1 < demoState.pathTiles.size()) {
                            demoState.waypointIndex++;
                        }
                    }
                    const float steer = (dxWp > 4.0f) ? 1.0f : ((dxWp < -10.0f) ? -1.0f : 1.0f);
                    demoState.dir = steer;
                    touchMove = steer;
                    if (player.onGround && demoState.jumpCooldown <= 0.0f && dyWp < -10.0f) {
                        demoState.jumpCooldown = 0.18f;
                        demoState.jumpHoldTimer = 0.11f;
                    }
                }

                bool bumperAhead = false;
                bool springAhead = false;
                bool fastTravelAhead = false;
                int fastTravelAheadId = -1;
                float nearestFastTravelDist = std::numeric_limits<float>::max();
                float nearestFastTravelCenterX = 0.0f;
                float nearestSpringDist = std::numeric_limits<float>::max();
                float nearestSpringCenterX = 0.0f;
                const float playerCenterX = player.x + player.w * 0.5f;
                const float playerCenterY = player.y + player.h * 0.5f;
                for (const auto& obj : objects) {
                    int objId = 0;
                    try { objId = std::stoi(obj.id); } catch (...) { continue; }
                    const float objCenterX = obj.x;
                    const float objCenterY = obj.y;
                    const float dx = objCenterX - playerCenterX;
                    const float dy = objCenterY - playerCenterY;
                    const float aheadDist = dx * ((demoState.dir > 0.0f) ? 1.0f : -1.0f);
                    if (objId == 46) {
                        if (aheadDist >= -8.0f && aheadDist <= (float)t * 3.0f && std::fabs(dy) <= (float)t * 1.5f) {
                            bumperAhead = true;
                        }
                        continue;
                    }
                    if (objId == 31) {
                        if (aheadDist >= -10.0f && aheadDist <= (float)t * 4.0f && std::fabs(dy) <= (float)t * 1.8f) {
                            if (aheadDist < nearestSpringDist) {
                                nearestSpringDist = aheadDist;
                                nearestSpringCenterX = objCenterX;
                                springAhead = true;
                            }
                        }
                        continue;
                    }
                    if (objId < 57 || objId > 61) continue;
                    if (aheadDist < -20.0f || aheadDist > (float)t * 7.0f) continue;
                    if (std::fabs(dy) > (float)t * 2.5f) continue;
                    if (aheadDist < nearestFastTravelDist) {
                        nearestFastTravelDist = aheadDist;
                        nearestFastTravelCenterX = objCenterX;
                        fastTravelAheadId = objId;
                        fastTravelAhead = true;
                    }
                }

                bool shouldJump = false;
                if (player.onGround && demoState.jumpCooldown <= 0.0f) {
                    shouldJump = wallAhead || (gapAhead && safeLandingAhead) || bumperAhead;
                }

                if (springAhead) {
                    // Line up with spring and let spring impulse handle the vertical movement.
                    const float springDeltaX = nearestSpringCenterX - playerCenterX;
                    if (std::fabs(springDeltaX) > 4.0f) {
                        touchMove = (springDeltaX > 0.0f) ? 1.0f : -1.0f;
                        demoState.dir = touchMove;
                    }
                    if (nearestSpringDist <= (float)t * 1.6f) {
                        shouldJump = false;
                        demoState.jumpHoldTimer = 0.0f;
                    }
                    // If terrain demands vertical gain and a spring is nearby, trust spring pathing.
                    if (wallAhead || gapAhead) {
                        shouldJump = false;
                    }
                }

                if (fastTravelAhead) {
                    touchMove = demoState.dir;
                    const float targetXDelta = nearestFastTravelCenterX - playerCenterX;
                    if (std::fabs(targetXDelta) > 6.0f) {
                        touchMove = (targetXDelta > 0.0f) ? 1.0f : -1.0f;
                        demoState.dir = touchMove;
                    }
                    if (fastTravelAheadId == 58) {
                        demoDown = true;
                    }
                    if (fastTravelAheadId == 57 || fastTravelAheadId == 61) {
                        shouldJump = false;
                    }
                    // Safety: avoid entering nearby fast-travel if already close to route end-sign zone.
                    if (endSignState.present && std::fabs(endSignState.objectX - playerCenterX) < (float)t * 10.0f) {
                        fastTravelAhead = false;
                        demoDown = false;
                        touchMove = demoState.dir;
                    }
                }

                if (bumperAhead && !wallAhead && player.onGround) {
                    // Prefer avoiding bumper collisions when there is space to sidestep/back off.
                    shouldJump = false;
                    if (!gapAhead) touchMove = -demoState.dir;
                }

                if (shouldJump) {
                    demoState.jumpCooldown = bumperAhead ? 0.14f : 0.22f;
                    demoState.jumpHoldTimer = bumperAhead ? 0.14f : 0.10f;
                }
                demoJump = (demoState.jumpHoldTimer > 0.0f);
                const float dx = player.x - demoState.lastX;
                if (std::fabs(dx) < 2.0f) demoState.stuckTime += dt;
                else demoState.stuckTime = 0.0f;
                if (player.onGround && demoState.stuckTime > 0.9f) {
                    demoState.dir = -demoState.dir;
                    demoState.stuckTime = 0.0f;
                    demoState.jumpCooldown = 0.0f;
                    demoState.jumpHoldTimer = 0.12f;
                    demoJump = true;
                }
                demoState.lastX = player.x;
                if (!fastTravelAhead) {
                    touchMove = demoState.dir;
                } else {
                    touchMove = std::clamp(touchMove, -1.0f, 1.0f);
                }
                touchDown = demoDown;
                touchJump = demoJump;
            }
            const bool forceRightMovement = endSignState.lockPlayerRight;
            float gamepadMove = forceRightMovement ? 1.0f : input.gameplayMoveX();
            bool gamepadDown = forceRightMovement ? false : input.gameplayDownHeld();
            bool gamepadJump = forceRightMovement ? false : input.gameplayJumpHeld();
            bool gamepadFreeMove = input.freeMoveHeld();
            const bool fastTravelEnabled = fastTravelTriggered;
            if (playerSpawnLockFrames > 0) {
                playerSpawnLockFrames--;
                player.x = playerSpawnLockX;
                player.y = playerSpawnLockY;
                player.vx = 0.0f;
                player.vy = 0.0f;
                player.jumpHeld = false;
                player.jumpWasDown = false;
                player.jumpHoldTime = 0.0f;
                player.jumpBufferTime = 0.0f;
                touchMove = 0.0f;
                touchDown = false;
                touchJump = false;
                inputMove = 0.0f;
                inputDown = false;
                gamepadMove = 0.0f;
                gamepadDown = false;
                gamepadJump = false;
                gamepadFreeMove = false;
            }
            if (forceRightMovement) {
                touchMove = 1.0f;
                touchDown = false;
                touchJump = false;
                inputMove = 1.0f;
                inputDown = false;
            }
            PlayerUpdateResult upd = PlayerUpdateResult::RenderOnly;
            const float beforeNormalX = player.x;
            const float beforeNormalY = player.y;
                if (!fastTravelEnabled) {
                    upd = UpdatePlayerMovement(
                        player, map, dt, jumpBufferMax, movementCfg,
                        touchMove, touchDown, touchJump,
                        gamepadMove, gamepadDown, gamepadJump, gamepadFreeMove,
                        keybinds,
                        inputMove, inputDown
                    );
                    if (std::fabs(player.x - beforeNormalX) > 0.01f || std::fabs(player.y - beforeNormalY) > 0.01f) {
                        addMovementReason(MR_NORMAL_MOVEMENT);
                    }
            } else {
                // Fast-travel mode disables normal movement/physics.
                inputMove = 0.0f;
                inputDown = false;
            }
            replayInput.touchMove = touchMove;
            replayInput.touchDown = touchDown;
            replayInput.touchJump = touchJump;
            replayInput.gamepadMove = gamepadMove;
            replayInput.gamepadDown = gamepadDown;
            replayInput.gamepadJump = gamepadJump;
            replayInput.gamepadFreeMove = gamepadFreeMove;
            replayInput.inputMove = inputMove;
            replayInput.inputDown = inputDown;
            replayInput.forceRightMovement = forceRightMovement;
            replayInput.fastTravelEnabled = fastTravelEnabled;
            replayInput.demoEnabled = demoState.enabled;
            {
                const float beforeWrapX = player.x;
                const float beforeWrapY = player.y;
                bool wrappedX = false;
                bool wrappedY = false;
                const bool horizontalWrapActive = gameplayWrapX;
                const bool verticalWrapNow = isVerticalWrapEnabledAtX(player.x);
                verticalWrapActive = verticalWrapNow;
                if (verticalWrapNow) {
                    const float mapHeightPx = (float)(map.h * map.tileSize);
                    while (player.y + (float)player.h < 0.0f) {
                        player.y += mapHeightPx;
                        wrappedY = true;
                    }
                    while (player.y >= mapHeightPx) {
                        player.y -= mapHeightPx;
                        wrappedY = true;
                    }
                }
                if (horizontalWrapActive) {
                    const float mapWidthPx = (float)(map.w * map.tileSize);
                    while (player.x + (float)player.w < 0.0f) {
                        player.x += mapWidthPx;
                        wrappedX = true;
                    }
                    while (player.x >= mapWidthPx) {
                        player.x -= mapWidthPx;
                        wrappedX = true;
                    }
                }
                if ((wrappedX || wrappedY) && RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                    const float wrappedPosX = player.x;
                    const float wrappedPosY = player.y;
                    bool foundSafe = false;
                    auto tryPlace = [&](float nx, float ny) -> bool {
                        if (RectHitsSolid(map, nx, ny, player.w, player.h)) return false;
                        player.x = nx;
                        player.y = ny;
                        foundSafe = true;
                        return true;
                    };
                    float preferX = 0.0f;
                    float preferY = 0.0f;
                    if (wrappedX) {
                        if (player.vx > 0.01f) preferX = -1.0f;
                        else if (player.vx < -0.01f) preferX = 1.0f;
                        else preferX = (beforeWrapX <= player.x) ? 1.0f : -1.0f;
                    }
                    if (wrappedY) {
                        if (player.vy > 0.01f) preferY = -1.0f;
                        else if (player.vy < -0.01f) preferY = 1.0f;
                        else preferY = (beforeWrapY <= player.y) ? 1.0f : -1.0f;
                    }
                    for (int up = 0; up <= 72 && !foundSafe; up += 2) {
                        for (int side = 0; side <= 36 && !foundSafe; side += 2) {
                            const float sx = (side == 0) ? 0.0f : ((preferX == 0.0f) ? (float)side : preferX * (float)side);
                            const float uy = (up == 0) ? 0.0f : ((preferY == 0.0f) ? -(float)up : preferY * (float)up);
                            if (tryPlace(wrappedPosX + sx, wrappedPosY + uy)) break;
                            if (side > 0 && tryPlace(wrappedPosX - sx, wrappedPosY + uy)) break;
                            if (up > 0 && tryPlace(wrappedPosX + sx, wrappedPosY - uy)) break;
                        }
                    }
                    if (!foundSafe) {
                        player.x = beforeWrapX;
                        player.y = beforeWrapY;
                        if (wrappedX) player.vx = 0.0f;
                        if (wrappedY) player.vy = 0.0f;
                    } else if (std::fabs(player.y - wrappedPosY) > 0.01f) {
                        player.onGround = false;
                    }
                }
                if (std::fabs(player.x - beforeWrapX) > 0.01f || std::fabs(player.y - beforeWrapY) > 0.01f) {
                    addMovementReason(MR_WORLD_WRAP);
                }
            }
            if (upd == PlayerUpdateResult::Reloaded) {
                if (levelLoadDeathGraceTimer > 0.0f) {
                    continue;
                }
                const float pitResetY = (float)((map.h + 7) * map.tileSize);
                const bool bottomlessPit = (player.y >= pitResetY);
                if (bottomlessPit) {
                    // Bottomless pit routing, based on world/area/time variant logic.
                    const char tw = levelManager.timeWarpId();
                    const int world = levelManager.worldId();
                    const int area = levelManager.levelPartId();
                    const int ot = levelManager.timeId();
                    if (tw != 'F' && world > 0 && area > 0) {
                        // In normal world ('N'), pit falls should trigger death flow.
                        if (tw != 'N') {
                            int targetTime = 2;
                            if (ot == 1) {
                                if (tw == '2') {
                                    targetTime = 3;
                                } else {
                                    targetTime = 2;
                                }
                            } else if (ot == 2) {
                                targetTime = 3;
                            } else {
                                targetTime = 2;
                            }
                            std::string pitPath = levelManager.levelPathByCode(world * 100 + area * 10 + targetTime);
                            if (!pitPath.empty()) {
                                    levelManager.setLevelPath(pitPath);
                                    droppedCoins.clear();
                                    reloadLevel();
                                    continue;
                            }
                        }
                    }
                }
                deathSequenceActive = true;
                deathLifeDeducted = false;
                deathTimer = 0.0f;
                audio.haltAllChannels();
                audio.haltMusic();
                audio.playLoseSfx();
                continue;
            }

            if (upd == PlayerUpdateResult::RenderOnly) {
                updatePlayerAnimState(inputMove, inputDown, dt);
                goto RENDER_ONLY;
            }

            const bool isSecretBossLevel = (currentLevelId == 59);
            if (!isSecretBossLevel &&
                !bossState.active && bossState.activationCooldown <= 0.0f &&
                playerTouchesTileId(map, player, 68, 68, gameplayWrapX, gameplayWrapY)) {
                bossState.active = true;
                removeTimewarpObjectsAndExit();
            }

            if (bossState.active && bossState.activationCooldown <= 0.0f && !levelCompleteActive) {
                auto applyBossContactDamageToPlayer = [&]() -> bool {
                    const bool hasCoins = levelManager.coinCount() > 0;
                    if (hasCoins) {
                        dropPlayerCoins(player.x + player.w * 0.5f, player.y + player.h * 0.5f);
                        playerInvincibleTimer = kPlayerInvincibleDuration;
                        player.anim = ANIM_HURT;
                        player.animTime = 0.0f;
                        const float playerCenterX = player.x + player.w * 0.5f;
                        const float pushDir = (playerCenterX < bossState.x) ? -1.0f : 1.0f;
                        player.vx = pushDir * 520.0f;
                        player.vy = -760.0f;
                        player.onGround = false;
                        audio.playLoseSfx();
                        return false;
                    }
                    deathSequenceActive = true;
                    deathLifeDeducted = false;
                    deathTimer = 0.0f;
                    audio.haltAllChannels();
                    audio.haltMusic();
                    audio.playLoseSfx();
                    return true;
                };
                auto applyBossDamage = [&](int amount) {
                    if (amount <= 0 || !bossState.active) return;
                    const int oldHp = bossState.health;
                    bossState.health = std::max(0, bossState.health - amount);
                    if (bossState.health < oldHp) {
                        bossState.hurtFlash = 0.18f;
                    }
                    if (bossState.health <= 0) {
                        bossState.active = false;
                        secretFireballs.clear();
                        secretExplosions.clear();
                        if (bossState.world == 1) {
                            clampCamX = false;
                        }
                        startLevelCompleteSequence();
                    }
                };

                const bool bossUsesFinalAnimation = (bossState.sourceWorld == 7);
                const float bossBaseSize = bossUsesFinalAnimation ? 56.0f : 28.0f; // normal-animation bosses are 50% smaller
                const float bossW = bossBaseSize;
                const float bossH = bossBaseSize;
                const float halfW = bossW * 0.5f;
                const float halfH = bossH * 0.5f;
                const float secretCenterX = (float)(map.w * map.tileSize) * 0.5f;
                const float secretCenterY = (float)(map.h * map.tileSize) * 0.5f;
                float arenaLeft = (float)map.tileSize * 2.0f;
                float arenaTop = (float)map.tileSize * 2.0f;
                float arenaRight = (float)(map.w * map.tileSize) - (float)map.tileSize * 2.0f;
                float arenaBottom = (float)(map.h * map.tileSize) - (float)map.tileSize * 2.0f;
                if (!isSecretBossLevel && bossState.sourceWorld == 3) {
                    const float playerCenterX = player.x + player.w * 0.5f;
                    const float playerCenterY = player.y + player.h * 0.5f;
                    if (bossState.phase == 0) {
                        if (!bossState.replayPath.empty()) {
                            bossState.x = bossState.replayPath[0].x;
                            bossState.y = bossState.replayPath[0].y;
                        }
                        const float dx = playerCenterX - bossState.x;
                        const float dy = playerCenterY - bossState.y;
                        if (dx * dx + dy * dy <= 260.0f * 260.0f) {
                            bossState.phase = 1;
                            bossState.replayIndex = 0;
                            bossState.replayFrameAcc = 0.0f;
                            SDL_Log("boss.w3: phase 0 -> 1 (start replay)");
                        }
                    } else if (bossState.phase == 1) {
                        if (!bossState.replayPath.empty()) {
                            bossState.replayFrameAcc += dt * 24.0f;
                            while (bossState.replayFrameAcc >= 1.0f && bossState.replayIndex + 1 < bossState.replayPath.size()) {
                                bossState.replayFrameAcc -= 1.0f;
                                bossState.replayIndex++;
                            }
                            const SDL_FPoint& p = bossState.replayPath[std::min(bossState.replayIndex, bossState.replayPath.size() - 1)];
                            bossState.x = p.x;
                            bossState.y = p.y;
                            if (bossState.replayIndex + 1 >= bossState.replayPath.size()) {
                                bossState.phase = 2;
                                SDL_Log("boss.w3: phase 1 -> 2 (replay done)");
                            }
                        } else {
                            bossState.phase = 2;
                        }
                    } else if (bossState.phase == 2) {
                        const float dx = playerCenterX - bossState.x;
                        const float dy = playerCenterY - bossState.y;
                        if (dx * dx + dy * dy <= 300.0f * 300.0f) {
                            bossState.phase = 3;
                            bossState.health = bossState.maxHealth;
                            bossState.vx = 280.0f;
                            bossState.vy = 220.0f;
                            removeTimewarpObjectsAndExit();
                            SDL_Log("boss.w3: phase 2 -> 3 (fight active)");
                        }
                    }
                }
                const bool hasBossCameraLock =
                    isSecretBossLevel ||
                    (bossState.sourceWorld == 1 || bossState.sourceWorld == 2) ||
                    (bossState.sourceWorld == 4) ||
                    (bossState.sourceWorld == 5) ||
                    (bossState.sourceWorld == 7) ||
                    (bossState.sourceWorld == 3 && bossState.phase == 3);
                if (hasBossCameraLock) {
                    float lockCx = 1170.0f;
                    float lockCy = 132.0f;
                    if (isSecretBossLevel) {
                        lockCx = secretCenterX;
                        lockCy = secretCenterY;
                    } else if (bossState.sourceWorld == 2) {
                        lockCx = 1887.0f;
                        lockCy = 744.0f;
                    } else if (bossState.sourceWorld == 4) {
                        lockCx = 1315.0f + kGameplayViewW * 0.5f;
                        lockCy = 202.0f + kGameplayViewH * 0.5f;
                    } else if (bossState.sourceWorld == 5) {
                        lockCx = 5728.0f + kGameplayViewW * 0.5f;
                        lockCy = 2832.0f + kGameplayViewH * 0.5f;
                    } else if (bossState.sourceWorld == 7) {
                        lockCx = 96.0f + kGameplayViewW * 0.5f;
                        lockCy = 32.0f + kGameplayViewH * 0.5f;
                    } else if (bossState.sourceWorld == 3) {
                        lockCx = 2528.0f + kGameplayViewW * 0.5f;
                        lockCy = 976.0f + kGameplayViewH * 0.5f;
                    }
                    const float lockLeft = lockCx - kGameplayViewW * 0.5f + halfW + 8.0f;
                    const float lockRight = lockCx + kGameplayViewW * 0.5f - halfW - 8.0f;
                    const float lockTop = lockCy - kGameplayViewH * 0.5f + halfH + 8.0f;
                    const float lockBottom = lockCy + kGameplayViewH * 0.5f - halfH - 8.0f;
                    arenaLeft = std::max(arenaLeft, lockLeft);
                    arenaRight = std::min(arenaRight, lockRight);
                    arenaTop = std::max(arenaTop, lockTop);
                    arenaBottom = std::min(arenaBottom, lockBottom);
                }
                if (arenaLeft > arenaRight) std::swap(arenaLeft, arenaRight);
                if (arenaTop > arenaBottom) std::swap(arenaTop, arenaBottom);
                const bool bossCanMoveAndTakeDamage = !(bossState.sourceWorld == 3 && bossState.phase != 3);
                const bool world4RainbowActive = (bossState.sourceWorld == 4 && bossState.rainbowTimer > 0.0f);
                if (bossCanMoveAndTakeDamage && !world4RainbowActive) {
                    if (isSecretBossLevel) {
                        bossState.x = secretCenterX;
                        bossState.y = secretCenterY;
                        bossState.vx = 0.0f;
                        bossState.vy = 0.0f;
                    } else if (bossState.sourceWorld == 7) {
                        bossState.vy += bossGravity * dt;
                        bossState.y += bossState.vy * dt;
                        if (bossState.y + halfH < arenaTop) {
                            const float minX = arenaLeft + halfW;
                            const float maxX = std::max(minX, arenaRight - halfW);
                            const float rx = (float)std::rand() / (float)RAND_MAX;
                            bossState.x = minX + (maxX - minX) * rx;
                            bossState.y = arenaBottom + halfH + 24.0f;
                            bossState.vy = -std::fabs(bossState.vy);
                        }
                    } else {
                        bossState.vy += bossGravity * dt;
                        bossState.x += bossState.vx * dt;
                        bossState.y += bossState.vy * dt;
                        if (bossState.x - halfW < arenaLeft) {
                            bossState.x = arenaLeft + halfW;
                            bossState.vx = std::fabs(bossState.vx);
                        }
                        if (bossState.x + halfW > arenaRight) {
                            bossState.x = arenaRight - halfW;
                            bossState.vx = -std::fabs(bossState.vx);
                        }
                        if (bossState.y - halfH < arenaTop) {
                            bossState.y = arenaTop + halfH;
                            bossState.vy = std::fabs(bossState.vy);
                        }
                        if (bossState.y + halfH > arenaBottom) {
                            bossState.y = arenaBottom - halfH;
                            bossState.vy = -std::fabs(bossState.vy);
                        }
                    }
                }
                if (isSecretBossLevel) {
                    bossState.secretShotTimer -= dt;
                    if (bossState.secretShotTimer <= 0.0f) {
                        bossState.secretShotTimer += 0.85f;
                        SecretFireball fb{};
                        fb.x = bossState.x;
                        fb.y = bossState.y - halfH - 6.0f;
                        fb.vx = 0.0f;
                        fb.vy = -900.0f;
                        fb.timer = 0.0f;
                        fb.phase = 0;
                        secretFireballs.push_back(fb);
                    }
                    bool playerKilledByExplosion = false;
                    for (auto& fb : secretFireballs) {
                        if (fb.phase == 0) {
                            fb.timer += dt;
                            fb.y += fb.vy * dt;
                            if (fb.timer >= 0.33f) {
                                const float dir = ((float)std::rand() / (float)RAND_MAX) < 0.5f ? -1.0f : 1.0f;
                                const float speedX = 220.0f + ((float)std::rand() / (float)RAND_MAX) * 260.0f;
                                fb.vx = dir * speedX;
                                fb.vy = -180.0f;
                                fb.phase = 1;
                            }
                        } else {
                            fb.vy += 1800.0f * dt;
                            fb.x += fb.vx * dt;
                            fb.y += fb.vy * dt;
                            const float toBossX = fb.x - bossState.x;
                            const float toBossY = fb.y - bossState.y;
                            const bool hitBoss = (toBossX * toBossX + toBossY * toBossY) <= (halfW + 8.0f) * (halfW + 8.0f);
                            const bool hitGround = fb.y >= arenaBottom - 2.0f;
                            if (hitGround || hitBoss) {
                                SecretExplosion ex{};
                                ex.x = fb.x;
                                ex.y = std::min(fb.y, arenaBottom - 2.0f);
                                secretExplosions.push_back(ex);
                                fb.timer = -9999.0f;
                            }
                        }
                    }
                    secretFireballs.erase(
                        std::remove_if(secretFireballs.begin(), secretFireballs.end(), [&](const SecretFireball& fb) {
                            return fb.timer < -1000.0f ||
                                fb.y > arenaBottom + 80.0f ||
                                fb.x < arenaLeft - 120.0f ||
                                fb.x > arenaRight + 120.0f;
                        }),
                        secretFireballs.end());
                    for (auto& ex : secretExplosions) {
                        ex.life -= dt;
                        const float pCx = player.x + player.w * 0.5f;
                        const float pCy = player.y + player.h * 0.5f;
                        const float pDx = pCx - ex.x;
                        const float pDy = pCy - ex.y;
                        const bool touchingPlayer = (pDx * pDx + pDy * pDy) <= ex.radius * ex.radius;
                        if (!ex.hitPlayer && touchingPlayer && playerInvincibleTimer <= 0.0f && levelLoadDeathGraceTimer <= 0.0f) {
                            ex.hitPlayer = true;
                            if (applyBossContactDamageToPlayer()) {
                                playerKilledByExplosion = true;
                            }
                        }
                        const float bDx = bossState.x - ex.x;
                        const float bDy = bossState.y - ex.y;
                        const bool touchingBoss = (bDx * bDx + bDy * bDy) <= (ex.radius + halfW) * (ex.radius + halfW);
                        if (!ex.hitBoss && touchingBoss && bossState.active) {
                            ex.hitBoss = true;
                            applyBossDamage(1);
                        }
                    }
                    secretExplosions.erase(
                        std::remove_if(secretExplosions.begin(), secretExplosions.end(), [](const SecretExplosion& ex) {
                            return ex.life <= 0.0f;
                        }),
                        secretExplosions.end());
                    if (playerKilledByExplosion) {
                        continue;
                    }
                }

                const float bx = bossState.x - halfW;
                const float by = bossState.y - halfH;
                float testBx = bx;
                float testBy = by;
                const bool overlap = overlapPlayerWithWrappedRect(bx, by, bossW, bossH, testBx, testBy);
                if (overlap) {
                    if (isSecretBossLevel) {
                        if (bossState.secretTouchDamageCooldown <= 0.0f) {
                            applyBossDamage(1);
                            bossState.secretTouchDamageCooldown = 0.20f;
                            audio.playBumperSfx();
                        }
                        if (playerInvincibleTimer <= 0.0f && levelLoadDeathGraceTimer <= 0.0f) {
                            if (applyBossContactDamageToPlayer()) continue;
                        }
                    } else if (playerInvincibleTimer <= 0.0f && levelLoadDeathGraceTimer <= 0.0f) {
                        if (!bossCanMoveAndTakeDamage) {
                            if (applyBossContactDamageToPlayer()) continue;
                        } else {
                            const float py2 = player.y + (float)player.h;
                            const bool stomp = (player.vy > 80.0f) && (py2 <= testBy + bossH * 0.55f);
                            if (stomp) {
                                player.y = testBy - (float)player.h;
                                player.vy = -1000.0f;
                                player.onGround = false;
                                playerInvincibleTimer = 0.12f;
                                applyBossDamage(1);
                                if (bossState.sourceWorld == 4) {
                                    bossState.rainbowTimer = 3.0f;
                                    const float pad = 20.0f;
                                    const float minX = std::min(arenaLeft + halfW + pad, arenaRight - halfW - pad);
                                    const float maxX = std::max(arenaLeft + halfW + pad, arenaRight - halfW - pad);
                                    const float minY = std::min(arenaTop + halfH + pad, arenaBottom - halfH - pad);
                                    const float maxY = std::max(arenaTop + halfH + pad, arenaBottom - halfH - pad);
                                    const float rx = (float)std::rand() / (float)RAND_MAX;
                                    const float ry = (float)std::rand() / (float)RAND_MAX;
                                    bossState.x = minX + (maxX - minX) * rx;
                                    bossState.y = minY + (maxY - minY) * ry;
                                } else if (bossState.sourceWorld == 7) {
                                    const float minX = arenaLeft + halfW;
                                    const float maxX = std::max(minX, arenaRight - halfW);
                                    const float rx = (float)std::rand() / (float)RAND_MAX;
                                    bossState.x = minX + (maxX - minX) * rx;
                                    bossState.y = arenaBottom + halfH + 24.0f;
                                    bossState.vy = -std::fabs(bossState.vy);
                                }
                                audio.playBumperSfx();
                            } else {
                                if (applyBossContactDamageToPlayer()) continue;
                            }
                        }
                    }
                }
            }

            if (!paused && !deathSequenceActive && !levelCompleteActive && player.onGround) {
                const float footX = player.x + player.w * 0.5f;
                const float footY = player.y + (float)player.h + 1.0f;
                int tx = (int)std::floor(footX / (float)map.tileSize);
                int ty = (int)std::floor(footY / (float)map.tileSize);
                if (gameplayWrapX && map.w > 0) {
                    tx %= map.w;
                    if (tx < 0) tx += map.w;
                }
                if (gameplayWrapY && map.h > 0) {
                    ty %= map.h;
                    if (ty < 0) ty += map.h;
                }
                if (tx >= 0 && tx < map.w && ty >= 0 && ty < map.h) {
                    const int idx = ty * map.w + tx;
                    const int standingTileId = (int)map.tileIds[idx];
                    if (standingTileId == 20) {
                        levelManager.setTileAt(map, idx, 21);
                        levelManager.addCoins(5);
                        audio.playCoinSfx();
                    } else if (standingTileId == 13 && playerInvincibleTimer <= 0.0f && levelLoadDeathGraceTimer <= 0.0f) {
                        const bool hasCoins = levelManager.coinCount() > 0;
                        if (hasCoins) {
                            dropPlayerCoins(player.x + player.w * 0.5f, player.y + player.h * 0.5f);
                            playerInvincibleTimer = kPlayerInvincibleDuration;
                            player.anim = ANIM_HURT;
                            player.animTime = 0.0f;
                            player.vx = (player.facing < 0) ? 320.0f : -320.0f;
                            player.vy = -760.0f;
                            player.onGround = false;
                            audio.playLoseSfx();
                        } else {
                            deathSequenceActive = true;
                            deathLifeDeducted = false;
                            deathTimer = 0.0f;
                            audio.haltAllChannels();
                            audio.haltMusic();
                            audio.playLoseSfx();
                            continue;
                        }
                    }
                }
            }

            int collectedNow = levelManager.collectCoinsAtPlayer(map, player, gameplayWrapX, gameplayWrapY);
            if (collectedNow > 0) {
                audio.playCoinSfx();
            }
            if (timeTravelTriggerCooldown <= 0.0f) {
                levelManager.updateTimeWarpIdAtPlayer(map, player, gameplayWrapX, gameplayWrapY);
            }

            // Spring objects (id 31): bounce player upward on top contact.
            for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
                const auto& obj = objects[objIdx];
                if (obj.id != "31") continue;
                const float sx = obj.x - 16.0f;
                const float sy = obj.y - 16.0f;
                const float sw = 32.0f;
                const float sh = 32.0f;
                float testSx = sx;
                float testSy = sy;
                if (!overlapPlayerWithWrappedRect(sx, sy, sw, sh, testSx, testSy)) continue;
                const float px1 = player.x;
                const float px2 = player.x + (float)player.w;
                const float py2 = player.y + (float)player.h;
                const bool xOverlap = (px2 > testSx) && (px1 < testSx + sw);
                const bool nearTop = (py2 >= testSy) && (py2 <= testSy + sh * 0.75f);
                if (xOverlap && nearTop && player.vy >= 0.0f) {
                    player.y = testSy - (float)player.h;
                    player.vy = -1800.0f;
                    player.onGround = false;
                    addMovementReason(MR_SPRING);
                    break;
                }
            }
            // Bumper objects (id 46): vertical bounce, up/down depending approach.
            auto applyBumperBounce = [&](float targetY, float launchVy) -> bool {
                const float oldY = player.y;
                player.y = targetY;

                // Keep player out of solids near bumpers placed against floor/ceiling.
                const float nudgeDir = (launchVy < 0.0f) ? 1.0f : -1.0f;
                int nudgeSteps = 0;
                while (RectHitsSolid(map, player.x, player.y, player.w, player.h) && nudgeSteps < 64) {
                    player.y += nudgeDir;
                    ++nudgeSteps;
                }
                if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                    player.y = oldY;
                    return false;
                }

                // If launch direction is immediately blocked, don't force into ceiling/floor.
                const float probeY = player.y + ((launchVy < 0.0f) ? -2.0f : 2.0f);
                if (RectHitsSolid(map, player.x, probeY, player.w, player.h)) {
                    player.vy = 0.0f;
                    player.onGround = (launchVy > 0.0f);
                    return false;
                }

                player.vy = launchVy;
                player.onGround = false;
                return true;
            };
            for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
                const auto& obj = objects[objIdx];
                if (obj.id != "46") continue;
                const float bx = obj.x - 16.0f;
                const float by = obj.y - 16.0f;
                const float bw = 32.0f;
                const float bh = 32.0f;
                float testBx = bx;
                float testBy = by;
                const bool overlap = overlapPlayerWithWrappedRect(bx, by, bw, bh, testBx, testBy);
                if (!overlap) continue;

                const float playerCY = player.y + player.h * 0.5f;
                const float bumperCY = testBy + bh * 0.5f;
                if (playerCY <= bumperCY && player.vy >= 0.0f) {
                    if (applyBumperBounce(testBy - (float)player.h, -1200.0f)) {
                        addMovementReason(MR_BUMPER);
                        activeBumperIndices.insert(objIdx);
                        audio.playBumperSfx();
                    }
                    break;
                }
                if (playerCY > bumperCY && player.vy <= 0.0f) {
                    if (applyBumperBounce(testBy + bh, 1200.0f)) {
                        addMovementReason(MR_BUMPER);
                        activeBumperIndices.insert(objIdx);
                        audio.playBumperSfx();
                    }
                    break;
                }
            }
            if (showDebugView && (std::fabs(player.x - frameStartX) > 0.01f || std::fabs(player.y - frameStartY) > 0.01f)) {
                std::string reasonText;
                if (movementReasons & MR_FAST_TRAVEL) reasonText += (reasonText.empty() ? "" : ",") + std::string("fast_travel");
                if (movementReasons & MR_NORMAL_MOVEMENT) reasonText += (reasonText.empty() ? "" : ",") + std::string("normal_movement");
                if (movementReasons & MR_WORLD_WRAP) reasonText += (reasonText.empty() ? "" : ",") + std::string("world_wrap");
                if (movementReasons & MR_SPRING) reasonText += (reasonText.empty() ? "" : ",") + std::string("spring");
                if (movementReasons & MR_BUMPER) reasonText += (reasonText.empty() ? "" : ",") + std::string("bumper");
                if (reasonText.empty()) reasonText = "unknown";
                SDL_Log("player move: from=(%.2f, %.2f) to=(%.2f, %.2f) delta=(%.2f, %.2f) reason=%s",
                        frameStartX, frameStartY,
                        player.x, player.y,
                        player.x - frameStartX, player.y - frameStartY,
                        reasonText.c_str());
            }

            if (!levelCompleteActive && endSignState.present && !endSignState.triggered) {
                const float playerCenterX = player.x + player.w * 0.5f;
                const float prevPlayerCenterX = frameStartX + player.w * 0.5f;
                int triggerIdx = -1;
                float triggerX = 0.0f;
                float triggerY = 0.0f;
                float bestDxNow = 1e30f;
                for (int i = 0; i < (int)objects.size(); ++i) {
                    if (objects[i].id != "67") continue;
                    const float signX = objects[i].x;
                    const float signY = objects[i].y;
                    const float dxNow = signX - playerCenterX;
                    const float aboveDelta = player.y - signY;
                    const bool crossedSignX = prevPlayerCenterX < signX && playerCenterX >= signX;
                    const bool signNotFarLeft = signX >= prevPlayerCenterX - 8.0f;
                    const bool signNotFarAbove = aboveDelta <= 220.0f;
                    const bool signLikelyVisible = dxNow <= (float)kGameplayViewW * 0.95f;
                    if (!crossedSignX || !signNotFarLeft || !signNotFarAbove || !signLikelyVisible) continue;
                    if (dxNow < bestDxNow) {
                        bestDxNow = dxNow;
                        triggerIdx = i;
                        triggerX = signX;
                        triggerY = signY;
                    }
                }
                if (triggerIdx >= 0) {
                    endSignState.objectIndex = triggerIdx;
                    endSignState.objectX = triggerX;
                    endSignState.objectY = triggerY;
                    endSignState.triggered = true;
                    endSignState.phase = EndSignPhase::SignForward;
                    endSignState.frameTimer = 0.0f;
                    endSignCameraLocked = false; // recapture stable X lock at trigger time
                    cameraSmoothingSuppressTimer = std::max(cameraSmoothingSuppressTimer, 0.12f);
                }
            }

            // Fallback for levels without end_sign object.
            if (!levelCompleteActive && !endSignState.present &&
                playerTouchesTileId(map, player, 30, 30, gameplayWrapX, gameplayWrapY)) {
                startLevelCompleteSequence();
            }
            updatePlayerAnimState(inputMove, inputDown, dt);
        }

        RENDER_ONLY:
        {
            const int targetPresentIntervalMs =
                (powerManagementEnabled && mainWindowMinimized) ? 250 :
                (powerManagementEnabled && (!mainWindowFocused || paused)) ? 33 : 16;
            const Uint32 renderNow = SDL_GetTicks();
            if (renderNow < nextPresentTicks) {
                // Skip rendering on this tick so simulation updates can run independently.
                SDL_Delay(1);
                continue;
            }
        }

        const bool renderWrapX = true;
        const bool renderWrapY = true;
        const int renderLevelId = currentLevelId;
        const bool cameraWrapX = (renderLevelId == 39 || renderLevelId == 40);
        bool cameraWrapY = false;
        if (((renderLevelId == 29 &&
            player.x > 1250.0f) || (renderLevelId == 30 &&
            player.x > 1250.0f) || renderLevelId == 39 ||
             renderLevelId == 40 || renderLevelId == 53 || renderLevelId == 54)) {
            cameraWrapY = true;
        }
        if ((renderLevelId == 21 || renderLevelId == 22 || renderLevelId == 23 || renderLevelId == 24) &&
            player.x > 3211.0f && player.x < 4559.0f) {
            cameraWrapY = true;
        }

        const int worldViewW = kGameplayViewW;
        const int worldViewH = kGameplayViewH;
        const bool renderSecretBossLevel = (renderLevelId == 59);
        const bool lockCameraToEndSign =
            endSignState.triggered &&
            endSignState.objectIndex >= 0 &&
            endSignState.phase != EndSignPhase::Done;
        const bool forceBossCamera =
            bossState.active &&
            (renderSecretBossLevel ||
             bossState.sourceWorld == 1 ||
             bossState.sourceWorld == 2 ||
             bossState.sourceWorld == 4 ||
             bossState.sourceWorld == 5 ||
             bossState.sourceWorld == 7 ||
             (bossState.sourceWorld == 3 && bossState.phase == 3));
        float forcedBossCameraX = 1170.0f;
        float forcedBossCameraY = 132.0f;
        if (renderSecretBossLevel) {
            forcedBossCameraX = (float)(map.w * map.tileSize) * 0.5f;
            forcedBossCameraY = (float)(map.h * map.tileSize) * 0.5f;
        } else if (bossState.sourceWorld == 2) {
            forcedBossCameraX = 1887.0f;
            forcedBossCameraY = 744.0f;
        } else if (bossState.sourceWorld == 4) {
            forcedBossCameraX = 1315.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 202.0f + kGameplayViewH * 0.5f;
        } else if (bossState.sourceWorld == 5) {
            forcedBossCameraX = 5728.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 2832.0f + kGameplayViewH * 0.5f;
        } else if (bossState.sourceWorld == 7) {
            forcedBossCameraX = 96.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 32.0f + kGameplayViewH * 0.5f;
        } else if (bossState.sourceWorld == 3) {
            forcedBossCameraX = 2528.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 976.0f + kGameplayViewH * 0.5f;
        }
        const bool forceBossCameraActive = forceBossCamera && !lockCameraToEndSign;
        const float cameraTargetX = forceBossCameraActive
            ? forcedBossCameraX
            : (lockCameraToEndSign ? endSignState.objectX : (player.x + player.w * 0.5f));
        const float cameraTargetY = forceBossCameraActive
            ? forcedBossCameraY
            : (player.y + player.h * 0.5f);
        const float freeCamX = cameraTargetX - worldViewW * 0.5f;
        float camX = freeCamX;
        const float freeCamY = cameraTargetY - worldViewH * 0.5f;
        float camY = freeCamY;
        float maxCamX = map.w * map.tileSize - worldViewW - map.tileSize;
        float maxCamY = map.h * map.tileSize - worldViewH;
        const float clampedCamX = std::clamp(freeCamX, (float)map.tileSize, std::max((float)map.tileSize, maxCamX));
        const float clampXTarget = cameraWrapX ? 0.0f : 1.0f;
        const float clampLerpSpeed = 7.5f;
        const float clampBlendStep = 1.0f - std::exp(-clampLerpSpeed * std::max(0.0f, dt));
        if (cameraSmoothingSuppressTimer > 0.0f) {
            camXClampBlend = clampXTarget;
        } else {
            camXClampBlend += (clampXTarget - camXClampBlend) * clampBlendStep;
        }
        camX = freeCamX * (1.0f - camXClampBlend) + clampedCamX * camXClampBlend;
        const float clampedCamY = std::clamp(freeCamY, (float)map.tileSize, std::max((float)map.tileSize, maxCamY));
        const float clampTarget = cameraWrapY ? 0.0f : 1.0f;
        if (cameraSmoothingSuppressTimer > 0.0f) {
            camYClampBlend = clampTarget;
        } else {
            camYClampBlend += (clampTarget - camYClampBlend) * clampBlendStep;
        }
        camY = freeCamY * (1.0f - camYClampBlend) + clampedCamY * camYClampBlend;
        if (lockCameraToEndSign) {
            if (!endSignCameraLocked) {
                endSignCameraLocked = true;
                endSignCameraX = camX;
            }
            camX = endSignCameraX;
        } else {
            endSignCameraLocked = false;
        }
        if (levelCompleteActive) {
            if (!levelCompleteCameraLocked) {
                levelCompleteCameraLocked = true;
                levelCompleteCameraX = camX;
                levelCompleteCameraY = camY;
            }
            camX = levelCompleteCameraX;
            camY = levelCompleteCameraY;
        } else {
            levelCompleteCameraLocked = false;
        }
        // Snap render camera to pixel grid to avoid subpixel shimmer across all worlds.
        camX = std::round(camX);
        camY = std::round(camY);

        SDL_SetRenderTarget(ren, worldTarget);
        const int currentWorldId = levelManager.worldId();
        if (currentWorldId == 5) {
            const SDL_Color top{0x66, 0xea, 0xff, 0xff};    // #66eaff
            const SDL_Color bottom{0xc0, 0x68, 0x72, 0xff}; // #c06872
            for (int y = 0; y < worldViewH; ++y) {
                const float t = (worldViewH > 1) ? ((float)y / (float)(worldViewH - 1)) : 0.0f;
                const Uint8 r = (Uint8)std::lround((float)top.r + ((float)bottom.r - (float)top.r) * t);
                const Uint8 g = (Uint8)std::lround((float)top.g + ((float)bottom.g - (float)top.g) * t);
                const Uint8 b = (Uint8)std::lround((float)top.b + ((float)bottom.b - (float)top.b) * t);
                SDL_SetRenderDrawColor(ren, r, g, b, 255);
                SDL_RenderLine(ren, 0.0f, (float)y, (float)worldViewW, (float)y);
            }
        } else {
            SDL_SetRenderDrawColor(ren, 221, 248, 255, 255); // #ddf8ff
            SDL_RenderClear(ren);
        }

        // World 3 uses a full-screen block pattern from DF_Blocks (3.1..3.10).
        const bool renderedWorld3PatternBg = (currentWorldId == 3) &&
            RenderWorld3PatternBackground(
                ren,
                blocksTex,
                world3PatternFrames,
                currentLevelId,
                map.tileSize,
                worldViewW,
                worldViewH,
                camX,
                camY
            );

        // World 6 uses an animated full-screen background instead of parallax.
        bool renderedWorld6AnimatedBg = false;
        if (currentWorldId == 6 && bgTexWorld6 && !bgAnimFramesWorld6.empty()) {
            const int animIndex = (int)((SDL_GetTicks() / 120) % (Uint32)bgAnimFramesWorld6.size());
            const Frame& animFrame = bgAnimFramesWorld6[animIndex];
            const SDL_Rect fullDst{0, 0, worldViewW, worldViewH};
            renderFrame(ren, bgTexWorld6, animFrame, fullDst);
            renderedWorld6AnimatedBg = true;
        }
        if (!renderedWorld3PatternBg && !renderedWorld6AnimatedBg) {
            RenderParallaxBackground(
                ren,
                currentWorldId,
                camX,
                camY,
                map.w,
                map.h,
                map.tileSize,
                worldViewW,
                worldViewH,
                parallaxLayerScales,
                ParallaxWorldAssets{bgTexWorld1, &bgFrameByNameWorld1, &bgFrameListWorld1},
                ParallaxWorldAssets{bgTexWorld2, &bgFrameByNameWorld2, &bgFrameListWorld2},
                ParallaxWorldAssets{bgTexWorld4, &bgFrameByNameWorld4, &bgFrameListWorld4},
                ParallaxWorldAssets{bgTexWorld5, &bgFrameByNameWorld5, &bgFrameListWorld5}
            );
        }
        if (currentWorldId == 5) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            const SDL_Color top{0x66, 0xea, 0xff, 0x58};    // #66eaff
            const SDL_Color bottom{0xc0, 0x68, 0x72, 0x72}; // #c06872
            for (int y = 0; y < worldViewH; ++y) {
                const float t = (worldViewH > 1) ? ((float)y / (float)(worldViewH - 1)) : 0.0f;
                const Uint8 r = (Uint8)std::lround((float)top.r + ((float)bottom.r - (float)top.r) * t);
                const Uint8 g = (Uint8)std::lround((float)top.g + ((float)bottom.g - (float)top.g) * t);
                const Uint8 b = (Uint8)std::lround((float)top.b + ((float)bottom.b - (float)top.b) * t);
                const Uint8 a = (Uint8)std::lround((float)top.a + ((float)bottom.a - (float)top.a) * t);
                SDL_SetRenderDrawColor(ren, r, g, b, a);
                SDL_RenderLine(ren, 0.0f, (float)y, (float)worldViewW, (float)y);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        map.renderBgDebug(ren, camX, camY);

        const int tileMinX = renderWrapX
            ? ((int)std::floor(camX / map.tileSize) - 1)
            : std::max(0, (int)std::floor(camX / map.tileSize) - 1);
        const int tileMaxX = renderWrapX
            ? ((int)std::floor((camX + worldViewW) / map.tileSize) + 1)
            : std::min(map.w - 1, (int)std::floor((camX + worldViewW) / map.tileSize) + 1);
        const int tileMinY = renderWrapY
            ? ((int)std::floor(camY / map.tileSize) - 1)
            : std::max(0, (int)std::floor(camY / map.tileSize) - 1);
        const int tileMaxY = renderWrapY
            ? ((int)std::floor((camY + worldViewH) / map.tileSize) + 1)
            : std::min(map.h - 1, (int)std::floor((camY + worldViewH) / map.tileSize) + 1);

        // Tile textures (DF_Blocks)
        if (blocksTex && !blocksFrameList.empty()) {
            auto frameByName = [&](const std::string& key) -> const Frame* {
                auto it = blocksFrameByName.find(key);
                if (it != blocksFrameByName.end()) return &it->second;
                auto itPng = blocksFrameByName.find(key + ".png");
                if (itPng != blocksFrameByName.end()) return &itPng->second;
                return nullptr;
            };
            const float tileSizeF = (float)map.tileSize;
            const int cycleIndex = (int)((SDL_GetTicks() / 300) % 8);
            const int tileStartY = renderWrapY ? tileMinY : std::max(1, tileMinY);
            for (int y = tileStartY; y <= tileMaxY; y++) {
                int mapY = y;
                if (renderWrapY) {
                    mapY %= map.h;
                    if (mapY < 0) mapY += map.h;
                }
                const int row = mapY * map.w;
                const int screenY = (int)(y * tileSizeF - camY);
                for (int x = tileMinX; x <= tileMaxX; x++) {
                    int mapX = x;
                    if (renderWrapX) {
                        mapX %= map.w;
                        if (mapX < 0) mapX += map.w;
                    }
                    const unsigned short id = map.tileIds[row + mapX];
                    if (id == 0) continue;

                    const Frame* frame = nullptr;
                    if (id == 24) {
                        frame = cycleFrames[cycleIndex];
                    } else {
                        auto tileIt = tileFrameById.find((int)id);
                        if (tileIt != tileFrameById.end()) {
                            frame = frameByName(tileIt->second);
                        }
                    }
                    if (!frame && levelManager.worldId() == 3 && id >= 3 && id <= 11) {
                        const unsigned int h = (unsigned int)mapX * 73856093u ^
                                               (unsigned int)mapY * 19349663u ^
                                               (unsigned int)id * 83492791u;
                        const int v = (int)(h % 10u) + 1; // 1..10
                        const std::string key = std::string("3.") + std::to_string(v);
                        frame = frameByName(key);
                    }
                    if (!frame) {
                        frame = blocksFrameById[id];
                    }
                    if (!frame) continue;

                    SDL_Rect dst{(int)(x * tileSizeF - camX), screenY, map.tileSize, map.tileSize};
                    renderFrame(ren, blocksTex, *frame, dst);
                }
            }
        } else {
            map.renderWater(ren, camX, camY);
            map.renderSemiSolid(ren, camX, camY);
            map.renderSolid(ren, camX, camY);
        }

        std::string debugAnimName = "IDLE";
        std::string renderFrameName;
        int renderAnimFrameIndex = -1;
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

        if (showHitboxes || showPlayerHitbox) {
            if (showHitboxes) {
                // Tile hitboxes
                solidHitboxes.clear();
                semiHitboxes.clear();
                waterHitboxes.clear();
                airDebugHitboxes.clear();
                int visibleTiles = (tileMaxX - tileMinX + 1) * (tileMaxY - tileMinY + 1);
                if ((int)solidHitboxes.capacity() < visibleTiles) solidHitboxes.reserve(visibleTiles);
                if ((int)semiHitboxes.capacity() < visibleTiles) semiHitboxes.reserve(visibleTiles);
                if ((int)waterHitboxes.capacity() < visibleTiles) waterHitboxes.reserve(visibleTiles);
                if ((int)airDebugHitboxes.capacity() < visibleTiles) airDebugHitboxes.reserve(visibleTiles);
                const float tileSizeF = (float)map.tileSize;
                constexpr int kUpdateThreads = 3;
                struct HitboxBuckets {
                    std::vector<SDL_FRect> solid;
                    std::vector<SDL_FRect> semi;
                    std::vector<SDL_FRect> water;
                    std::vector<SDL_FRect> air;
                };
                std::array<HitboxBuckets, kUpdateThreads> buckets;
                std::array<std::thread, kUpdateThreads> workers;
                const int totalRows = tileMaxY - tileMinY + 1;
                for (int t = 0; t < kUpdateThreads; ++t) {
                    workers[t] = std::thread([&, t]() {
                        int yStart = tileMinY + (totalRows * t) / kUpdateThreads;
                        int yEnd = tileMinY + (totalRows * (t + 1)) / kUpdateThreads - 1;
                        if (yStart > yEnd) return;
                        int rows = yEnd - yStart + 1;
                        int cap = rows * (tileMaxX - tileMinX + 1);
                        buckets[t].solid.reserve(cap / 2);
                        buckets[t].semi.reserve(cap / 4);
                        buckets[t].water.reserve(cap / 4);
                        buckets[t].air.reserve(cap / 4);
                        for (int y = yStart; y <= yEnd; y++) {
                            const float screenY = y * tileSizeF - camY;
                            int mapY = y;
                            if (renderWrapY) {
                                mapY %= map.h;
                                if (mapY < 0) mapY += map.h;
                            }
                            const int row = mapY * map.w;
                            for (int x = tileMinX; x <= tileMaxX; x++) {
                                int mapX = x;
                                if (renderWrapX) {
                                    mapX %= map.w;
                                    if (mapX < 0) mapX += map.w;
                                }
                                int idx = row + mapX;
                                const bool isSolid = map.solid[idx] != 0;
                                const bool isSemi = map.semisolid[idx] != 0;
                                const bool isWater = map.water[idx] != 0;
                                if (!isSolid && !isSemi && !isWater && map.tileIds[idx] == 2) continue;

                                SDL_FRect rc{ x * tileSizeF - camX, screenY, tileSizeF, tileSizeF };
                                if (isSolid) {
                                    buckets[t].solid.push_back(rc);
                                    continue;
                                }
                                if (isSemi) {
                                    buckets[t].semi.push_back(rc);
                                    continue;
                                }
                                if (isWater) {
                                    buckets[t].water.push_back(rc);
                                    continue;
                                }
                                int id = (int)map.tileIds[idx];
                                if (id != 2) buckets[t].air.push_back(rc);
                            }
                        }
                    });
                }
                for (auto& th : workers) th.join();
                for (int t = 0; t < kUpdateThreads; ++t) {
                    solidHitboxes.insert(solidHitboxes.end(), buckets[t].solid.begin(), buckets[t].solid.end());
                    semiHitboxes.insert(semiHitboxes.end(), buckets[t].semi.begin(), buckets[t].semi.end());
                    waterHitboxes.insert(waterHitboxes.end(), buckets[t].water.begin(), buckets[t].water.end());
                    airDebugHitboxes.insert(airDebugHitboxes.end(), buckets[t].air.begin(), buckets[t].air.end());
                }
                if (!solidHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
                    SDL_RenderDrawRectsF(ren, solidHitboxes.data(), (int)solidHitboxes.size());
                }
                if (!semiHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 120, 220, 255, 255);
                    SDL_RenderDrawRectsF(ren, semiHitboxes.data(), (int)semiHitboxes.size());
                }
                if (!waterHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 60, 120, 220, 255);
                    SDL_RenderDrawRectsF(ren, waterHitboxes.data(), (int)waterHitboxes.size());
                }
                if (!airDebugHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 180, 180, 180, 255);
                    SDL_RenderDrawRectsF(ren, airDebugHitboxes.data(), (int)airDebugHitboxes.size());
                }
            }

            // Player hitbox
            if (showPlayerHitbox) {
                SDL_SetRenderDrawColor(ren, 255, 200, 80, 255);
                float playerHitboxScreenY = player.y - camY;
                if (playerHitboxScreenY < 0.0f) playerHitboxScreenY = 0.0f;
                SDL_FRect pr{ player.x - camX, playerHitboxScreenY, (float)player.w, (float)player.h };
                SDL_RenderDrawRectF(ren, &pr);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect prTop{ pr.x, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prBottom{ pr.x, pr.y + wrapH, pr.w, pr.h };
                    SDL_RenderDrawRectF(ren, &prTop);
                    SDL_RenderDrawRectF(ren, &prBottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect prLeft{ pr.x - wrapW, pr.y, pr.w, pr.h };
                    SDL_FRect prRight{ pr.x + wrapW, pr.y, pr.w, pr.h };
                    SDL_RenderDrawRectF(ren, &prLeft);
                    SDL_RenderDrawRectF(ren, &prRight);
                }
            }
        }


        for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
            const auto& obj = objects[objIdx];
            int objId = 0;
            try { objId = std::stoi(obj.id); } catch (...) { objId = 0; }
            const bool isFastTravelChanger = (objId >= 57 && objId <= 61);
            const bool isBumper = (obj.id == "46");
            const bool isEndSign = (obj.id == "67");
            float entityBaseX = obj.x - 16.0f;
            float entityBaseY = obj.y - 16.0f;
            if (renderWrapX) {
                const float wrapW = (float)(map.w * map.tileSize);
                // Keep the primary entity render closest to camera X for stable wrapping.
                while ((entityBaseX - camX) < -wrapW * 0.5f) entityBaseX += wrapW;
                while ((entityBaseX - camX) >  wrapW * 0.5f) entityBaseX -= wrapW;
            }
            if (renderWrapY) {
                const float wrapH = (float)(map.h * map.tileSize);
                // Keep the primary entity render closest to camera Y for stable wrapping.
                while ((entityBaseY - camY) < -wrapH * 0.5f) entityBaseY += wrapH;
                while ((entityBaseY - camY) >  wrapH * 0.5f) entityBaseY -= wrapH;
            }
            const Frame* of = nullptr;
            SDL_Texture* objectTex = entitiesTex;
            bool objectTypeKnown = false;
            if (isEndSign) {
                objectTex = endSignTex;
                std::string key = "SignPost9";
                if (endSignState.present && endSignState.triggered) {
                    if (endSignState.phase == EndSignPhase::PlayerForward ||
                        endSignState.phase == EndSignPhase::PlayerBackward ||
                        endSignState.phase == EndSignPhase::TriggerComplete ||
                        endSignState.phase == EndSignPhase::Done) {
                        key = std::string("SignPostPlayer1.") + std::to_string(std::clamp(endSignState.playerFrame, 1, 7));
                    } else {
                        key = std::string("SignPost") + std::to_string(std::clamp(endSignState.signFrame, 9, 15));
                    }
                }
                auto sit = endSignFrames.find(key);
                if (sit == endSignFrames.end()) sit = endSignFrames.find(key + ".png");
                if (sit != endSignFrames.end()) {
                    of = &sit->second;
                    objectTypeKnown = true;
                }
            } else {
                std::string frameKey = obj.id;
                if (obj.id == "46" && activeBumperIndices.find(objIdx) != activeBumperIndices.end()) {
                    frameKey = "Bumper";
                }
                auto mapIt = entityFrameKeyByObjectId.find(obj.id);
                if (mapIt != entityFrameKeyByObjectId.end()) frameKey = mapIt->second;

                auto it = entitiesFrameByName.find(frameKey);
                if (it != entitiesFrameByName.end()) {
                    of = &it->second;
                    objectTypeKnown = true;
                } else {
                    std::string pngKey = frameKey + ".png";
                    it = entitiesFrameByName.find(pngKey);
                    if (it != entitiesFrameByName.end()) {
                        of = &it->second;
                        objectTypeKnown = true;
                    }
                }
            }
            if (!of && isEndSign) of = defaultEndSignFrame;
            if (!of) of = defaultEntityFrame;
            if (!isFastTravelChanger && !isBumper && !isEndSign && hideUnknownObjectTypes && !objectTypeKnown) {
                continue;
            }

            if (!isFastTravelChanger && objectTex && of) {
                int fw = 32;
                int fh = 32;
                int dstX = (int)std::lround(entityBaseX - camX);
                int dstY = (int)std::lround(entityBaseY - camY);
                if (isEndSign) {
                    const int srcW = of->rotated ? of->rect.h : of->rect.w;
                    const int srcH = of->rotated ? of->rect.w : of->rect.h;
                    // Use a stable world-scale size for signpost frames.
                    const int targetH = 70;
                    fh = targetH;
                    fw = std::max(18, (int)std::lround((srcW / (double)std::max(1, srcH)) * targetH));
                    // Anchor end_sign to object center X and tile-bottom Y for consistent visibility.
                    const float objectCenterScreenX = obj.x - camX;
                    const float objectBottomScreenY = (obj.y - 16.0f) - camY + 32.0f;
                    dstX = (int)std::lround(objectCenterScreenX - fw * 0.5f);
                    dstY = (int)std::lround(objectBottomScreenY - fh);
                }
                SDL_Rect dst{
                    dstX,
                    dstY,
                    fw,
                    fh
                };
                auto isOnscreen = [&](const SDL_Rect& r) -> bool {
                    if (r.x + r.w <= 0 || r.y + r.h <= 0) return false;
                    if (r.x >= worldViewW || r.y >= worldViewH) return false;
                    return true;
                };
                auto renderObjFrame = [&](const SDL_Rect& outDst) {
                    if (isEndSign && !isOnscreen(outDst)) return;
                    renderFrame(ren, objectTex, *of, outDst);
                };
                renderObjFrame(dst);
                if (renderWrapY && !isEndSign) {
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTop = dst;
                    SDL_Rect dstBottom = dst;
                    dstTop.y -= wrapH;
                    dstBottom.y += wrapH;
                    renderObjFrame(dstTop);
                    renderObjFrame(dstBottom);
                }
                if (renderWrapX && !isEndSign) {
                    const int wrapW = map.w * map.tileSize;
                    SDL_Rect dstLeft = dst;
                    SDL_Rect dstRight = dst;
                    dstLeft.x -= wrapW;
                    dstRight.x += wrapW;
                    renderObjFrame(dstLeft);
                    renderObjFrame(dstRight);
                }
                if (renderWrapY && renderWrapX && !isEndSign) {
                    const int wrapW = map.w * map.tileSize;
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTL = dst;
                    SDL_Rect dstTR = dst;
                    SDL_Rect dstBL = dst;
                    SDL_Rect dstBR = dst;
                    dstTL.x -= wrapW; dstTL.y -= wrapH;
                    dstTR.x += wrapW; dstTR.y -= wrapH;
                    dstBL.x -= wrapW; dstBL.y += wrapH;
                    dstBR.x += wrapW; dstBR.y += wrapH;
                    renderObjFrame(dstTL);
                    renderObjFrame(dstTR);
                    renderObjFrame(dstBL);
                    renderObjFrame(dstBR);
                }
            } else if (!isFastTravelChanger) {
                SDL_SetRenderDrawColor(ren, 120, 220, 120, 255);
                SDL_FRect orc{ entityBaseX - camX, entityBaseY - camY, 32.0f, 32.0f };
                SDL_RenderDrawRectF(ren, &orc);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect top{orc.x, orc.y - wrapH, orc.w, orc.h};
                    SDL_FRect bottom{orc.x, orc.y + wrapH, orc.w, orc.h};
                    SDL_RenderDrawRectF(ren, &top);
                    SDL_RenderDrawRectF(ren, &bottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect left{orc.x - wrapW, orc.y, orc.w, orc.h};
                    SDL_FRect right{orc.x + wrapW, orc.y, orc.w, orc.h};
                    SDL_RenderDrawRectF(ren, &left);
                    SDL_RenderDrawRectF(ren, &right);
                }
                if (renderWrapY && renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect tl{orc.x - wrapW, orc.y - wrapH, orc.w, orc.h};
                    SDL_FRect tr{orc.x + wrapW, orc.y - wrapH, orc.w, orc.h};
                    SDL_FRect bl{orc.x - wrapW, orc.y + wrapH, orc.w, orc.h};
                    SDL_FRect br{orc.x + wrapW, orc.y + wrapH, orc.w, orc.h};
                    SDL_RenderDrawRectF(ren, &tl);
                    SDL_RenderDrawRectF(ren, &tr);
                    SDL_RenderDrawRectF(ren, &bl);
                    SDL_RenderDrawRectF(ren, &br);
                }
            }

            if (showHitboxes) {
                if (isFastTravelChanger) SDL_SetRenderDrawColor(ren, 220, 60, 220, 255);
                else SDL_SetRenderDrawColor(ren, 255, 150, 60, 255);
                SDL_FRect ehb{
                    entityBaseX - camX,
                    entityBaseY - camY,
                    32.0f,
                    32.0f
                };
                SDL_RenderDrawRectF(ren, &ehb);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect top{ehb.x, ehb.y - wrapH, ehb.w, ehb.h};
                    SDL_FRect bottom{ehb.x, ehb.y + wrapH, ehb.w, ehb.h};
                    SDL_RenderDrawRectF(ren, &top);
                    SDL_RenderDrawRectF(ren, &bottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect left{ehb.x - wrapW, ehb.y, ehb.w, ehb.h};
                    SDL_FRect right{ehb.x + wrapW, ehb.y, ehb.w, ehb.h};
                    SDL_RenderDrawRectF(ren, &left);
                    SDL_RenderDrawRectF(ren, &right);
                }

                const int idScale = 1;
                DrawText(ren, (int)std::lround(ehb.x), (int)std::lround(ehb.y) - 10, idScale, obj.id);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    DrawText(ren, (int)std::lround(ehb.x), (int)std::lround(ehb.y - wrapH) - 10, idScale, obj.id);
                    DrawText(ren, (int)std::lround(ehb.x), (int)std::lround(ehb.y + wrapH) - 10, idScale, obj.id);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    DrawText(ren, (int)std::lround(ehb.x - wrapW), (int)std::lround(ehb.y) - 10, idScale, obj.id);
                    DrawText(ren, (int)std::lround(ehb.x + wrapW), (int)std::lround(ehb.y) - 10, idScale, obj.id);
                }
            }
        }

        if (bossState.active && bossesTex && bossNormalFrame) {
            const Frame* bf = (bossState.hurtFlash > 0.0f && bossHurtFrame) ? bossHurtFrame : bossNormalFrame;
            if (bossState.sourceWorld == 7 && bossFinalNormalFrame) {
                bf = bossFinalNormalFrame;
            }
            const bool bossUsesFinalAnimation = (bossState.sourceWorld == 7);
            int dstW = bossUsesFinalAnimation ? 56 : 28;
            int dstH = bossUsesFinalAnimation ? 56 : 28;
            if (bossUsesFinalAnimation && bf) {
                const int srcW = bf->rotated ? bf->rect.h : bf->rect.w;
                const int srcH = bf->rotated ? bf->rect.w : bf->rect.h;
                const double safeW = (double)std::max(1, srcW);
                const double safeH = (double)std::max(1, srcH);
                const double scale = std::max(56.0 / safeW, 56.0 / safeH); // cover 56x56 hitbox
                dstW = std::max(56, (int)std::lround(safeW * scale));
                dstH = std::max(56, (int)std::lround(safeH * scale));
            }
            SDL_Rect dst{
                (int)std::lround((bossState.x - dstW * 0.5f) - camX),
                (int)std::lround((bossState.y - dstH * 0.5f) - camY),
                dstW,
                dstH
            };
            if (!(dst.x + dst.w <= 0 || dst.y + dst.h <= 0 || dst.x >= worldViewW || dst.y >= worldViewH)) {
                if (bossState.sourceWorld == 4 && bossState.rainbowTimer > 0.0f) {
                    const float t = (float)((double)SDL_GetTicksNS() / 1000000000.0 * 6.5);
                    const int r = (int)std::lround(127.0 + 128.0 * std::sin(t));
                    const int g = (int)std::lround(127.0 + 128.0 * std::sin(t + 2.0943951f));
                    const int b = (int)std::lround(127.0 + 128.0 * std::sin(t + 4.1887902f));
                    SDL_SetTextureColorMod(bossesTex, (Uint8)std::clamp(r, 0, 255), (Uint8)std::clamp(g, 0, 255), (Uint8)std::clamp(b, 0, 255));
                } else {
                    SDL_SetTextureColorMod(bossesTex, 255, 255, 255);
                }
                renderFrame(ren, bossesTex, *bf, dst);
                SDL_SetTextureColorMod(bossesTex, 255, 255, 255);
                if (showHitboxes) {
                    SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
                    SDL_FRect hb{(float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h};
                    SDL_RenderDrawRectF(ren, &hb);
                    if (renderWrapY) {
                        const float wrapH = (float)(map.h * map.tileSize);
                        SDL_FRect top{hb.x, hb.y - wrapH, hb.w, hb.h};
                        SDL_FRect bottom{hb.x, hb.y + wrapH, hb.w, hb.h};
                        SDL_RenderDrawRectF(ren, &top);
                        SDL_RenderDrawRectF(ren, &bottom);
                    }
                    if (renderWrapX) {
                        const float wrapW = (float)(map.w * map.tileSize);
                        SDL_FRect left{hb.x - wrapW, hb.y, hb.w, hb.h};
                        SDL_FRect right{hb.x + wrapW, hb.y, hb.w, hb.h};
                        SDL_RenderDrawRectF(ren, &left);
                        SDL_RenderDrawRectF(ren, &right);
                    }
                    if (renderWrapY && renderWrapX) {
                        const float wrapW = (float)(map.w * map.tileSize);
                        const float wrapH = (float)(map.h * map.tileSize);
                        SDL_FRect tl{hb.x - wrapW, hb.y - wrapH, hb.w, hb.h};
                        SDL_FRect tr{hb.x + wrapW, hb.y - wrapH, hb.w, hb.h};
                        SDL_FRect bl{hb.x - wrapW, hb.y + wrapH, hb.w, hb.h};
                        SDL_FRect br{hb.x + wrapW, hb.y + wrapH, hb.w, hb.h};
                        SDL_RenderDrawRectF(ren, &tl);
                        SDL_RenderDrawRectF(ren, &tr);
                        SDL_RenderDrawRectF(ren, &bl);
                        SDL_RenderDrawRectF(ren, &br);
                    }
                }
                if (showDebugView) {
                    const std::string hpText = std::string("BOSS HP: ") + std::to_string(bossState.health) + "/" + std::to_string(bossState.maxHealth);
                    DrawText(ren, std::max(8, dst.x - 8), std::max(8, dst.y - 18), 1, hpText);
                }
            }
        }
        if (!secretFireballs.empty() || !secretExplosions.empty()) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            for (const auto& fb : secretFireballs) {
                const SDL_FRect r{
                    fb.x - camX - 5.0f,
                    fb.y - camY - 5.0f,
                    10.0f,
                    10.0f
                };
                SDL_SetRenderDrawColor(ren, 255, 110, 20, 255);
                SDL_RenderFillRectF(ren, &r);
            }
            for (const auto& ex : secretExplosions) {
                const float d = ex.radius * 2.0f;
                const SDL_FRect r{
                    ex.x - camX - ex.radius,
                    ex.y - camY - ex.radius,
                    d,
                    d
                };
                const float t = std::clamp(ex.life / 0.24f, 0.0f, 1.0f);
                const Uint8 a = (Uint8)std::lround(200.0f * t);
                SDL_SetRenderDrawColor(ren, 255, 170, 70, a);
                SDL_RenderFillRectF(ren, &r);
                SDL_SetRenderDrawColor(ren, 255, 235, 130, a);
                SDL_RenderDrawRectF(ren, &r);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        if (!droppedCoins.empty()) {
            const int cycleIndex = (int)((SDL_GetTicks() / 300) % 8);
            const Frame* coinFrame = cycleFrames[cycleIndex];
            for (const auto& c : droppedCoins) {
                SDL_Rect dst{
                    (int)std::lround(c.x - 8.0f - camX),
                    (int)std::lround(c.y - 8.0f - camY),
                    16, 16
                };
                if (dst.x + dst.w <= 0 || dst.y + dst.h <= 0 || dst.x >= worldViewW || dst.y >= worldViewH) continue;
                if (blocksTex && coinFrame) {
                    renderFrame(ren, blocksTex, *coinFrame, dst);
                } else {
                    SDL_SetRenderDrawColor(ren, 255, 220, 40, 255);
                    SDL_RenderFillRect(ren, &dst);
                }
            }
        }

        if (showDemoPath && !demoState.pathTiles.empty()) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            for (size_t i = 0; i < demoState.pathTiles.size(); ++i) {
                const SDL_Point p = demoState.pathTiles[i];
                const bool completed = i < demoState.waypointIndex;
                SDL_SetRenderDrawColor(ren, completed ? 70 : 80, completed ? 80 : 240, completed ? 95 : 120, completed ? 110 : 190);
                SDL_FRect tileRect{
                    p.x * (float)map.tileSize - camX + 4.0f,
                    p.y * (float)map.tileSize - camY + 4.0f,
                    (float)map.tileSize - 8.0f,
                    (float)map.tileSize - 8.0f
                };
                SDL_RenderDrawRectF(ren, &tileRect);
                if (i > 0) {
                    const SDL_Point prev = demoState.pathTiles[i - 1];
                    const bool segmentCompleted = (i - 1) < demoState.waypointIndex;
                    SDL_SetRenderDrawColor(ren, segmentCompleted ? 70 : 80, segmentCompleted ? 80 : 240, segmentCompleted ? 95 : 120, segmentCompleted ? 95 : 180);
                    const float x1 = prev.x * (float)map.tileSize + map.tileSize * 0.5f - camX;
                    const float y1 = prev.y * (float)map.tileSize + map.tileSize * 0.5f - camY;
                    const float x2 = p.x * (float)map.tileSize + map.tileSize * 0.5f - camX;
                    const float y2 = p.y * (float)map.tileSize + map.tileSize * 0.5f - camY;
                    SDL_RenderLine(ren, x1, y1, x2, y2);
                }
            }
            if (!demoState.pathTiles.empty()) {
                const SDL_Point start = demoState.pathTiles.front();
                const SDL_Point finish = demoState.pathTiles.back();
                SDL_SetRenderDrawColor(ren, 90, 180, 255, 220);
                SDL_FRect sRect{
                    start.x * (float)map.tileSize - camX + 1.0f,
                    start.y * (float)map.tileSize - camY + 1.0f,
                    (float)map.tileSize - 2.0f,
                    (float)map.tileSize - 2.0f
                };
                SDL_RenderDrawRectF(ren, &sRect);
                SDL_SetRenderDrawColor(ren, 255, 120, 90, 220);
                SDL_FRect fRect{
                    finish.x * (float)map.tileSize - camX + 1.0f,
                    finish.y * (float)map.tileSize - camY + 1.0f,
                    (float)map.tileSize - 2.0f,
                    (float)map.tileSize - 2.0f
                };
                SDL_RenderDrawRectF(ren, &fRect);
            }
            if (demoState.waypointIndex < demoState.pathTiles.size()) {
                const SDL_Point wp = demoState.pathTiles[demoState.waypointIndex];
                SDL_SetRenderDrawColor(ren, 255, 240, 80, 220);
                SDL_FRect wpRect{
                    wp.x * (float)map.tileSize - camX + 2.0f,
                    wp.y * (float)map.tileSize - camY + 2.0f,
                    (float)map.tileSize - 4.0f,
                    (float)map.tileSize - 4.0f
                };
                SDL_RenderDrawRectF(ren, &wpRect);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            DrawText(ren, 12, 82, 2, "PATH S->F");
        }

        SDL_FRect pr{ player.x - camX, player.y - camY, (float)player.w, (float)player.h };
        if (renderWrapX) {
            const float wrapW = (float)(map.w * map.tileSize);
            while (pr.x < -wrapW * 0.5f) pr.x += wrapW;
            while (pr.x >  wrapW * 0.5f) pr.x -= wrapW;
        }
        if (renderWrapY) {
            const float wrapH = (float)(map.h * map.tileSize);
            while (pr.y < -wrapH * 0.5f) pr.y += wrapH;
            while (pr.y >  wrapH * 0.5f) pr.y -= wrapH;
        }
        {
            const std::vector<const Frame*>* seq = &animIdleFrames;
            switch (player.anim) {
                case ANIM_WALK: seq = &animWalkFrames; break;
                case ANIM_JUMP: seq = &animJumpFrames; break;
                case ANIM_FALL: seq = &animFallFrames; break;
                case ANIM_CROUCH: seq = &animCrouchFrames; break;
                case ANIM_SKID: seq = &animSkidFrames; break;
                case ANIM_HURT: seq = &animHurtFrames; break;
                case ANIM_DEATH: seq = &animDeathFrames; break;
                default: seq = &animIdleFrames; break;
            }

            const Frame* renderFramePtr = nullptr;
            int frameCount = (int)seq->size();
            if (frameCount > 0) {
                float fps = animCfg.fps > 0.0f ? animCfg.fps : 20.0f;
                fps = std::max(20.0f, fps);
                int idx = (int)(player.animTime * fps) % frameCount;
                renderAnimFrameIndex = idx;
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
                if (playerInvincibleTimer > 0.0f) {
                    const float phase = (float)(SDL_GetTicksNS() / 1000000000.0) * 14.0f;
                    const float wave = 0.5f + 0.5f * std::sin(phase);
                    const Uint8 alpha = (Uint8)std::clamp((int)std::lround(95.0f + 160.0f * wave), 70, 255);
                    SDL_SetTextureAlphaMod(playerTex, alpha);
                } else {
                    SDL_SetTextureAlphaMod(playerTex, 255);
                }
                SDL_Rect dst{
                    (int)pr.x,
                    (int)(pr.y + pr.h - pr.h),
                    (int)pr.w,
                    (int)pr.h
                };
                SDL_RendererFlip flip = (player.facing < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                renderFrameEx(ren, playerTex, *renderFramePtr, dst, flip);
                if (renderWrapY) {
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTop = dst;
                    SDL_Rect dstBottom = dst;
                    dstTop.y -= wrapH;
                    dstBottom.y += wrapH;
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstTop, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstBottom, flip);
                }
                if (renderWrapX) {
                    const int wrapW = map.w * map.tileSize;
                    SDL_Rect dstLeft = dst;
                    SDL_Rect dstRight = dst;
                    dstLeft.x -= wrapW;
                    dstRight.x += wrapW;
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstLeft, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstRight, flip);
                }
                if (renderWrapY && renderWrapX) {
                    const int wrapW = map.w * map.tileSize;
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTL = dst;
                    SDL_Rect dstTR = dst;
                    SDL_Rect dstBL = dst;
                    SDL_Rect dstBR = dst;
                    dstTL.x -= wrapW; dstTL.y -= wrapH;
                    dstTR.x += wrapW; dstTR.y -= wrapH;
                    dstBL.x -= wrapW; dstBL.y += wrapH;
                    dstBR.x += wrapW; dstBR.y += wrapH;
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstTL, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstTR, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstBL, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstBR, flip);
                }
                SDL_SetTextureAlphaMod(playerTex, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 200, 200, 230, 255);
                SDL_RenderFillRectF(ren, &pr);
                SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                SDL_RenderDrawRectF(ren, &pr);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect prTop{ pr.x, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prBottom{ pr.x, pr.y + wrapH, pr.w, pr.h };
                    SDL_RenderFillRectF(ren, &prTop);
                    SDL_RenderFillRectF(ren, &prBottom);
                    SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                    SDL_RenderDrawRectF(ren, &prTop);
                    SDL_RenderDrawRectF(ren, &prBottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect prLeft{ pr.x - wrapW, pr.y, pr.w, pr.h };
                    SDL_FRect prRight{ pr.x + wrapW, pr.y, pr.w, pr.h };
                    SDL_RenderFillRectF(ren, &prLeft);
                    SDL_RenderFillRectF(ren, &prRight);
                    SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                    SDL_RenderDrawRectF(ren, &prLeft);
                    SDL_RenderDrawRectF(ren, &prRight);
                }
                if (renderWrapY && renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect prTL{ pr.x - wrapW, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prTR{ pr.x + wrapW, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prBL{ pr.x - wrapW, pr.y + wrapH, pr.w, pr.h };
                    SDL_FRect prBR{ pr.x + wrapW, pr.y + wrapH, pr.w, pr.h };
                    SDL_RenderFillRectF(ren, &prTL);
                    SDL_RenderFillRectF(ren, &prTR);
                    SDL_RenderFillRectF(ren, &prBL);
                    SDL_RenderFillRectF(ren, &prBR);
                    SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                    SDL_RenderDrawRectF(ren, &prTL);
                    SDL_RenderDrawRectF(ren, &prTR);
                    SDL_RenderDrawRectF(ren, &prBL);
                    SDL_RenderDrawRectF(ren, &prBR);
                }
            }
        }

        SDL_SetRenderTarget(ren, gameTarget);
        SDL_Rect worldDst{0, 0, screenW, screenH};
        SDL_RenderTexture(ren, worldTarget, nullptr, &worldDst);

        // HUD: coin/timer/score
        int hudSeconds = levelCompleteActive ? levelCompleteSnapshotSeconds : (int)levelTimerSeconds;
        int mins = hudSeconds / 60;
        int secs = hudSeconds % 60;
        std::ostringstream timerText;
        timerText << mins << "," << std::setw(2) << std::setfill('0') << secs;
        const int hudSlideOutX = -(int)std::lround(levelCompleteUiLerp * 260.0f);
        DrawText(ren, 16 + hudSlideOutX, 16, 2, "COINS");
        DrawText(ren, 94 + hudSlideOutX, 16, 2, std::to_string(levelManager.coinCount()));
        DrawText(ren, 16 + hudSlideOutX, 48, 2, "TIME");
        DrawText(ren, 94 + hudSlideOutX, 48, 2, timerText.str());
        DrawText(ren, 16 + hudSlideOutX, 80, 2, "SCORE");
        DrawText(ren, 94 + hudSlideOutX, 80, 2, std::to_string(scoreCount));

        // HUD: lives counter (bottom-left).
        DrawText(ren, 16 + hudSlideOutX, screenH - 32, 2, "LIVES");
        DrawText(ren, 94 + hudSlideOutX, screenH - 32, 2, std::to_string(livesCount));
        if (levelReloadTitleTimer > 0.0f) {
            const float showT = std::clamp(levelReloadTitleTimer / 2.0f, 0.0f, 1.0f);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 8, 10, 14, (Uint8)std::lround(140.0f * showT));
            SDL_FRect titleBackdrop{(float)(screenW / 2 - 300), 18.0f, 600.0f, 52.0f};
            SDL_RenderFillRect(ren, &titleBackdrop);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, (Uint8)std::lround(255.0f * std::min(1.0f, showT * 2.0f)));
            const int titleScale = 2;
            const int titleW = MeasureTextWidth(titleScale, currentFancyLevelName);
            DrawText(ren, screenW / 2 - titleW / 2, 32, titleScale, currentFancyLevelName);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        if (paused) {
            const float uiButtonScale = std::clamp((float)uiScalePercent / 100.0f, 0.5f, 2.0f);
            auto scaleRectCentered = [&](const SDL_Rect& in) -> SDL_Rect {
                const float cx = (float)in.x + (float)in.w * 0.5f;
                const float cy = (float)in.y + (float)in.h * 0.5f;
                const int nw = std::max(1, (int)std::lround((float)in.w * uiButtonScale));
                const int nh = std::max(1, (int)std::lround((float)in.h * uiButtonScale));
                return SDL_Rect{
                    (int)std::lround(cx - (float)nw * 0.5f),
                    (int)std::lround(cy - (float)nh * 0.5f),
                    nw,
                    nh
                };
            };
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
                SDL_Rect panelDst = scaleRectCentered(SDL_Rect{
                    screenW / 2 - panelFrame.rect.w / 2,
                    screenH / 2 - panelFrame.rect.h / 2,
                    panelFrame.rect.w,
                    panelFrame.rect.h
                });
                renderFrame(ren, pauseTex, panelFrame, panelDst);

                const Frame& continueFrame = pauseFrames["Continuebtn"];
                const Frame& restartFrame = pauseFrames["Retartbtn"];
                const Frame& exitFrame = pauseFrames["exitbtn"];

                int contW = std::max(1, (int)std::lround((continueFrame.rotated ? continueFrame.rect.h : continueFrame.rect.w) * uiButtonScale));
                int contH = std::max(1, (int)std::lround((continueFrame.rotated ? continueFrame.rect.w : continueFrame.rect.h) * uiButtonScale));
                int restartW = std::max(1, (int)std::lround((restartFrame.rotated ? restartFrame.rect.h : restartFrame.rect.w) * uiButtonScale));
                int restartH = std::max(1, (int)std::lround((restartFrame.rotated ? restartFrame.rect.w : restartFrame.rect.h) * uiButtonScale));
                int exitW = std::max(1, (int)std::lround((exitFrame.rotated ? exitFrame.rect.h : exitFrame.rect.w) * uiButtonScale));
                int exitH = std::max(1, (int)std::lround((exitFrame.rotated ? exitFrame.rect.w : exitFrame.rect.h) * uiButtonScale));

                int spacing = std::max(8, (int)std::lround(18.0f * uiButtonScale));
                int totalW = contW + restartW + exitW + spacing * 2;
                int startX = screenW / 2 - totalW / 2;
                int centerY = screenH / 2 + (int)std::lround(30.0f * uiButtonScale);

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
                SDL_Rect panel = scaleRectCentered(SDL_Rect{screenW / 2 - 140, screenH / 2 - 90, 280, 180});
                SDL_SetRenderDrawColor(ren, 30, 30, 38, 230);
                SDL_RenderFillRect(ren, &panel);
                SDL_SetRenderDrawColor(ren, 80, 90, 110, 255);
                SDL_RenderDrawRect(ren, &panel);

                SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
                DrawText(ren, screenW / 2 - MeasureTextWidth(3, "PAUSED") / 2, panel.y + (int)std::lround(22.0f * uiButtonScale), 3, "PAUSED");

                SDL_Rect resumeBtn = scaleRectCentered(SDL_Rect{screenW / 2 - 140, screenH / 2 + 10, 100, 36});
                SDL_Rect restartBtn = scaleRectCentered(SDL_Rect{screenW / 2 - 50, screenH / 2 + 10, 100, 36});
                SDL_Rect quitBtn = scaleRectCentered(SDL_Rect{screenW / 2 + 40, screenH / 2 + 10, 100, 36});
                const int labelLineH = std::max(10, (int)std::lround(16.0f * uiButtonScale));
                pauseBtnContinue = resumeBtn;
                pauseBtnRestart = restartBtn;
                pauseBtnExit = quitBtn;

                SDL_SetRenderDrawColor(ren, pauseSelection == 0 ? 70 : 45, pauseSelection == 0 ? 120 : 70, pauseSelection == 0 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &resumeBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, resumeBtn.x + (resumeBtn.w - MeasureTextWidth(2, "RESUME")) / 2, resumeBtn.y + (resumeBtn.h - labelLineH) / 2, 2, "RESUME");

                SDL_SetRenderDrawColor(ren, pauseSelection == 1 ? 70 : 45, pauseSelection == 1 ? 120 : 70, pauseSelection == 1 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &restartBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, restartBtn.x + (restartBtn.w - MeasureTextWidth(2, "RESTART")) / 2, restartBtn.y + (restartBtn.h - labelLineH) / 2, 2, "RESTART");

                SDL_SetRenderDrawColor(ren, pauseSelection == 2 ? 120 : 70, pauseSelection == 2 ? 70 : 50, pauseSelection == 2 ? 70 : 60, 255);
                SDL_RenderFillRect(ren, &quitBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, quitBtn.x + (quitBtn.w - MeasureTextWidth(2, "QUIT")) / 2, quitBtn.y + (quitBtn.h - labelLineH) / 2, 2, "QUIT");
            }

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
        if (levelCompleteUiLerp > 0.001f) {
            const int centerY = screenH / 2;
            auto completeSlideInXForY = [&](int y) -> int {
                const float yBias = std::abs((float)(y - centerY)) * 0.45f;
                const float base = (float)screenW + 120.0f + yBias;
                return (int)std::lround((1.0f - levelCompleteUiLerp) * base);
            };
            const std::string titleTop = "DIRK HAS";
            std::string titleBottom = std::string("PASSED AREA ") + std::to_string(levelCompleteAreaId);
            const int titleScale = 3;
            int topW = MeasureTextWidth(titleScale, titleTop);
            int bottomW = MeasureTextWidth(titleScale, titleBottom);
            const int titleTopY = centerY - 130;
            const int titleBottomY = centerY - 110;
            DrawText(ren, screenW / 2 - topW / 2 + completeSlideInXForY(titleTopY), titleTopY, titleScale, titleTop);
            DrawText(ren, screenW / 2 - bottomW / 2 + completeSlideInXForY(titleBottomY), titleBottomY, titleScale, titleBottom);

            const int bonusScale = 2;
            const int bonusLineGap = 18;
            const int bonusStartY = centerY - 50;
            const std::string bonusLine1 = std::string("COIN BONUS: ") + std::to_string(levelCompleteCoinBonus);
            const std::string bonusLine2 = std::string("TIME BONUS: ") + std::to_string(levelCompleteTimeScore);
            const std::string bonusLine3 = std::string("TOTAL SCORE: ") + std::to_string(levelCompleteAccountedScore);
            DrawText(ren, screenW / 2 - MeasureTextWidth(bonusScale, bonusLine1) / 2 + completeSlideInXForY(bonusStartY), bonusStartY, bonusScale, bonusLine1);
            DrawText(ren, screenW / 2 - MeasureTextWidth(bonusScale, bonusLine2) / 2 + completeSlideInXForY(bonusStartY + bonusLineGap), bonusStartY + bonusLineGap, bonusScale, bonusLine2);
            DrawText(ren, screenW / 2 - MeasureTextWidth(bonusScale, bonusLine3) / 2 + completeSlideInXForY(bonusStartY + bonusLineGap * 2), bonusStartY + bonusLineGap * 2, bonusScale, bonusLine3);
        }
        if (showFpsCounter) {
            const std::string ufpsText = std::string("UFPS: ") + std::to_string(updateFpsDisplay);
            const std::string rfpsText = std::string("RFPS: ") + std::to_string(renderFpsDisplay);
            const int fpsScale = 2;
            const int fpsX = screenW - 10 - std::max(MeasureTextWidth(fpsScale, ufpsText), MeasureTextWidth(fpsScale, rfpsText));
            DrawText(ren, fpsX, 10, fpsScale, ufpsText);
            DrawText(ren, fpsX, 34, fpsScale, rfpsText);
        }
        if (demoState.enabled) {
            DrawText(ren, 12, 10, 2, "DEMO");
        }
        if (replayRecorder.enabled) {
            DrawText(ren, 12, 34, 2, "REC");
        }
        if (replayPlayback.active) {
            DrawText(ren, 12, 58, 2, "PBK");
        }
        if (replayRecorder.enabled && replayRecorder.out.is_open()) {
            try {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                nlohmann::json frame;
                frame["type"] = "frame";
                frame["i"] = replayRecorder.frameIndex++;
                frame["ticks_ns"] = (uint64_t)SDL_GetTicksNS();
                frame["dt"] = dt;
                frame["paused"] = paused;
                frame["level_timer"] = levelTimerSeconds;
                frame["level_path"] = levelManager.levelPath();
                frame["world"] = levelManager.worldId();
                frame["area"] = levelManager.levelPartId();
                frame["cam"] = {{"x", camX}, {"y", camY}};
                frame["player"] = {
                    {"x", player.x}, {"y", player.y},
                    {"w", player.w}, {"h", player.h},
                    {"vx", player.vx}, {"vy", player.vy},
                    {"on_ground", player.onGround},
                    {"in_water", player.inWater},
                    {"facing", player.facing},
                    {"free_move", player.freeMove},
                    {"anim", player.anim},
                    {"anim_name", debugAnimName},
                    {"anim_time", player.animTime},
                    {"frame_index", renderAnimFrameIndex},
                    {"frame_name", renderFrameName},
                    {"jump_held", player.jumpHeld},
                    {"jump_hold_time", player.jumpHoldTime},
                    {"jump_was_down", player.jumpWasDown},
                    {"jump_buffer_time", player.jumpBufferTime}
                };
                frame["input_map"] = {
                    {"touch_move", replayInput.touchMove},
                    {"touch_down", replayInput.touchDown},
                    {"touch_jump", replayInput.touchJump},
                    {"gamepad_move", replayInput.gamepadMove},
                    {"gamepad_down", replayInput.gamepadDown},
                    {"gamepad_jump", replayInput.gamepadJump},
                    {"gamepad_free_move", replayInput.gamepadFreeMove},
                    {"input_move", replayInput.inputMove},
                    {"input_down", replayInput.inputDown},
                    {"force_right_movement", replayInput.forceRightMovement},
                    {"fast_travel_enabled", replayInput.fastTravelEnabled},
                    {"demo_enabled", replayInput.demoEnabled},
                    {"keyboard", {
                        {"left", keys[keybinds.moveLeft] || keys[SDL_SCANCODE_LEFT]},
                        {"right", keys[keybinds.moveRight] || keys[SDL_SCANCODE_RIGHT]},
                        {"up", keys[keybinds.jump] || keys[SDL_SCANCODE_UP]},
                        {"down", keys[keybinds.moveDown] || keys[SDL_SCANCODE_DOWN]},
                        {"jump", keys[keybinds.jump]},
                        {"shift", keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]}
                    }}
                };
                replayRecorder.out << frame.dump() << "\n";
                if ((replayRecorder.frameIndex % 120) == 0) replayRecorder.out.flush();
            } catch (...) {
                replayRecorder.enabled = false;
            }
        }

        if (showDebugView) {
            // Debug UI (highest render priority)
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
            SDL_Rect dbgPanel{10, 10, 340, 196};
            SDL_RenderFillRect(ren, &dbgPanel);
            SDL_SetRenderDrawColor(ren, 80, 90, 110, 220);
            SDL_RenderDrawRect(ren, &dbgPanel);

            SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
            DrawText(ren, 18, 18, 2, "DEBUG");
            DrawDebugNumber(ren, 18, 36, 2, "PX", (int)player.x);
            DrawDebugNumber(ren, 18, 50, 2, "PY", (int)player.y);
            DrawDebugNumber(ren, 18, 64, 2, "VX", (int)player.vx);
            DrawDebugNumber(ren, 18, 78, 2, "VY", (int)player.vy);
            DrawDebugNumber(ren, 140, 36, 2, "CAMX", (int)camX);
            DrawDebugNumber(ren, 140, 50, 2, "CAMY", (int)camY);
            DrawDebugNumber(ren, 140, 64, 2, "WTR", player.inWater ? 1 : 0);
            DrawDebugNumber(ren, 140, 78, 2, "DRN", (int)(45.0f - player.drownTimer));
            float maxCamX = std::max(0.0f, (float)(map.h * map.tileSize - screenW));
            float maxCamY = std::max(0.0f, (float)(map.w * map.tileSize - screenH));
            DrawDebugNumber(ren, 18, 92, 2, "BW", map.w);
            DrawDebugNumber(ren, 140, 92, 2, "BH", map.h);
            DrawDebugNumber(ren, 18, 106, 2, "CMINX", 0);
            DrawDebugNumber(ren, 140, 106, 2, "CMAXX", (int)maxCamX);
            DrawDebugNumber(ren, 18, 120, 2, "CMINY", 0);
            DrawDebugNumber(ren, 140, 120, 2, "CMAXY", (int)maxCamY);
            DrawDebugNumber(ren, 18, 134, 2, "UFPS", updateFpsDisplay);
            DrawDebugNumber(ren, 140, 134, 2, "RFPS", renderFpsDisplay);
            DrawText(ren, 18, 148, 2, std::string("ANIM ") + debugAnimName);
            if (!renderFrameName.empty()) {
                std::string id = renderFrameName;
                if (id.size() > 4 && id.substr(id.size() - 4) == ".png") id.resize(id.size() - 4);
                DrawText(ren, 18, 162, 2, std::string("ID ") + id);
            }
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
            for (int y = tileMinY; y <= tileMaxY; y++) {
                for (int x = tileMinX; x <= tileMaxX; x++) {
                    int idx = y * map.w + x;
                    int id = (int)map.tileIds[idx];
                    int screenX = (int)(x * map.tileSize - camX);
                    int screenY = (int)(y * map.tileSize - camY);
                    const std::string idText = std::to_string(id);
                    int idScale = std::clamp(map.tileSize / 14, 1, 3);
                    const int fitW = std::max(4, map.tileSize - 4);
                    const int fitH = std::max(4, map.tileSize - 4);
                    while (idScale > 1 &&
                           (MeasureTextWidth(idScale, idText) > fitW || (10 * idScale) > fitH)) {
                        --idScale;
                    }
                    const int textW = MeasureTextWidth(idScale, idText);
                    const int textH = 10 * idScale;
                    const int textX = screenX + (map.tileSize - textW) / 2;
                    const int textY = screenY + (map.tileSize - textH) / 2;
                    DrawText(ren, textX, textY, idScale, idText);
                }
            }
        }

        if (!paused) {
            int touchDeviceCount = 0;
            SDL_TouchID* touchDevices = SDL_GetTouchDevices(&touchDeviceCount);
            if (touchDevices) SDL_free(touchDevices);
            bool showMobileUi = (touchDeviceCount > 0) || !activeTouches.empty();
            if (showMobileUi) {
                auto drawTouchBtn = [&](const SDL_FRect& r, bool active) {
                    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ren, active ? 180 : 120, active ? 180 : 120, active ? 180 : 120, active ? 120 : 80);
                    SDL_RenderFillRectF(ren, &r);
                    SDL_SetRenderDrawColor(ren, 230, 230, 230, 180);
                    SDL_RenderDrawRectF(ren, &r);
                };
                drawTouchBtn(touchLeftBtn, touchLeft);
                drawTouchBtn(touchRightBtn, touchRight);
                drawTouchBtn(touchDownBtn, touchDown);
                drawTouchBtn(touchJumpBtn, touchJump);
                SDL_SetRenderDrawColor(ren, 240, 240, 240, 220);
                int touchLabelScale = std::clamp((int)std::lround(uiSize / 44.0f), 2, 4);
                auto drawBtnLabel = [&](const SDL_FRect& btn, const std::string& text) {
                    int tw = MeasureTextWidth(touchLabelScale, text);
                    int tx = (int)std::lround(btn.x + (btn.w - tw) * 0.5f);
                    int ty = (int)std::lround(btn.y + (btn.h * 0.5f) - (10.0f * touchLabelScale));
                    DrawText(ren, tx, ty, touchLabelScale, text);
                };
                drawBtnLabel(touchLeftBtn, "L");
                drawBtnLabel(touchRightBtn, "R");
                drawBtnLabel(touchDownBtn, "DN");
                drawBtnLabel(touchJumpBtn, "JMP");
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            }
        }

        if (embeddedDetailedDebugger) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect panel{8, 6, kBaseScreenW - 16, std::min(360, kBaseScreenH - 12)};
            SDL_SetRenderDrawColor(ren, 10, 12, 16, 210);
            SDL_RenderFillRect(ren, &panel);
            SDL_SetRenderDrawColor(ren, 180, 200, 230, 220);
            SDL_RenderDrawRect(ren, &panel);

            DrawText(ren, 12, 10, 2, "DETAILED DEBUGGER (F5)");
            SDL_Rect tab0{12, 38, 130, 36};
            SDL_Rect tab1{152, 38, 130, 36};
            SDL_Rect tab2{292, 38, 130, 36};
            SDL_Rect tab3{432, 38, 130, 36};
            SDL_Rect tabs[4] = {tab0, tab1, tab2, tab3};
            const char* tabNames[4] = {"OVERVIEW", "OBJECT INDEX", "PERF", "PLAYER"};
            for (int i = 0; i < 4; ++i) {
                SDL_SetRenderDrawColor(ren, i == detailedDebugSubmenu ? 70 : 45, 85, 120, i == detailedDebugSubmenu ? 180 : 120);
                SDL_RenderFillRect(ren, &tabs[i]);
                SDL_SetRenderDrawColor(ren, 180, 200, 230, 255);
                SDL_RenderDrawRect(ren, &tabs[i]);
                DrawText(ren, tabs[i].x + 8, tabs[i].y + 8, 2, tabNames[i]);
            }
            SDL_Rect closeBtn{500, 8, 52, 24};
            SDL_SetRenderDrawColor(ren, 85, 55, 55, 210);
            SDL_RenderFillRect(ren, &closeBtn);
            SDL_SetRenderDrawColor(ren, 220, 180, 180, 255);
            SDL_RenderDrawRect(ren, &closeBtn);
            DrawText(ren, 514, 12, 2, "X");

            int y = 88;
            if (detailedDebugSubmenu == 0) {
                DrawText(ren, 12, y, 2, std::string("UFPS/RFPS: ") + std::to_string(updateFpsDisplay) + "/" + std::to_string(renderFpsDisplay)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Frame ms: ") + std::to_string((int)std::lround(dt * 1000.0f))); y += 20;
                DrawText(ren, 12, y, 2, std::string("Player X/Y: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Player VX/VY: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(ren, 12, y, 2, std::string("OnGround/InWater: ") + (player.onGround ? "1" : "0") + "/" + (player.inWater ? "1" : "0")); y += 20;
                DrawText(ren, 12, y, 2, std::string("Objects: ") + std::to_string((int)objects.size())); y += 20;
            } else if (detailedDebugSubmenu == 1) {
                if (objects.empty()) {
                    detailedDebugObjectIndex = 0;
                    DrawText(ren, 12, y, 2, "No objects in current level");
                } else {
                    detailedDebugObjectIndex = std::clamp(detailedDebugObjectIndex, 0, (int)objects.size() - 1);
                    SDL_Rect prevBtn{12, 92, 120, 34};
                    SDL_Rect nextBtn{142, 92, 120, 34};
                    SDL_SetRenderDrawColor(ren, 55, 70, 95, 180);
                    SDL_RenderFillRect(ren, &prevBtn);
                    SDL_RenderFillRect(ren, &nextBtn);
                    SDL_SetRenderDrawColor(ren, 180, 200, 230, 255);
                    SDL_RenderDrawRect(ren, &prevBtn);
                    SDL_RenderDrawRect(ren, &nextBtn);
                    DrawText(ren, prevBtn.x + 12, prevBtn.y + 7, 2, "PREV");
                    DrawText(ren, nextBtn.x + 12, nextBtn.y + 7, 2, "NEXT");
                    y = 134;
                    const ObjectInstance& sel = objects[detailedDebugObjectIndex];
                    DrawText(ren, 12, y, 2, std::string("Selected: ") + std::to_string(detailedDebugObjectIndex)); y += 20;
                    DrawText(ren, 12, y, 2, std::string("ID: ") + sel.id); y += 20;
                    DrawText(ren, 12, y, 2, std::string("Pos: ") + std::to_string((int)sel.x) + ", " + std::to_string((int)sel.y)); y += 20;
                }
            } else if (detailedDebugSubmenu == 2) {
                DrawText(ren, 12, y, 2, std::string("Frame ms: ") + std::to_string((int)std::lround(dt * 1000.0f))); y += 20;
                DrawText(ren, 12, y, 2, "Target: <16ms (60 FPS)"); y += 20;
                DrawText(ren, 12, y, 2, "Use submenu for full perf details on desktop"); y += 20;
            } else {
                DrawText(ren, 12, y, 2, std::string("Position: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Velocity: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Facing: ") + (player.facing < 0 ? "LEFT" : "RIGHT")); y += 20;
                DrawText(ren, 12, y, 2, std::string("Anim: ") + std::to_string(player.anim)); y += 20;
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        SDL_SetRenderTarget(ren, nullptr);
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Rect presentDst = computePresentRect(winW, winH, kBaseScreenW, kBaseScreenH, 1.0f);
        SDL_SetRenderDrawColor(ren, 221, 248, 255, 255); // #ddf8ff
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, gameTarget, nullptr, &presentDst);
        SDL_RenderPresent(ren);
        {
            const Uint32 presentedAt = SDL_GetTicks();
            const Uint32 presentDelta = presentedAt - lastPresentTicks;
            const int renderFpsInstant = std::clamp((presentDelta > 0) ? (int)(1000u / presentDelta) : 0, 0, kFpsDisplayMax);
            if (renderFpsSmoothed <= 0.0f) renderFpsSmoothed = (float)renderFpsInstant;
            else renderFpsSmoothed += ((float)renderFpsInstant - renderFpsSmoothed) * 0.20f;
            renderFpsDisplay = std::clamp((int)std::lround(renderFpsSmoothed), 0, kFpsDisplayMax);
            lastPresentTicks = presentedAt;
            // Keep render cadence capped while updates remain independent.
            const int targetPresentIntervalMs =
                (powerManagementEnabled && mainWindowMinimized) ? 250 :
                (powerManagementEnabled && (!mainWindowFocused || paused)) ? 33 : 16;
            nextPresentTicks = presentedAt + (Uint32)targetPresentIntervalMs;
        }
        if (showDetailedDebugger && debugRen && debugWin && !embeddedDetailedDebugger) {
            int dbgW = 0, dbgH = 0;
            SDL_GetWindowSize(debugWin, &dbgW, &dbgH);
            SDL_SetRenderDrawColor(debugRen, 10, 12, 16, 255);
            SDL_RenderClear(debugRen);

            int y = 10;
            DrawText(debugRen, 12, y, 2, "DETAILED DEBUGGER (F5)"); y += 24;
            SDL_Rect tab0{12, 38, 130, 36};
            SDL_Rect tab1{152, 38, 130, 36};
            SDL_Rect tab2{292, 38, 130, 36};
            SDL_Rect tab3{432, 38, 130, 36};
            SDL_Rect tabs[4] = {tab0, tab1, tab2, tab3};
            const char* tabNames[4] = {"OVERVIEW", "OBJECT INDEX", "PERFORMANCE", "PLAYER STATUS"};
            for (int i = 0; i < 4; ++i) {
                SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(debugRen, i == detailedDebugSubmenu ? 70 : 45, 85, 120, i == detailedDebugSubmenu ? 180 : 120);
                SDL_RenderFillRect(debugRen, &tabs[i]);
                SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(debugRen, 180, 200, 230, 255);
                SDL_RenderDrawRect(debugRen, &tabs[i]);
                DrawText(debugRen, tabs[i].x + (tabs[i].w - MeasureTextWidth(2, tabNames[i])) / 2, tabs[i].y + 8, 2, tabNames[i]);
            }
            y = 84;
            const char* submenuNames[4] = {"OVERVIEW", "OBJECT INDEX", "PERFORMANCE", "PLAYER STATUS"};
            DrawText(debugRen, 12, y, 2, std::string("SUBMENU (F4): ") + submenuNames[detailedDebugSubmenu]); y += 20;
            if (detailedDebugSubmenu == 0) {
                long rssKB = -1, vmKB = -1;
                readProcessMemoryKB(rssKB, vmKB);
                DrawText(debugRen, 12, y, 2, std::string("UFPS: ") + std::to_string(updateFpsDisplay)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("RFPS: ") + std::to_string(renderFpsDisplay)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Frame ms: ") + std::to_string((int)std::lround(dt * 1000.0f))); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Memory RSS MB: ") + (rssKB >= 0 ? std::to_string((int)(rssKB / 1024)) : "N/A")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Memory VM MB: ") + (vmKB >= 0 ? std::to_string((int)(vmKB / 1024)) : "N/A")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Player X/Y: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Player VX/VY: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("OnGround/InWater: ") + (player.onGround ? "1" : "0") + "/" + (player.inWater ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Cam X/Y: ") + std::to_string((int)camX) + ", " + std::to_string((int)camY)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Map W/H: ") + std::to_string(map.w) + " x " + std::to_string(map.h)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("TileSize: ") + std::to_string(map.tileSize)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Coins/Score/Lives: ") + std::to_string(levelManager.coinCount()) + "/" + std::to_string(scoreCount) + "/" + std::to_string(livesCount)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Objects: ") + std::to_string((int)objects.size())); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Level IDs (W/P/T): ") + std::to_string(levelManager.worldId()) + "/" + std::to_string(levelManager.levelPartId()) + "/" + std::to_string(levelManager.timeId())); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("TimeWarpId: ") + std::string(1, levelManager.timeWarpId())); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Flags H/P/D/FPS: ") +
                                            (showHitboxes ? "1" : "0") + "/" +
                                            (showPlayerHitbox ? "1" : "0") + "/" +
                                            (showDebugView ? "1" : "0") + "/" +
                                            (showFpsCounter ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Paused/Death/Complete: ") +
                                            (paused ? "1" : "0") + "/" +
                                            (deathSequenceActive ? "1" : "0") + "/" +
                                            (levelCompleteActive ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, "Guideline: target <16ms for 60 FPS"); y += 20;
                DrawText(debugRen, 12, y, 2, "Guideline: keep RSS stable during play"); y += 20;
            } else if (detailedDebugSubmenu == 1) {
                if (objects.empty()) {
                    detailedDebugObjectIndex = 0;
                    DrawText(debugRen, 12, y, 2, "No objects in current level");
                } else {
                    if (detailedDebugObjectIndex < 0) detailedDebugObjectIndex = 0;
                    if (detailedDebugObjectIndex >= (int)objects.size()) detailedDebugObjectIndex = (int)objects.size() - 1;
                    SDL_Rect prevBtn{12, 92, 120, 34};
                    SDL_Rect nextBtn{142, 92, 120, 34};
                    SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(debugRen, 55, 70, 95, 180);
                    SDL_RenderFillRect(debugRen, &prevBtn);
                    SDL_RenderFillRect(debugRen, &nextBtn);
                    SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(debugRen, 180, 200, 230, 255);
                    SDL_RenderDrawRect(debugRen, &prevBtn);
                    SDL_RenderDrawRect(debugRen, &nextBtn);
                    DrawText(debugRen, prevBtn.x + 12, prevBtn.y + 7, 2, "PREV");
                    DrawText(debugRen, nextBtn.x + 12, nextBtn.y + 7, 2, "NEXT");
                    y = 134;
                    DrawText(debugRen, 12, y, 2, "Use UP/DOWN to select object index"); y += 20;
                    const ObjectInstance& sel = objects[detailedDebugObjectIndex];
                    DrawText(debugRen, 12, y, 2, std::string("Selected Index: ") + std::to_string(detailedDebugObjectIndex)); y += 20;
                    DrawText(debugRen, 12, y, 2, std::string("ID: ") + sel.id); y += 20;
                    DrawText(debugRen, 12, y, 2, std::string("Pos X/Y: ") + std::to_string((int)sel.x) + ", " + std::to_string((int)sel.y)); y += 20;
                    DrawText(debugRen, 12, y, 2, std::string("Screen X/Y: ") + std::to_string((int)(sel.x - camX)) + ", " + std::to_string((int)(sel.y - camY))); y += 24;

                    // Decoded object map from level data.
                    const int mapPanelX = 360;
                    const int mapPanelY = 92;
                    const int mapPanelW = std::max(180, dbgW - mapPanelX - 12);
                    const int mapPanelH = std::max(150, dbgH - mapPanelY - 12);
                    SDL_Rect objMapRect{mapPanelX, mapPanelY, mapPanelW, mapPanelH};
                    SDL_SetRenderDrawColor(debugRen, 36, 44, 58, 255);
                    SDL_RenderDrawRect(debugRen, &objMapRect);
                    DrawText(debugRen, mapPanelX + 8, mapPanelY + 8, 2, "OBJECT MAP (DECODED)");

                    const float worldW = std::max(1.0f, (float)(map.w * map.tileSize));
                    const float worldH = std::max(1.0f, (float)(map.h * map.tileSize));
                    const float plotW = (float)(mapPanelW - 16);
                    const float plotH = (float)(mapPanelH - 28);
                    const int plotX = mapPanelX + 8;
                    const int plotY = mapPanelY + 20;

                    SDL_Rect plotRect{plotX, plotY, (int)plotW, (int)plotH};
                    SDL_SetRenderDrawColor(debugRen, 26, 32, 42, 255);
                    SDL_RenderFillRect(debugRen, &plotRect);
                    SDL_SetRenderDrawColor(debugRen, 70, 90, 120, 255);
                    SDL_RenderDrawRect(debugRen, &plotRect);

                    for (int i = 0; i < (int)objects.size(); ++i) {
                        const ObjectInstance& o = objects[i];
                        int px = plotX + (int)std::lround((std::clamp(o.x, 0.0f, worldW) / worldW) * (plotW - 1.0f));
                        int py = plotY + (int)std::lround((std::clamp(o.y, 0.0f, worldH) / worldH) * (plotH - 1.0f));
                        bool selected = (i == detailedDebugObjectIndex);
                        SDL_SetRenderDrawColor(debugRen, selected ? 255 : 130, selected ? 230 : 190, selected ? 90 : 140, 255);
                        SDL_Rect dot{px - (selected ? 2 : 1), py - (selected ? 2 : 1), selected ? 5 : 3, selected ? 5 : 3};
                        SDL_RenderFillRect(debugRen, &dot);
                    }

                    int ppx = plotX + (int)std::lround((std::clamp(player.x, 0.0f, worldW) / worldW) * (plotW - 1.0f));
                    int ppy = plotY + (int)std::lround((std::clamp(player.y, 0.0f, worldH) / worldH) * (plotH - 1.0f));
                    SDL_SetRenderDrawColor(debugRen, 80, 180, 255, 255);
                    SDL_RenderDrawLine(debugRen, ppx - 3, ppy, ppx + 3, ppy);
                    SDL_RenderDrawLine(debugRen, ppx, ppy - 3, ppx, ppy + 3);

                    int typeY = mapPanelY + mapPanelH - 82;
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, "ENTITY SPAWN POS:");
                    typeY += 18;
                    std::string posLine;
                    const int maxShowPos = std::min((int)meta.entitySpawnPos.size(), 12);
                    for (int i = 0; i < maxShowPos; ++i) {
                        if (i > 0) posLine += ",";
                        posLine += std::to_string(meta.entitySpawnPos[i]);
                    }
                    if ((int)meta.entitySpawnPos.size() > maxShowPos) posLine += "...";
                    if (posLine.empty()) posLine = "(empty)";
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, posLine);

                    typeY += 18;
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, "ENTITY SPAWN TYPE:");
                    typeY += 18;
                    std::string typesLine;
                    const int maxShow = std::min((int)meta.entitySpawnType.size(), 12);
                    for (int i = 0; i < maxShow; ++i) {
                        if (i > 0) typesLine += ",";
                        typesLine += std::to_string(meta.entitySpawnType[i]);
                    }
                    if ((int)meta.entitySpawnType.size() > maxShow) typesLine += "...";
                    if (typesLine.empty()) typesLine = "(empty)";
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, typesLine);

                    int start = std::max(0, detailedDebugObjectIndex - 10);
                    int end = std::min((int)objects.size(), start + 20);
                    for (int i = start; i < end; ++i) {
                        const ObjectInstance& o = objects[i];
                        std::string line = (i == detailedDebugObjectIndex ? "> " : "  ")
                                           + std::to_string(i) + " id=" + o.id
                                           + " x=" + std::to_string((int)o.x)
                                           + " y=" + std::to_string((int)o.y);
                        DrawText(debugRen, 12, y, 2, line);
                        y += 18;
                    }
                }
            } else if (detailedDebugSubmenu == 2) {
                long rssKB = -1, vmKB = -1;
                readProcessMemoryKB(rssKB, vmKB);
                const int panelX = 12;
                const int panelY = std::max(120, y + 8);
                const int panelW = 210;
                const int panelH = std::max(140, dbgH - panelY - 12);
                SDL_Rect guideRect{panelX, panelY, panelW, panelH};
                SDL_SetRenderDrawColor(debugRen, 36, 44, 58, 255);
                SDL_RenderDrawRect(debugRen, &guideRect);
                int gy = panelY + 10;
                DrawText(debugRen, panelX + 8, gy, 2, "GUIDELINES"); gy += 22;
                DrawText(debugRen, panelX + 8, gy, 2, "- target <16ms @60fps"); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, "- >33ms causes drops"); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, "- keep RSS stable"); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, "- rising RSS => leaks"); gy += 22;
                DrawText(debugRen, panelX + 8, gy, 2, std::string("RSS MB: ") + (rssKB >= 0 ? std::to_string((int)(rssKB / 1024)) : "N/A")); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, std::string("VM MB: ") + (vmKB >= 0 ? std::to_string((int)(vmKB / 1024)) : "N/A"));

                const int graphX = panelX + panelW + 12;
                const int graphY = panelY;
                const int graphW = std::max(180, dbgW - graphX - 12);
                const int graphH = std::max(120, (dbgH - graphY - 24) / 2);
                SDL_Rect graphRect{graphX, graphY, graphW, graphH};
                SDL_SetRenderDrawColor(debugRen, 40, 50, 65, 255);
                SDL_RenderDrawRect(debugRen, &graphRect);
                SDL_SetRenderDrawColor(debugRen, 80, 160, 255, 255);
                for (int i = 0; i < graphW; ++i) {
                    int idx = (frameMsHistoryHead + i * (int)frameMsHistory.size() / std::max(1, graphW)) % (int)frameMsHistory.size();
                    float ms = frameMsHistory[idx];
                    float norm = std::clamp(ms / 50.0f, 0.0f, 1.0f);
                    int hPx = (int)std::lround(norm * (graphH - 2));
                    SDL_RenderDrawLine(debugRen, graphX + i, graphY + graphH - 1, graphX + i, graphY + graphH - 1 - hPx);
                }
                DrawText(debugRen, graphX + 8, graphY + 8, 2, "Frame Time History (0-50ms)");
                DrawText(debugRen, graphX + 8, graphY + 28, 2, std::string("Samples: ") + std::to_string((int)frameMsHistory.size()));
                DrawText(debugRen, graphX + 8, graphY + 48, 2, std::string("Current ms: ") + std::to_string((int)std::lround(dt * 1000.0f)));

                const int memGraphY = graphY + graphH + 12;
                const int memGraphH = std::max(90, dbgH - memGraphY - 12);
                SDL_Rect memGraphRect{graphX, memGraphY, graphW, memGraphH};
                SDL_SetRenderDrawColor(debugRen, 40, 50, 65, 255);
                SDL_RenderDrawRect(debugRen, &memGraphRect);
                float maxMem = 1.0f;
                for (float v : memRssHistory) maxMem = std::max(maxMem, v);
                SDL_SetRenderDrawColor(debugRen, 110, 220, 120, 255);
                for (int i = 0; i < graphW; ++i) {
                    int idx = (frameMsHistoryHead + i * (int)memRssHistory.size() / std::max(1, graphW)) % (int)memRssHistory.size();
                    float mem = memRssHistory[idx];
                    float norm = std::clamp(mem / maxMem, 0.0f, 1.0f);
                    int hPx = (int)std::lround(norm * (memGraphH - 2));
                    SDL_RenderDrawLine(debugRen, graphX + i, memGraphY + memGraphH - 1, graphX + i, memGraphY + memGraphH - 1 - hPx);
                }
                DrawText(debugRen, graphX + 8, memGraphY + 8, 2, "Memory RSS History");
                DrawText(debugRen, graphX + 8, memGraphY + 28, 2, std::string("Peak MB: ") + std::to_string((int)std::lround(maxMem)));
            } else {
                DrawText(debugRen, 12, y, 2, "PLAYER STATUS"); y += 24;
                DrawText(debugRen, 12, y, 2, std::string("Position: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Velocity: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Facing: ") + (player.facing < 0 ? "LEFT" : "RIGHT")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Anim: ") + std::to_string(player.anim) + " (" + debugAnimName + ")"); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Hitbox W/H: ") + std::to_string(player.w) + "/" + std::to_string(player.h)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("OnGround: ") + (player.onGround ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("InWater: ") + (player.inWater ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("FreeMove: ") + (player.freeMove ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("JumpHeld/WasDown: ") + (player.jumpHeld ? "1" : "0") + "/" + (player.jumpWasDown ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("JumpHoldTime: ") + std::to_string((int)std::lround(player.jumpHoldTime * 1000.0f)) + "ms"); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("JumpBuffer: ") + std::to_string((int)std::lround(player.jumpBufferTime * 1000.0f)) + "ms"); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("DrownTimer: ") + std::to_string((int)std::lround(player.drownTimer * 1000.0f)) + "ms"); y += 20;
            }

            SDL_RenderPresent(debugRen);
        }
        }
        if (!running) break;
        stopReplayRecording(returnToSelect ? "return_to_select" : "level_end");
        audio.unloadLevelMusic();
        if (returnToSelect) {
            if (selectedFromUserMenu) reopenUserLevelMenu = true;
            continue;
        }
    }

    if (blocksTex) SDL_DestroyTexture(blocksTex);
    if (entitiesTex) SDL_DestroyTexture(entitiesTex);
    if (bossesTex) SDL_DestroyTexture(bossesTex);
    if (endSignTex) SDL_DestroyTexture(endSignTex);
    if (pauseTex) SDL_DestroyTexture(pauseTex);
    if (introCardTex) SDL_DestroyTexture(introCardTex);
    if (bgTexWorld1) SDL_DestroyTexture(bgTexWorld1);
    if (bgTexWorld2) SDL_DestroyTexture(bgTexWorld2);
    if (bgTexWorld4) SDL_DestroyTexture(bgTexWorld4);
    if (bgTexWorld5) SDL_DestroyTexture(bgTexWorld5);
    if (bgTexWorld6) SDL_DestroyTexture(bgTexWorld6);
    if (worldTarget) SDL_DestroyTexture(worldTarget);
    if (gameTarget) SDL_DestroyTexture(gameTarget);
    if (debugRen && debugRen != ren) SDL_DestroyRenderer(debugRen);
    if (debugWin && debugWin != win) SDL_DestroyWindow(debugWin);
    ShutdownTextRenderer();
    audio.shutdown();
    sendDiscordTelemetry("shutdown", {
        {"build_uuid", buildUuid},
        {"version", appVersion},
        {"uptime_ms", (long long)SDL_GetTicks()}
    });
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Log("Shutting down");
    CrashReporter::stop();
    SDL_Quit();
    return 0;
}

