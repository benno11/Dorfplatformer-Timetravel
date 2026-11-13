#include "GameApp.h"
#include "CustomBootLoader.h"
#include "Platform.h"
#include "GameSandbox.h"
#include "SandboxPointers.h"
#include <SDL3/SDL.h>
#include "AssetPath.h"
#include <filesystem>
#if PLATFORMER_APPLE_SDL_MAIN
#include <SDL3/SDL_main.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#if defined(__linux__) && !defined(__ANDROID__)
#include <limits.h>
#include <unistd.h>

static void useExecutableDirectoryAsWorkingDirectory() {
    char exePath[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) return;
    exePath[len] = '\0';

    char* lastSlash = nullptr;
    for (char* p = exePath; *p; ++p) {
        if (*p == '/') lastSlash = p;
    }
    if (!lastSlash || lastSlash == exePath) return;
    *lastSlash = '\0';
    (void)chdir(exePath);
}
#endif

static int runMainImpl(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__)
    useExecutableDirectoryAsWorkingDirectory();
#elif defined(__APPLE__) && !PLATFORMER_IOS
    if(const char* base=SDL_GetBasePath()) {
        std::error_code error;std::filesystem::current_path(std::filesystem::u8path(base),error);
    }
#endif
    try {
#if !defined(__ANDROID__)
        const CustomBoot::Result boot = CustomBoot::Run(argc, argv);
        if (!boot.ok) {
            return boot.exitCode;
        }
#endif
        std::string sandboxStatus;
        GameSandbox::install(std::filesystem::current_path(), std::filesystem::u8path(GetAppSaveRootPath()), sandboxStatus);
        SDL_Log("%s", sandboxStatus.c_str());
        if (!GameSandbox::nativeModsAllowed()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Limited process protection", sandboxStatus.c_str(), nullptr);
        }
        // Android launches a shared library and reads assets through SDL's APK
        // asset loader. Desktop executable/partition checks cannot run there.
#if defined(__ANDROID__) || PLATFORMER_IOS
        int result;
        do { result = RunGameApp(argc, argv); } while (result == kGameRestartExitCode);
        return result;
#else
        // The broker starts a fresh restricted process for Restart Game.
        return RunGameApp(argc, argv);
#endif
    } catch (const std::bad_alloc&) {
        std::terminate();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FATAL: uncaught std::exception: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "FATAL: uncaught non-std exception\n");
        return 1;
    }
}

#if defined(_WIN32) && defined(PLATFORMER_GAME_LIBRARY)
extern "C" __declspec(dllexport) int DFGameMain(int argc, char** argv, const DFSandboxAPI* api) {
    SandboxPointers::bind(api);
    return runMainImpl(argc, argv);
}
#else
int main(int argc, char** argv) {
    return GameSandbox::supervise(argc, argv, runMainImpl);
}


#endif // standalone entry points
