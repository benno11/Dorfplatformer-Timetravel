#include "df_mod.h"
static const DFHostAPI* host;
static void DF_MOD_CALL update(void* userdata, double seconds) {
    (void)userdata; (void)seconds;
    DFPlayerState player = {0}; player.struct_size = sizeof(player);
    if (host->get_player(host->context, &player) && player.on_ground) {
        player.velocity_y = -600;
        player.on_ground = 0;
        host->set_player(host->context, &player);
    }
}
DF_MOD_ENTRY int32_t DF_MOD_CALL df_mod_load(const DFModContext* context, DFModCallbacks* callbacks) {
    if (!context || !context->host || context->host->api_major != DF_MOD_API_MAJOR ||
        context->host->struct_size < sizeof(DFHostAPI)) return 0;
    host = context->host;
    callbacks->struct_size = sizeof(*callbacks);
    callbacks->on_update = update;
    host->log(host->context, "Auto Hop loaded using stable mod API 1.0");
    return 1;
}
