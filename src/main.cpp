#include <sdl3/SDL.h>
#include <sdl3/SDL_image.h>
#if __has_include(<sdl3/SDL_mixer.h>)
#include <sdl3/SDL_mixer.h>
#define HAS_SDL_MIXER 1
#elif __has_include(<SDL_mixer.h>)
#include <SDL_mixer.h>
#define HAS_SDL_MIXER 1
#else
#define HAS_SDL_MIXER 0
#endif
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
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <unistd.h>
#include <cstdlib>
#include <vector>
#if defined(__ANDROID__)
#include <jni.h>
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
#include "CrashReporter.h"
int main(int argc, char** argv) {
    CrashReporter::start();
    const std::string buildUuid = makeBuildUuid();
    auto reportStartupError = [](const char* title, const std::string& msg, SDL_Window* parent) {
#if defined(__ANDROID__)
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s: %s", title, msg.c_str());
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg.c_str(), parent);
#endif
    };
    SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "2");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    auto sdlErr = []() -> std::string {
        const char* e = SDL_GetError();
        if (e && *e) return std::string(e);
        return "unknown error";
    };
    auto envOrUnset = [](const char* name) -> std::string {
        const char* v = std::getenv(name);
        return (v && *v) ? std::string(v) : std::string("<unset>");
    };
    struct InitAttempt {
        const char* label;
        const char* videoDriver; // nullptr means keep current env
        const char* audioDriver; // nullptr means keep current env
        Uint32 flags;
    };
    std::vector<InitAttempt> attempts = {
        {"video+audio (env defaults)", nullptr, nullptr, SDL_INIT_VIDEO | SDL_INIT_AUDIO},
        {"video only (env defaults)", nullptr, nullptr, SDL_INIT_VIDEO},
        {"video+audio (x11 + dummy audio)", "x11", "dummy", SDL_INIT_VIDEO | SDL_INIT_AUDIO},
        {"video only (x11)", "x11", nullptr, SDL_INIT_VIDEO},
        {"video+audio (wayland + dummy audio)", "wayland", "dummy", SDL_INIT_VIDEO | SDL_INIT_AUDIO},
        {"video only (wayland)", "wayland", nullptr, SDL_INIT_VIDEO},
    };

    bool sdlOk = false;
    std::string initTrace;
    for (const auto& a : attempts) {
        if (a.videoDriver) setenv("SDL_VIDEODRIVER", a.videoDriver, 1);
        if (a.audioDriver) setenv("SDL_AUDIODRIVER", a.audioDriver, 1);
        SDL_Quit();
        if (SDL_Init(a.flags) == 0) {
            initTrace += std::string(a.label) + ": ok\n";
            sdlOk = true;
            break;
        }
        initTrace += std::string(a.label) + ": " + sdlErr() + "\n";
    }
    if (!sdlOk) {
        std::string msg = "SDL_Init failed.\n";
        msg += initTrace;
        msg += "DISPLAY=" + envOrUnset("DISPLAY") + "\n";
        msg += "WAYLAND_DISPLAY=" + envOrUnset("WAYLAND_DISPLAY") + "\n";
        msg += "XDG_RUNTIME_DIR=" + envOrUnset("XDG_RUNTIME_DIR") + "\n";
        msg += "SDL_VIDEODRIVER=" + envOrUnset("SDL_VIDEODRIVER") + "\n";
        msg += "SDL_AUDIODRIVER=" + envOrUnset("SDL_AUDIODRIVER");
        reportStartupError("SDL Init Error", msg, nullptr);
        CrashReporter::stop();
        return 1;
    }
    SDL_Log("SDL_Init completed");
    InitTextRenderer(ResolveAssetPath("assets/Fonts/Main.ttf"));
    bool audioReady = false;
#if HAS_SDL_MIXER
    int mixerFlags = MIX_INIT_MP3;
    audioReady = (Mix_Init(mixerFlags) & mixerFlags) == mixerFlags &&
                 Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0;
    if (!audioReady) SDL_Log("Audio mixer init failed: %s", Mix_GetError());
#else
    SDL_Log("SDL_mixer not found at compile time: music playback disabled.");
#endif

    constexpr int kBaseScreenW = 960;
    constexpr int kBaseScreenH = 540;

    SDL_Window* win = SDL_CreateWindow(
        "Dorfplatformer Timetravel",
        kBaseScreenW, kBaseScreenH,
        SDL_WINDOW_RESIZABLE
    );
    if (!win) {
        reportStartupError("Window Error", std::string("SDL_CreateWindow failed: ") + SDL_GetError(), nullptr);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
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
    if (!gameTarget) {
        reportStartupError("Render Target Error", std::string("SDL_CreateTexture failed: ") + SDL_GetError(), win);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }
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

    std::string bgPlist = texPath("plists", "background", "assets/Sheets/DF_Background-uhd.plist");
    SDL_Texture* bgTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "background", "assets/Sheets/DF_Background-uhd.png")).c_str());
    auto bgFrameList = loadPlistFrameList(bgPlist);
    std::unordered_map<std::string, Frame> bgFrameByName;
    bgFrameByName.reserve(bgFrameList.size());
    for (const auto& e : bgFrameList) bgFrameByName[e.name] = e.frame;

    const std::string entitiesPlist = "assets/Sheets/DF_Enitys-uhd.plist";
    SDL_Texture* entitiesTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/DF_Enitys-uhd.png").c_str());
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
    auto playerFrameList = loadPlistFrameList(playerPlist);
    std::unordered_map<std::string, Frame> playerFramesByName;
    playerFramesByName.reserve(playerFrameList.size());
    for (const auto& e : playerFrameList) playerFramesByName[e.name] = e.frame;
    const Frame* fallbackPlayerFrame = !playerFrameList.empty() ? &playerFrameList[0].frame : nullptr;
#if HAS_SDL_MIXER
    Mix_Chunk* coinSfx = nullptr;
    Mix_Chunk* loseSfx = nullptr;
    Mix_Chunk* victorySfx = nullptr;
    Mix_Chunk* messageSfx = nullptr;
    Mix_Chunk* bumperSfx = nullptr;
    Mix_Music* menuMusic = nullptr;
    bool menuMusicPlaying = false;
    if (audioReady) {
        coinSfx = Mix_LoadWAV(ResolveAssetPath("assets/Audio/sfx/Coin.mp3").c_str());
        if (!coinSfx) SDL_Log("Could not load coin sfx: %s", Mix_GetError());
        loseSfx = Mix_LoadWAV(ResolveAssetPath("assets/Audio/sfx/Lose.mp3").c_str());
        if (!loseSfx) SDL_Log("Could not load lose sfx: %s", Mix_GetError());
        victorySfx = Mix_LoadWAV(ResolveAssetPath("assets/Audio/sfx/Victory.mp3").c_str());
        if (!victorySfx) SDL_Log("Could not load victory sfx: %s", Mix_GetError());
        messageSfx = Mix_LoadWAV(ResolveAssetPath("assets/Audio/sfx/Message.mp3").c_str());
        if (!messageSfx) SDL_Log("Could not load message sfx: %s", Mix_GetError());
        bumperSfx = Mix_LoadWAV(ResolveAssetPath("assets/Audio/sfx/Bumper.mp3").c_str());
        if (!bumperSfx) SDL_Log("Could not load bumper sfx: %s", Mix_GetError());
        menuMusic = Mix_LoadMUS(ResolveAssetPath("assets/Audio/Music/menu.mp3").c_str());
        if (!menuMusic) menuMusic = Mix_LoadMUS(ResolveAssetPath("assets/Audio/Music/Menu.mp3").c_str());
        if (!menuMusic) SDL_Log("Could not load menu music: %s", Mix_GetError());
    }
