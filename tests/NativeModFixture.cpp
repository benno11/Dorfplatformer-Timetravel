#include "../sdk/include/df_mod.h"
#include <filesystem>
#include <fstream>
#include <limits>
static const DFHostAPI* host;
static std::string config;
static void DF_MOD_CALL levelStart(void*, const DFLevelState*) {
    DFPlayerState p{}; p.struct_size = sizeof(p);
    if (host->get_player(host->context, &p)) { p.x += 10; host->set_player(host->context, &p); }
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
DF_MOD_ENTRY int32_t DF_MOD_CALL df_mod_load(const DFModContext* context, DFModCallbacks* callbacks) {
    host = context->host; config = context->config_directory;
    DFPlayerState p{}; p.struct_size = sizeof(p);
    if (host->get_player(host->context, &p)) return 0; // No gameplay state during load.
    if (std::filesystem::path(config).filename() == "minimal.prefix") {
        callbacks->struct_size = offsetof(DFModCallbacks, on_level_start);
        return 1;
    }
    std::ofstream(std::filesystem::path(config).parent_path() / "load-order", std::ios::app) << std::filesystem::path(config).filename().string() << "\n";
    callbacks->struct_size = sizeof(*callbacks);
    callbacks->on_level_start = levelStart; callbacks->on_update = update; callbacks->on_unload = unload;
    return 1;
}
