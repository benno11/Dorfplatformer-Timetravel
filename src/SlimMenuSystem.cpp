#include "BuildInfo.h"
#include "SlimMenuSystem.h"
#include "SlimMenuLayout.h"
#include "DifficultySelector.h"

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "AssetPath.h"
#include "NetworkTransport.h"
#include "GameSupport.h"
#include "LevelSelect.h"
#include "LevelManager.h"
#include "GameProgression.h"
#include "UiScale.h"

#if defined(DrawText)
#undef DrawText
#endif

#include "TextRenderer.h"

#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif
#ifdef DrawText
#undef DrawText
#endif

namespace {

constexpr const char* kSavedGameSelectionToken = "__DF_SAVEGAME_CONTINUE__";
constexpr const char* kSlimMenuUserAgent = PLATFORMER_CLIENT_USER_AGENT;

struct MenuItem {
    enum class Type { Text, Button, Slider };
    Type type = Type::Text;
    std::string label;
    std::string command;
    std::string arg;
    SlimMenuLayout::Align horizontal = SlimMenuLayout::Align::Center;
    SlimMenuLayout::Align vertical = SlimMenuLayout::Align::Center;
    SlimMenuLayout::Align labelHorizontal = SlimMenuLayout::Align::Center;
    SlimMenuLayout::Align labelVertical = SlimMenuLayout::Align::Center;
};

struct SlimMenu {
    std::string name;
    std::string title;
    std::vector<MenuItem> items;
    float scale = 1.0f;
    SlimMenuLayout::Align horizontal = SlimMenuLayout::Align::Center;
    SlimMenuLayout::Align vertical = SlimMenuLayout::Align::Center;
};

struct SlimLevelEntry {
    std::string label;
    std::string path;
    int difficulty = 0;
    int downloads = 0;
    int likes = 0;
    int dislikes = 0;
    std::string displayName;
};

struct OnlineLevelPage {
    std::vector<SlimLevelEntry> levels;
    int page = 0;
    int pageCount = 1;
    int total = 0;
};

struct SlimRuntime {
    struct PendingGet {
        std::string variable;
        std::future<std::string> response;
    };
    SDL_Texture* menuTexture = nullptr;
    bool ownsMenuTexture = true;
    std::unordered_map<std::string, Frame> menuFrames;
    SDL_Texture* difficultyTexture = nullptr;
    Frame difficultyFrame{};
    bool difficultyAssetLoaded = false;
    std::unordered_map<std::string, std::string> scriptSources;
    std::vector<SlimLevelEntry> localLevels, onlineLevels;
    bool localLevelsLoaded = false, onlineLevelsLoaded = false;
    std::future<OnlineLevelPage> onlineLevelsRequest;
    std::string onlineLevelsKey, pendingOnlineLevelsKey;
    int onlinePageCount = 1, onlineTotal = 0;
    std::future<bool> accountRoleRequest;
    std::string accountRoleKey, pendingAccountRoleKey;
    std::unordered_map<std::string, std::string> vars;
    std::string waitingBind;
    std::vector<PendingGet> pendingGets;
    float scrollY = 0.0f;
    bool difficultyPopupOpen = false, difficultySaving = false;
    SlimLevelEntry difficultyLevel;
    int difficultyChoice = 0, difficultyFocus = 0;
    std::string difficultyStatus;

    SlimRuntime() = default;
    SlimRuntime(const SlimRuntime&) = delete;
    SlimRuntime& operator=(const SlimRuntime&) = delete;
    ~SlimRuntime() {
        if (menuTexture && ownsMenuTexture) SDL_DestroyTexture(menuTexture);
        if (difficultyTexture) SDL_DestroyTexture(difficultyTexture);
    }
};

constexpr SDL_Rect kMenuExitRect{12, 12, 44, 44};

bool menuNetworkBusy(const SlimRuntime& runtime) {
    if (runtime.difficultySaving) return true;
    auto pending = [](const auto& request) {
        return request.valid() && request.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
    };
    if (pending(runtime.onlineLevelsRequest) || pending(runtime.accountRoleRequest)) return true;
    return std::any_of(runtime.pendingGets.begin(), runtime.pendingGets.end(),
        [&](const SlimRuntime::PendingGet& request) { return pending(request.response); });
}

struct ParsedLine {
    int lineNo = 0;
    std::string text;
    std::vector<std::string> parts;
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

std::string toLowerAscii(std::string text) {
    for (char& ch : text) ch = (char)std::tolower(static_cast<unsigned char>(ch));
    return text;
}

std::string normalizeVarName(std::string name) {
    name = trim(name);
    if (name.size() >= 3 && name[0] == '$' && name[1] == '{' && name.back() == '}') {
        name = name.substr(2, name.size() - 3);
    }
    return name;
}

constexpr int kSaveSlotCount = 3;

int parseSaveSlotArg(const std::string& arg) {
    return std::clamp(std::atoi(arg.c_str()) - 1, 0, kSaveSlotCount - 1);
}

std::filesystem::path saveSlotPath(int slotIndex) {
    const int slot = std::clamp(slotIndex, 0, kSaveSlotCount - 1);
    return std::filesystem::path(GetAppSaveRootPath()) / "saves" / ("save_slot_" + std::to_string(slot + 1) + ".json");
}

bool saveSlotExists(int slotIndex) {
    std::error_code ec;
    return std::filesystem::exists(saveSlotPath(slotIndex), ec);
}

std::string saveSlotStatus(int slotIndex, const SlimMenuContext& ctx) {
    const int slot = std::clamp(slotIndex, 0, kSaveSlotCount - 1);
    if (ctx.activeSaveSlotIndex && *ctx.activeSaveSlotIndex == slot) return "ACTIVE";
    return saveSlotExists(slot) ? "READY" : "EMPTY";
}

bool* boolSettingPtr(const std::string& var, SlimMenuContext& ctx) {
    if (var == "fullscreen") return ctx.fullscreen;
    if (var == "vsync_enabled") return ctx.vsyncEnabled;
    if (var == "camera_clamp_x") return ctx.clampCamX;
    if (var == "show_fps_counter") return ctx.defaultShowFpsCounter;
    if (var == "show_detailed_debugger") return ctx.defaultShowDetailedDebugger;
    if (var == "show_hitboxes") return ctx.defaultShowHitboxes;
    if (var == "show_player_hitbox") return ctx.defaultShowPlayerHitbox;
    if (var == "show_debug_view") return ctx.defaultShowDebugView;
    if (var == "hide_unknown_object_types") return ctx.defaultHideUnknownObjectTypes;
    if (var == "power_management_enabled") return ctx.powerManagementEnabled;
    if (var == "low_power_mode_enabled") return ctx.lowPowerModeEnabled;
    if (var == "show_experimental_features") return ctx.showExperimentalFeatures;
    if (var == "menu_music_enabled") return ctx.menuMusicEnabled;
    if (var == "mute_all_audio") return ctx.muteAllAudio;
    if (var == "level_select_enabled") return ctx.levelSelectEnabled;
    if (var == "native_text_resolution_enabled") return ctx.nativeTextResolutionEnabled;
    if (var == "water_effects_enabled") return ctx.waterEffectsEnabled;
    if (var == "send_anonymous_metrics" && ctx.extraSettings && ctx.extraSettingsCount > 44) return &ctx.extraSettings[44];
    return nullptr;
}

SDL_Scancode* keyBindingPtr(const std::string& var, SlimMenuContext& ctx) {
    if (var == "move_left") return ctx.keyMoveLeft;
    if (var == "move_right") return ctx.keyMoveRight;
    if (var == "move_down") return ctx.keyMoveDown;
    if (var == "jump") return ctx.keyJump;
    if (var == "pause") return ctx.keyPause;
    return nullptr;
}

std::string keyBindingValue(const std::string& name, const SlimMenuContext& ctx) {
    SlimMenuContext mutableCtx = ctx;
    SDL_Scancode* value = keyBindingPtr(name, mutableCtx);
    if (!value) return {};
    const char* keyName = SDL_GetScancodeName(*value);
    return (keyName && *keyName) ? std::string(keyName) : std::string("Unbound");
}

std::string boolSettingValue(const std::string& name, const SlimMenuContext& ctx) {
    SlimMenuContext mutableCtx = ctx;
    bool* value = boolSettingPtr(name, mutableCtx);
    if (!value) return {};
    return *value ? "ON" : "OFF";
}

std::string fallbackText(const std::string& value, const std::string& fallback = "unknown") {
    return value.empty() ? fallback : value;
}

std::string aboutVarValue(const std::string& var, const SlimMenuContext& ctx) {
    if (var == "about_version") return fallbackText(ctx.versionString, "dev");
    if (var == "about_version_id") return fallbackText(ctx.versionIdString, "dev");
    if (var == "about_build_uuid") return fallbackText(ctx.buildUuid);
    if (var == "about_build_time") return fallbackText(ctx.buildTimestamp);
    if (var == "about_build_timezone") return fallbackText(ctx.buildTimezone);
    if (var == "about_base_size") return std::to_string(ctx.baseScreenW) + "x" + std::to_string(ctx.baseScreenH);
    if (var == "about_window_size") {
        int winW = 0;
        int winH = 0;
        getWindowSizeInPixelsCompat(ctx.win, winW, winH);
        return std::to_string(winW) + "x" + std::to_string(winH);
    }
    if (var == "about_sdl_version") {
        const int sdlVer = SDL_GetVersion();
        return std::to_string(SDL_VERSIONNUM_MAJOR(sdlVer)) + "." +
               std::to_string(SDL_VERSIONNUM_MINOR(sdlVer)) + "." +
               std::to_string(SDL_VERSIONNUM_MICRO(sdlVer));
    }
    if (var == "about_sdl_revision") return fallbackText(SDL_GetRevision() ? SDL_GetRevision() : "");
    if (var == "about_platform") return fallbackText(SDL_GetPlatform() ? SDL_GetPlatform() : "");
    if (var == "about_renderer") return fallbackText(SDL_GetRendererName(ctx.ren) ? SDL_GetRendererName(ctx.ren) : "");
    if (var == "about_video_driver") return fallbackText(SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "");
    if (var == "about_audio_driver") return fallbackText(SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "");
    if (var == "about_cpu_cores") return std::to_string(SDL_GetNumLogicalCPUCores());
    if (var == "about_system_ram") return std::to_string(SDL_GetSystemRAM()) + " MiB";
    if (var == "about_ui_scale" && ctx.uiScalePercent) return std::to_string(*ctx.uiScalePercent) + "%";
    if (var == "about_ui_edge_padding" && ctx.uiEdgePadding) return std::to_string(*ctx.uiEdgePadding) + " PX";
    if (var == "about_updater_status") return ctx.getUpdaterStatusText ? ctx.getUpdaterStatusText() : "not configured";
    return {};
}

std::string contextVarValue(const std::string& name, const SlimRuntime& runtime, const SlimMenuContext& ctx) {
    const std::string var = normalizeVarName(name);
    auto it = runtime.vars.find(var);
    if (it != runtime.vars.end()) return it->second;
    if (var == "world_select_unlocked") return ctx.progression && ctx.progression->worldSelectUnlocked ? "true" : "false";
    if (var == "hardmode_unlocked") return ctx.progression && ctx.progression->hardmodeUnlocked ? "true" : "false";
    if (var == "hardmode_complete") return ctx.progression && ctx.progression->hardmodeComplete ? "YES" : "NO";
    if (var == "hardmode_furthest_level") return std::to_string(ctx.progression ? ctx.progression->hardmodeFurthestLevel : 0);
    if (var == "hardmode_save_exists") return FileExists(HardmodeSavePath()) ? "true" : "false";
    if (var == "username" && ctx.levelServerAccountUsername) return *ctx.levelServerAccountUsername;
    if (var == "api" && ctx.levelServerUrl) return *ctx.levelServerUrl;
    if (var == "debugger_attached" && ctx.debuggerAttached) return *ctx.debuggerAttached ? "true" : "false";
    if (var == "account_manager_url" && ctx.accountManagerUrl) return *ctx.accountManagerUrl;
    if (var == "account_status") {
        const bool hasUser = ctx.levelServerAccountUsername && !ctx.levelServerAccountUsername->empty();
        const bool hasToken = ctx.levelServerAuthToken && !ctx.levelServerAuthToken->empty();
        if (hasUser && hasToken) return "SIGNED IN: " + *ctx.levelServerAccountUsername;
        if (hasUser) return "INVALID LOGIN";
        return "SIGNED OUT";
    }
    const std::string boolValue = boolSettingValue(var, ctx);
    if (!boolValue.empty()) return boolValue;
    const std::string keyValue = keyBindingValue(var, ctx);
    if (!keyValue.empty()) return keyValue;
    if (var == "music_volume" && ctx.musicVolume) return std::to_string(*ctx.musicVolume);
    if (var == "sfx_volume" && ctx.sfxVolume) return std::to_string(*ctx.sfxVolume);
    if (var == "ui_scale_percent" && ctx.uiScalePercent) return std::to_string(*ctx.uiScalePercent);
    if (var == "ui_edge_padding" && ctx.uiEdgePadding) return std::to_string(*ctx.uiEdgePadding);
    if (var == "active_save_slot" && ctx.activeSaveSlotIndex) return std::to_string(*ctx.activeSaveSlotIndex + 1);
    if (var == "save_slot_1_status") return saveSlotStatus(0, ctx);
    if (var == "save_slot_2_status") return saveSlotStatus(1, ctx);
    if (var == "save_slot_3_status") return saveSlotStatus(2, ctx);
    if (var == "save_slot_1_exists") return saveSlotExists(0) ? "true" : "false";
    if (var == "save_slot_2_exists") return saveSlotExists(1) ? "true" : "false";
    if (var == "save_slot_3_exists") return saveSlotExists(2) ? "true" : "false";
    const std::string aboutValue = aboutVarValue(var, ctx);
    if (!aboutValue.empty()) return aboutValue;
    return {};
}

bool varIsTruthy(const std::string& name, const SlimRuntime& runtime, const SlimMenuContext& ctx) {
    const std::string value = toLowerAscii(trim(contextVarValue(name, runtime, ctx)));
    if (value.empty()) return false;
    if (value == "false" || value == "0" || value == "off" || value == "no" || value == "null") return false;
    return true;
}

std::string expandVars(std::string text, const SlimRuntime& runtime, const SlimMenuContext& ctx) {
    if (text.find("${") == std::string::npos) return text;
    for (const char* var : {"world_select_unlocked", "hardmode_unlocked", "hardmode_complete", "hardmode_furthest_level", "hardmode_save_exists"})
        text = replaceAll(text, "${" + std::string(var) + "}", contextVarValue(var, runtime, ctx));
    if (text.find("${username}") != std::string::npos)
        text = replaceAll(text, "${username}", contextVarValue("username", runtime, ctx));
    if (text.find("${api}") != std::string::npos)
        text = replaceAll(text, "${api}", contextVarValue("api", runtime, ctx));
    if (text.find("${debugger_attached}") != std::string::npos)
        text = replaceAll(text, "${debugger_attached}", contextVarValue("debugger_attached", runtime, ctx));
    if (text.find("${account_status}") != std::string::npos)
        text = replaceAll(text, "${account_status}", contextVarValue("account_status", runtime, ctx));
    if (text.find("${account_manager_url}") != std::string::npos)
        text = replaceAll(text, "${account_manager_url}", contextVarValue("account_manager_url", runtime, ctx));
    if (text.find("${menu_music_enabled}") != std::string::npos)
        text = replaceAll(text, "${menu_music_enabled}", contextVarValue("menu_music_enabled", runtime, ctx));
    if (text.find("${mute_all_audio}") != std::string::npos)
        text = replaceAll(text, "${mute_all_audio}", contextVarValue("mute_all_audio", runtime, ctx));
    if (text.find("${level_select_enabled}") != std::string::npos)
        text = replaceAll(text, "${level_select_enabled}", contextVarValue("level_select_enabled", runtime, ctx));
    if (text.find("${fullscreen}") != std::string::npos)
        text = replaceAll(text, "${fullscreen}", contextVarValue("fullscreen", runtime, ctx));
    if (text.find("${vsync_enabled}") != std::string::npos)
        text = replaceAll(text, "${vsync_enabled}", contextVarValue("vsync_enabled", runtime, ctx));
    if (text.find("${camera_clamp_x}") != std::string::npos)
        text = replaceAll(text, "${camera_clamp_x}", contextVarValue("camera_clamp_x", runtime, ctx));
    if (text.find("${native_text_resolution_enabled}") != std::string::npos)
        text = replaceAll(text, "${native_text_resolution_enabled}", contextVarValue("native_text_resolution_enabled", runtime, ctx));
    if (text.find("${water_effects_enabled}") != std::string::npos)
        text = replaceAll(text, "${water_effects_enabled}", contextVarValue("water_effects_enabled", runtime, ctx));
    if (text.find("${show_fps_counter}") != std::string::npos)
        text = replaceAll(text, "${show_fps_counter}", contextVarValue("show_fps_counter", runtime, ctx));
    if (text.find("${show_detailed_debugger}") != std::string::npos)
        text = replaceAll(text, "${show_detailed_debugger}", contextVarValue("show_detailed_debugger", runtime, ctx));
    if (text.find("${show_hitboxes}") != std::string::npos)
        text = replaceAll(text, "${show_hitboxes}", contextVarValue("show_hitboxes", runtime, ctx));
    if (text.find("${show_player_hitbox}") != std::string::npos)
        text = replaceAll(text, "${show_player_hitbox}", contextVarValue("show_player_hitbox", runtime, ctx));
    if (text.find("${show_debug_view}") != std::string::npos)
        text = replaceAll(text, "${show_debug_view}", contextVarValue("show_debug_view", runtime, ctx));
    if (text.find("${hide_unknown_object_types}") != std::string::npos)
        text = replaceAll(text, "${hide_unknown_object_types}", contextVarValue("hide_unknown_object_types", runtime, ctx));
    if (text.find("${power_management_enabled}") != std::string::npos)
        text = replaceAll(text, "${power_management_enabled}", contextVarValue("power_management_enabled", runtime, ctx));
    if (text.find("${low_power_mode_enabled}") != std::string::npos)
        text = replaceAll(text, "${low_power_mode_enabled}", contextVarValue("low_power_mode_enabled", runtime, ctx));
    if (text.find("${show_experimental_features}") != std::string::npos)
        text = replaceAll(text, "${show_experimental_features}", contextVarValue("show_experimental_features", runtime, ctx));
    if (text.find("${send_anonymous_metrics}") != std::string::npos)
        text = replaceAll(text, "${send_anonymous_metrics}", contextVarValue("send_anonymous_metrics", runtime, ctx));
    if (text.find("${move_left}") != std::string::npos)
        text = replaceAll(text, "${move_left}", contextVarValue("move_left", runtime, ctx));
    if (text.find("${move_right}") != std::string::npos)
        text = replaceAll(text, "${move_right}", contextVarValue("move_right", runtime, ctx));
    if (text.find("${move_down}") != std::string::npos)
        text = replaceAll(text, "${move_down}", contextVarValue("move_down", runtime, ctx));
    if (text.find("${jump}") != std::string::npos)
        text = replaceAll(text, "${jump}", contextVarValue("jump", runtime, ctx));
    if (text.find("${pause}") != std::string::npos)
        text = replaceAll(text, "${pause}", contextVarValue("pause", runtime, ctx));
    if (text.find("${music_volume}") != std::string::npos)
        text = replaceAll(text, "${music_volume}", contextVarValue("music_volume", runtime, ctx));
    if (text.find("${sfx_volume}") != std::string::npos)
        text = replaceAll(text, "${sfx_volume}", contextVarValue("sfx_volume", runtime, ctx));
    if (text.find("${ui_scale_percent}") != std::string::npos)
        text = replaceAll(text, "${ui_scale_percent}", contextVarValue("ui_scale_percent", runtime, ctx));
    if (text.find("${ui_edge_padding}") != std::string::npos)
        text = replaceAll(text, "${ui_edge_padding}", contextVarValue("ui_edge_padding", runtime, ctx));
    if (text.find("${active_save_slot}") != std::string::npos)
        text = replaceAll(text, "${active_save_slot}", contextVarValue("active_save_slot", runtime, ctx));
    if (text.find("${save_slot_1_status}") != std::string::npos)
        text = replaceAll(text, "${save_slot_1_status}", contextVarValue("save_slot_1_status", runtime, ctx));
    if (text.find("${save_slot_2_status}") != std::string::npos)
        text = replaceAll(text, "${save_slot_2_status}", contextVarValue("save_slot_2_status", runtime, ctx));
    if (text.find("${save_slot_3_status}") != std::string::npos)
        text = replaceAll(text, "${save_slot_3_status}", contextVarValue("save_slot_3_status", runtime, ctx));
    if (text.find("${save_slot_1_exists}") != std::string::npos)
        text = replaceAll(text, "${save_slot_1_exists}", contextVarValue("save_slot_1_exists", runtime, ctx));
    if (text.find("${save_slot_2_exists}") != std::string::npos)
        text = replaceAll(text, "${save_slot_2_exists}", contextVarValue("save_slot_2_exists", runtime, ctx));
    if (text.find("${save_slot_3_exists}") != std::string::npos)
        text = replaceAll(text, "${save_slot_3_exists}", contextVarValue("save_slot_3_exists", runtime, ctx));
    if (text.find("${about_version}") != std::string::npos)
        text = replaceAll(text, "${about_version}", contextVarValue("about_version", runtime, ctx));
    if (text.find("${about_version_id}") != std::string::npos)
        text = replaceAll(text, "${about_version_id}", contextVarValue("about_version_id", runtime, ctx));
    if (text.find("${about_build_uuid}") != std::string::npos)
        text = replaceAll(text, "${about_build_uuid}", contextVarValue("about_build_uuid", runtime, ctx));
    if (text.find("${about_build_time}") != std::string::npos)
        text = replaceAll(text, "${about_build_time}", contextVarValue("about_build_time", runtime, ctx));
    if (text.find("${about_build_timezone}") != std::string::npos)
        text = replaceAll(text, "${about_build_timezone}", contextVarValue("about_build_timezone", runtime, ctx));
    if (text.find("${about_sdl_version}") != std::string::npos)
        text = replaceAll(text, "${about_sdl_version}", contextVarValue("about_sdl_version", runtime, ctx));
    if (text.find("${about_sdl_revision}") != std::string::npos)
        text = replaceAll(text, "${about_sdl_revision}", contextVarValue("about_sdl_revision", runtime, ctx));
    if (text.find("${about_platform}") != std::string::npos)
        text = replaceAll(text, "${about_platform}", contextVarValue("about_platform", runtime, ctx));
    if (text.find("${about_renderer}") != std::string::npos)
        text = replaceAll(text, "${about_renderer}", contextVarValue("about_renderer", runtime, ctx));
    if (text.find("${about_window_size}") != std::string::npos)
        text = replaceAll(text, "${about_window_size}", contextVarValue("about_window_size", runtime, ctx));
    if (text.find("${about_base_size}") != std::string::npos)
        text = replaceAll(text, "${about_base_size}", contextVarValue("about_base_size", runtime, ctx));
    if (text.find("${about_ui_scale}") != std::string::npos)
        text = replaceAll(text, "${about_ui_scale}", contextVarValue("about_ui_scale", runtime, ctx));
    if (text.find("${about_ui_edge_padding}") != std::string::npos)
        text = replaceAll(text, "${about_ui_edge_padding}", contextVarValue("about_ui_edge_padding", runtime, ctx));
    if (text.find("${about_video_driver}") != std::string::npos)
        text = replaceAll(text, "${about_video_driver}", contextVarValue("about_video_driver", runtime, ctx));
    if (text.find("${about_audio_driver}") != std::string::npos)
        text = replaceAll(text, "${about_audio_driver}", contextVarValue("about_audio_driver", runtime, ctx));
    if (text.find("${about_cpu_cores}") != std::string::npos)
        text = replaceAll(text, "${about_cpu_cores}", contextVarValue("about_cpu_cores", runtime, ctx));
    if (text.find("${about_system_ram}") != std::string::npos)
        text = replaceAll(text, "${about_system_ram}", contextVarValue("about_system_ram", runtime, ctx));
    if (text.find("${about_updater_status}") != std::string::npos)
        text = replaceAll(text, "${about_updater_status}", contextVarValue("about_updater_status", runtime, ctx));
    for (const auto& [key, value] : runtime.vars) {
        text = replaceAll(text, "${" + key + "}", value);
    }
    return text;
}

std::string menuPath(const std::string& name) {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return name;
    return "assets/menus/" + name + ".menu";
}

std::string makeApiUrl(const SlimMenuContext& ctx, std::string path);
SlimMenu buildGeneratedLevelMenu(const std::string& name, SlimRuntime& runtime, const SlimMenuContext& ctx);

bool parseMenuAlignment(std::string value, SlimMenuLayout::Align& out) {
    value = toLowerAscii(value);
    if (value == "left" || value == "top" || value == "start") out = SlimMenuLayout::Align::Start;
    else if (value == "center" || value == "middle") out = SlimMenuLayout::Align::Center;
    else if (value == "right" || value == "bottom" || value == "end") out = SlimMenuLayout::Align::End;
    else return false;
    return true;
}

SlimMenu loadMenu(const std::string& name, SlimRuntime& runtime, const SlimMenuContext& ctx) {
    if (name == "locallevels" || name == "onlinelevels" || name == "leveldetail" || name == "worldselect" || name == "hardmodepractice") {
        return buildGeneratedLevelMenu(name, runtime, ctx);
    }

    SlimMenu menu;
    menu.name = name;
    MenuItem style;
    auto source = runtime.scriptSources.find(name);
    if (source == runtime.scriptSources.end()) {
        source = runtime.scriptSources.emplace(name, ReadTextFile(menuPath(name))).first;
    }
    const std::string& text = source->second;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    std::vector<ParsedLine> lines;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> parts = tokenize(line);
        if (parts.empty()) continue;
        lines.push_back(ParsedLine{lineNo, line, parts});
    }