#endif

    auto getPlayerFrame = [&](const std::string& name) -> const Frame* {
        auto it = playerFramesByName.find(name);
        if (it == playerFramesByName.end()) return fallbackPlayerFrame;
        return &it->second;
    };

    struct AnimConfig {
        float fps = 15.0f;
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
    float jumpBufferMax = 0.12f;
    MovementConfig movementCfg{};
    {
        const std::string text = ReadTextFile("assets/config.json");
        if (!text.empty()) {
            nlohmann::json cfg;
            try { cfg = nlohmann::json::parse(text); } catch (...) { cfg = nlohmann::json(); }
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
            }
        }
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
        if (audioReady) {
#if HAS_SDL_MIXER
            Mix_CloseAudio();
            Mix_Quit();
#endif
        }
#if HAS_SDL_MIXER
        if (coinSfx) Mix_FreeChunk(coinSfx);
        if (loseSfx) Mix_FreeChunk(loseSfx);
        if (victorySfx) Mix_FreeChunk(victorySfx);
        if (messageSfx) Mix_FreeChunk(messageSfx);
        if (bumperSfx) Mix_FreeChunk(bumperSfx);
        if (menuMusic) Mix_FreeMusic(menuMusic);
#endif
        ShutdownTextRenderer();
        SDL_Quit();
        return 1;
    }
    if (!gameTarget) {
        reportStartupError("Render Target Error", "Failed to create game render target.", win);
        if (playerTex) SDL_DestroyTexture(playerTex);
        if (bgTex) SDL_DestroyTexture(bgTex);
        if (blocksTex) SDL_DestroyTexture(blocksTex);
        if (entitiesTex) SDL_DestroyTexture(entitiesTex);
        if (pauseTex) SDL_DestroyTexture(pauseTex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
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
    bool menuMusicEnabled = true;
    bool muteAllAudio = false;
    float fastTravelChangeDelay = 0.09f;
    int musicVolume = 96; // 0..128
    int sfxVolume = 96;   // 0..128
    std::string clientSettingsPath = "client_settings.json";
    {
        char* prefPath = SDL_GetPrefPath("Benno111", "DorfplatformerTimetravel");
        if (prefPath) {
            clientSettingsPath = std::string(prefPath) + "client_settings.json";
            SDL_free(prefPath);
        }
    }
    auto saveClientSettings = [&]() {
        nlohmann::json j;
        j["build_uuid"] = buildUuid;
        j["fullscreen"] = fullscreen;
        j["vsync"] = vsyncEnabled;
        j["clamp_cam_x"] = clampCamX;
        j["show_fps_counter"] = defaultShowFpsCounter;
        j["show_detailed_debugger"] = defaultShowDetailedDebugger;
        j["show_hitboxes"] = defaultShowHitboxes;
        j["show_player_hitbox"] = defaultShowPlayerHitbox;
        j["show_debug_view"] = defaultShowDebugView;
        j["menu_music_enabled"] = menuMusicEnabled;
        j["mute_all_audio"] = muteAllAudio;
        j["fast_travel_delay"] = fastTravelChangeDelay;
        j["music_volume"] = musicVolume;
        j["sfx_volume"] = sfxVolume;
        std::ofstream out(clientSettingsPath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) out << j.dump(2);
    };
    {
        const std::string text = ReadTextFile(clientSettingsPath);
        if (!text.empty()) {
            nlohmann::json j;
            try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
            if (j.contains("fullscreen") && j["fullscreen"].is_boolean()) fullscreen = j["fullscreen"].get<bool>();
            if (j.contains("vsync") && j["vsync"].is_boolean()) vsyncEnabled = j["vsync"].get<bool>();
            if (j.contains("clamp_cam_x") && j["clamp_cam_x"].is_boolean()) clampCamX = j["clamp_cam_x"].get<bool>();
            if (j.contains("show_fps_counter") && j["show_fps_counter"].is_boolean()) defaultShowFpsCounter = j["show_fps_counter"].get<bool>();
            if (j.contains("show_detailed_debugger") && j["show_detailed_debugger"].is_boolean()) defaultShowDetailedDebugger = j["show_detailed_debugger"].get<bool>();
            if (j.contains("show_hitboxes") && j["show_hitboxes"].is_boolean()) defaultShowHitboxes = j["show_hitboxes"].get<bool>();
            if (j.contains("show_player_hitbox") && j["show_player_hitbox"].is_boolean()) defaultShowPlayerHitbox = j["show_player_hitbox"].get<bool>();
            if (j.contains("show_debug_view") && j["show_debug_view"].is_boolean()) defaultShowDebugView = j["show_debug_view"].get<bool>();
            if (j.contains("menu_music_enabled") && j["menu_music_enabled"].is_boolean()) menuMusicEnabled = j["menu_music_enabled"].get<bool>();
            if (j.contains("mute_all_audio") && j["mute_all_audio"].is_boolean()) muteAllAudio = j["mute_all_audio"].get<bool>();
            if (j.contains("fast_travel_delay") && j["fast_travel_delay"].is_number()) {
                fastTravelChangeDelay = std::clamp((float)j["fast_travel_delay"].get<double>(), 0.0f, 0.5f);
            }
            if (j.contains("music_volume") && j["music_volume"].is_number_integer()) musicVolume = std::clamp(j["music_volume"].get<int>(), 0, 128);
            if (j.contains("sfx_volume") && j["sfx_volume"].is_number_integer()) sfxVolume = std::clamp(j["sfx_volume"].get<int>(), 0, 128);
        } else {
            const std::string placeholderPath = "assets/client_settings.json";
            const std::string placeholderText = ReadTextFile(placeholderPath);
            bool usedPlaceholder = false;
            if (!placeholderText.empty()) {
                nlohmann::json j;
                try { j = nlohmann::json::parse(placeholderText); } catch (...) { j = nlohmann::json(); }
                if (!j.is_null()) {
                    if (j.contains("fullscreen") && j["fullscreen"].is_boolean()) fullscreen = j["fullscreen"].get<bool>();
                    if (j.contains("vsync") && j["vsync"].is_boolean()) vsyncEnabled = j["vsync"].get<bool>();
                    if (j.contains("clamp_cam_x") && j["clamp_cam_x"].is_boolean()) clampCamX = j["clamp_cam_x"].get<bool>();
                    if (j.contains("show_fps_counter") && j["show_fps_counter"].is_boolean()) defaultShowFpsCounter = j["show_fps_counter"].get<bool>();
                    if (j.contains("show_detailed_debugger") && j["show_detailed_debugger"].is_boolean()) defaultShowDetailedDebugger = j["show_detailed_debugger"].get<bool>();
                    if (j.contains("show_hitboxes") && j["show_hitboxes"].is_boolean()) defaultShowHitboxes = j["show_hitboxes"].get<bool>();
                    if (j.contains("show_player_hitbox") && j["show_player_hitbox"].is_boolean()) defaultShowPlayerHitbox = j["show_player_hitbox"].get<bool>();
                    if (j.contains("show_debug_view") && j["show_debug_view"].is_boolean()) defaultShowDebugView = j["show_debug_view"].get<bool>();
                    if (j.contains("menu_music_enabled") && j["menu_music_enabled"].is_boolean()) menuMusicEnabled = j["menu_music_enabled"].get<bool>();
                    if (j.contains("mute_all_audio") && j["mute_all_audio"].is_boolean()) muteAllAudio = j["mute_all_audio"].get<bool>();
                    if (j.contains("fast_travel_delay") && j["fast_travel_delay"].is_number()) {
                        fastTravelChangeDelay = std::clamp((float)j["fast_travel_delay"].get<double>(), 0.0f, 0.5f);
                    }
                    if (j.contains("music_volume") && j["music_volume"].is_number_integer()) musicVolume = std::clamp(j["music_volume"].get<int>(), 0, 128);
                    if (j.contains("sfx_volume") && j["sfx_volume"].is_number_integer()) sfxVolume = std::clamp(j["sfx_volume"].get<int>(), 0, 128);
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
    applyRenderVsync();
    SDL_Log("Build UUID: %s", buildUuid.c_str());
#if HAS_SDL_MIXER
    auto applyAudioVolumes = [&]() {
        if (!audioReady) return;
        const int appliedMusic = muteAllAudio ? 0 : musicVolume;
        const int appliedSfx = muteAllAudio ? 0 : sfxVolume;
        Mix_VolumeMusic(appliedMusic);
        Mix_Volume(-1, appliedSfx);
        if (coinSfx) Mix_VolumeChunk(coinSfx, appliedSfx);
        if (loseSfx) Mix_VolumeChunk(loseSfx, appliedSfx);
        if (victorySfx) Mix_VolumeChunk(victorySfx, appliedSfx);
        if (messageSfx) Mix_VolumeChunk(messageSfx, appliedSfx);
        if (bumperSfx) Mix_VolumeChunk(bumperSfx, appliedSfx);
    };
    applyAudioVolumes();
    auto ensureMenuMusic = [&]() {
        if (!audioReady || !menuMusic || !menuMusicEnabled) return;
        if (!menuMusicPlaying || !Mix_PlayingMusic()) {
            Mix_PlayMusic(menuMusic, -1);
            menuMusicPlaying = true;
        }
    };
    auto stopMenuMusic = [&]() {
        if (!audioReady) return;
        if (menuMusicPlaying) {
            Mix_HaltMusic();
            menuMusicPlaying = false;
        }
    };
#endif
    LevelManager levelManager;
    bool running = true;
    bool startupNoticeShown = false;
    FrontendMenuContext frontendCtx{};
    frontendCtx.win = win;
    frontendCtx.ren = ren;
    frontendCtx.gameTarget = gameTarget;
    frontendCtx.baseScreenW = kBaseScreenW;
    frontendCtx.baseScreenH = kBaseScreenH;
    frontendCtx.buildUuid = buildUuid;
    frontendCtx.running = &running;
    frontendCtx.fullscreen = &fullscreen;
    frontendCtx.vsyncEnabled = &vsyncEnabled;
    frontendCtx.clampCamX = &clampCamX;
    frontendCtx.defaultShowFpsCounter = &defaultShowFpsCounter;
    frontendCtx.defaultShowDetailedDebugger = &defaultShowDetailedDebugger;
    frontendCtx.defaultShowHitboxes = &defaultShowHitboxes;
    frontendCtx.defaultShowPlayerHitbox = &defaultShowPlayerHitbox;
    frontendCtx.defaultShowDebugView = &defaultShowDebugView;
    frontendCtx.menuMusicEnabled = &menuMusicEnabled;
    frontendCtx.muteAllAudio = &muteAllAudio;
    frontendCtx.fastTravelChangeDelay = &fastTravelChangeDelay;
    frontendCtx.musicVolume = &musicVolume;
    frontendCtx.sfxVolume = &sfxVolume;
#if HAS_SDL_MIXER
    frontendCtx.applyAudioVolumes = applyAudioVolumes;
#endif
    while (running) {
#if HAS_SDL_MIXER
        ensureMenuMusic();
#endif
        if (!startupNoticeShown) {
#if defined(__ANDROID__)
            SDL_Log("In Development: This build is in development.");
            startupNoticeShown = true;
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
                                     "In Development",
                                     "This build is in development.",
                                     win);
            startupNoticeShown = true;
            if (!running) break;
#endif
        }

        FrontendAction action = runFrontendMenu(frontendCtx);
        saveClientSettings();
        if (!running || action == FrontendAction::Quit) break;

        std::string selectedLevelPath = RunLevelSelect(win, ren);
        if (selectedLevelPath.empty()) {
            continue;
        }
#if HAS_SDL_MIXER
        stopMenuMusic();
#endif
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
        float levelTimerSeconds = 0.0f;
        float cameraSmoothingSuppressTimer = 0.0f;
        enum FastTravelDir {
            FT_UP = 0,
            FT_DOWN = 1,
            FT_LEFT = 2,
            FT_RIGHT = 3,
            FT_EXIT = 4
        };
        int fastTravelActiveDir = -1;
        int fastTravelPendingDir = -1;
        float fastTravelPendingTimer = 0.0f;
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
            if (!(reason && std::string(reason) == "noop")) {
                logFastTravelFlags(reason);
            }
        };
        auto queueFastTravelDirChange = [&](int dir) {
            if (dir == fastTravelActiveDir && fastTravelPendingDir < 0) return;
            if (fastTravelPendingDir == dir) return;
            fastTravelPendingDir = dir;
            fastTravelPendingTimer = std::max(0.0f, fastTravelChangeDelay);
        };
        float timeTravelTriggerCooldown = 0.0f;
#if HAS_SDL_MIXER
        Mix_Music* levelMusic = nullptr;
#endif

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
            cameraSmoothingSuppressTimer = 0.20f;
            setFastTravelActiveDir(-1, "reload");
            fastTravelPendingDir = -1;
            fastTravelPendingTimer = 0.0f;
            fastTravelOverlapWasActive = false;
            fastTravelBlendVx = 0.0f;
            fastTravelBlendVy = 0.0f;
            timeTravelTriggerCooldown = 0.35f;

            if (audioReady) {
#if HAS_SDL_MIXER
                std::string musicPath = levelManager.musicPath();
                if (levelMusic) {
                    Mix_FreeMusic(levelMusic);
                    levelMusic = nullptr;
                }
                levelMusic = musicPath.empty() ? nullptr : Mix_LoadMUS(ResolveAssetPath(musicPath).c_str());
                if (levelMusic) {
                    menuMusicPlaying = false;
                    Mix_PlayMusic(levelMusic, -1);
                } else if (!musicPath.empty()) {
                    SDL_Log("Could not load music: %s (%s)", musicPath.c_str(), Mix_GetError());
                }
#endif
            }
        };

        reloadLevel();

        bool levelRunning = true;
        bool paused = false;
        bool deathSequenceActive = false;
        bool deathLifeDeducted = false;
        float deathTimer = 0.0f;
        float fastTravelCooldown = 0.0f;
        bool showHitboxes = defaultShowHitboxes;
        bool showPlayerHitbox = defaultShowPlayerHitbox;
        bool showDebugView = defaultShowDebugView;
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
        SDL_Event e;
        Uint32 lastTicks = SDL_GetTicks();
        Uint32 lastPresentTicks = lastTicks;
        Uint32 nextPresentTicks = lastTicks;
        int renderFpsDisplay = 0;

        SDL_Rect pauseBtnContinue{0,0,0,0};
        SDL_Rect pauseBtnRestart{0,0,0,0};
        SDL_Rect pauseBtnExit{0,0,0,0};

        auto handlePauseSelect = [&](int sel) {
            pauseSelection = sel;
            if (pauseSelection == 0) {
                paused = false;
            } else if (pauseSelection == 1) {
                reloadLevel();
                paused = false;
            } else {
                returnToSelect = true;
                levelRunning = false;
            }
        };

        while (levelRunning) {
            Uint32 now = SDL_GetTicks();
            float dt = (now - lastTicks) / 1000.0f;
            lastTicks = now;
            const int updateFpsDisplay = std::clamp((dt > 0.0f) ? (int)(1.0f / dt) : 0, 0, 999999);
            bool temp1TouchedThisFrame = false;
            verticalWrapActive = false;
            activeBumperIndices.clear();
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
            }
            if (fastTravelCooldown > 0.0f) {
                fastTravelCooldown = std::max(0.0f, fastTravelCooldown - dt);
            }
            if (fastTravelPendingTimer > 0.0f) {
                fastTravelPendingTimer = std::max(0.0f, fastTravelPendingTimer - dt);
            }
            if (fastTravelPendingDir >= 0 && fastTravelPendingTimer <= 0.0f) {
                setFastTravelActiveDir(fastTravelPendingDir, "set_delayed");
                fastTravelPendingDir = -1;
            }
            if (timeTravelTriggerCooldown > 0.0f) {
                timeTravelTriggerCooldown = std::max(0.0f, timeTravelTriggerCooldown - dt);
            }
            if (cameraSmoothingSuppressTimer > 0.0f) {
                cameraSmoothingSuppressTimer = std::max(0.0f, cameraSmoothingSuppressTimer - dt);
            }
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
#if HAS_SDL_MIXER
                if (!audioReady || levelCompleteAudioChannel < 0 || !Mix_Playing(levelCompleteAudioChannel)) {
                    levelCompleteCounting = true;
                }
#else
                levelCompleteCounting = true;
#endif
            }
            if (levelCompleteActive && levelCompleteCounting && !paused) {
                // Uncapped payout during level complete: process full remaining bonus immediately.
                int payoutPerFrame = std::max(1, levelCompleteCoinBonus + levelCompleteTimeScore);
                int coinStep = std::min(levelCompleteCoinBonus, payoutPerFrame);
                if (coinStep > 0) {
                    levelCompleteCoinBonus -= coinStep;
                    scoreCount += coinStep;
                    levelCompleteAccountedScore += coinStep;
#if HAS_SDL_MIXER
                    if (audioReady && messageSfx) Mix_PlayChannel(-1, messageSfx, 0);
#endif
                }
                int timeStep = std::min(levelCompleteTimeScore, payoutPerFrame);
                if (timeStep > 0) {
                    levelCompleteTimeScore -= timeStep;
                    scoreCount += timeStep;
                    levelCompleteAccountedScore += timeStep;
#if HAS_SDL_MIXER
                    if (audioReady && messageSfx) Mix_PlayChannel(-1, messageSfx, 0);
#endif
                }
                if (levelCompleteCoinBonus <= 0 && levelCompleteTimeScore <= 0) {
                    if (!levelCompleteNextPath.empty()) {
                        levelManager.setLevelPath(levelCompleteNextPath);
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
                if (!windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy)) continue;
                float px = (float)gx;
                float py = (float)gy;
                if (pointInRectF(px, py, leftHit)) touchLeft = true;
                if (pointInRectF(px, py, rightHit)) touchRight = true;
                if (pointInRectF(px, py, downHit)) touchDown = true;
                if (pointInRectF(px, py, jumpHit)) touchJump = true;
            }
        };
        computeTouchButtons();

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; levelRunning = false; }
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && debugWin &&
                e.window.windowID == SDL_GetWindowID(debugWin)) {
                SDL_HideWindow(debugWin);
                showDetailedDebugger = false;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && debugWin && debugRen &&
                e.button.windowID == SDL_GetWindowID(debugWin) &&
                e.button.button == SDL_BUTTON_LEFT) {
                const int mx = e.button.x;
                const int my = e.button.y;
                // Touch-friendly submenu tabs.
                SDL_Rect tab0{12, 38, 130, 36};
                SDL_Rect tab1{152, 38, 130, 36};
                SDL_Rect tab2{292, 38, 130, 36};
                SDL_Rect tab3{432, 38, 130, 36};
                SDL_Point pt{mx, my};
                if (SDL_PointInRect(&pt, &tab0)) detailedDebugSubmenu = 0;
                else if (SDL_PointInRect(&pt, &tab1)) detailedDebugSubmenu = 1;
                else if (SDL_PointInRect(&pt, &tab2)) detailedDebugSubmenu = 2;
                else if (SDL_PointInRect(&pt, &tab3)) detailedDebugSubmenu = 3;
                if (detailedDebugSubmenu == 1) {
                    SDL_Rect prevBtn{12, 92, 120, 34};
                    SDL_Rect nextBtn{142, 92, 120, 34};
                    if (SDL_PointInRect(&pt, &prevBtn)) detailedDebugObjectIndex--;
                    if (SDL_PointInRect(&pt, &nextBtn)) detailedDebugObjectIndex++;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                paused = true;
            }
            if (e.type == SDL_FINGERDOWN) {
                activeTouches[e.tfinger.fingerID] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
            }
            if (e.type == SDL_FINGERMOTION) {
                activeTouches[e.tfinger.fingerID] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
            }
            if (e.type == SDL_FINGERUP) {
                activeTouches.erase(e.tfinger.fingerID);
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
                if (e.key.key == SDLK_F6) {
                    showFpsCounter = !showFpsCounter;
                }
                if (e.key.key == SDLK_F10) {
                    clampCamX = !clampCamX;
                }
                if (e.key.key == SDLK_F5) {
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
                }
                if (e.key.key == SDLK_F4) {
                    detailedDebugSubmenu = (detailedDebugSubmenu + 1) % 4;
                }
                if (showDetailedDebugger && detailedDebugSubmenu == 1) {
                    if (e.key.key == SDLK_UP) detailedDebugObjectIndex--;
                    if (e.key.key == SDLK_DOWN) detailedDebugObjectIndex++;
                }
                if (e.key.key == SDLK_F9) {
                    std::string nextPath = levelManager.nextLevelPath();
                    if (!nextPath.empty()) {
                        levelManager.setLevelPath(nextPath);
                        reloadLevel();
                    }
                }
                if (!levelCompleteActive && (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK)) {
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
            if (paused && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                if (windowToGamePoint(e.button.x, e.button.y, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy)) {
                    SDL_Point pt{gx, gy};
                    if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                    else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                    else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
                }
            }
            if (paused && e.type == SDL_FINGERDOWN) {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                int wx = (int)(e.tfinger.x * winW);
                int wy = (int)(e.tfinger.y * winH);
                int gx = 0, gy = 0;
                if (windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy)) {
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
                    reloadLevel();
                    continue;
                } else {
                    returnToSelect = true;
                    levelRunning = false;
                    continue;
                }
            }
        }

        if (!paused && !deathSequenceActive) {
            const float frameStartX = player.x;
            const float frameStartY = player.y;
            std::string movementReasons;
            auto addMovementReason = [&](const char* reason) {
                if (!reason || !*reason) return;
                if (movementReasons.find(reason) != std::string::npos) return;
                if (!movementReasons.empty()) movementReasons += ",";
                movementReasons += reason;
            };
            bool fastTravelReload = false;
            bool fastTravelTriggered = false;
            auto ejectFromFastTravel = [&](int dirHint) {
                float ex = 0.0f;
                float ey = 0.0f;
                const float blendLen = std::sqrt(fastTravelBlendVx * fastTravelBlendVx + fastTravelBlendVy * fastTravelBlendVy);
                if (blendLen > 0.001f) {
                    ex = fastTravelBlendVx / blendLen;
                    ey = fastTravelBlendVy / blendLen;
                } else {
                    if (dirHint == FT_UP) ey = -1.0f;
                    else if (dirHint == FT_DOWN) ey = 1.0f;
                    else if (dirHint == FT_LEFT) ex = -1.0f;
                    else if (dirHint == FT_RIGHT) ex = 1.0f;
                    else ey = -1.0f;
                }

                player.x += ex * 18.0f;
                player.y += ey * 18.0f;

                // Try to push player out if the exit lands inside a solid tile.
                if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                    for (int i = 0; i < 8; ++i) {
                        player.x += ex * 4.0f;
                        player.y += ey * 4.0f;
                        if (!RectHitsSolid(map, player.x, player.y, player.w, player.h)) break;
                    }
                }
                if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                    for (int i = 0; i < 12; ++i) {
                        player.y -= 4.0f;
                        if (!RectHitsSolid(map, player.x, player.y, player.w, player.h)) break;
                    }
                }

                const float ejectSpeed = 520.0f;
                player.vx = ex * ejectSpeed;
                player.vy = ey * ejectSpeed;
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
                    const float px1 = player.x;
                    const float px2 = player.x + (float)player.w;
                    const float py1 = player.y;
                    const float py2 = player.y + (float)player.h;
                    const bool overlap = (px2 > ox) && (px1 < ox + ow) && (py2 > oy) && (py1 < oy + oh);
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
                if (overlapDir >= 0 && !fastTravelOverlapWasActive) {
                    queueFastTravelDirChange(overlapDir);
                }
                if (overlapDir < 0) {
                    fastTravelPendingDir = -1;
                    fastTravelPendingTimer = 0.0f;
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
                if (positionChanged) addMovementReason("fast_travel");
                player.vx = fastTravelBlendVx;
                player.vy = fastTravelBlendVy;
                player.onGround = false;
                if (positionChanged || fastTravelReload) {
                    fastTravelTriggered = true;
                }
                if (positionChanged) {
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
                reloadLevel();
                continue;
            }

            float touchMove = 0.0f;
            if (touchLeft) touchMove -= 1.0f;
            if (touchRight) touchMove += 1.0f;
            const bool fastTravelEnabled = fastTravelTriggered;
            PlayerUpdateResult upd = PlayerUpdateResult::RenderOnly;
            const float beforeNormalX = player.x;
            const float beforeNormalY = player.y;
            if (!fastTravelEnabled) {
                upd = UpdatePlayerMovement(
                    player, map, dt, jumpBufferMax, movementCfg,
                    touchMove, touchDown, touchJump, inputMove, inputDown
                );
                if (std::fabs(player.x - beforeNormalX) > 0.01f || std::fabs(player.y - beforeNormalY) > 0.01f) {
                    addMovementReason("normal_movement");
                }
            } else {
                // Fast-travel mode disables normal movement/physics.
                inputMove = 0.0f;
                inputDown = false;
            }
            {
                const float beforeWrapX = player.x;
                const float beforeWrapY = player.y;
                const int currentLevelId = parseLevelIdFromLevelPath(levelManager.levelPath());
                const bool horizontalWrapActive = (currentLevelId == 39 || currentLevelId == 40);
                if (((currentLevelId == 29 &&
                    player.x > 1250.0f) || (currentLevelId == 30 &&
                    player.x > 1250.0f) || currentLevelId == 39 ||
                     currentLevelId == 40 || currentLevelId == 53 || currentLevelId == 54)) {
                    verticalWrapActive = true;
                }
                if ((currentLevelId == 21 || currentLevelId == 22 || currentLevelId == 23 || currentLevelId == 24) &&
                    player.x > 3211.0f && player.x < 4559.0f) {
                    verticalWrapActive = true;
                }
                if (verticalWrapActive) {
                    const float mapHeightPx = (float)(map.h * map.tileSize);
                    while (player.y + (float)player.h < 0.0f) player.y += mapHeightPx;
                    while (player.y >= mapHeightPx) player.y -= mapHeightPx;
                }
                if (horizontalWrapActive) {
                    const float mapWidthPx = (float)(map.w * map.tileSize);
                    while (player.x + (float)player.w < 0.0f) player.x += mapWidthPx;
                    while (player.x >= mapWidthPx) player.x -= mapWidthPx;
                }
                if (std::fabs(player.x - beforeWrapX) > 0.01f || std::fabs(player.y - beforeWrapY) > 0.01f) {
                    addMovementReason("world_wrap");
                }
            }
            if (upd == PlayerUpdateResult::Reloaded) {
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
                                    reloadLevel();
                                    continue;
                            }
                        }
                    }
                }
                deathSequenceActive = true;
                deathLifeDeducted = false;
                deathTimer = 0.0f;
