#include "SlimMenuSystem.h"

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
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "AssetPath.h"
#include "GameSupport.h"
#include "LevelSelect.h"
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
constexpr const char* kSlimMenuUserAgent = "DF-New slim-menu";
constexpr Uint64 kMenuFadeMs = 120;

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
};

struct SlimRuntime {
    std::unordered_map<std::string, std::string> vars;
    std::string waitingBind;
};

struct ParsedLine {
    int lineNo = 0;
    std::string text;
    std::vector<std::string> parts;
};

struct SlimLevelEntry {
    std::string label;
    std::string path;
    int difficulty = 0;
    int downloads = 0;
    int likes = 0;
    int dislikes = 0;
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
    if (var == "username" && ctx.levelServerAccountUsername) return *ctx.levelServerAccountUsername;
    if (var == "api" && ctx.levelServerUrl) return *ctx.levelServerUrl;
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
    text = replaceAll(text, "${username}", contextVarValue("username", runtime, ctx));
    text = replaceAll(text, "${api}", contextVarValue("api", runtime, ctx));
    text = replaceAll(text, "${account_status}", contextVarValue("account_status", runtime, ctx));
    text = replaceAll(text, "${account_manager_url}", contextVarValue("account_manager_url", runtime, ctx));
    text = replaceAll(text, "${menu_music_enabled}", contextVarValue("menu_music_enabled", runtime, ctx));
    text = replaceAll(text, "${mute_all_audio}", contextVarValue("mute_all_audio", runtime, ctx));
    text = replaceAll(text, "${level_select_enabled}", contextVarValue("level_select_enabled", runtime, ctx));
    text = replaceAll(text, "${fullscreen}", contextVarValue("fullscreen", runtime, ctx));
    text = replaceAll(text, "${vsync_enabled}", contextVarValue("vsync_enabled", runtime, ctx));
    text = replaceAll(text, "${camera_clamp_x}", contextVarValue("camera_clamp_x", runtime, ctx));
    text = replaceAll(text, "${native_text_resolution_enabled}", contextVarValue("native_text_resolution_enabled", runtime, ctx));
    text = replaceAll(text, "${show_fps_counter}", contextVarValue("show_fps_counter", runtime, ctx));
    text = replaceAll(text, "${show_detailed_debugger}", contextVarValue("show_detailed_debugger", runtime, ctx));
    text = replaceAll(text, "${show_hitboxes}", contextVarValue("show_hitboxes", runtime, ctx));
    text = replaceAll(text, "${show_player_hitbox}", contextVarValue("show_player_hitbox", runtime, ctx));
    text = replaceAll(text, "${show_debug_view}", contextVarValue("show_debug_view", runtime, ctx));
    text = replaceAll(text, "${hide_unknown_object_types}", contextVarValue("hide_unknown_object_types", runtime, ctx));
    text = replaceAll(text, "${power_management_enabled}", contextVarValue("power_management_enabled", runtime, ctx));
    text = replaceAll(text, "${low_power_mode_enabled}", contextVarValue("low_power_mode_enabled", runtime, ctx));
    text = replaceAll(text, "${show_experimental_features}", contextVarValue("show_experimental_features", runtime, ctx));
    text = replaceAll(text, "${send_anonymous_metrics}", contextVarValue("send_anonymous_metrics", runtime, ctx));
    text = replaceAll(text, "${move_left}", contextVarValue("move_left", runtime, ctx));
    text = replaceAll(text, "${move_right}", contextVarValue("move_right", runtime, ctx));
    text = replaceAll(text, "${move_down}", contextVarValue("move_down", runtime, ctx));
    text = replaceAll(text, "${jump}", contextVarValue("jump", runtime, ctx));
    text = replaceAll(text, "${pause}", contextVarValue("pause", runtime, ctx));
    text = replaceAll(text, "${music_volume}", contextVarValue("music_volume", runtime, ctx));
    text = replaceAll(text, "${sfx_volume}", contextVarValue("sfx_volume", runtime, ctx));
    text = replaceAll(text, "${ui_scale_percent}", contextVarValue("ui_scale_percent", runtime, ctx));
    text = replaceAll(text, "${ui_edge_padding}", contextVarValue("ui_edge_padding", runtime, ctx));
    text = replaceAll(text, "${active_save_slot}", contextVarValue("active_save_slot", runtime, ctx));
    text = replaceAll(text, "${save_slot_1_status}", contextVarValue("save_slot_1_status", runtime, ctx));
    text = replaceAll(text, "${save_slot_2_status}", contextVarValue("save_slot_2_status", runtime, ctx));
    text = replaceAll(text, "${save_slot_3_status}", contextVarValue("save_slot_3_status", runtime, ctx));
    text = replaceAll(text, "${save_slot_1_exists}", contextVarValue("save_slot_1_exists", runtime, ctx));
    text = replaceAll(text, "${save_slot_2_exists}", contextVarValue("save_slot_2_exists", runtime, ctx));
    text = replaceAll(text, "${save_slot_3_exists}", contextVarValue("save_slot_3_exists", runtime, ctx));
    text = replaceAll(text, "${about_version}", contextVarValue("about_version", runtime, ctx));
    text = replaceAll(text, "${about_version_id}", contextVarValue("about_version_id", runtime, ctx));
    text = replaceAll(text, "${about_build_uuid}", contextVarValue("about_build_uuid", runtime, ctx));
    text = replaceAll(text, "${about_build_time}", contextVarValue("about_build_time", runtime, ctx));
    text = replaceAll(text, "${about_build_timezone}", contextVarValue("about_build_timezone", runtime, ctx));
    text = replaceAll(text, "${about_sdl_version}", contextVarValue("about_sdl_version", runtime, ctx));
    text = replaceAll(text, "${about_sdl_revision}", contextVarValue("about_sdl_revision", runtime, ctx));
    text = replaceAll(text, "${about_platform}", contextVarValue("about_platform", runtime, ctx));
    text = replaceAll(text, "${about_renderer}", contextVarValue("about_renderer", runtime, ctx));
    text = replaceAll(text, "${about_window_size}", contextVarValue("about_window_size", runtime, ctx));
    text = replaceAll(text, "${about_base_size}", contextVarValue("about_base_size", runtime, ctx));
    text = replaceAll(text, "${about_ui_scale}", contextVarValue("about_ui_scale", runtime, ctx));
    text = replaceAll(text, "${about_ui_edge_padding}", contextVarValue("about_ui_edge_padding", runtime, ctx));
    text = replaceAll(text, "${about_video_driver}", contextVarValue("about_video_driver", runtime, ctx));
    text = replaceAll(text, "${about_audio_driver}", contextVarValue("about_audio_driver", runtime, ctx));
    text = replaceAll(text, "${about_cpu_cores}", contextVarValue("about_cpu_cores", runtime, ctx));
    text = replaceAll(text, "${about_system_ram}", contextVarValue("about_system_ram", runtime, ctx));
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

SlimMenu loadMenu(const std::string& name, SlimRuntime& runtime, const SlimMenuContext& ctx) {
    if (name == "locallevels" || name == "onlinelevels" || name == "leveldetail") {
        return buildGeneratedLevelMenu(name, runtime, ctx);
    }

    SlimMenu menu;
    menu.name = name;
    const std::string text = ReadTextFile(menuPath(name));
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
            menu.items.push_back(MenuItem{MenuItem::Type::Text, parts[1], {}, {}});
            ++i;
        } else if (op == "button" && parts.size() >= 3) {
            const std::string arg = parts.size() >= 4 ? parts[3] : std::string();
            menu.items.push_back(MenuItem{MenuItem::Type::Button, parts[1], parts[2], arg});
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
            jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(
                    cls, "gameServerLookupAccount",
                    "(Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
                if (mid) {
                    jstring jBase = env->NewStringUTF(base.c_str());
                    jstring jToken = env->NewStringUTF(token.c_str());
                    jobject jRespObj = env->CallStaticObjectMethod(cls, mid, jBase, jToken, (jint)10000);
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
    const CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc == CURLE_OK && code >= 200 && code < 300) return respBody;
#endif

    return {};
}

bool currentAccountIsMod(SlimRuntime& runtime, const SlimMenuContext& ctx) {
    auto cached = runtime.vars.find("__account_is_mod");
    if (cached != runtime.vars.end()) return cached->second == "true";
    const bool isMod = accountLookupResponseIsMod(accountLookupBody(ctx));
    runtime.vars["__account_is_mod"] = isMod ? "true" : "false";
    return isMod;
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

std::vector<SlimLevelEntry> loadOnlineSlimLevels(const SlimMenuContext& ctx) {
    std::vector<SlimLevelEntry> out;
    const std::string rootText = ReadTextFile(makeApiUrl(ctx, "/levels.json?metadata=true"));
    if (rootText.empty()) return out;
    nlohmann::json j;
    try { j = nlohmann::json::parse(rootText); } catch (...) { return out; }
    if (!j.is_object()) return out;
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
    }
    std::sort(out.begin(), out.end(), [](const SlimLevelEntry& a, const SlimLevelEntry& b) { return a.label < b.label; });
    return out;
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
    std::vector<SlimLevelEntry> levels = prefix == "online" ? loadOnlineSlimLevels(ctx) : loadLocalSlimLevels();
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
                    jint code = env->CallStaticIntMethod(cls, mid, jUrl, jBody, jToken, (jint)15000);
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
    const CURLcode rc = curl_easy_perform(curl);
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
                    jint code = env->CallStaticIntMethod(cls, mid, jUrl, jBody, jToken, (jint)15000);
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
    const CURLcode rc = curl_easy_perform(curl);
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
                    jint code = env->CallStaticIntMethod(cls, mid, jUrl, jBody, jToken, (jint)15000);
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
    const CURLcode rc = curl_easy_perform(curl);
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
        runtime.vars["detail_label"] = level.label;
        runtime.vars["detail_path"] = level.path;
        runtime.vars["detail_difficulty"] = level.difficulty > 0 ? std::to_string(level.difficulty) : "unrated";
        runtime.vars["detail_downloads"] = std::to_string(level.downloads);
        runtime.vars["detail_likes"] = std::to_string(level.likes);
        runtime.vars["detail_dislikes"] = std::to_string(level.dislikes);
        if (source != "online") {
            runtime.vars["detail_verified"] = IsLocalLevelVerified(level.path) ? "true" : "false";
        }
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "Name: " + level.label, {}, {}});
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "Difficulty: " + runtime.vars["detail_difficulty"], {}, {}});
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
                const int nextDifficulty = level.difficulty >= 9 ? 0 : level.difficulty + 1;
                const std::string label = nextDifficulty > 0
                    ? ("Rate D" + std::to_string(nextDifficulty))
                    : "Clear Rating";
                menu.items.push_back(MenuItem{MenuItem::Type::Button, label, "rate_online_level", std::to_string(index)});
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

    std::vector<SlimLevelEntry> levels = online ? loadOnlineSlimLevels(ctx) : loadLocalSlimLevels();
    const std::string filter = runtime.vars[filterVar];
    std::vector<SlimLevelEntry> filtered;
    for (const auto& level : levels) {
        if (levelMatchesFilter(level, filter)) filtered.push_back(level);
    }

    constexpr int kPageSize = 10;
    const int pageCount = std::max(1, (int)((filtered.size() + kPageSize - 1) / kPageSize));
    int page = std::clamp(std::atoi(runtime.vars[pageVar].c_str()), 0, pageCount - 1);
    runtime.vars[pageVar] = std::to_string(page);

    SlimMenu menu;
    menu.name = name;
    menu.title = online ? "Online Levels" : "Local Levels";
    menu.items.push_back(MenuItem{MenuItem::Type::Text,
        "Filter: " + levelFilterLabel(filter) + "  Page " + std::to_string(page + 1) + "/" + std::to_string(pageCount), {}, {}});
    if (!online) {
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "${level_detail_status}", {}, {}});
        menu.items.push_back(MenuItem{MenuItem::Type::Button, "Create", "create_level", {}});
    }
    if (filtered.empty()) {
        menu.items.push_back(MenuItem{MenuItem::Type::Text, "No levels match this filter.", {}, {}});
    }

    const int start = page * kPageSize;
    const int end = std::min((int)filtered.size(), start + kPageSize);
    for (int i = start; i < end; ++i) {
        const auto& level = filtered[i];
        menu.items.push_back(MenuItem{MenuItem::Type::Button,
            level.label + difficultySuffix(level.difficulty) + (online ? levelStatsSuffix(level) : std::string()),
            "level_detail", prefix + ":" + std::to_string(i)});
    }
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

