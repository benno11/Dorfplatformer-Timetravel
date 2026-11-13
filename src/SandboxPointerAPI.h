#ifndef DF_SANDBOX_POINTER_API_H
#define DF_SANDBOX_POINTER_API_H
#include <stdint.h>
#include <stddef.h>
#include "SandboxUpdateAPI.h"
#include "../sdk/include/df_mod.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DF_SANDBOX_API_VERSION 1u
/* Opaque, process-local handle. Copy as a whole; never interpret its fields. */
typedef struct DFSandboxHandle { uint64_t id; uint8_t authentication[32]; } DFSandboxHandle;
struct DFSandboxAPI;
typedef int (*DFSandboxGameEntry)(int, char**, const struct DFSandboxAPI*);
typedef struct DFSandboxAPI {
    uint32_t struct_size, version;
    DFSandboxHandle (*allocate)(size_t bytes);
    DFSandboxHandle (*store_immutable)(const void* data, size_t bytes);
    void (*read)(DFSandboxHandle, size_t offset, void* destination, size_t bytes);
    void (*write)(DFSandboxHandle, size_t offset, const void* source, size_t bytes);
    void (*release)(DFSandboxHandle);
    DFSandboxHandle (*register_game)(DFSandboxGameEntry);
    int (*call_game)(DFSandboxHandle, int argc, char** argv);
    DFSandboxHandle (*register_mod_load)(DFModLoad);
    int32_t (*call_mod_load)(DFSandboxHandle, const DFModContext*, DFModCallbacks*);
    DFSandboxHandle (*register_mod_callbacks)(const DFModCallbacks*);
    void (*mod_level_start)(DFSandboxHandle, const DFLevelState*);
    void (*mod_update)(DFSandboxHandle, double seconds);
    void (*mod_unload)(DFSandboxHandle);
    void (*mod_menu_load)(DFSandboxHandle, const DFMenuAPI*);
    void (*mod_menu_action)(DFSandboxHandle, const char* menu, const char* action);
    /* Append-only sandbox service API; no caller-supplied URLs or programs. */
    int32_t (*update_action)(uint32_t action);
    int32_t (*update_status)(DFSandboxUpdateStatus* status);
} DFSandboxAPI;
#ifdef __cplusplus
}
#endif
#endif