#if HAS_SDL_MIXER
                if (audioReady) {
                    Mix_HaltChannel(-1);
                    Mix_HaltMusic();
                    if (loseSfx) Mix_PlayChannel(-1, loseSfx, 0);
                }
#endif
                continue;
            }

            if (upd == PlayerUpdateResult::RenderOnly) {
                goto RENDER_ONLY;
            }

            int collectedNow = levelManager.collectCoinsAtPlayer(map, player);
            if (collectedNow > 0) {
#if HAS_SDL_MIXER
                if (audioReady && coinSfx) Mix_PlayChannel(-1, coinSfx, 0);
#endif
            }
            if (timeTravelTriggerCooldown <= 0.0f) {
                levelManager.updateTimeWarpIdAtPlayer(map, player);
            }

            // Spring objects (id 31): bounce player upward on top contact.
            for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
                const auto& obj = objects[objIdx];
                if (obj.id != "31") continue;
                const float sx = obj.x - 16.0f;
                const float sy = obj.y - 16.0f;
                const float sw = 32.0f;
                const float sh = 32.0f;
                const float px1 = player.x;
                const float px2 = player.x + (float)player.w;
                const float py2 = player.y + (float)player.h;
                const bool xOverlap = (px2 > sx) && (px1 < sx + sw);
                const bool nearTop = (py2 >= sy) && (py2 <= sy + sh * 0.75f);
                if (xOverlap && nearTop && player.vy >= 0.0f) {
                    player.y = sy - (float)player.h;
                    player.vy = -1800.0f;
                    player.onGround = false;
                    addMovementReason("spring");
                    break;
                }
            }
            // Bumper objects (id 46): vertical bounce, up/down depending approach.
            for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
                const auto& obj = objects[objIdx];
                if (obj.id != "46") continue;
                const float bx = obj.x - 16.0f;
                const float by = obj.y - 16.0f;
                const float bw = 32.0f;
                const float bh = 32.0f;
                const float px1 = player.x;
                const float px2 = player.x + (float)player.w;
                const float py1 = player.y;
                const float py2 = player.y + (float)player.h;
                const bool overlap = (px2 > bx) && (px1 < bx + bw) && (py2 > by) && (py1 < by + bh);
                if (!overlap) continue;

                const float playerCY = player.y + player.h * 0.5f;
                const float bumperCY = by + bh * 0.5f;
                if (playerCY <= bumperCY && player.vy >= 0.0f) {
                    player.y = by - (float)player.h;
                    player.vy = -1200.0f;
                    player.onGround = false;
                    addMovementReason("bumper");
                    activeBumperIndices.insert(objIdx);
#if HAS_SDL_MIXER
                    if (audioReady && bumperSfx) Mix_PlayChannel(-1, bumperSfx, 0);
#endif
                    break;
                }
                if (playerCY > bumperCY && player.vy <= 0.0f) {
                    player.y = by + bh;
                    player.vy = 1200.0f;
                    player.onGround = false;
                    addMovementReason("bumper");
                    activeBumperIndices.insert(objIdx);
#if HAS_SDL_MIXER
                    if (audioReady && bumperSfx) Mix_PlayChannel(-1, bumperSfx, 0);
#endif
                    break;
                }
            }
            if (std::fabs(player.x - frameStartX) > 0.01f || std::fabs(player.y - frameStartY) > 0.01f) {
                if (movementReasons.empty()) movementReasons = "unknown";
                SDL_Log("player move: from=(%.2f, %.2f) to=(%.2f, %.2f) delta=(%.2f, %.2f) reason=%s",
                        frameStartX, frameStartY,
                        player.x, player.y,
                        player.x - frameStartX, player.y - frameStartY,
                        movementReasons.c_str());
            }

            if (!levelCompleteActive && playerTouchesTileId(map, player, 30, 68)) {
                levelCompleteActive = true;
                levelCompleteCounting = false;
                levelCompleteNextPath = levelManager.nextLevelPath();
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
#if HAS_SDL_MIXER
                if (audioReady) {
                    Mix_HaltMusic();
                    if (victorySfx) {
                        levelCompleteAudioChannel = Mix_PlayChannel(-1, victorySfx, 0);
                    } else {
                        levelCompleteCounting = true;
                    }
                } else {
                    levelCompleteCounting = true;
                }
#else
                levelCompleteCounting = true;
#endif
            }
        }

