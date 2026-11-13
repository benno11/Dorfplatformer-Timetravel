#ifndef DF_MOD_H
#define DF_MOD_H
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#define DF_MOD_EXPORT __declspec(dllexport)
#define DF_MOD_CALL __cdecl
#else
#define DF_MOD_EXPORT __attribute__((visibility("default")))
#define DF_MOD_CALL
#endif
#ifdef __cplusplus
#define DF_MOD_ENTRY extern "C" DF_MOD_EXPORT
extern "C" {
#else
#define DF_MOD_ENTRY DF_MOD_EXPORT
#endif
#define DF_MOD_API_MAJOR 1u
#define DF_MOD_API_MINOR 1u
/* Append fields only within an API major. Check struct_size before newer fields.
   No STL, SDL, or game-internal classes cross this ABI. */
typedef struct DFPlayerState {
    uint32_t struct_size;
    float x, y, velocity_x, velocity_y;
    int32_t on_ground, facing, has_shield;
} DFPlayerState;
typedef struct DFLevelState {
    uint32_t struct_size;
    int32_t level_id, world, area, time_period;
    int32_t width_tiles, height_tiles, tile_size;
} DFLevelState;
typedef struct DFRunState {
    uint32_t struct_size;
    int32_t coins, lives, score;
} DFRunState;
#define DF_MENU_TEXT 0
#define DF_MENU_BUTTON 1
#define DF_MENU_SLIDER 2
/* Fixed UTF-8 buffers; always terminate strings. Commands use built-in game
   commands, or "mod_action" with a mod-local action in argument. */
typedef struct DFMenuItem {
    uint32_t struct_size;
    int32_t type;
    char label[256], command[64], argument[256];
} DFMenuItem;
typedef struct DFMenuAPI {
    uint32_t struct_size;
    void* context;
    const char* menu_id;
    uint32_t (DF_MOD_CALL *count)(void*);
    int32_t (DF_MOD_CALL *get)(void*, uint32_t, DFMenuItem*);
    int32_t (DF_MOD_CALL *set)(void*, uint32_t, const DFMenuItem*);
    int32_t (DF_MOD_CALL *insert)(void*, uint32_t, const DFMenuItem*);
    int32_t (DF_MOD_CALL *remove)(void*, uint32_t);
    int32_t (DF_MOD_CALL *set_title)(void*, const char*);
} DFMenuAPI;
typedef struct DFHostAPI {
    uint32_t struct_size, api_major, api_minor;
    void* context;
    void (DF_MOD_CALL *log)(void*, const char*);
    int32_t (DF_MOD_CALL *get_player)(void*, DFPlayerState*);
    int32_t (DF_MOD_CALL *set_player)(void*, const DFPlayerState*);
    int32_t (DF_MOD_CALL *get_level)(void*, DFLevelState*);
    /* API 1.1: valid only within gameplay callbacks. */
    int32_t (DF_MOD_CALL *get_run_state)(void*, DFRunState*);
    int32_t (DF_MOD_CALL *set_run_state)(void*, const DFRunState*);
    int32_t (DF_MOD_CALL *get_tile)(void*, int32_t x, int32_t y, int32_t* tile_id);
    int32_t (DF_MOD_CALL *set_tile)(void*, int32_t x, int32_t y, int32_t tile_id);
} DFHostAPI;
typedef struct DFModContext {
    uint32_t struct_size;
    const DFHostAPI* host;
    const char* mod_directory;
    const char* config_directory;
} DFModContext;
typedef struct DFModCallbacks {
    uint32_t struct_size;
    void* userdata;
    void (DF_MOD_CALL *on_level_start)(void*, const DFLevelState*);
    void (DF_MOD_CALL *on_update)(void*, double seconds);
    void (DF_MOD_CALL *on_unload)(void*);
    /* API 1.1: edit a menu once when it opens/reloads; API lifetime is this call. */
    void (DF_MOD_CALL *on_menu_load)(void*, const DFMenuAPI*);
    void (DF_MOD_CALL *on_menu_action)(void*, const char* menu_id, const char* action);
} DFModCallbacks;
typedef int32_t (DF_MOD_CALL *DFModLoad)(const DFModContext*, DFModCallbacks*);
/* Export: int32_t DF_MOD_CALL df_mod_load(const DFModContext*, DFModCallbacks*);
   Return 1 on success, 0 on failure. All callbacks run on the game thread.
   Player/level access is available only inside gameplay callbacks. */
#ifdef __cplusplus
}
#endif
#endif
