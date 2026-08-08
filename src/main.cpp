#include "GameApp.h"
#include "CustomBootLoader.h"
#include "Platform.h"
#if PLATFORMER_APPLE_SDL_MAIN
#include <SDL3/SDL_main.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <exception>
#if defined(__linux__)
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
#if defined(__linux__)
    useExecutableDirectoryAsWorkingDirectory();
#endif
    try {
        const CustomBoot::Result boot = CustomBoot::Run(argc, argv);
        if (!boot.ok) {
            return boot.exitCode;
        }
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
#include <tlhelp32.h>

static HANDLE gSingleInstanceMutex = nullptr;

static DWORD getParentProcessId() {
    const DWORD currentPid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        return 0;
    }

    do {
        if (entry.th32ProcessID == currentPid) {
            CloseHandle(snapshot);
            return entry.th32ParentProcessID;
        }
    } while (Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return 0;
}

static void rebindConsoleStream(FILE* target, const char* device, const char* mode) {
    FILE* rebound = nullptr;
    if (freopen_s(&rebound, device, mode, target) == 0 && rebound != nullptr) {
        std::clearerr(target);
    }
}

static void attachToParentConsoleIfAvailable() {
    if (GetConsoleWindow() != nullptr) {
        return;
    }

    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (!attached) {
        const DWORD parentPid = getParentProcessId();
        if (parentPid != 0) {
            attached = AttachConsole(parentPid) != FALSE;
        }
    }
    if (!attached) {
        return;
    }

    rebindConsoleStream(stdin, "CONIN$", "r");
    rebindConsoleStream(stdout, "CONOUT$", "w");
    rebindConsoleStream(stderr, "CONOUT$", "w");
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    attachToParentConsoleIfAvailable();
    gSingleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\DFNewGameSingleInstance");
    if (!gSingleInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (gSingleInstanceMutex) {
            CloseHandle(gSingleInstanceMutex);
            gSingleInstanceMutex = nullptr;
        }
        return 0;
    }

    const int rc = runMainImpl(__argc, __argv);
    if (gSingleInstanceMutex) {
        ReleaseMutex(gSingleInstanceMutex);
        CloseHandle(gSingleInstanceMutex);
        gSingleInstanceMutex = nullptr;
    }
    return rc;
}
#endif
