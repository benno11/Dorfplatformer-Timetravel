#include "SandboxPointers.h"
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
namespace SandboxPointers {
namespace { const DFSandboxAPI* bound=nullptr; }
void bind(const DFSandboxAPI* value) {
    if(!value||value->version!=DF_SANDBOX_API_VERSION||value->struct_size<sizeof(DFSandboxAPI)||bound) {
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(),0xC0000409UL);
#endif
        std::abort();
    }
    bound=value;
}
const DFSandboxAPI& api() {
#if !defined(_WIN32) || !defined(PLATFORMER_GAME_LIBRARY)
    if(!bound)bound=runtimeAPI();
#endif
    if(!bound)std::abort();
    return *bound;
}
}
