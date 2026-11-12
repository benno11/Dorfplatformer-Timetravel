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
#define DF_MOD_API_MINOR 0u
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
typedef struct DFHostAPI {
    uint32_t struct_size, api_major, api_minor;
    void* context;
    void (DF_MOD_CALL *log)(void*, const char*);
    int32_t (DF_MOD_CALL *get_player)(void*, DFPlayerState*);
    int32_t (DF_MOD_CALL *set_player)(void*, const DFPlayerState*);
    int32_t (DF_MOD_CALL *get_level)(void*, DFLevelState*);
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
} DFModCallbacks;
typedef int32_t (DF_MOD_CALL *DFModLoad)(const DFModContext*, DFModCallbacks*);
/* Export: int32_t DF_MOD_CALL df_mod_load(const DFModContext*, DFModCallbacks*);
   Return 1 on success, 0 on failure. All callbacks run on the game thread.
   Player/level access is available only inside gameplay callbacks. */
#ifdef __cplusplus
}
#endif
#endif
