#include "GameApp.h"
#include <cstdio>
#include <exception>

static int runMainImpl(int argc, char** argv) {
    try {
        return RunGameApp(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FATAL: uncaught std::exception: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "FATAL: uncaught non-std exception\n");
        return 1;
    }
}

int main(int argc, char** argv) {
    return runMainImpl(argc, argv);
}

#if defined(_WIN32)
#include <windows.h>
extern "C" {
extern int __argc;
extern char** __argv;
}

static void attachToParentConsoleIfAvailable() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }

    FILE* stream = nullptr;
    freopen_s(&stream, "CONIN$", "r", stdin);
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    attachToParentConsoleIfAvailable();
    return runMainImpl(__argc, __argv);
}
#endif