    for (std::size_t i = 0; i < lines.size();) {
        const ParsedLine& lineInfo = lines[i];
        const std::vector<std::string>& parts = lineInfo.parts;
        const std::string& op = parts[0];
        if (op == "title" && parts.size() >= 2) {
            menu.title = parts[1];
            ++i;
        } else if (op == "text" && parts.size() >= 2) {
            MenuItem item = style;
            item.type = MenuItem::Type::Text;
            item.label = parts[1];
            menu.items.push_back(std::move(item));
            ++i;
        } else if ((op == "button" || op == "slider") && parts.size() >= 3) {
            const std::string arg = parts.size() >= 4 ? parts[3] : std::string();
            MenuItem item = style;
            item.type = op == "slider" ? MenuItem::Type::Slider : MenuItem::Type::Button;
            item.label = parts[1];
            item.command = parts[2];
            item.arg = arg;
            menu.items.push_back(std::move(item));
            ++i;
        } else if (op == "scale" && parts.size() >= 2) {
            char* end = nullptr;
            const float value = std::strtof(parts[1].c_str(), &end);
            if (end != parts[1].c_str() && *end == '\0' && std::isfinite(value) && value > 0.0f) {
                menu.scale = std::clamp(value, 0.25f, 4.0f);
            } else {
                SDL_Log("SLIM MENU: invalid scale at %s:%d", menuPath(name).c_str(), lineInfo.lineNo);
            }
            ++i;
        } else if ((op == "align" || op == "menu_align" || op == "text_align") && parts.size() >= 3) {
            SlimMenuLayout::Align horizontal, vertical;
            if (parseMenuAlignment(parts[1], horizontal) && parseMenuAlignment(parts[2], vertical)) {
                if (op == "menu_align") {
                    menu.horizontal = horizontal;
                    menu.vertical = vertical;
                } else if (op == "text_align") {
                    style.labelHorizontal = horizontal;
                    style.labelVertical = vertical;
                } else {
                    style.horizontal = horizontal;
                    style.vertical = vertical;
                }
            } else {
                SDL_Log("SLIM MENU: invalid alignment at %s:%d", menuPath(name).c_str(), lineInfo.lineNo);
            }
            ++i;
        } else if (op == "get" && parts.size() >= 3) {
            const std::string url = makeApiUrl(ctx, expandVars(parts[2], runtime, ctx));
            runtime.vars[normalizeVarName(parts[1])] = url.empty() ? std::string() : ReadTextFile(url);
            ++i;
        } else if (op == "set" && parts.size() >= 3) {
            runtime.vars[normalizeVarName(parts[1])] = expandVars(parts[2], runtime, ctx);
            ++i;
        } else if (op == "if" && parts.size() >= 4) {
            int skipLines = 0;
            if (parts.size() >= 4 && parts[2] == "skip") {
                skipLines = std::max(0, std::atoi(parts[3].c_str()));
            } else if (parts.size() >= 5 && parts[2] == "false" && parts[3] == "skip") {
                skipLines = std::max(0, std::atoi(parts[4].c_str()));
            }
            if (skipLines <= 0) {
                SDL_Log("SLIM MENU: ignored malformed if at %s:%d: %s",
                        menuPath(name).c_str(), lineInfo.lineNo, lineInfo.text.c_str());
                ++i;
                continue;
            }
            i += varIsTruthy(parts[1], runtime, ctx) ? 1 : (std::size_t)skipLines + 1;
        } else {
            SDL_Log("SLIM MENU: ignored %s:%d: %s", menuPath(name).c_str(), lineInfo.lineNo, lineInfo.text.c_str());
            ++i;
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

std::string localLevelsFolderPath() {
    std::filesystem::path dir = std::filesystem::path(GetAppSaveRootPath()) / "local levels";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

std::string joinUrlPath(std::string base, std::string path) {
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (path.empty() || path.front() != '/') path = "/" + path;
    return base + path;
}

std::string urlEncodePathSegment(const std::string& in) {
    std::ostringstream out;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char ch : in) {
        const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                          ch == '.' || ch == '~';
        if (safe) {
            out << (char)ch;
        } else {
            out << '%' << hex[ch >> 4] << hex[ch & 15];
        }
    }
    return out.str();
}

bool jsonValueLooksMod(const nlohmann::json& value) {
    if (!value.is_object()) return false;
    const auto boolField = [&](const char* key) {
        return value.contains(key) && value[key].is_boolean() && value[key].get<bool>();
    };
    if (boolField("mod") || boolField("moderator") || boolField("isMod") ||
        boolField("isModerator") || boolField("admin") || boolField("isAdmin")) {
        return true;
    }
    auto roleLooksMod = [](std::string role) {
        role = toLowerAscii(trim(role));
        return role == "mod" || role == "moderator" || role == "admin" || role == "owner";
    };
    if (value.contains("role") && value["role"].is_string() && roleLooksMod(value["role"].get<std::string>())) return true;
    if (value.contains("roles") && value["roles"].is_array()) {
        for (const auto& role : value["roles"]) {
            if (role.is_string() && roleLooksMod(role.get<std::string>())) return true;
        }
    }
    if (value.contains("claims") && jsonValueLooksMod(value["claims"])) return true;
    if (value.contains("customClaims") && jsonValueLooksMod(value["customClaims"])) return true;
    return false;
}

bool accountLookupResponseIsMod(const std::string& body) {
    if (body.empty()) return false;
    try {
        const nlohmann::json resp = nlohmann::json::parse(body);
        if (jsonValueLooksMod(resp)) return true;
        if (resp.is_object() && resp.contains("users") && resp["users"].is_array()) {
            for (const auto& user : resp["users"]) {
                if (jsonValueLooksMod(user)) return true;
            }
        }
    } catch (...) {}
    return false;
}

std::string accountLookupBody(const SlimMenuContext& ctx) {
    const std::string token = ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string();
    std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (token.empty() || base.empty()) return {};

#if defined(__ANDROID__)
    {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (env) {
            jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
            jclass cls = activity ? env->GetObjectClass(activity) : nullptr;
            if (activity) env->DeleteLocalRef(activity);
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(
                    cls, "gameServerLookupAccount",
                    "(Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
                if (mid) {
                    jstring jBase = env->NewStringUTF(base.c_str());
                    jstring jToken = env->NewStringUTF(token.c_str());
                    jobject jRespObj = PerformJavaNetworkObject(env, cls, mid, {jBase, jToken}, (jint)10000);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (jBase) env->DeleteLocalRef(jBase);
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
                    if (!respBody.empty()) return respBody;
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

#if defined(HAVE_CURL) && HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) return {};
    const std::string url = base + "/api/auth/lookup";
    nlohmann::json req;
    req["idToken"] = token;
    const std::string body = req.dump();
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string authorization = "Authorization: Bearer " + token;
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kSlimMenuUserAgent);
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
    if (rc == CURLE_OK && code >= 200 && code < 300) return respBody;
#endif

    return {};
}

bool currentAccountIsMod(SlimRuntime& runtime, const SlimMenuContext& ctx) {
    std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
    std::string token = ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string();
    const std::string key = base + "\n" + token;
    if (runtime.accountRoleKey != key) runtime.vars.erase("__account_is_mod");
    auto cached = runtime.vars.find("__account_is_mod");
    if (cached != runtime.vars.end()) return cached->second == "true";
    if (base.empty() || token.empty()) return false;
    if (!runtime.accountRoleRequest.valid()) {
        runtime.pendingAccountRoleKey = key;
        runtime.accountRoleRequest = SubmitNetworkOperation([base, token]() mutable {
            SlimMenuContext snapshot;
            snapshot.levelServerUrl = &base;
            snapshot.levelServerAuthToken = &token;
            return accountLookupResponseIsMod(accountLookupBody(snapshot));
        });
    }
    return false;
}

std::string prettyLevelLabelFromPath(const std::filesystem::path& path, const std::filesystem::path& root) {
    std::string label = path.stem().string();
    if (label.empty()) label = path.filename().string();
    const std::string relParent = path.parent_path().lexically_relative(root).generic_string();
    if (!relParent.empty() && relParent != ".") label = relParent + "/" + label;
    for (char& ch : label) {
        if (ch == '_') ch = ' ';
    }
    return label;
}

void addUniqueLevel(std::vector<SlimLevelEntry>& out, SlimLevelEntry entry) {
    for (const auto& existing : out) {
        if (existing.path == entry.path) return;
    }
    out.push_back(std::move(entry));
}

std::vector<SlimLevelEntry> loadSlimLevelsFromDir(const std::string& dirPath) {
    std::vector<SlimLevelEntry> out;
    std::filesystem::path root(dirPath);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) return out;
    std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    while (!ec && it != end) {
        const auto entry = *it;
        std::error_code typeEc;
        if (entry.is_regular_file(typeEc) && !typeEc) {
            const std::filesystem::path p = entry.path();
            const std::string ext = toLowerAscii(p.extension().string());
            if (ext.empty() || ext == ".txt" || ext == ".bnnlvl" || ext == ".bin") {
                addUniqueLevel(out, SlimLevelEntry{prettyLevelLabelFromPath(p, root), p.string(), 0});
            }
        }
        it.increment(ec);
    }
    return out;
}

std::vector<SlimLevelEntry> loadSlimLevelsFromJson(const std::string& jsonPath, const std::string& baseDir) {
    std::vector<SlimLevelEntry> out;
    const std::string text = ReadTextFile(jsonPath);
    if (text.empty()) return out;
    nlohmann::json j;
    try { j = nlohmann::json::parse(text); } catch (...) { return out; }
    if (!j.contains("levels") || !j["levels"].is_array()) return out;
    for (const auto& v : j["levels"]) {
        if (!v.is_string()) continue;
        const std::string name = v.get<std::string>();
        std::filesystem::path labelPath(name);
        std::string label = labelPath.stem().string();
        if (label.empty()) label = labelPath.filename().string();
        const std::string parent = labelPath.parent_path().generic_string();
        if (!parent.empty() && parent != ".") label = parent + "/" + label;
        std::string path = baseDir.empty() ? name : (baseDir + "/" + name);
        const std::string bnn = ".bnnlvl";
        if (!FileExists(path) && path.size() > bnn.size() && path.substr(path.size() - bnn.size()) == bnn) {
            const std::string txtPath = path.substr(0, path.size() - bnn.size()) + ".txt";
            if (FileExists(txtPath)) path = txtPath;
        }
        addUniqueLevel(out, SlimLevelEntry{label, path, 0});
    }
    return out;
}

std::vector<SlimLevelEntry> loadLocalSlimLevels() {
    std::vector<SlimLevelEntry> out;
    for (const auto& e : loadSlimLevelsFromJson("assets/custom_levels/levels.json", "assets/custom_levels")) addUniqueLevel(out, e);
    for (const auto& e : loadSlimLevelsFromDir(localLevelsFolderPath())) addUniqueLevel(out, e);
#if !PLATFORMER_MOBILE
    for (const auto& e : loadSlimLevelsFromDir("custom_levels")) addUniqueLevel(out, e);
    for (const auto& e : loadSlimLevelsFromDir("assets/custom_levels")) addUniqueLevel(out, e);
#endif
    std::sort(out.begin(), out.end(), [](const SlimLevelEntry& a, const SlimLevelEntry& b) { return a.label < b.label; });
    return out;
}

OnlineLevelPage loadOnlineSlimLevels(const SlimMenuContext& ctx, int page, const std::string& filter) {
    OnlineLevelPage result;
    if (!ctx.levelServerUrl || ctx.levelServerUrl->empty()) return result;
    const std::string rootText = ReadTextFile(makeApiUrl(ctx, "/levels.json?metadata=true&page=" +
        std::to_string(page) + "&page_size=10&filter=" + filter));
    const auto response = nlohmann::json::parse(rootText);
    const auto& j = response.at("levels");
    if (!j.is_object()) throw std::runtime_error("Invalid level page response");
    result.page = response.at("page").get<int>();
    result.pageCount = std::max(1, response.at("page_count").get<int>());
    result.total = std::max(0, response.at("total").get<int>());
    auto& out = result.levels;
    const std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
    for (auto it = j.begin(); it != j.end(); ++it) {
        int difficulty = 0;
        if (it.value().is_object() &&
            it.value().contains("difficulty") &&
            it.value()["difficulty"].is_number_integer()) {
            difficulty = std::clamp(it.value()["difficulty"].get<int>(), 0, 9);
        }
        int downloads = 0;
        int likes = 0;
        int dislikes = 0;
        if (it.value().is_object()) {
            if (it.value().contains("downloads") && it.value()["downloads"].is_number_integer()) {
                downloads = std::max(0, it.value()["downloads"].get<int>());
            }
            if (it.value().contains("likes") && it.value()["likes"].is_number_integer()) {
                likes = std::max(0, it.value()["likes"].get<int>());
            }
            if (it.value().contains("dislikes") && it.value()["dislikes"].is_number_integer()) {
                dislikes = std::max(0, it.value()["dislikes"].get<int>());
            }
        }
        const std::string id = it.key();
        addUniqueLevel(out, SlimLevelEntry{id, joinUrlPath(base, "/levels/" + id + "/data.json"), difficulty, downloads, likes, dislikes});
        if (it.value().is_object() && it.value().contains("name") && it.value()["name"].is_string()) {
            out.back().displayName = it.value()["name"].get<std::string>();
        }
    }
    std::sort(out.begin(), out.end(), [](const SlimLevelEntry& a, const SlimLevelEntry& b) { return a.label < b.label; });
    return result;
}

std::string onlinePageKey(const SlimRuntime& runtime, const SlimMenuContext& ctx) {
    const auto page = runtime.vars.find("online_level_page");
    const auto filter = runtime.vars.find("online_level_filter");
    return (ctx.levelServerUrl ? *ctx.levelServerUrl : std::string()) + "\n" +
        (page == runtime.vars.end() ? "0" : page->second) + "\n" +
        (filter == runtime.vars.end() ? "all" : filter->second);
}

const std::vector<SlimLevelEntry>& cachedSlimLevels(bool online, SlimRuntime& runtime, const SlimMenuContext& ctx) {
    if (online) {
        if (!runtime.vars.count("online_level_page")) runtime.vars["online_level_page"] = "0";
        if (!runtime.vars.count("online_level_filter")) runtime.vars["online_level_filter"] = "all";
        const std::string key = onlinePageKey(runtime, ctx);
        if (runtime.onlineLevelsKey != key) {
            runtime.onlineLevelsKey = key;
            runtime.onlineLevelsLoaded = false;
            runtime.onlineLevels.clear();
        }
        if (!runtime.onlineLevelsLoaded && !runtime.onlineLevelsRequest.valid()) {
            std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
            const int page = std::max(0, std::atoi(runtime.vars["online_level_page"].c_str()));
            const std::string filter = runtime.vars.count("online_level_filter") ? runtime.vars["online_level_filter"] : "all";
            runtime.pendingOnlineLevelsKey = key;
            runtime.vars["online_level_status"].clear();
            runtime.onlineLevelsRequest = SubmitNetworkOperation([base, page, filter]() mutable {
                SlimMenuContext snapshot;
                snapshot.levelServerUrl = &base;
                return loadOnlineSlimLevels(snapshot, page, filter);
            });
        }
        return runtime.onlineLevels;
    }
    if (!runtime.localLevelsLoaded) {
        runtime.localLevels = loadLocalSlimLevels();
        runtime.localLevelsLoaded = true;
    }
    return runtime.localLevels;
}

std::string levelFilterLabel(const std::string& filter) {
    if (filter == "easy") return "EASY 1-3";
    if (filter == "medium") return "MEDIUM 4-6";
    if (filter == "hard") return "HARD 7-9";
    if (filter == "unrated") return "UNRATED";
    return "ALL";
}

bool levelMatchesFilter(const SlimLevelEntry& level, const std::string& filter) {
    if (filter == "easy") return level.difficulty >= 1 && level.difficulty <= 3;
    if (filter == "medium") return level.difficulty >= 4 && level.difficulty <= 6;
    if (filter == "hard") return level.difficulty >= 7 && level.difficulty <= 9;
    if (filter == "unrated") return level.difficulty == 0;
    return true;
}

std::string difficultySuffix(int difficulty) {
    return difficulty >= 1 && difficulty <= 9 ? (" [D" + std::to_string(difficulty) + "]") : std::string();
}

std::string levelStatsSuffix(const SlimLevelEntry& level) {
    return " [DL " + std::to_string(level.downloads) +
           " +" + std::to_string(level.likes) +
           " -" + std::to_string(level.dislikes) + "]";
}

std::vector<SlimLevelEntry> filteredGeneratedLevels(const std::string& prefix, SlimRuntime& runtime, const SlimMenuContext& ctx) {
    const auto& levels = cachedSlimLevels(prefix == "online", runtime, ctx);
    const std::string filterVar = prefix + "_level_filter";
    if (!runtime.vars.count(filterVar)) runtime.vars[filterVar] = "all";
    std::vector<SlimLevelEntry> filtered;
    for (const auto& level : levels) {
        if (levelMatchesFilter(level, runtime.vars[filterVar])) filtered.push_back(level);
    }
    return filtered;
}

std::string cachePathForOnlineLevel(const SlimLevelEntry& level) {
    const std::filesystem::path dir = std::filesystem::path(GetAppSaveRootPath()) / "user_levels";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string safe = level.label;
    for (char& ch : safe) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    if (safe.empty()) safe = "online_level";
    return (dir / (safe + "_" + std::to_string((unsigned long long)std::hash<std::string>{}(level.path)) + ".bin")).string();
}

bool updateOnlineLevelCache(const SlimLevelEntry& level, std::string& outPath) {
    outPath.clear();
    const std::string body = ReadTextFile(level.path);
    if (body.empty()) return false;
    const std::filesystem::path cachePath = cachePathForOnlineLevel(level);
    const std::filesystem::path tmpPath = cachePath.string() + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << body;
        out.flush();
        if (!out.good()) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, cachePath, ec);
    if (ec) {
        std::filesystem::remove(cachePath, ec);
        ec.clear();
        std::filesystem::rename(tmpPath, cachePath, ec);
        if (ec) return false;
    }
    outPath = cachePath.string();
    return true;
}

bool putOnlineLevelDifficulty(const SlimLevelEntry& level, int difficulty, const SlimMenuContext& ctx, std::string& status) {
    status.clear();
    const std::string token = ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string();
    std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) {
        status = "Level server URL is not configured.";
        return false;
    }
    if (token.empty()) {
        status = "Sign in as a moderator first.";
        return false;
    }
    difficulty = std::clamp(difficulty, 0, 9);
    const std::string url = base + "/levels/" + urlEncodePathSegment(level.label) + "/difficulty";
    nlohmann::json payload;
    payload["difficulty"] = difficulty;
    const std::string body = payload.dump();

#if defined(__ANDROID__)
    {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (env) {
            jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(
                    cls, "gameServerUploadLevel",
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)I");
                if (mid) {
                    jstring jUrl = env->NewStringUTF(url.c_str());
                    jstring jBody = env->NewStringUTF(body.c_str());
                    jstring jToken = env->NewStringUTF(token.c_str());
                    jint code = PerformJavaNetworkInt(env, cls, mid, {jUrl, jBody, jToken}, (jint)15000);
                    if (jToken) env->DeleteLocalRef(jToken);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (jUrl) env->DeleteLocalRef(jUrl);
                    if (jBody) env->DeleteLocalRef(jBody);
                    env->DeleteLocalRef(cls);
                    if (code >= 200 && code < 300) {
                        status = difficulty > 0 ? ("Rated D" + std::to_string(difficulty) + ".") : "Rating cleared.";
                        return true;
                    }
                    status = "Rate failed (" + std::to_string((int)code) + ").";
                    return false;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
    }
#endif

#if defined(HAVE_CURL) && HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        status = "Rate failed (curl init).";
        return false;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string authorization = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, authorization.c_str());
    std::string respBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kSlimMenuUserAgent);
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
        status = difficulty > 0 ? ("Rated D" + std::to_string(difficulty) + ".") : "Rating cleared.";
        return true;
    }
    status = "Rate failed (" + std::to_string(code) + ").";
    return false;
#else
    status = "Rate failed (network PUT unavailable).";
    return false;
#endif
}

bool putOnlineLevelVote(const SlimLevelEntry& level, const std::string& vote, const SlimMenuContext& ctx, std::string& status) {
    status.clear();
    const std::string token = ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string();
    std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) {
        status = "Level server URL is not configured.";
        return false;
    }
    if (token.empty()) {
        status = "Sign in to vote.";
        return false;
    }
    const std::string url = base + "/levels/" + urlEncodePathSegment(level.label) + "/vote";
    nlohmann::json payload;
    payload["vote"] = vote;
    const std::string body = payload.dump();

#if defined(__ANDROID__)
    {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (env) {
            jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(
                    cls, "gameServerUploadLevel",
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)I");
                if (mid) {
                    jstring jUrl = env->NewStringUTF(url.c_str());
                    jstring jBody = env->NewStringUTF(body.c_str());
                    jstring jToken = env->NewStringUTF(token.c_str());
                    jint code = PerformJavaNetworkInt(env, cls, mid, {jUrl, jBody, jToken}, (jint)15000);
                    if (jToken) env->DeleteLocalRef(jToken);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (jUrl) env->DeleteLocalRef(jUrl);
                    if (jBody) env->DeleteLocalRef(jBody);
                    env->DeleteLocalRef(cls);
                    if (code >= 200 && code < 300) {
                        status = vote == "clear" ? "Vote cleared." : "Vote saved.";
                        return true;
                    }
                    status = "Vote failed (" + std::to_string((int)code) + ").";
                    return false;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
    }
#endif

#if defined(HAVE_CURL) && HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        status = "Vote failed (curl init).";
        return false;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string authorization = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, authorization.c_str());
    std::string respBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kSlimMenuUserAgent);
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
        status = vote == "clear" ? "Vote cleared." : "Vote saved.";
        return true;
    }
    status = "Vote failed (" + std::to_string(code) + ").";
    return false;
#else
    status = "Vote failed (network PUT unavailable).";
    return false;
#endif
}

bool putServerUpdateTrigger(const SlimMenuContext& ctx, std::string& status) {
    status.clear();
    const std::string token = ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string();
    std::string base = ctx.levelServerUrl ? *ctx.levelServerUrl : std::string();
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) {
        status = "Level server URL is not configured.";
        return false;
    }
    if (token.empty()) {
        status = "Sign in as a moderator first.";
        return false;
    }
    const std::string url = base + "/api/server/update";
    const std::string body = "{}";

#if defined(__ANDROID__)
    {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (env) {
            jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(
                    cls, "gameServerUploadLevel",
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)I");
                if (mid) {
                    jstring jUrl = env->NewStringUTF(url.c_str());
                    jstring jBody = env->NewStringUTF(body.c_str());
                    jstring jToken = env->NewStringUTF(token.c_str());
                    jint code = PerformJavaNetworkInt(env, cls, mid, {jUrl, jBody, jToken}, (jint)15000);
                    if (jToken) env->DeleteLocalRef(jToken);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (jUrl) env->DeleteLocalRef(jUrl);
                    if (jBody) env->DeleteLocalRef(jBody);
                    env->DeleteLocalRef(cls);
                    if (code >= 200 && code < 300) {
                        status = "Server update triggered.";
                        return true;
                    }
                    if (code == 503) status = "Server update is not configured.";
                    else if (code == 403) status = "Moderator account required.";
                    else status = "Server update failed (" + std::to_string((int)code) + ").";
                    return false;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cls);
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
    }
#endif

#if defined(HAVE_CURL) && HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        status = "Server update failed (curl init).";
        return false;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string authorization = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, authorization.c_str());
    std::string respBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kSlimMenuUserAgent);
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
        status = "Server update triggered.";
        return true;
    }
    if (code == 503) status = "Server update is not configured.";
    else if (code == 403) status = "Moderator account required.";
    else status = "Server update failed (" + std::to_string(code) + ").";
    return false;
