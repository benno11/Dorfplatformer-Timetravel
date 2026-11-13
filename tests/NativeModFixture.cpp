#include "../sdk/include/df_mod.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <cstring>
static const DFHostAPI* host;
static std::string config;
static bool extended;
static void DF_MOD_CALL levelStart(void*, const DFLevelState*) {
    DFPlayerState p{}; p.struct_size = sizeof(p);
    if (host->get_player(host->context, &p)) { p.x += 10; host->set_player(host->context, &p); }
    if (extended) {
        DFRunState run{sizeof(DFRunState), 0, 0, 0}; int32_t tile = -1;
        if (host->get_run_state(host->context, &run)) {
            run.coins += 3; run.lives += 1; run.score += 50; host->set_run_state(host->context, &run);
            run.coins = -1; host->set_run_state(host->context, &run); // Invalid state must be rejected.
        }
        if (host->get_tile(host->context, 1, 1, &tile) && tile == 2) host->set_tile(host->context, 1, 1, 7);
        host->set_tile(host->context, -1, 1, 7);
        host->set_tile(host->context, 1, 1, 65536);
    }
}
static void DF_MOD_CALL update(void*, double dt) {
    DFPlayerState p{}; p.struct_size = sizeof(p);
    if (!host->get_player(host->context, &p)) return;
    const float x = p.x;
    p.x = std::numeric_limits<float>::quiet_NaN();
    host->set_player(host->context, &p); // Must be rejected atomically.
    host->get_player(host->context, &p);
    if (p.x != x) return;
    p.velocity_x += (float)dt;
    if (p.on_ground) { p.velocity_y = -600; p.on_ground = 0; }
    host->set_player(host->context, &p);
}
static void DF_MOD_CALL unload(void*) {
    std::ofstream(config + "/unloaded") << "done";
    std::ofstream(std::filesystem::path(config).parent_path() / "unload-order", std::ios::app) << std::filesystem::path(config).filename().string() << "\n";
}
static void DF_MOD_CALL menuLoad(void*, const DFMenuAPI* menu) {
    DFRunState run{sizeof(DFRunState), 0, 0, 0};
    if (host->get_run_state(host->context, &run)) return; // No stale gameplay state in menus.
    menu->set_title(menu->context, "Modified by mod");
    DFMenuItem row{}; row.struct_size = sizeof(row);
    if (menu->get(menu->context, 0, &row)) {
        std::strcpy(row.label, "Edited row"); menu->set(menu->context, 0, &row);
    }
    row = {}; row.struct_size = sizeof(row); row.type = DF_MENU_BUTTON;
    std::strcpy(row.label, "Injected action"); std::strcpy(row.command, "mod_action"); std::strcpy(row.argument, "press");
    menu->insert(menu->context, menu->count(menu->context), &row);
    menu->remove(menu->context, 99999); // Out-of-bounds edits must be rejected.
}
static void DF_MOD_CALL menuAction(void*, const char* menu, const char* action) {
    std::ofstream(config + "/action") << menu << "|" << action;
}
DF_MOD_ENTRY int32_t DF_MOD_CALL df_mod_load(const DFModContext* context, DFModCallbacks* callbacks) {
    host = context->host; config = context->config_directory;
    extended = std::filesystem::path(config).filename() == "enhanced";
    DFPlayerState p{}; p.struct_size = sizeof(p);
    if (host->get_player(host->context, &p)) return 0; // No gameplay state during load.
    if (std::filesystem::path(config).filename() == "minimal.prefix") {
        callbacks->struct_size = offsetof(DFModCallbacks, on_level_start);
        return 1;
    }
    std::ofstream(std::filesystem::path(config).parent_path() / "load-order", std::ios::app) << std::filesystem::path(config).filename().string() << "\n";
    callbacks->struct_size = sizeof(*callbacks);
    callbacks->on_level_start = levelStart; callbacks->on_update = update; callbacks->on_unload = unload;
    if (extended) { callbacks->on_menu_load = menuLoad; callbacks->on_menu_action = menuAction; }
    return 1;
}