int textRowCountFor(const SlimMenu& menu, const SlimRuntime& runtime) {
    int rows = runtime.waitingBind.empty() ? 0 : 1;
    for (const auto& item : menu.items) {
        if (item.type == MenuItem::Type::Text) ++rows;
    }
    return rows;
}

SDL_Rect buttonRectFor(const SlimMenuContext& ctx, int buttonIndex, int buttonCount, int textRows = 0) {
    const int w = std::clamp(ctx.baseScreenW * 2 / 5, 220, 520);
    const int h = buttonCount > 14 ? 22 : (buttonCount > 10 ? 30 : 54);
    const int gap = buttonCount > 14 ? 3 : (buttonCount > 10 ? 6 : 14);
    const int totalH = buttonCount * h + std::max(0, buttonCount - 1) * gap;
    const int defaultStartY = buttonCount > 10 ? std::max(86, ctx.baseScreenH / 2 - totalH / 2) : std::max(132, ctx.baseScreenH / 2 - totalH / 2);
    const int textDrivenStartY = textRows > 0 ? 106 + textRows * 28 + 10 : 0;
    int startY = defaultStartY;
    if (textDrivenStartY > defaultStartY && textDrivenStartY + totalH <= ctx.baseScreenH - 16) {
        startY = textDrivenStartY;
    }
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
    const int textRows = textRowCountFor(menu, runtime);
    int buttonIndex = 0;
    int textY = 106;
    if (!runtime.waitingBind.empty()) {
        const std::string bindLabel = "Press a key for " + runtime.waitingBind;
        DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, bindLabel) / 2, textY, 2, bindLabel);
        textY += 28;
    }
    for (const auto& item : menu.items) {
        const std::string label = expandVars(item.label, runtime, ctx);
        if (item.type == MenuItem::Type::Text) {
            DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(2, label) / 2, textY, 2, label);
            textY += 28;
            continue;
        }
        SDL_Rect r = buttonRectFor(ctx, buttonIndex, buttonCount, textRows);
        const bool isSelected = buttonIndex == selected;
        SDL_SetRenderDrawColor(ctx.ren, isSelected ? 96 : 54, isSelected ? 116 : 74, isSelected ? 134 : 92, 255);
        SDL_RenderFillRect(ctx.ren, &r);
        SDL_SetRenderDrawColor(ctx.ren, isSelected ? 220 : 160, isSelected ? 232 : 184, isSelected ? 242 : 204, 255);
        SDL_RenderRect(ctx.ren, &r);
        DrawText(ctx.ren, r.x + (r.w - MeasureTextWidth(2, label)) / 2, r.y + (r.h - 20) / 2, 2, label);
        ++buttonIndex;
    }
}

