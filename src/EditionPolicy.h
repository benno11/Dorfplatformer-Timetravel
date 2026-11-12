#pragma once
#include <string>

#ifndef PLATFORMER_LITE_EDITION
#define PLATFORMER_LITE_EDITION 0
#endif

namespace Edition {
inline constexpr bool lite = PLATFORMER_LITE_EDITION != 0;
inline constexpr bool scoresEnabled = !lite;
inline constexpr const char* name = lite ? "Dorfplatformer Timetravel Lite" : "Dorfplatformer Timetravel";
inline bool worldSelectable(int world) {
    return world >= 1 && world <= 6 && (!lite || (world != 2 && world != 4 && world != 6));
}
inline constexpr const char* storageName = lite ? "DorfplatformerTimetravelLite" : "DorfplatformerTimetravel";
// Lite is an allowlist: future menus and commands require an explicit opt-in.
inline bool menuAllowed(const std::string& menu) {
    if (!lite) return true;
    for (const auto* allowed : {"mainsetting", "mainplay", "saveplay", "worldselect", "savesettings", "savedelete",
        "settingsgeneral", "settingsdisplay", "settingsaudio", "settingscontrols", "settingsdebug",
        "settingsabout", "settingsaboutrevision", "settingsupdater", "achievements"})
        if (menu == allowed) return true;
    return false;
}
inline bool commandAllowed(const std::string& command, const std::string& arg) {
    if (!lite || command.empty()) return true;
    if (command == "menu") return menuAllowed(arg);
    if (command == "toggle") {
        for (const auto* setting : {"fullscreen", "vsync_enabled", "camera_clamp_x", "power_management_enabled",
            "low_power_mode_enabled", "native_text_resolution_enabled", "water_effects_enabled", "menu_music_enabled",
            "mute_all_audio", "show_fps_counter", "show_detailed_debugger", "show_hitboxes", "show_player_hitbox",
            "show_debug_view", "hide_unknown_object_types"})
            if (arg == setting) return true;
        return false;
    }
    if (command == "inc" || command == "dec")
        return arg == "music_volume" || arg == "sfx_volume" || arg == "ui_scale_percent" || arg == "ui_edge_padding";
    for (const auto* allowed : {"back", "quit", "world", "campaign", "saved", "save_slot", "select_save",
        "delete_save", "start", "toggle", "bind", "inc", "dec", "updater", "copy"})
        if (command == allowed) return true;
    return false;
}
}
