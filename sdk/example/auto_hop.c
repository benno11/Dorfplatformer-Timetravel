#include "df_mod.h"
#include <string.h>
static const DFHostAPI* host;
static int enabled = 1;
static void DF_MOD_CALL update(void* userdata, double seconds) {
    (void)userdata; (void)seconds;
    DFPlayerState player = {0}; player.struct_size = sizeof(player);
    if (enabled && host->get_player(host->context, &player) && player.on_ground) {
        player.velocity_y = -600; player.on_ground = 0;
        host->set_player(host->context, &player);
    }
}
static void DF_MOD_CALL menuLoad(void* userdata, const DFMenuAPI* menu) {
    (void)userdata;
    if (strcmp(menu->menu_id, "settingsmods") != 0) return;
    DFMenuItem row = {0}; row.struct_size = sizeof(row); row.type = DF_MENU_BUTTON;
    strcpy(row.label, enabled ? "Example: Auto Hop ON" : "Example: Auto Hop OFF");
    strcpy(row.command, "mod_action"); strcpy(row.argument, "toggle_auto_hop");
    const uint32_t count = menu->count(menu->context);
    menu->insert(menu->context, count ? count - 1 : 0, &row);
}
static void DF_MOD_CALL menuAction(void* userdata, const char* menu, const char* action) {
    (void)userdata; (void)menu;
    if (strcmp(action, "toggle_auto_hop") == 0) enabled = !enabled;
}
DF_MOD_ENTRY int32_t DF_MOD_CALL df_mod_load(const DFModContext* context, DFModCallbacks* callbacks) {
    const size_t oldHostSize = offsetof(DFHostAPI, get_run_state);
    const size_t oldCallbacksSize = offsetof(DFModCallbacks, on_menu_load);
    if (!context || !context->host || context->host->api_major != DF_MOD_API_MAJOR ||
        context->host->struct_size < oldHostSize || !callbacks || callbacks->struct_size < oldCallbacksSize) return 0;
    host = context->host;
    const uint32_t capacity = callbacks->struct_size;
    callbacks->struct_size = capacity < sizeof(*callbacks) ? capacity : sizeof(*callbacks);
    callbacks->on_update = update;
    // Optional 1.1 hooks; the same source still works on a 1.0 host.
    if (host->api_minor >= 1 && capacity >= sizeof(*callbacks)) {
        callbacks->on_menu_load = menuLoad; callbacks->on_menu_action = menuAction;
    }
    host->log(host->context, "Auto Hop loaded using stable mod API 1.x");
    return 1;
}