void renderMenuFadeOverlay(const SlimMenuContext& ctx, Uint8 alpha) {
    if (alpha == 0) return;
    SDL_SetRenderTarget(ctx.ren, ctx.gameTarget);
    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, alpha);
    SDL_Rect overlay{0, 0, ctx.baseScreenW, ctx.baseScreenH};
    SDL_RenderFillRect(ctx.ren, &overlay);
    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
}

void presentMenuFrame(const SlimMenu& menu, SlimRuntime& runtime, const SlimMenuContext& ctx, int selected, Uint8 fadeAlpha = 0) {
    renderMenu(menu, runtime, ctx, selected);
    renderMenuFadeOverlay(ctx, fadeAlpha);
    SDL_SetRenderTarget(ctx.ren, nullptr);
    SDL_RenderTexture(ctx.ren, ctx.gameTarget, static_cast<const SDL_Rect*>(nullptr), static_cast<const SDL_Rect*>(nullptr));
    SDL_RenderPresent(ctx.ren);
}

void playMenuFade(const SlimMenu& menu, SlimRuntime& runtime, const SlimMenuContext& ctx, int selected, bool fadeIn) {
    const Uint64 start = SDL_GetTicks();
    while (ctx.running && *ctx.running) {
        const Uint64 elapsed = SDL_GetTicks() - start;
        const float t = std::clamp((float)elapsed / (float)kMenuFadeMs, 0.0f, 1.0f);
        const float alphaT = fadeIn ? (1.0f - t) : t;
        const Uint8 alpha = (Uint8)std::clamp((int)std::lround(alphaT * 255.0f), 0, 255);
        presentMenuFrame(menu, runtime, ctx, selected, alpha);
        if (elapsed >= kMenuFadeMs) break;
        SDL_Delay(16);
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
    if (command == "level_page") {
        const std::string::size_type colon = arg.find(':');
        const std::string prefix = colon == std::string::npos ? "local" : arg.substr(0, colon);
        const std::string direction = colon == std::string::npos ? arg : arg.substr(colon + 1);
        const std::string pageVar = prefix + "_level_page";
        int page = std::max(0, std::atoi(runtime.vars[pageVar].c_str()));
        if (direction == "prev") page = std::max(0, page - 1);
        if (direction == "next") page += 1;
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
    if (command == "create_level") {
        const std::string createdPath = OpenLocalLevelEditorForMenu(ctx.win, ctx.ren, {});
        if (!createdPath.empty() && ctx.selectedLevelPath) {
            *ctx.selectedLevelPath = createdPath;
            return SlimMenuExit::StartGame;
        }
        runtime.vars["level_detail_status"] = "Create cancelled.";
        currentMenu = "locallevels";
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
        if (!editedPath.empty() && ctx.selectedLevelPath) {
            *ctx.selectedLevelPath = editedPath;
            return SlimMenuExit::StartGame;
        }
        runtime.vars["level_detail_status"] = "Edit cancelled.";
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
        if (ok) runtime.vars["detail_cached_path"] = cachedPath;
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
        const int nextDifficulty = levels[index].difficulty >= 9 ? 0 : levels[index].difficulty + 1;
        std::string status;
        const bool ok = putOnlineLevelDifficulty(levels[index], nextDifficulty, ctx, status);
        runtime.vars["level_detail_status"] = status.empty() ? (ok ? "Rating updated." : "Rate failed.") : status;
        runtime.vars["__reload_menu"] = "1";
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
        SlimMenu menu = loadMenu(currentMenu, runtime, ctx);
        int selected = 0;
        playMenuFade(menu, runtime, ctx, selected, true);
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
                    if (!runtime.waitingBind.empty()) {
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
                    if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK) return SlimMenuExit::Back;
                    if (e.key.key == SDLK_UP || e.key.key == SDLK_LEFT) selected = (selected + count - 1) % count;
                    if (e.key.key == SDLK_DOWN || e.key.key == SDLK_RIGHT) selected = (selected + 1) % count;
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        if (MenuItem* item = buttonAt(menu, selected)) {
                            const std::string before = currentMenu;
                            const SlimMenuExit exit = execute(*item, runtime, ctx, currentMenu);
                            if (exit == SlimMenuExit::Back) return exit;
                            if (exit == SlimMenuExit::StartGame || exit == SlimMenuExit::Quit) return exit;
                            if (runtime.vars.erase("__reload_menu") > 0) {
                                playMenuFade(menu, runtime, ctx, selected, false);
                                goto next_menu;
                            }
                            if (currentMenu != before) {
                                playMenuFade(menu, runtime, ctx, selected, false);
                                goto next_menu;
                            }
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
                    const int textRows = textRowCountFor(menu, runtime);
                    for (int i = 0; i < count; ++i) {
                        SDL_Rect r = buttonRectFor(ctx, i, count, textRows);
                        SDL_Point pt{gx, gy};
                        if (!SDL_PointInRect(&pt, &r)) continue;
                        selected = i;
                        if (MenuItem* item = buttonAt(menu, selected)) {
                            const std::string before = currentMenu;
                            const SlimMenuExit exit = execute(*item, runtime, ctx, currentMenu);
                            if (exit == SlimMenuExit::Back) return exit;
                            if (exit == SlimMenuExit::StartGame || exit == SlimMenuExit::Quit) return exit;
                            if (runtime.vars.erase("__reload_menu") > 0) {
                                playMenuFade(menu, runtime, ctx, selected, false);
                                goto next_menu;
                            }
                            if (currentMenu != before) {
                                playMenuFade(menu, runtime, ctx, selected, false);
                                goto next_menu;
                            }
                        }
                    }
                }
            }
            presentMenuFrame(menu, runtime, ctx, selected);
            SDL_Delay(16);
        }
next_menu:
        continue;
    }
    return SlimMenuExit::Quit;
}