#else
    status = "Server update failed (network PUT unavailable).";
    return false;
#endif
}

SlimMenu buildGeneratedLevelMenu(const std::string& name, SlimRuntime& runtime, const SlimMenuContext& ctx) {
    if (name == "hardmodepractice") {
        SlimMenu menu;
        menu.name = name;
        menu.title = "Hardmode Practice";
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "Practice does not affect run progress or completion.", {}, {}});
        if (ctx.progression && ctx.progression->hardmodeUnlocked) {
            for (int level = 1; level <= kHardmodeLevelCount; ++level)
                menu.items.push_back(MenuItem{MenuItem::Type::Button, "Level " + std::to_string(level),
                    "hardmode_practice", std::to_string(level)});
        } else menu.items.push_back(MenuItem{MenuItem::Type::Text, "Reach the good ending to unlock Hardmode.", {}, {}});
        menu.items.push_back(MenuItem{MenuItem::Type::Button, "Back", "menu", "hardmode"});
        return menu;
    }
    if (name == "worldselect") {
        SlimMenu menu;
        menu.name = name;
        menu.title = "World Select";
        if (ctx.progression && ctx.progression->worldSelectUnlocked) {
            for (int world = 1; world <= 7; ++world)
                menu.items.push_back(MenuItem{MenuItem::Type::Button, "World " + std::to_string(world), "world", std::to_string(world)});
        } else menu.items.push_back(MenuItem{MenuItem::Type::Text, "Reach an ending to unlock World Select.", {}, {}});
        menu.items.push_back(MenuItem{MenuItem::Type::Button, "Back", "menu", "mainplay"});
        return menu;
    }
    if (name == "leveldetail") {
        const std::string source = runtime.vars.count("detail_source") ? runtime.vars["detail_source"] : "local";
        const int index = runtime.vars.count("detail_index") ? std::atoi(runtime.vars["detail_index"].c_str()) : -1;
        std::vector<SlimLevelEntry> levels = filteredGeneratedLevels(source, runtime, ctx);
        SlimMenu menu;
        menu.name = name;
        menu.title = source == "online" ? "Online Level" : "Local Level";
        if (index < 0 || index >= (int)levels.size()) {
            menu.items.push_back(MenuItem{MenuItem::Type::Text, "Level no longer available.", {}, {}});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Back", "menu", source == "online" ? "onlinelevels" : "locallevels"});
            return menu;
        }
        const SlimLevelEntry& level = levels[index];
        runtime.vars["detail_label"] = level.displayName.empty() ? level.label : level.displayName;
        runtime.vars["detail_path"] = level.path;
        runtime.vars["detail_difficulty"] = level.difficulty > 0 ? std::to_string(level.difficulty) : "unrated";
        runtime.vars["detail_downloads"] = std::to_string(level.downloads);
        runtime.vars["detail_likes"] = std::to_string(level.likes);
        runtime.vars["detail_dislikes"] = std::to_string(level.dislikes);
        if (source != "online") {
            runtime.vars["detail_verified"] = IsLocalLevelVerified(level.path) ? "true" : "false";
        }
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "Name: " + runtime.vars["detail_label"], {}, {}});
        menu.items.push_back(MenuItem{MenuItem::Type::Text,
            level.difficulty > 0 ? std::to_string(level.difficulty) : "?", "difficulty_badge", {}});
        if (source == "online") {
            menu.items.push_back(MenuItem{MenuItem::Type::Text, "Source: online", {}, {}});
            menu.items.push_back(MenuItem{MenuItem::Type::Text,
                "Downloads: " + runtime.vars["detail_downloads"] +
                "  Likes: +" + runtime.vars["detail_likes"] +
                " / -" + runtime.vars["detail_dislikes"], {}, {}});
            menu.items.push_back(MenuItem{MenuItem::Type::Text, "${level_detail_status}", {}, {}});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Play", "start", level.path});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Update Download", "update_online_level", std::to_string(index)});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Like", "vote_online_level", std::to_string(index) + ":like"});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Dislike", "vote_online_level", std::to_string(index) + ":dislike"});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Clear Vote", "vote_online_level", std::to_string(index) + ":clear"});
            if (currentAccountIsMod(runtime, ctx)) {
                menu.items.push_back(MenuItem{MenuItem::Type::Button, "Set Difficulty", "rate_online_level", std::to_string(index)});
            }
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Back", "menu", "onlinelevels"});
        } else {
            menu.items.push_back(MenuItem{MenuItem::Type::Text, std::string("Verified: ") + runtime.vars["detail_verified"], {}, {}});
            menu.items.push_back(MenuItem{MenuItem::Type::Text, "${level_detail_status}", {}, {}});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Play", "start", level.path});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Edit", "edit_level", std::to_string(index)});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Upload", "upload_level", std::to_string(index)});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Delete", "delete_level", std::to_string(index)});
            menu.items.push_back(MenuItem{MenuItem::Type::Button, "Back", "menu", "locallevels"});
        }
        return menu;
    }

    const bool online = name == "onlinelevels";
    const std::string prefix = online ? "online" : "local";
    const std::string pageVar = prefix + "_level_page";
    const std::string filterVar = prefix + "_level_filter";
    if (!runtime.vars.count(pageVar)) runtime.vars[pageVar] = "0";
    if (!runtime.vars.count(filterVar)) runtime.vars[filterVar] = "all";

    const auto& levels = cachedSlimLevels(online, runtime, ctx);
    const std::string filter = runtime.vars[filterVar];
    std::vector<SlimLevelEntry> filtered;
    for (const auto& level : levels) {
        if (levelMatchesFilter(level, filter)) filtered.push_back(level);
    }

    constexpr int kPageSize = 10;
    const int pageCount = online ? runtime.onlinePageCount : std::max(1, (int)((filtered.size() + kPageSize - 1) / kPageSize));
    int page = online ? std::max(0, std::atoi(runtime.vars[pageVar].c_str()))
                      : std::clamp(std::atoi(runtime.vars[pageVar].c_str()), 0, pageCount - 1);
    runtime.vars[pageVar] = std::to_string(page);

    SlimMenu menu;
    menu.name = name;
    menu.title = online ? "Online Levels" : "Local Levels";
    menu.items.push_back(MenuItem{MenuItem::Type::Text,
        "Filter: " + levelFilterLabel(filter) + "  Page " + std::to_string(page + 1) + "/" + std::to_string(pageCount) +
        (online && runtime.onlineLevelsLoaded ? "  (" + std::to_string(runtime.onlineTotal) + " levels)" : std::string()), {}, {}});
    if (!online) {
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "${level_detail_status}", {}, {}});
        menu.items.push_back(MenuItem{MenuItem::Type::Button, "Create", "create_level", {}});
    } else if (!runtime.vars["online_level_status"].empty()) {
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "${online_level_status}", {}, {}});
    }
    if (filtered.empty()) {
        menu.items.push_back(MenuItem{MenuItem::Type::Text,
            online && runtime.onlineLevelsRequest.valid() ? "Loading online levels..." : "No levels match this filter.", {}, {}});
    }

    const int start = online ? 0 : page * kPageSize;
    const int end = std::min((int)filtered.size(), start + kPageSize);
    for (int i = start; i < end; ++i) {
        const auto& level = filtered[i];
        menu.items.push_back(MenuItem{MenuItem::Type::Button,
            level.label + difficultySuffix(level.difficulty) + (online ? levelStatsSuffix(level) : std::string()),
            "level_detail", prefix + ":" + std::to_string(i)});
    }
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Refresh", "refresh_levels", prefix});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Previous Page", "level_page", prefix + ":prev"});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Next Page", "level_page", prefix + ":next"});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Filter: All", "level_filter", prefix + ":all"});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Filter: Easy", "level_filter", prefix + ":easy"});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Filter: Medium", "level_filter", prefix + ":medium"});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Filter: Hard", "level_filter", prefix + ":hard"});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Filter: Unrated", "level_filter", prefix + ":unrated"});
    menu.items.push_back(MenuItem{MenuItem::Type::Button, "Back", "menu", "mainedit"});
    return menu;
}