RENDER_ONLY:

        const bool renderWrapX = true;
        const bool renderWrapY = true;
        const int renderLevelId = parseLevelIdFromLevelPath(levelManager.levelPath());
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

        const float freeCamX = player.x + player.w * 0.5f - screenW * 0.5f;
        float camX = freeCamX;
        const float freeCamY = player.y + player.h * 0.5f - screenH * 0.5f;
        float camY = freeCamY;
        float maxCamX = map.w * map.tileSize - screenW - map.tileSize;
        float maxCamY = map.h * map.tileSize - screenH;
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

        SDL_SetRenderTarget(ren, gameTarget);
        SDL_SetRenderDrawColor(ren, 12, 14, 18, 255);
        SDL_RenderClear(ren);

        // Parallax background (3 layers)
        if (bgTex && !bgFrameByName.empty()) {
            auto getBg = [&](const std::string& name) -> const Frame* {
                auto it = bgFrameByName.find(name);
                if (it != bgFrameByName.end()) return &it->second;
                if (name.size() > 4 && name.substr(name.size() - 4) == ".png") {
                    std::string noExt = name.substr(0, name.size() - 4);
                    it = bgFrameByName.find(noExt);
                    if (it != bgFrameByName.end()) return &it->second;
                }
                return nullptr;
            };
            struct Layer {
                const char* frame;
                bool verticalParallax;
            };
            Layer layers[3] = {
                {"w1b.png", false},
                {"w1f.png", true},
                {"w1f.png", true}
            };
            const float parallaxFactor = 0.5f;
            for (const auto& layer : layers) {
                const Frame* f = getBg(layer.frame);
                if (!f) continue;
                int fw = f->rotated ? f->rect.h : f->rect.w;
                int fh = f->rotated ? f->rect.w : f->rect.h;
                if (fw <= 0 || fh <= 0) continue;
                float ox = std::fmod(camX * parallaxFactor, (float)fw);
                float maxCamY = std::max(1.0f, (float)(map.h * map.tileSize - screenH));
                float parallaxCamY = maxCamY - camY;
                float t = std::clamp((parallaxCamY / maxCamY) * parallaxFactor, 0.0f, 1.0f);
                if (!layer.verticalParallax) t = 0.0f;
                if (ox < 0) ox += fw;
                float yF = (float)screenH - (float)fh + t * (float)fh;
                int y = (int)std::lround(yF);
                for (int x = -1; x <= screenW / fw + 1; ++x) {
                    SDL_Rect dst{
                        (int)(x * fw - ox),
                        y,
                        fw,
                        fh
                    };
                    renderFrame(ren, bgTex, *f, dst);
                }
            }
        }

        map.renderBgDebug(ren, camX, camY);

        const int tileMinX = renderWrapX
            ? ((int)std::floor(camX / map.tileSize) - 1)
            : std::max(0, (int)std::floor(camX / map.tileSize) - 1);
        const int tileMaxX = renderWrapX
            ? ((int)std::floor((camX + screenW) / map.tileSize) + 1)
            : std::min(map.w - 1, (int)std::floor((camX + screenW) / map.tileSize) + 1);
        const int tileMinY = renderWrapY
            ? ((int)std::floor(camY / map.tileSize) - 1)
            : std::max(0, (int)std::floor(camY / map.tileSize) - 1);
        const int tileMaxY = renderWrapY
            ? ((int)std::floor((camY + screenH) / map.tileSize) + 1)
            : std::min(map.h - 1, (int)std::floor((camY + screenH) / map.tileSize) + 1);

        // Tile textures (DF_Blocks)
        if (blocksTex && !blocksFrameList.empty()) {
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
                    } else if (levelManager.worldId() == 3 && id >= 3 && id <= 11) {
                        const unsigned int h = (unsigned int)mapX * 73856093u ^
                                               (unsigned int)mapY * 19349663u ^
                                               (unsigned int)id * 83492791u;
                        const int v = (int)(h % 10u) + 1; // 1..10
                        const std::string key = std::string("3.") + std::to_string(v);
                        auto it = blocksFrameByName.find(key);
                        if (it != blocksFrameByName.end()) {
                            frame = &it->second;
                        } else {
                            auto itPng = blocksFrameByName.find(key + ".png");
                            if (itPng != blocksFrameByName.end()) frame = &itPng->second;
                        }
                        if (!frame) frame = blocksFrameById[id];
                    } else {
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
            std::string frameKey = obj.id;
            if (obj.id == "46" && activeBumperIndices.find(objIdx) != activeBumperIndices.end()) {
                frameKey = "Bumper";
            }
            auto mapIt = entityFrameKeyByObjectId.find(obj.id);
            if (mapIt != entityFrameKeyByObjectId.end()) frameKey = mapIt->second;

            auto it = entitiesFrameByName.find(frameKey);
            if (it != entitiesFrameByName.end()) {
                of = &it->second;
            } else {
                std::string pngKey = frameKey + ".png";
                it = entitiesFrameByName.find(pngKey);
                if (it != entitiesFrameByName.end()) of = &it->second;
            }
            if (!of) of = defaultEntityFrame;

            if (!isFastTravelChanger && entitiesTex && of) {
                const int fw = 32;
                const int fh = 32;
                SDL_Rect dst{
                    (int)std::lround(entityBaseX - camX),
                    (int)std::lround(entityBaseY - camY),
                    fw,
                    fh
                };
                renderFrame(ren, entitiesTex, *of, dst);
                if (renderWrapY) {
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTop = dst;
                    SDL_Rect dstBottom = dst;
                    dstTop.y -= wrapH;
                    dstBottom.y += wrapH;
                    renderFrame(ren, entitiesTex, *of, dstTop);
                    renderFrame(ren, entitiesTex, *of, dstBottom);
                }
                if (renderWrapX) {
                    const int wrapW = map.w * map.tileSize;
                    SDL_Rect dstLeft = dst;
                    SDL_Rect dstRight = dst;
                    dstLeft.x -= wrapW;
                    dstRight.x += wrapW;
                    renderFrame(ren, entitiesTex, *of, dstLeft);
                    renderFrame(ren, entitiesTex, *of, dstRight);
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
                    renderFrame(ren, entitiesTex, *of, dstTL);
                    renderFrame(ren, entitiesTex, *of, dstTR);
                    renderFrame(ren, entitiesTex, *of, dstBL);
                    renderFrame(ren, entitiesTex, *of, dstBR);
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

        SDL_FRect pr{ player.x - camX, player.y - camY, (float)player.w, (float)player.h };
        {
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

            auto framesFromNames = [&](const std::vector<std::string>& names) {
                std::vector<const Frame*> out;
                out.reserve(names.size());
                for (const auto& n : names) out.push_back(getPlayerFrame(n));
                return out;
            };

            const std::vector<const Frame*> idle = framesFromNames(animCfg.idle);
            const std::vector<const Frame*> walk = framesFromNames(animCfg.walk);
            const std::vector<const Frame*> jump = framesFromNames(animCfg.jump);
            const std::vector<const Frame*> fall = framesFromNames(animCfg.fall);
            const std::vector<const Frame*> crouch = framesFromNames(animCfg.crouch);
            const std::vector<const Frame*> skid = framesFromNames(animCfg.skid);
            const std::vector<const Frame*> hurt = framesFromNames(animCfg.hurt);
            const std::vector<const Frame*> death = framesFromNames(animCfg.death);

            int newAnim = ANIM_IDLE;
            float vxAbs = std::abs(player.vx);
            if (player.freeMove) {
                newAnim = ANIM_IDLE;
            } else if (!player.onGround) {
                newAnim = (player.vy < 0.0f) ? ANIM_JUMP : ANIM_FALL;
            } else if (inputDown) {
                newAnim = ANIM_CROUCH;
            } else if (vxAbs > 8.0f) {
                bool opposing = (player.vx > 20.0f && inputMove < -0.1f) || (player.vx < -20.0f && inputMove > 0.1f);
                newAnim = opposing ? ANIM_SKID : ANIM_WALK;
            }

            if (inputMove < -0.1f) player.facing = -1;
            if (inputMove > 0.1f) player.facing = 1;

            if (newAnim != player.anim) {
                player.anim = newAnim;
                player.animTime = 0.0f;
            } else {
                player.animTime += dt;
            }

            const std::vector<const Frame*>* seq = &idle;
            switch (player.anim) {
                case ANIM_WALK: seq = &walk; break;
                case ANIM_JUMP: seq = &jump; break;
                case ANIM_FALL: seq = &fall; break;
                case ANIM_CROUCH: seq = &crouch; break;
                case ANIM_SKID: seq = &skid; break;
                case ANIM_HURT: seq = &hurt; break;
                case ANIM_DEATH: seq = &death; break;
                default: seq = &idle; break;
            }

            const Frame* renderFramePtr = nullptr;
            int frameCount = (int)seq->size();
            if (frameCount > 0) {
                float fps = animCfg.fps > 0.0f ? animCfg.fps : 15.0f;
                int idx = (int)(player.animTime * fps) % frameCount;
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
            }
        }

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

        if (paused) {
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
                SDL_Rect panelDst{screenW / 2 - panelFrame.rect.w / 2,
                                  screenH / 2 - panelFrame.rect.h / 2,
                                  panelFrame.rect.w, panelFrame.rect.h};
                renderFrame(ren, pauseTex, panelFrame, panelDst);

                const Frame& continueFrame = pauseFrames["Continuebtn"];
                const Frame& restartFrame = pauseFrames["Retartbtn"];
                const Frame& exitFrame = pauseFrames["exitbtn"];

                int contW = continueFrame.rotated ? continueFrame.rect.h : continueFrame.rect.w;
                int contH = continueFrame.rotated ? continueFrame.rect.w : continueFrame.rect.h;
                int restartW = restartFrame.rotated ? restartFrame.rect.h : restartFrame.rect.w;
                int restartH = restartFrame.rotated ? restartFrame.rect.w : restartFrame.rect.h;
                int exitW = exitFrame.rotated ? exitFrame.rect.h : exitFrame.rect.w;
                int exitH = exitFrame.rotated ? exitFrame.rect.w : exitFrame.rect.h;

                int spacing = 18;
                int totalW = contW + restartW + exitW + spacing * 2;
                int startX = screenW / 2 - totalW / 2;
                int centerY = screenH / 2 + 30;

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
                SDL_Rect panel{screenW / 2 - 140, screenH / 2 - 90, 280, 180};
                SDL_SetRenderDrawColor(ren, 30, 30, 38, 230);
                SDL_RenderFillRect(ren, &panel);
                SDL_SetRenderDrawColor(ren, 80, 90, 110, 255);
                SDL_RenderDrawRect(ren, &panel);

                SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
                DrawText(ren, screenW / 2 - 60, screenH / 2 - 70, 3, "PAUSED");

                SDL_Rect resumeBtn{screenW / 2 - 140, screenH / 2 + 10, 100, 36};
                SDL_Rect restartBtn{screenW / 2 - 50, screenH / 2 + 10, 100, 36};
                SDL_Rect quitBtn{screenW / 2 + 40, screenH / 2 + 10, 100, 36};
                pauseBtnContinue = resumeBtn;
                pauseBtnRestart = restartBtn;
                pauseBtnExit = quitBtn;

                SDL_SetRenderDrawColor(ren, pauseSelection == 0 ? 70 : 45, pauseSelection == 0 ? 120 : 70, pauseSelection == 0 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &resumeBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, resumeBtn.x + 35, resumeBtn.y + 8, 2, "RESUME");

                SDL_SetRenderDrawColor(ren, pauseSelection == 1 ? 70 : 45, pauseSelection == 1 ? 120 : 70, pauseSelection == 1 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &restartBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, restartBtn.x + 18, restartBtn.y + 8, 2, "RESTART");

                SDL_SetRenderDrawColor(ren, pauseSelection == 2 ? 120 : 70, pauseSelection == 2 ? 70 : 50, pauseSelection == 2 ? 70 : 60, 255);
                SDL_RenderFillRect(ren, &quitBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, quitBtn.x + 30, quitBtn.y + 8, 2, "QUIT");
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
            const std::string fpsText = std::string("UFPS: ") + std::to_string(updateFpsDisplay) +
                                        " RFPS: " + std::to_string(renderFpsDisplay);
            const int fpsScale = 2;
            const int fpsX = screenW - 10 - MeasureTextWidth(fpsScale, fpsText);
            DrawText(ren, fpsX, 10, fpsScale, fpsText);
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
                    int centerX = screenX + map.tileSize / 2;
                    int centerY = screenY + map.tileSize / 2;
                    DrawNumberCentered(ren, centerX, centerY - (5 * 2) / 2, 2, id);
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

        SDL_SetRenderTarget(ren, nullptr);
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Rect presentDst = computePresentRect(winW, winH, kBaseScreenW, kBaseScreenH);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, gameTarget, nullptr, &presentDst);
        {
            const Uint32 presentNow = SDL_GetTicks();
            const bool shouldPresent = !vsyncEnabled || presentNow >= nextPresentTicks;
            if (shouldPresent) {
                SDL_RenderPresent(ren);
                const Uint32 presentedAt = SDL_GetTicks();
                const Uint32 presentDelta = presentedAt - lastPresentTicks;
                renderFpsDisplay = std::clamp((presentDelta > 0) ? (int)(1000u / presentDelta) : 0, 0, 999999);
                lastPresentTicks = presentedAt;
                // Render cadence limiter only; update loop remains uncapped.
                nextPresentTicks = presentedAt + 16;
            }
        }
        if (showDetailedDebugger && debugRen && debugWin) {
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
        if (audioReady) {
#if HAS_SDL_MIXER
            if (levelMusic) {
            Mix_HaltMusic();
            Mix_FreeMusic(levelMusic);
            levelMusic = nullptr;
            }
#endif
        }
        if (returnToSelect) continue;
    }

    if (blocksTex) SDL_DestroyTexture(blocksTex);
    if (entitiesTex) SDL_DestroyTexture(entitiesTex);
    if (pauseTex) SDL_DestroyTexture(pauseTex);
    if (gameTarget) SDL_DestroyTexture(gameTarget);
    if (debugRen && debugRen != ren) SDL_DestroyRenderer(debugRen);
    if (debugWin && debugWin != win) SDL_DestroyWindow(debugWin);
    ShutdownTextRenderer();
    if (audioReady) {
#if HAS_SDL_MIXER
        Mix_HaltMusic();
        Mix_CloseAudio();
        Mix_Quit();
#endif
    }
#if HAS_SDL_MIXER
    if (coinSfx) Mix_FreeChunk(coinSfx);
    if (loseSfx) Mix_FreeChunk(loseSfx);
    if (victorySfx) Mix_FreeChunk(victorySfx);
    if (messageSfx) Mix_FreeChunk(messageSfx);
    if (bumperSfx) Mix_FreeChunk(bumperSfx);
    if (menuMusic) Mix_FreeMusic(menuMusic);
#endif
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Log("Shutting down");
    CrashReporter::stop();
    SDL_Quit();
    return 0;
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL
Java_com_Benno111_dorfplatformertimetravel_MainActivity_runNativeGame(JNIEnv*, jobject) {
    char arg0[] = "platformer";
    char* argv[] = { arg0, nullptr };
    main(1, argv);
}
#endif
