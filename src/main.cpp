#include "GameApp.h"
#include <cstdio>
#include <cstdlib>
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
#include <tlhelp32.h>

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
    return runMainImpl(__argc, __argv);
}
#endif
