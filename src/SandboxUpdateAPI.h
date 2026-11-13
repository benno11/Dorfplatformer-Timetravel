#ifndef DF_SANDBOX_UPDATE_API_H
#define DF_SANDBOX_UPDATE_API_H
#include <stdint.h>
#define DF_UPDATE_READY 0u
#define DF_UPDATE_CHECKING 1u
#define DF_UPDATE_CURRENT 2u
#define DF_UPDATE_AVAILABLE 3u
#define DF_UPDATE_ERROR 4u
#define DF_UPDATE_NOT_CONFIGURED 5u
#define DF_UPDATE_CHECK 1u
/* No executable paths, command lines, download URLs or credentials cross this
   interface. The sandbox owns the configured endpoint and selected package. */
typedef struct DFSandboxUpdateStatus {
    uint32_t struct_size, state;
    char current_sandbox[32];
    char latest_game[128];
    char latest_sandbox[32];
    char detail[512];
    char notes[2048];
} DFSandboxUpdateStatus;
#endif