SlimMenuLayout::Layout layoutFor(const SlimMenu& menu, const SlimRuntime& runtime, const SlimMenuContext& ctx) {
    std::vector<SlimMenuLayout::Item> items;
    const int buttonWidth = std::clamp(ctx.baseScreenW * 2 / 5, 220, 520);
    for (const auto& item : menu.items) {
        const bool button = item.type != MenuItem::Type::Text;
        if (item.command == "difficulty_badge") {
            items.push_back({64, 64, item.horizontal, item.vertical});
            continue;
        }
        items.push_back({button ? buttonWidth : MeasureTextWidth(2, expandVars(item.label, runtime, ctx)),
                         item.type == MenuItem::Type::Slider ? 80 : (button ? 54 : 24), item.horizontal, item.vertical});
    }
    return SlimMenuLayout::build(ctx.baseScreenW, ctx.baseScreenH, menu.scale, menu.horizontal, menu.vertical, items);
}

size_t buttonItemIndex(const SlimMenu& menu, int button) {
    for (size_t i = 0; i < menu.items.size(); ++i) {
        if (menu.items[i].type != MenuItem::Type::Text && button-- == 0) return i;
    }
    return menu.items.size();
}

SDL_Rect menuScrollTrack(const SlimMenuLayout::Layout& layout) {
    return {layout.body.x + layout.body.w + 8, layout.body.y, 6, layout.body.h};
}

SDL_Rect menuScrollThumb(const SlimMenuLayout::Layout& layout, float scroll) {
    SDL_Rect thumb = menuScrollTrack(layout);
    thumb.h = std::min(thumb.h, std::max(12, thumb.h * layout.body.h / std::max(1, layout.contentHeight)));
    thumb.y += (int)std::lround((layout.body.h - thumb.h) * scroll / std::max(1, layout.maxScroll));
    return thumb;
}

bool drawMenuSprite(const SlimRuntime& runtime, SDL_Renderer* ren, const char* name,
                    const SDL_Rect& dst, float border = 0.0f) {
    const auto it = runtime.menuFrames.find(name);
    if (!runtime.menuTexture || it == runtime.menuFrames.end()) return false;
    const Frame& frame = it->second;
    if (border > 0.0f && !frame.rotated) {
        const SDL_FRect src{(float)frame.rect.x, (float)frame.rect.y, (float)frame.rect.w, (float)frame.rect.h};
        const SDL_FRect out{(float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h};
        // Keep the atlas corners and bevels intact when resizing a panel or button.
        const float scale = std::min(1.0f, std::min(dst.w, dst.h) / (border * 2.0f));
        return SDL_RenderTexture9Grid(ren, runtime.menuTexture, &src, border, border, border, border, scale, &out);
    } else {
        renderFrame(ren, runtime.menuTexture, frame, dst);
    }
    return true;
}

int* audioSliderValue(const MenuItem& item, const SlimMenuContext& ctx) {
    if (item.type != MenuItem::Type::Slider) return nullptr;
    const std::string name = normalizeVarName(item.command);
    if (name == "music_volume") return ctx.musicVolume;
    if (name == "sfx_volume") return ctx.sfxVolume;
    return nullptr;
}

SDL_Rect audioSliderTrack(const SDL_Rect& row) {
    const int inset = std::min(20, (row.w - 1) / 2);
    return {row.x + inset, row.y + row.h - 24, std::max(1, row.w - 2 * inset), 8};
}

void renderMenuExit(const SlimRuntime& runtime, const SlimMenuContext& ctx, bool focused) {
    const bool enabled = !menuNetworkBusy(runtime);
    if (runtime.menuTexture) {
        Uint8 red = 255, green = 255, blue = 255;
        SDL_GetTextureColorMod(runtime.menuTexture, &red, &green, &blue);
        const Uint8 brightness = enabled ? (focused ? 255 : 220) : 100;
        SDL_SetTextureColorMod(runtime.menuTexture, brightness, brightness, brightness);
        const bool textured = drawMenuSprite(runtime, ctx.ren, "close-window", kMenuExitRect);
        SDL_SetTextureColorMod(runtime.menuTexture, red, green, blue);
        if (textured) return;
    }
    SDL_SetRenderDrawColor(ctx.ren, enabled ? (focused ? 100 : 55) : 45, enabled ? 80 : 48, enabled ? 100 : 52, 255);
    SDL_RenderFillRect(ctx.ren, &kMenuExitRect);
    SDL_SetRenderDrawColor(ctx.ren, enabled ? 210 : 85, enabled ? 225 : 90, enabled ? 245 : 95, 255);
    SDL_RenderRect(ctx.ren, &kMenuExitRect);
    const SDL_Color color = enabled ? SDL_Color{255, 255, 255, 255} : SDL_Color{125, 130, 140, 255};
    DrawTextColored(ctx.ren, kMenuExitRect.x + (kMenuExitRect.w - MeasureTextWidth(2, "X")) / 2,
        kMenuExitRect.y + 10, 2, "X", color);
}

bool drawDifficultyBadge(SlimRuntime& runtime, SDL_Renderer* ren, const SDL_Rect& rect,
                         const std::string& label, bool chosen = false) {
    if (!runtime.difficultyAssetLoaded) {
        runtime.difficultyAssetLoaded = true;
        const auto frames = loadPlistFrames("assets/Sheets/df_menu-uhd.plist");
        const auto frame = frames.find("Diff");
        if (frame != frames.end()) {
            runtime.difficultyFrame = frame->second;
            runtime.difficultyTexture = loadTextureSafe(ren, "assets/Sheets/df_menu-uhd.png");
            if (runtime.difficultyTexture) {
                SDL_SetTextureScaleMode(runtime.difficultyTexture, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureBlendMode(runtime.difficultyTexture, SDL_BLENDMODE_BLEND);
            }
        }
    }
    if (!runtime.difficultyTexture) return false;
    SDL_SetTextureColorMod(runtime.difficultyTexture, chosen ? 140 : 255, chosen ? 225 : 255, 255);
    renderFrame(ren, runtime.difficultyTexture, runtime.difficultyFrame, rect);
    SDL_SetTextureColorMod(runtime.difficultyTexture, 255, 255, 255);
    const int scale = rect.h >= 60 ? 3 : (rect.h >= 32 ? 2 : 1);
    DrawTextColored(ren, rect.x + (rect.w - MeasureTextWidth(scale, label)) / 2,
        rect.y + (rect.h - scale * 10) / 2, scale, label, SDL_Color{30, 30, 30, 255});
    return true;
}

void renderDifficultyPopup(SlimRuntime& runtime, const SlimMenuContext& ctx) {
    int savedW = 0, savedH = 0;
    SDL_RendererLogicalPresentation savedPresentation;
    SDL_GetRenderLogicalPresentation(ctx.ren, &savedW, &savedH, &savedPresentation);
    SDL_SetRenderLogicalPresentation(ctx.ren, ctx.baseScreenW, ctx.baseScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderViewport(ctx.ren, nullptr);
    SDL_SetRenderScale(ctx.ren, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, 185);
    const SDL_Rect canvas{0, 0, ctx.baseScreenW, ctx.baseScreenH};
    SDL_RenderFillRect(ctx.ren, &canvas);
    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
    const auto layout = DifficultySelector::build(ctx.baseScreenW, ctx.baseScreenH);
    if (!drawMenuSprite(runtime, ctx.ren, "window", layout.panel, 56.0f)) {
        SDL_SetRenderDrawColor(ctx.ren, 34, 49, 65, 255);
        SDL_RenderFillRect(ctx.ren, &layout.panel);
    }
    SDL_SetRenderDrawColor(ctx.ren, 175, 205, 230, 255);
    SDL_RenderRect(ctx.ren, &layout.panel);
    auto centered = [&](const std::string& text, int y, int scale) {
        while (scale > 1 && MeasureTextWidth(scale, text) > layout.panel.w - 24) --scale;
        DrawText(ctx.ren, layout.panel.x + (layout.panel.w - MeasureTextWidth(scale, text)) / 2, y, scale, text);
    };
    centered("Select Difficulty", layout.panel.y + 12, layout.panel.h < 300 ? 2 : 3);
    centered("1-3 Easy   4-6 Medium   7-9 Hard", layout.panel.y + layout.panel.h * 15 / 100, 1);
    for (int i = 0; i < (int)layout.buttons.size(); ++i) {
        const bool chosen = i <= 9 && i == runtime.difficultyChoice;
        const bool focused = i == runtime.difficultyFocus;
        const auto& rect = layout.buttons[i];
        if (!drawMenuSprite(runtime, ctx.ren, "button_genreaic", rect, 6.0f)) {
            SDL_SetRenderDrawColor(ctx.ren, chosen ? 65 : 48, chosen ? 135 : 73, chosen ? 170 : 98, 255);
            SDL_RenderFillRect(ctx.ren, &rect);
        }
        SDL_SetRenderDrawColor(ctx.ren, focused ? 255 : 130, focused ? 240 : 165, focused ? 165 : 195, 255);
        SDL_RenderRect(ctx.ren, &rect);
        if (i <= 9) {
            const int size = std::max(1, std::min(rect.w - 8, rect.h - 8));
            const SDL_Rect badge{i == 0 ? rect.x + 8 : rect.x + (rect.w - size) / 2,
                rect.y + (rect.h - size) / 2, size, size};
            if (drawDifficultyBadge(runtime, ctx.ren, badge, i == 0 ? "?" : std::to_string(i), chosen)) {
                if (chosen) {
                    SDL_SetRenderDrawColor(ctx.ren, 100, 225, 255, 255);
                    const SDL_Rect outline{badge.x - 2, badge.y - 2, badge.w + 4, badge.h + 4};
                    SDL_RenderRect(ctx.ren, &outline);
                }
                if (i == 0) {
                    const int scale = MeasureTextWidth(2, "Unrated") < rect.w - size - 24 ? 2 : 1;
                    DrawText(ctx.ren, rect.x + size + 16, rect.y + (rect.h - scale * 10) / 2, scale, "Unrated");
                }
                continue;
            }
        }
        const std::string text = i == 0 ? "Unrated" : (i == 10 ? "Apply" : (i == 11 ? "Cancel" : std::to_string(i)));
        const int scale = MeasureTextWidth(2, text) < rect.w - 12 ? 2 : 1;
        DrawText(ctx.ren, rect.x + (rect.w - MeasureTextWidth(scale, text)) / 2,
            rect.y + (rect.h - scale * 10) / 2, scale, text);
    }
    if (runtime.difficultySaving) centered("Saving...", layout.panel.y + layout.panel.h * 80 / 100, 1);
    else if (!runtime.difficultyStatus.empty()) centered(runtime.difficultyStatus, layout.panel.y + layout.panel.h * 80 / 100, 1);
    renderMenuExit(runtime, ctx, false);
    SDL_SetRenderScale(ctx.ren, 1.0f, 1.0f);
    SDL_SetRenderViewport(ctx.ren, nullptr);
    SDL_SetRenderLogicalPresentation(ctx.ren, savedW, savedH, savedPresentation);
}

void renderMenu(const SlimMenu& menu, SlimRuntime& runtime, const SlimMenuContext& ctx, int selected,
                const SlimMenuLayout::Layout& layout) {
    SDL_SetRenderTarget(ctx.ren, ctx.gameTarget);
    SDL_SetRenderViewport(ctx.ren, nullptr);
    SDL_SetRenderClipRect(ctx.ren, nullptr);
    SDL_SetRenderScale(ctx.ren, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(ctx.ren, 20, 26, 34, 255);
    SDL_RenderClear(ctx.ren);

    runtime.scrollY = std::clamp(runtime.scrollY, 0.0f, (float)layout.maxScroll);
    SDL_SetRenderScale(ctx.ren, layout.scale, layout.scale);
    const SDL_Rect panel = layout.panel;
    SDL_SetRenderClipRect(ctx.ren, &panel);
    if (!drawMenuSprite(runtime, ctx.ren, "window", panel, 56.0f)) {
        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ctx.ren, 66, 101, 126, 255);
        SDL_RenderFillRect(ctx.ren, &panel);
        SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
    }

    const std::string title = expandVars(menu.title, runtime, ctx);
    int titleScale = 4;
    const int titleWidth = std::min(panel.w - 40, (int)(ctx.baseScreenW / layout.scale) - 224);
    while (titleScale > 1 && MeasureTextWidth(titleScale, title) > titleWidth) --titleScale;
    DrawText(ctx.ren, panel.x + (panel.w - MeasureTextWidth(titleScale, title)) / 2, panel.y + 22, titleScale, title);
    if (!runtime.waitingBind.empty() || !runtime.pendingGets.empty() ||
        runtime.onlineLevelsRequest.valid() || runtime.accountRoleRequest.valid()) {
        const std::string status = runtime.waitingBind.empty()
            ? "Loading" + std::string((SDL_GetTicks() / 250) % 4, '.')
            : "Press a key for " + runtime.waitingBind;
        DrawText(ctx.ren, panel.x + (panel.w - MeasureTextWidth(1, status)) / 2, panel.y + 70, 1, status);
    }

    SDL_SetRenderClipRect(ctx.ren, &layout.body);
    int buttonIndex = 0;
    for (size_t index = 0; index < menu.items.size(); ++index) {
        const auto& item = menu.items[index];
        const std::string label = expandVars(item.label, runtime, ctx);
        const SDL_Rect r = SlimMenuLayout::scrolled(layout.items[index], runtime.scrollY);
        const bool isSelected = item.type != MenuItem::Type::Text && buttonIndex++ == selected;
        if (r.y + r.h <= layout.body.y || r.y >= layout.body.y + layout.body.h) continue;
        if (item.type == MenuItem::Type::Text) {
            if (item.command == "difficulty_badge") {
                if (!drawDifficultyBadge(runtime, ctx.ren, r, label)) {
                    const std::string fallback = "Difficulty: " + runtime.vars["detail_difficulty"];
                    DrawText(ctx.ren, r.x + (r.w - MeasureTextWidth(2, fallback)) / 2,
                        r.y + (r.h - 20) / 2, 2, fallback);
                }
                continue;
            }
            const int textScale = MeasureTextWidth(2, label) <= r.w ? 2 : 1;
            DrawText(ctx.ren, r.x + SlimMenuLayout::offset(r.w - MeasureTextWidth(textScale, label), item.labelHorizontal),
                     r.y + SlimMenuLayout::offset(r.h - textScale * 10, item.labelVertical), textScale, label);
            continue;
        }
        if (item.type == MenuItem::Type::Slider) {
            const int* volume = audioSliderValue(item, ctx);
            const int value = volume ? std::clamp(*volume, 0, 128) : 0;
            const std::string sliderLabel = label + ": " + std::to_string((int)std::lround(value * 100.0f / 128.0f)) + "%";
            SDL_SetRenderDrawColor(ctx.ren, isSelected ? 74 : 45, isSelected ? 100 : 65, isSelected ? 125 : 84, 255);
            SDL_RenderFillRect(ctx.ren, &r);
            SDL_SetRenderDrawColor(ctx.ren, isSelected ? 220 : 130, isSelected ? 235 : 155, 255, 255);
            SDL_RenderRect(ctx.ren, &r);
            const int textScale = MeasureTextWidth(2, sliderLabel) <= r.w - 16 ? 2 : 1;
            DrawText(ctx.ren, r.x + (r.w - MeasureTextWidth(textScale, sliderLabel)) / 2, r.y + 8, textScale, sliderLabel);
            const SDL_Rect track = audioSliderTrack(r);
            SDL_SetRenderDrawColor(ctx.ren, 24, 35, 48, 255);
            SDL_RenderFillRect(ctx.ren, &track);
            const int filled = (int)std::lround(track.w * value / 128.0f);
            const SDL_Rect fill{track.x, track.y, filled, track.h};
            SDL_SetRenderDrawColor(ctx.ren, 110, 200, 245, 255);
            SDL_RenderFillRect(ctx.ren, &fill);
            const SDL_Rect thumb{track.x + filled - 6, track.y - 6, 12, 20};
            SDL_SetRenderDrawColor(ctx.ren, isSelected ? 255 : 210, isSelected ? 255 : 230, 255, 255);
            SDL_RenderFillRect(ctx.ren, &thumb);
            continue;
        }
        if (runtime.menuTexture) {
            const Uint8 brightness = isSelected ? 255 : 205;
            SDL_SetTextureColorMod(runtime.menuTexture, brightness, brightness, brightness);
        }
        const bool textured = drawMenuSprite(runtime, ctx.ren, "button_genreaic", r, 6.0f);
        if (runtime.menuTexture) SDL_SetTextureColorMod(runtime.menuTexture, 255, 255, 255);
        if (!textured) {
            SDL_SetRenderDrawColor(ctx.ren, isSelected ? 96 : 54, isSelected ? 116 : 74, isSelected ? 134 : 92, 255);
            SDL_RenderFillRect(ctx.ren, &r);
        }
        SDL_SetRenderDrawColor(ctx.ren, isSelected ? 220 : 160, isSelected ? 232 : 184, isSelected ? 242 : 204, 255);
        SDL_RenderRect(ctx.ren, &r);
        const int iconSize = std::max(12, std::min(36, r.h - 6));
        const SDL_Rect icon{r.x + r.w - iconSize - 8, r.y + (r.h - iconSize) / 2, iconSize, iconSize};
        bool hasIcon = false;
        if (item.command == "toggle") {
            const std::string value = boolSettingValue(normalizeVarName(expandVars(item.arg, runtime, ctx)), ctx);
            if (!value.empty()) {
                hasIcon = drawMenuSprite(runtime, ctx.ren, value == "ON" ? "checkbox_active" : "checkbox_disabled", icon);
            }
        } else if (item.command == "back" || item.command == "quit" || label == "Back") {
            hasIcon = drawMenuSprite(runtime, ctx.ren, "close-window", icon);
        }
        const int labelWidth = std::max(1, r.w - (hasIcon ? 2 * (iconSize + 16) : 16));
        const int labelScale = MeasureTextWidth(2, label) <= labelWidth ? 2 : 1;
        const int textInset = hasIcon ? iconSize + 16 : 8;
        DrawText(ctx.ren, r.x + textInset + SlimMenuLayout::offset(labelWidth - MeasureTextWidth(labelScale, label), item.labelHorizontal),
                 r.y + 4 + SlimMenuLayout::offset(r.h - 8 - labelScale * 10, item.labelVertical), labelScale, label);
    }
    SDL_SetRenderClipRect(ctx.ren, &panel);
    if (layout.maxScroll > 0) {
        const SDL_Rect track = menuScrollTrack(layout);
        const SDL_Rect thumb = menuScrollThumb(layout, runtime.scrollY);
        SDL_SetRenderDrawColor(ctx.ren, 45, 45, 45, 255);
        SDL_RenderFillRect(ctx.ren, &track);
        SDL_SetRenderDrawColor(ctx.ren, 220, 220, 220, 255);
        SDL_RenderFillRect(ctx.ren, &thumb);
    }
    SDL_SetRenderClipRect(ctx.ren, nullptr);
    SDL_SetRenderScale(ctx.ren, 1.0f, 1.0f);
    // The exit control stays in the screen corner, outside scrolling content.
    renderMenuExit(runtime, ctx, selected == buttonIndex);
}

void presentMenuFrame(const SlimMenu& menu, SlimRuntime& runtime, const SlimMenuContext& ctx, int selected,
                      const SlimMenuLayout::Layout* prepared = nullptr) {
    const auto computed = prepared ? SlimMenuLayout::Layout{} : layoutFor(menu, runtime, ctx);
    int outputW = 0, outputH = 0;
    SDL_GetRenderOutputSize(ctx.ren, &outputW, &outputH);
    const SDL_Rect dst = computePresentRect(outputW, outputH, ctx.baseScreenW, ctx.baseScreenH);
    BeginNativeTextOverlay(ctx.ren, ctx.baseScreenW, ctx.baseScreenH, dst);
    renderMenu(menu, runtime, ctx, selected, prepared ? *prepared : computed);
    SDL_SetRenderTarget(ctx.ren, nullptr);
    SDL_SetRenderViewport(ctx.ren, nullptr);
    SDL_SetRenderClipRect(ctx.ren, nullptr);
    SDL_SetRenderScale(ctx.ren, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, 255);
    SDL_RenderClear(ctx.ren);
    SDL_RenderTexture(ctx.ren, ctx.gameTarget, static_cast<const SDL_Rect*>(nullptr), &dst);
    FlushNativeTextOverlay(ctx.ren);
    if (runtime.difficultyPopupOpen) renderDifficultyPopup(runtime, ctx);
    SDL_RenderPresent(ctx.ren);
}

MenuItem* buttonAt(SlimMenu& menu, int selected) {
    int buttonIndex = 0;
    for (auto& item : menu.items) {
        if (item.type == MenuItem::Type::Text) continue;
        if (buttonIndex == selected) return &item;
        ++buttonIndex;
    }
    return nullptr;
}

int buttonCount(const SlimMenu& menu) {
    int count = 0;
    for (const auto& item : menu.items) if (item.type != MenuItem::Type::Text) ++count;
    return count;
}

bool toggleBool(const std::string& name, SlimMenuContext& ctx) {
    const std::string var = normalizeVarName(name);
    bool* target = boolSettingPtr(var, ctx);
    if (!target) return false;
    const bool next = !*target;
    if (var == "fullscreen" && ctx.applyFullscreen) {
        if (!ctx.applyFullscreen(next)) return false;
    }
    *target = next;
    if (var == "vsync_enabled" && ctx.applyRenderVsync) ctx.applyRenderVsync();
    if (var == "native_text_resolution_enabled") SetNativeTextResolutionEnabled(*target);
    if (var == "menu_music_enabled" && ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
    if ((var == "mute_all_audio" || var == "menu_music_enabled") && ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    if (ctx.saveClientSettings) ctx.saveClientSettings();
    return true;
}

bool adjustInt(const std::string& name, int delta, SlimMenuContext& ctx) {
    int* target = nullptr;
    const std::string var = normalizeVarName(name);
    if (var == "music_volume") target = ctx.musicVolume;
    if (var == "sfx_volume") target = ctx.sfxVolume;
    if (var == "ui_scale_percent") target = ctx.uiScalePercent;
    if (var == "ui_edge_padding") target = ctx.uiEdgePadding;
    if (!target) return false;
    if (var == "ui_scale_percent") {
        *target = UiScale::stepPercent(*target, delta > 0 ? 1 : -1);
        SetTextScaleMultiplier(UiScale::multiplier(*target));
        if (ctx.updateDynamicResolution) ctx.updateDynamicResolution();
    } else if (var == "ui_edge_padding") {
        *target = UiScale::stepEdgePadding(*target, delta > 0 ? 1 : -1);
    } else {
        *target = std::clamp(*target + delta, 0, 128);
        if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    }
    if (ctx.saveClientSettings) ctx.saveClientSettings();
    return true;
}

SlimMenuExit execute(MenuItem& item, SlimRuntime& runtime, SlimMenuContext& ctx, std::string& currentMenu) {
    if (item.type == MenuItem::Type::Slider) return SlimMenuExit::Continue;
    const std::string command = item.command;
    const std::string arg = expandVars(item.arg, runtime, ctx);
    if (command == "back") return menuNetworkBusy(runtime) ? SlimMenuExit::Continue : SlimMenuExit::Back;
    if (command == "quit") {
        if (ctx.running) *ctx.running = false;
        return SlimMenuExit::Quit;
    }
    if (command == "menu") {
        if (!arg.empty()) currentMenu = arg;
        return SlimMenuExit::Continue;
    }
    if (command == "refresh_levels") {
        if (arg == "online") runtime.onlineLevelsLoaded = false;
        else runtime.localLevelsLoaded = false;
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "level_page") {
        const std::string::size_type colon = arg.find(':');
        const std::string prefix = colon == std::string::npos ? "local" : arg.substr(0, colon);
        const std::string direction = colon == std::string::npos ? arg : arg.substr(colon + 1);
        const std::string pageVar = prefix + "_level_page";
        int page = std::max(0, std::atoi(runtime.vars[pageVar].c_str()));
        if (direction == "prev") page = std::max(0, page - 1);
        if (direction == "next" && (prefix != "online" || page + 1 < runtime.onlinePageCount)) page += 1;
        runtime.vars[pageVar] = std::to_string(page);
        currentMenu = prefix == "online" ? "onlinelevels" : "locallevels";
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "level_filter") {
        const std::string::size_type colon = arg.find(':');
        const std::string prefix = colon == std::string::npos ? "local" : arg.substr(0, colon);
        const std::string filter = colon == std::string::npos ? arg : arg.substr(colon + 1);
        runtime.vars[prefix + "_level_filter"] = filter.empty() ? "all" : filter;
        runtime.vars[prefix + "_level_page"] = "0";
        currentMenu = prefix == "online" ? "onlinelevels" : "locallevels";
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "level_detail") {
        const std::string::size_type colon = arg.find(':');
        const std::string source = colon == std::string::npos ? "local" : arg.substr(0, colon);
        const std::string index = colon == std::string::npos ? arg : arg.substr(colon + 1);
        runtime.vars["detail_source"] = source;
        runtime.vars["detail_index"] = index;
        runtime.vars["level_detail_status"].clear();
        currentMenu = "leveldetail";
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "world") {
        if (!ctx.progression || !ctx.progression->worldSelectUnlocked || !ctx.selectedLevelPath) return SlimMenuExit::Continue;
        const int world = std::atoi(arg.c_str());
        if (world < 1 || world > 7) return SlimMenuExit::Continue;
        LevelManager manager;
        const auto path = manager.levelPathByCode(world * 100 + 10 + (world >= 6 ? 3 : 1));
        if (path.empty()) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = path;
        if (ctx.selectedLevelName) ctx.selectedLevelName->clear();
        return SlimMenuExit::StartGame;
    }
    if (command == "hardmode_practice") {
        if (!ctx.progression || !ctx.progression->hardmodeUnlocked || !ctx.selectedLevelPath) return SlimMenuExit::Continue;
        const auto selection = HardmodePracticeSelection(std::atoi(arg.c_str()));
        if (selection.empty()) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = selection;
        if (ctx.selectedLevelName) ctx.selectedLevelName->clear();
        return SlimMenuExit::StartGame;
    }
    if (command == "hardmode") {
        if (!ctx.progression || !ctx.progression->hardmodeUnlocked || !ctx.selectedLevelPath) return SlimMenuExit::Continue;
        if (arg == "continue" && !FileExists(HardmodeSavePath())) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = arg == "continue" ? kContinueHardmodeToken : kStartHardmodeToken;
        if (ctx.selectedLevelName) ctx.selectedLevelName->clear();
        return SlimMenuExit::StartGame;
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
        runtime.localLevelsLoaded = false;
        if (path.empty()) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = path;
        return SlimMenuExit::StartGame;
    }
    if (command == "custom") {
        if (!ctx.selectedLevelPath) return SlimMenuExit::Back;
        const std::string path = RunCustomLevelSelect(ctx.win, ctx.ren);
        runtime.localLevelsLoaded = false;
        if (path.empty()) return SlimMenuExit::Continue;
        *ctx.selectedLevelPath = path;
        return SlimMenuExit::StartGame;
    }
    if (command == "create_level") {
        const std::string createdPath = OpenLocalLevelEditorForMenu(ctx.win, ctx.ren, {});
        runtime.localLevelsLoaded = false;
        runtime.vars["level_detail_status"] = createdPath.empty() ? "Create cancelled." : "Level saved.";
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "edit_level") {
        const int index = std::atoi(arg.c_str());
        std::vector<SlimLevelEntry> levels = filteredGeneratedLevels("local", runtime, ctx);
        if (index < 0 || index >= (int)levels.size()) {
            runtime.vars["level_detail_status"] = "Level no longer available.";
            runtime.vars["__reload_menu"] = "1";
            return SlimMenuExit::Continue;
        }
        const std::string editedPath = OpenLocalLevelEditorForMenu(ctx.win, ctx.ren, levels[index].path);
        runtime.localLevelsLoaded = false;
        runtime.vars["level_detail_status"] = editedPath.empty() ? "Edit cancelled." : "Level saved.";
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "upload_level") {
        const int index = std::atoi(arg.c_str());
        std::vector<SlimLevelEntry> levels = filteredGeneratedLevels("local", runtime, ctx);
        if (index < 0 || index >= (int)levels.size()) {
            runtime.vars["level_detail_status"] = "Level no longer available.";
            runtime.vars["__reload_menu"] = "1";
            return SlimMenuExit::Continue;
        }
        std::string status;
        const bool ok = uploadLocalLevelToServer(LevelEntry{levels[index].label, levels[index].path, levels[index].difficulty}, status);
        if (ok) runtime.onlineLevelsLoaded = false;
        runtime.vars["level_detail_status"] = status.empty() ? (ok ? "Uploaded." : "Upload failed.") : status;
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "delete_level") {
        const int index = std::atoi(arg.c_str());
        std::vector<SlimLevelEntry> levels = filteredGeneratedLevels("local", runtime, ctx);
        if (index >= 0 && index < (int)levels.size()) {
            std::error_code ec;
            const bool removed = std::filesystem::remove(levels[index].path, ec);
            ClearLocalLevelVerification(levels[index].path);
            runtime.vars["level_detail_status"] = removed ? "Deleted local level." : "Could not delete local level.";
        } else {
            runtime.vars["level_detail_status"] = "Level no longer available.";
        }
        currentMenu = "locallevels";
        runtime.localLevelsLoaded = false;
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "update_online_level") {
        const int index = std::atoi(arg.c_str());
        std::vector<SlimLevelEntry> levels = filteredGeneratedLevels("online", runtime, ctx);
        if (index < 0 || index >= (int)levels.size()) {
            runtime.vars["level_detail_status"] = "Level no longer available.";
            runtime.vars["__reload_menu"] = "1";
            return SlimMenuExit::Continue;
        }
        std::string cachedPath;
        const bool ok = updateOnlineLevelCache(levels[index], cachedPath);
        runtime.vars["level_detail_status"] = ok ? "Updated downloaded copy." : "Update failed.";
        if (ok) {
            runtime.vars["detail_cached_path"] = cachedPath;
            runtime.onlineLevelsLoaded = false;
        }
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "rate_online_level") {
        if (!currentAccountIsMod(runtime, ctx)) {
            runtime.vars["level_detail_status"] = "Moderator account required.";
            runtime.vars["__reload_menu"] = "1";
            return SlimMenuExit::Continue;
        }
        const int index = std::atoi(arg.c_str());
        std::vector<SlimLevelEntry> levels = filteredGeneratedLevels("online", runtime, ctx);
        if (index < 0 || index >= (int)levels.size()) {
            runtime.vars["level_detail_status"] = "Level no longer available.";
            runtime.vars["__reload_menu"] = "1";
            return SlimMenuExit::Continue;
        }
        runtime.difficultyLevel = levels[index];
        runtime.difficultyChoice = runtime.difficultyFocus = std::clamp(levels[index].difficulty, 0, 9);
        runtime.difficultyStatus.clear();
        runtime.difficultyPopupOpen = true;
        return SlimMenuExit::Continue;
    }
    if (command == "vote_online_level") {
        const std::string::size_type colon = arg.find(':');
        const int index = std::atoi((colon == std::string::npos ? arg : arg.substr(0, colon)).c_str());
        const std::string vote = colon == std::string::npos ? "like" : arg.substr(colon + 1);
        std::vector<SlimLevelEntry> levels = filteredGeneratedLevels("online", runtime, ctx);
        if (index < 0 || index >= (int)levels.size()) {
            runtime.vars["level_detail_status"] = "Level no longer available.";
            runtime.vars["__reload_menu"] = "1";
            return SlimMenuExit::Continue;
        }
        std::string status;
        const bool ok = putOnlineLevelVote(levels[index], vote, ctx, status);
        if (ok) runtime.onlineLevelsLoaded = false;
        runtime.vars["level_detail_status"] = status.empty() ? (ok ? "Vote saved." : "Vote failed.") : status;
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "saved") {
        if (ctx.selectedLevelPath) *ctx.selectedLevelPath = kSavedGameSelectionToken;
        return SlimMenuExit::StartGame;
    }
    if (command == "save_slot") {
        const int slot = parseSaveSlotArg(arg);
        if (ctx.activeSaveSlotIndex) *ctx.activeSaveSlotIndex = slot;
        if (ctx.selectedLevelPath) *ctx.selectedLevelPath = saveSlotExists(slot) ? kSavedGameSelectionToken : "";
        if (ctx.saveClientSettings) ctx.saveClientSettings();
        return SlimMenuExit::StartGame;
    }
    if (command == "select_save") {
        const int slot = parseSaveSlotArg(arg);
        if (ctx.activeSaveSlotIndex) *ctx.activeSaveSlotIndex = slot;
        runtime.vars["last_save_action"] = "Selected slot " + std::to_string(slot + 1) + ".";
        if (ctx.saveClientSettings) ctx.saveClientSettings();
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "delete_save") {
        const int slot = parseSaveSlotArg(arg);
        std::error_code ec;
        const bool removed = std::filesystem::remove(saveSlotPath(slot), ec);
        runtime.vars["last_save_action"] = removed
            ? "Deleted slot " + std::to_string(slot + 1) + "."
            : (ec ? "Could not delete slot " + std::to_string(slot + 1) + "." : "Slot " + std::to_string(slot + 1) + " was already empty.");
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "start") {
        if (ctx.selectedLevelPath) *ctx.selectedLevelPath = arg;
        if (ctx.selectedLevelName) {
            *ctx.selectedLevelName = arg == runtime.vars["detail_path"] ? runtime.vars["detail_label"] : std::string();
        }
        return SlimMenuExit::StartGame;
    }
    if (command == "account_open") {
        std::string url = ctx.accountManagerUrl ? *ctx.accountManagerUrl : std::string();
        if (url.empty() && ctx.levelServerUrl) url = *ctx.levelServerUrl;
        if (url.empty()) {
            runtime.vars["account_action_status"] = "Account manager unavailable.";
        } else if (!SDL_OpenURL(url.c_str())) {
            runtime.vars["account_action_status"] = "Could not open account manager.";
        } else {
            runtime.vars["account_action_status"] = "Opened account manager.";
        }
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "account_logout") {
        RevokeLevelServerSession();
        if (ctx.levelServerAuthToken) ctx.levelServerAuthToken->clear();
        if (ctx.levelServerAccountUsername) ctx.levelServerAccountUsername->clear();
        SetLevelServerAuthToken(ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string());
        SetLevelServerAccountUsername(ctx.levelServerAccountUsername ? *ctx.levelServerAccountUsername : std::string());
        if (ctx.saveClientSettings) ctx.saveClientSettings();
        runtime.vars["account_action_status"] = "Logged out.";
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "account_repair") {
        if (ctx.levelServerAuthToken) ctx.levelServerAuthToken->clear();
        if (ctx.levelServerAccountUsername) ctx.levelServerAccountUsername->clear();
        SetLevelServerAuthToken(ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string());
        SetLevelServerAccountUsername(ctx.levelServerAccountUsername ? *ctx.levelServerAccountUsername : std::string());
        if (ctx.saveClientSettings) ctx.saveClientSettings();
        runtime.vars["account_action_status"] = "Cleared saved login.";
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "server_update") {
        if (!currentAccountIsMod(runtime, ctx)) {
            runtime.vars["account_action_status"] = "Moderator account required.";
            runtime.vars["__reload_menu"] = "1";
            return SlimMenuExit::Continue;
        }
        std::string status;
        const bool ok = putServerUpdateTrigger(ctx, status);
        runtime.vars["account_action_status"] = status.empty() ? (ok ? "Server update triggered." : "Server update failed.") : status;
        runtime.vars["__reload_menu"] = "1";
        return SlimMenuExit::Continue;
    }
    if (command == "toggle") {
        (void)toggleBool(arg, ctx);
        return SlimMenuExit::Continue;
    }
    if (command == "bind") {
        runtime.waitingBind = normalizeVarName(arg);
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
            if (url.empty()) {
                runtime.vars[name].clear();
            } else {
                const bool pending = std::any_of(runtime.pendingGets.begin(), runtime.pendingGets.end(),
                    [&](const SlimRuntime::PendingGet& get) { return get.variable == name; });
                if (!pending) runtime.pendingGets.push_back({name, ReadTextFileAsync(url)});
            }
        }
        return SlimMenuExit::Continue;
    }
    SDL_Log("SLIM MENU: unknown command '%s'", command.c_str());
    return SlimMenuExit::Continue;
}

} // namespace

// Only menu data survives gameplay. Renderer assets belong to each menu visit.
struct SlimMenuSession {
    std::string menu;
    std::unordered_map<std::string, std::string> vars;
    std::vector<SlimLevelEntry> localLevels, onlineLevels;
    bool localLevelsLoaded = false, onlineLevelsLoaded = false;
    std::string onlineLevelsKey;
    int onlinePageCount = 1, onlineTotal = 0;
    int selected = 0;
    float scrollY = 0.0f;
};

std::shared_ptr<SlimMenuSession> CreateSlimMenuSession() {
    return std::make_shared<SlimMenuSession>();
}

std::string PendingSlimMenu(const std::shared_ptr<SlimMenuSession>& session) {
    return session ? session->menu : std::string();
}

SlimMenuExit RunSlimMenu(SlimMenuContext& ctx, const std::string& menuName) {
    // Drop any queued text belonging to the screen that opened this menu.
    CancelNativeTextOverlay(ctx.ren);
    SlimRuntime runtime;
    runtime.ownsMenuTexture = !ctx.menuTexture;
    runtime.menuTexture = ctx.menuTexture ? ctx.menuTexture : loadTextureSafe(ctx.ren, "assets/Sheets/DF_Menus-uhd.png");
    runtime.menuFrames = ctx.menuFrames ? *ctx.menuFrames : loadPlistFrames("assets/Sheets/DF_Menus-uhd.plist");
    if (runtime.menuTexture) {
        SDL_SetTextureScaleMode(runtime.menuTexture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(runtime.menuTexture, SDL_BLENDMODE_BLEND);
    }
    std::string currentMenu = menuName;
    std::string viewedMenu;
    std::string restoreCommand, restoreArgument, restoreLabel;
    bool restoreSelection = false;
    int selected = 0;
    if (ctx.session && !ctx.session->menu.empty()) {
        currentMenu = ctx.session->menu;
        viewedMenu = currentMenu;
        runtime.vars = ctx.session->vars;
        runtime.localLevels = ctx.session->localLevels;
        runtime.onlineLevels = ctx.session->onlineLevels;
        runtime.localLevelsLoaded = ctx.session->localLevelsLoaded;
        runtime.onlineLevelsLoaded = ctx.session->onlineLevelsLoaded;
        runtime.onlineLevelsKey = ctx.session->onlineLevelsKey;
        runtime.onlinePageCount = ctx.session->onlinePageCount;
        runtime.onlineTotal = ctx.session->onlineTotal;
        selected = ctx.session->selected;
        runtime.scrollY = ctx.session->scrollY;
        ctx.session->menu.clear();
    }
    while (ctx.running && *ctx.running) {
        SlimMenu menu = loadMenu(currentMenu, runtime, ctx);
        if (restoreSelection) {
            selected = 0;
            int index = 0;
            for (const auto& item : menu.items) {
                if (item.type == MenuItem::Type::Text) continue;
                if (item.command == restoreCommand && item.arg == restoreArgument &&
                    (item.command != "level_detail" || item.label == restoreLabel)) {
                    selected = index;
                    break;
                }
                ++index;
            }
            runtime.scrollY = SlimMenuLayout::reveal(layoutFor(menu, runtime, ctx), buttonItemIndex(menu, selected), runtime.scrollY);
            restoreSelection = false;
        }
        if (viewedMenu != currentMenu) {
            viewedMenu = currentMenu;
            selected = 0;
            runtime.scrollY = SlimMenuLayout::reveal(layoutFor(menu, runtime, ctx), buttonItemIndex(menu, selected), 0.0f);
        }
        bool draggingScrollbar = false, touchActive = false, touchDragged = false;
        int draggingSlider = -1;
        bool sliderUsesTouch = false;
        bool exitTouchActive = false;
        SDL_FingerID exitFinger = 0;
        bool popupTouchActive = false;
        SDL_FingerID popupFinger = 0;
        int popupTouchButton = -1;
        SDL_FingerID trackedFinger = 0;
        float thumbGrab = 0.0f, touchStartX = 0.0f, touchStartY = 0.0f, touchStartScroll = 0.0f;
        presentMenuFrame(menu, runtime, ctx, selected);
        SDL_Event e;
        while (ctx.running && *ctx.running) {
            bool reloadOnline = false;
            if (runtime.onlineLevelsRequest.valid() &&
                runtime.onlineLevelsRequest.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                const bool current = runtime.pendingOnlineLevelsKey == onlinePageKey(runtime, ctx);
                try {
                    OnlineLevelPage result = runtime.onlineLevelsRequest.get();
                    if (current) {
                        runtime.onlineLevels = std::move(result.levels);
                        runtime.onlinePageCount = result.pageCount;
                        runtime.onlineTotal = result.total;
                        runtime.vars["online_level_page"] = std::to_string(result.page);
                        runtime.onlineLevelsKey = onlinePageKey(runtime, ctx);
                        runtime.vars["online_level_status"].clear();
                    }
                } catch (const std::exception& error) {
                    SDL_Log("Online menu load failed: %s", error.what());
                    if (current) runtime.vars["online_level_status"] = "Could not load this page. Refresh to retry.";
                }
                runtime.onlineLevelsLoaded = current;
                reloadOnline = true;
            }
            if (runtime.accountRoleRequest.valid() &&
                runtime.accountRoleRequest.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                bool isMod = false;
                try { isMod = runtime.accountRoleRequest.get(); }
                catch (const std::exception& error) { SDL_Log("Account role lookup failed: %s", error.what()); }
                const std::string currentRoleKey = (ctx.levelServerUrl ? *ctx.levelServerUrl : std::string()) + "\n" +
                    (ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : std::string());
                if (runtime.pendingAccountRoleKey == currentRoleKey) {
                    runtime.accountRoleKey = currentRoleKey;
                    runtime.vars["__account_is_mod"] = isMod ? "true" : "false";
                }
                if (currentMenu == "leveldetail") reloadOnline = true;
            }
            if (reloadOnline && (currentMenu == "onlinelevels" ||
                (currentMenu == "leveldetail" && runtime.vars["detail_source"] == "online"))) {
                if (const MenuItem* focused = buttonAt(menu, selected)) {
                    restoreCommand = focused->command;
                    restoreArgument = focused->arg;
                    restoreLabel = focused->label;
                    restoreSelection = true;
                }
                if (currentMenu == "leveldetail") {
                    const auto levels = filteredGeneratedLevels("online", runtime, ctx);
                    const auto found = std::find_if(levels.begin(), levels.end(), [&](const SlimLevelEntry& level) {
                        return level.path == runtime.vars["detail_path"];
                    });
                    runtime.vars["detail_index"] = found == levels.end() ? "-1" : std::to_string(found - levels.begin());
                }
                goto next_menu;
            }
            for (auto it = runtime.pendingGets.begin(); it != runtime.pendingGets.end();) {
                if (it->response.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                    ++it;
                    continue;
                }
                try {
                    runtime.vars[it->variable] = it->response.get();
                } catch (const std::exception& error) {
                    runtime.vars[it->variable].clear();
                    SDL_Log("Menu GET failed: %s", error.what());
                }
                it = runtime.pendingGets.erase(it);
            }
            const int exitIndex = buttonCount(menu);
            const int count = exitIndex + 1;
            selected = std::clamp(selected, 0, count - 1);
            const auto layout = layoutFor(menu, runtime, ctx);
            runtime.scrollY = std::clamp(runtime.scrollY, 0.0f, (float)layout.maxScroll);
            auto scrollTo = [&](float y) { runtime.scrollY = std::clamp(y, 0.0f, (float)layout.maxScroll); };
            auto pointer = [&](float x, float y, bool normalized, float& outX, float& outY) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                SDL_GetWindowSize(ctx.win, &winW, &winH);
                if (normalized) { x *= winW; y *= winH; }
                if (!windowToGamePoint((int)x, (int)y, winW, winH, ctx.baseScreenW, ctx.baseScreenH, gx, gy)) return false;
                outX = gx / layout.scale;
                outY = gy / layout.scale;
                return true;
            };
            auto overExit = [&](float x, float y) {
                return SlimMenuLayout::contains(kMenuExitRect, x * layout.scale, y * layout.scale);
            };
            auto hitButton = [&](float x, float y) {
                int button = 0;
                for (size_t i = 0; i < menu.items.size(); ++i) {
                    if (menu.items[i].type == MenuItem::Type::Text) continue;
                    if (SlimMenuLayout::hit(layout, i, x, y, runtime.scrollY)) return button;
                    ++button;
                }
                return -1;
            };
            auto dragScrollbar = [&](float y) {
                const auto track = menuScrollTrack(layout);
                const auto thumb = menuScrollThumb(layout, runtime.scrollY);
                scrollTo((y - track.y - thumbGrab) * layout.maxScroll / std::max(1, track.h - thumb.h));
            };
            auto dragVolume = [&](float x) {
                MenuItem* item = buttonAt(menu, draggingSlider);
                if (!item) return;
                int* volume = audioSliderValue(*item, ctx);
                if (!volume) return;
                const auto row = SlimMenuLayout::scrolled(layout.items[buttonItemIndex(menu, draggingSlider)], runtime.scrollY);
                const auto track = audioSliderTrack(row);
                const int value = (int)std::lround(std::clamp((x - track.x) / track.w, 0.0f, 1.0f) * 128.0f);
                if (value != *volume) adjustInt(item->command, value - *volume, ctx);
            };
            auto chooseDifficulty = [&](int option) {
                if (menuNetworkBusy(runtime)) return;
                if (option >= 0 && option <= 9) {
                    runtime.difficultyChoice = runtime.difficultyFocus = option;
                    runtime.difficultyStatus.clear();
                } else if (option == 11) {
                    runtime.difficultyPopupOpen = false;
                } else if (option == 10) {
                    runtime.difficultySaving = true;
                    presentMenuFrame(menu, runtime, ctx, selected, &layout);
                    std::string status;
                    bool ok = false;
                    try { ok = putOnlineLevelDifficulty(runtime.difficultyLevel, runtime.difficultyChoice, ctx, status); }
                    catch (const std::exception& error) { SDL_Log("Difficulty save failed: %s", error.what()); }
                    runtime.difficultySaving = false;
                    runtime.vars["level_detail_status"] = status.empty() ? (ok ? "Rating updated." : "Rate failed.") : status;
                    if (ok) {
                        runtime.difficultyPopupOpen = false;
                        runtime.onlineLevelsLoaded = false;
                        runtime.vars["__reload_menu"] = "1";
                    } else {
                        runtime.difficultyStatus = "Could not save. Try again.";
                    }
                }
            };
            while (SDL_PollEvent(&e)) {
                int activate = -1;
                if (e.type == SDL_EVENT_QUIT) {
                    *ctx.running = false;
                    return SlimMenuExit::Quit;
                }
                if (runtime.difficultyPopupOpen) {
                    const auto popupLayout = DifficultySelector::build(ctx.baseScreenW, ctx.baseScreenH);
                    auto hitPopup = [&](float x, float y) {
                        if (overExit(x, y)) return 12;
                        return DifficultySelector::hit(popupLayout, x * layout.scale, y * layout.scale);
                    };
                    auto navigatePopup = [&](int dx, int dy) {
                        runtime.difficultyFocus = DifficultySelector::navigate(runtime.difficultyFocus, dx, dy);
                        if (runtime.difficultyFocus <= 9) runtime.difficultyChoice = runtime.difficultyFocus;
                    };
                    auto activatePopup = [&](int option) {
                        if (option == 12) return !menuNetworkBusy(runtime);
                        chooseDifficulty(option);
                        return false;
                    };
                    if (e.type == SDL_EVENT_KEY_DOWN) {
                        const auto key = e.key.key;
                        if (key == SDLK_ESCAPE || key == SDLK_AC_BACK) chooseDifficulty(11);
                        else if (key == SDLK_LEFT) navigatePopup(-1, 0);
                        else if (key == SDLK_RIGHT) navigatePopup(1, 0);
                        else if (key == SDLK_UP) navigatePopup(0, -1);
                        else if (key == SDLK_DOWN) navigatePopup(0, 1);
                        else if (key == SDLK_TAB) runtime.difficultyFocus = (runtime.difficultyFocus + ((e.key.mod & SDL_KMOD_SHIFT) ? 11 : 1)) % 12;
                        else if (key >= SDLK_0 && key <= SDLK_9) chooseDifficulty((int)(key - SDLK_0));
                        else if (!e.key.repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
                            const int focused = runtime.difficultyFocus;
                            chooseDifficulty(focused);
                            if (focused <= 9) runtime.difficultyFocus = 10;
                        }
                    }
                    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                        const auto button = e.gbutton.button;
                        if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) chooseDifficulty(11);
                        else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) navigatePopup(-1, 0);
                        else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) navigatePopup(1, 0);
                        else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) navigatePopup(0, -1);
                        else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) navigatePopup(0, 1);
                        else if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                            const int focused = runtime.difficultyFocus;
                            chooseDifficulty(focused);
                            if (focused <= 9) runtime.difficultyFocus = 10;
                        }
                    }
                    float px = 0.0f, py = 0.0f;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID &&
                        pointer(e.button.x, e.button.y, false, px, py) && activatePopup(hitPopup(px, py))) return SlimMenuExit::Back;
                    if (e.type == SDL_EVENT_FINGER_DOWN && !popupTouchActive && pointer(e.tfinger.x, e.tfinger.y, true, px, py)) {
                        popupTouchActive = true;
                        popupFinger = e.tfinger.fingerID;
                        popupTouchButton = hitPopup(px, py);
                    }
                    if (popupTouchActive && (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) && e.tfinger.fingerID == popupFinger) {
                        popupTouchActive = false;
                        if (e.type == SDL_EVENT_FINGER_UP && pointer(e.tfinger.x, e.tfinger.y, true, px, py) &&
                            hitPopup(px, py) == popupTouchButton && activatePopup(popupTouchButton)) return SlimMenuExit::Back;
                    }
                    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) popupTouchActive = false;
                    if (runtime.vars.erase("__reload_menu") > 0) goto next_menu;
                    continue;
                }
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    if (!runtime.waitingBind.empty()) {
                        if (e.key.repeat) continue;
                        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) {
                            runtime.waitingBind.clear();
                            continue;
                        }
                        if (SDL_Scancode* binding = keyBindingPtr(runtime.waitingBind, ctx)) {
                            if (e.key.scancode > SDL_SCANCODE_UNKNOWN && e.key.scancode < SDL_SCANCODE_COUNT) {
                                *binding = e.key.scancode;
                                if (ctx.saveClientSettings) ctx.saveClientSettings();
                            }
                        }
                        runtime.waitingBind.clear();
                        continue;
                    }
                    if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) {
                        if (!menuNetworkBusy(runtime)) return SlimMenuExit::Back;
                        continue;
                    }
                    if (MenuItem* item = buttonAt(menu, selected); item && audioSliderValue(*item, ctx)) {
                        const int value = *audioSliderValue(*item, ctx);
                        int delta = 0;
                        if (e.key.key == SDLK_LEFT) delta = -8;
                        if (e.key.key == SDLK_RIGHT) delta = 8;
                        if (e.key.key == SDLK_HOME) delta = -value;
                        if (e.key.key == SDLK_END) delta = 128 - value;
                        if (e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT || e.key.key == SDLK_HOME || e.key.key == SDLK_END) {
                            if (delta) adjustInt(item->command, delta, ctx);
                            continue;
                        }
                    }
                    const int before = selected;
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_LEFT) selected = (selected + count - 1) % count;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_RIGHT) selected = (selected + 1) % count;
                    const int page = std::max(1, layout.body.h / 66);
                    if (e.key.key == SDLK_PAGEUP) selected = std::max(0, selected - page);
                    if (e.key.key == SDLK_PAGEDOWN) selected = std::min(count - 1, selected + page);
                    if (e.key.key == SDLK_HOME) selected = 0;
                    if (e.key.key == SDLK_END) selected = count - 1;
                    if (buttonCount(menu) == 0) {
                        if (e.key.key == SDLK_UP) scrollTo(runtime.scrollY - 36);
                        if (e.key.key == SDLK_DOWN) scrollTo(runtime.scrollY + 36);
                        if (e.key.key == SDLK_PAGEUP) scrollTo(runtime.scrollY - layout.body.h);
                        if (e.key.key == SDLK_PAGEDOWN) scrollTo(runtime.scrollY + layout.body.h);
                        if (e.key.key == SDLK_HOME) scrollTo(0);
                        if (e.key.key == SDLK_END) scrollTo((float)layout.maxScroll);
                    } else if (selected != before || e.key.key == SDLK_HOME || e.key.key == SDLK_END ||
                               e.key.key == SDLK_UP || e.key.key == SDLK_DOWN || e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT ||
                               e.key.key == SDLK_PAGEUP || e.key.key == SDLK_PAGEDOWN) {
                        runtime.scrollY = SlimMenuLayout::reveal(layout, buttonItemIndex(menu, selected), runtime.scrollY);
                    }
                    if (!e.key.repeat && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) activate = selected;
                }
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && runtime.waitingBind.empty()) {
                    const auto button = e.gbutton.button;
                    if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
                        if (!menuNetworkBusy(runtime)) return SlimMenuExit::Back;
                        continue;
                    }
                    const bool left = button == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
                    const bool right = button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
                    MenuItem* item = buttonAt(menu, selected);
                    if ((left || right) && item && audioSliderValue(*item, ctx)) {
                        adjustInt(item->command, right ? 8 : -8, ctx);
                    } else {
                        if (button == SDL_GAMEPAD_BUTTON_DPAD_UP || left) selected = (selected + count - 1) % count;
                        if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN || right) selected = (selected + 1) % count;
                        runtime.scrollY = SlimMenuLayout::reveal(layout, buttonItemIndex(menu, selected), runtime.scrollY);
                    }
                    if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) activate = selected;
                }
                if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                    scrollTo(runtime.scrollY - e.wheel.y * direction * 48.0f / layout.scale);
                }
                float x = 0.0f, y = 0.0f;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID &&
                    pointer(e.button.x, e.button.y, false, x, y)) {
                    if (overExit(x, y)) {
                        if (!menuNetworkBusy(runtime)) return SlimMenuExit::Back;
                        continue;
                    }
                    auto track = menuScrollTrack(layout);
                    track.x -= 4; track.w += 8;
                    if (layout.maxScroll && SlimMenuLayout::contains(track, x, y)) {
                        const auto thumb = menuScrollThumb(layout, runtime.scrollY);
                        thumbGrab = SlimMenuLayout::contains(thumb, x, y) ? y - thumb.y : thumb.h / 2.0f;
                        draggingScrollbar = true;
                        dragScrollbar(y);
                    } else {
                        activate = hitButton(x, y);
                        if (MenuItem* item = buttonAt(menu, activate); item && audioSliderValue(*item, ctx)) {
                            selected = draggingSlider = activate;
                            sliderUsesTouch = false;
                            dragVolume(x);
                            activate = -1;
                        }
                    }
                }
                if (e.type == SDL_EVENT_MOUSE_MOTION && draggingScrollbar && pointer(e.motion.x, e.motion.y, false, x, y)) dragScrollbar(y);
                if (e.type == SDL_EVENT_MOUSE_MOTION && draggingSlider >= 0 && !sliderUsesTouch &&
                    pointer(e.motion.x, e.motion.y, false, x, y)) dragVolume(x);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                    draggingScrollbar = false;
                    if (!sliderUsesTouch) draggingSlider = -1;
                }
                if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    draggingScrollbar = false;
                    touchActive = false;
                    draggingSlider = -1;
                    exitTouchActive = false;
                }
                if (e.type == SDL_EVENT_FINGER_DOWN && !touchActive && !exitTouchActive &&
                    pointer(e.tfinger.x, e.tfinger.y, true, x, y) && overExit(x, y)) {
                    exitTouchActive = !menuNetworkBusy(runtime);
                    exitFinger = e.tfinger.fingerID;
                    continue;
                }
                if (exitTouchActive && (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) &&
                    e.tfinger.fingerID == exitFinger) {
                    exitTouchActive = false;
                    if (e.type == SDL_EVENT_FINGER_UP && pointer(e.tfinger.x, e.tfinger.y, true, x, y) &&
                        overExit(x, y) && !menuNetworkBusy(runtime)) return SlimMenuExit::Back;
                    continue;
                }
                if (e.type == SDL_EVENT_FINGER_DOWN && !touchActive && pointer(e.tfinger.x, e.tfinger.y, true, x, y) &&
                    SlimMenuLayout::contains(layout.body, x, y)) {
                    touchActive = true;
                    touchDragged = false;
                    trackedFinger = e.tfinger.fingerID;
                    touchStartX = x; touchStartY = y; touchStartScroll = runtime.scrollY;
                    const int hit = hitButton(x, y);
                    if (MenuItem* item = buttonAt(menu, hit); item && audioSliderValue(*item, ctx)) {
                        selected = draggingSlider = hit;
                        sliderUsesTouch = true;
                        dragVolume(x);
                    }
                }
                if (touchActive && e.type == SDL_EVENT_FINGER_MOTION && e.tfinger.fingerID == trackedFinger &&
                    pointer(e.tfinger.x, e.tfinger.y, true, x, y)) {
                    if (std::abs(y - touchStartY) > 8.0f / layout.scale || std::abs(x - touchStartX) > 8.0f / layout.scale) touchDragged = true;
                    if (draggingSlider >= 0 && sliderUsesTouch) dragVolume(x);
                    else if (touchDragged) scrollTo(touchStartScroll + touchStartY - y);
                }
                if (touchActive && (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) && e.tfinger.fingerID == trackedFinger) {
                    if (draggingSlider >= 0 && sliderUsesTouch) {
                        if (e.type == SDL_EVENT_FINGER_UP && pointer(e.tfinger.x, e.tfinger.y, true, x, y)) dragVolume(x);
                        draggingSlider = -1;
                    } else if (e.type == SDL_EVENT_FINGER_UP && !touchDragged && pointer(e.tfinger.x, e.tfinger.y, true, x, y)) activate = hitButton(x, y);
                    touchActive = false;
                }
                if (activate >= 0) {
                    selected = activate;
                    if (activate == exitIndex) {
                        if (!menuNetworkBusy(runtime)) return SlimMenuExit::Back;
                        continue;
                    }
                    if (MenuItem* item = buttonAt(menu, selected)) {
                        const std::string before = currentMenu;
                        const SlimMenuExit exit = execute(*item, runtime, ctx, currentMenu);
                        if (exit == SlimMenuExit::StartGame && ctx.session) {
                            ctx.session->menu = currentMenu;
                            ctx.session->vars = runtime.vars;
                            ctx.session->localLevels = runtime.localLevels;
                            ctx.session->onlineLevels = runtime.onlineLevels;
                            ctx.session->localLevelsLoaded = runtime.localLevelsLoaded;
                            ctx.session->onlineLevelsLoaded = runtime.onlineLevelsLoaded;
                            ctx.session->onlineLevelsKey = runtime.onlineLevelsKey;
                            ctx.session->onlinePageCount = runtime.onlinePageCount;
                            ctx.session->onlineTotal = runtime.onlineTotal;
                            ctx.session->selected = selected;
                            ctx.session->scrollY = runtime.scrollY;
                        }
                        if (exit == SlimMenuExit::Back || exit == SlimMenuExit::StartGame || exit == SlimMenuExit::Quit) return exit;
                        if (runtime.vars.erase("__reload_menu") > 0 || currentMenu != before) {
                            goto next_menu;
                        }
                    }
                }
            }
            presentMenuFrame(menu, runtime, ctx, selected, &layout);
            SDL_Delay(16);
        }
next_menu:
        continue;
    }
    return SlimMenuExit::Quit;
}
