#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr const wchar_t* kTrayMutexName = L"Local\\DFNewTrayAppMutex";
constexpr const wchar_t* kTrayStopEventName = L"Local\\DFNewTrayAppStopEvent";

struct AppState {
    std::filesystem::path rootDir;
    std::filesystem::path launcherPath;
    std::string currentVersion;
    std::string currentVersionId;
    std::string windowsUpdateManifestUrl;
    std::string updaterState;
    std::string updaterDetail;
    std::string latestVersion;
    int gameProcessCount = 0;
    int updaterProcessCount = 0;
};

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 0) return std::wstring(text.begin(), text.end());
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), needed);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring quoteWindowsArg(const std::wstring& value) {
    std::wstring out = L"\"";
    int backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append((size_t)backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            out.append((size_t)backslashes, L'\\');
            backslashes = 0;
        }
        out.push_back(ch);
    }
    if (backslashes > 0) out.append((size_t)backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::string trimCopy(std::string text) {
    auto notSpace = [](unsigned char ch) { return ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n'; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::string();
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string readCurrentVersionId(const std::filesystem::path& rootDir) {
    return trimCopy(readTextFile(rootDir / "current_version.txt"));
}

int countProcessesByName(const wchar_t* exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int count = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0) {
                ++count;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

BOOL CALLBACK closeWindowProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;
    auto* pids = reinterpret_cast<std::vector<DWORD>*>(lParam);
    if (std::find(pids->begin(), pids->end(), pid) != pids->end()) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

void closeProcessesByName(const wchar_t* exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> pids;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0) {
                pids.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (!pids.empty()) {
        EnumWindows(&closeWindowProc, reinterpret_cast<LPARAM>(&pids));
    }
}

std::filesystem::path updaterStatusPath(const std::filesystem::path& rootDir, const std::string& versionId) {
    if (versionId.empty()) return std::filesystem::path();
    return rootDir / "versions" / utf8ToWide(versionId) / "updater-status.json";
}

std::filesystem::path installedVersionDir(const std::filesystem::path& rootDir, const std::string& versionId) {
    if (versionId.empty()) return std::filesystem::path();
    return rootDir / "versions" / utf8ToWide(versionId);
}

AppState readAppState(const std::filesystem::path& rootDir, const std::filesystem::path& launcherPath) {
    AppState state;
    state.rootDir = rootDir;
    state.launcherPath = launcherPath;
    state.currentVersionId = readCurrentVersionId(rootDir);
    state.currentVersion = state.currentVersionId;
    state.gameProcessCount = countProcessesByName(L"platformer.exe");
    state.updaterProcessCount = countProcessesByName(L"df-updater.exe");
    const auto configPath = installedVersionDir(rootDir, state.currentVersionId) / "assets" / "config.json";
    if (!configPath.empty() && std::filesystem::exists(configPath)) {
        try {
            const auto json = nlohmann::json::parse(readTextFile(configPath));
            if (json.contains("version") && json["version"].is_string()) {
                state.currentVersion = json["version"].get<std::string>();
            }
            if (json.contains("version_id")) {
                if (json["version_id"].is_string()) {
                    state.currentVersionId = json["version_id"].get<std::string>();
                } else if (json["version_id"].is_number_integer()) {
                    state.currentVersionId = std::to_string(json["version_id"].get<long long>());
                } else if (json["version_id"].is_number_unsigned()) {
                    state.currentVersionId = std::to_string(json["version_id"].get<unsigned long long>());
                }
            }
            if (json.contains("windows_update_manifest_url") && json["windows_update_manifest_url"].is_string()) {
                state.windowsUpdateManifestUrl = json["windows_update_manifest_url"].get<std::string>();
            }
        } catch (...) {
        }
    }
    const auto statusPath = updaterStatusPath(rootDir, state.currentVersionId);
    if (!statusPath.empty() && std::filesystem::exists(statusPath)) {
        try {
            const auto json = nlohmann::json::parse(readTextFile(statusPath));
            if (json.contains("state") && json["state"].is_string()) {
                state.updaterState = json["state"].get<std::string>();
            }
            if (json.contains("detail") && json["detail"].is_string()) {
                state.updaterDetail = json["detail"].get<std::string>();
            }
            if (json.contains("latest_version") && json["latest_version"].is_string()) {
                state.latestVersion = json["latest_version"].get<std::string>();
            }
        } catch (...) {
            state.updaterState = "error";
            state.updaterDetail = "Could not parse updater status.";
        }
    } else {
        state.updaterState = "idle";
    }
    return state;
}

SDL_Surface* createTrayIconSurface() {
    constexpr int kIconSize = 16;

    wchar_t modulePathBuf[MAX_PATH] = {};
    const DWORD moduleLen = GetModuleFileNameW(nullptr, modulePathBuf, (DWORD)std::size(modulePathBuf));
    if (moduleLen > 0 && moduleLen < std::size(modulePathBuf)) {
        HICON smallIcon = nullptr;
        if (ExtractIconExW(modulePathBuf, 0, nullptr, &smallIcon, 1) > 0 && smallIcon) {
            BITMAPV5HEADER bi{};
            bi.bV5Size = sizeof(bi);
            bi.bV5Width = kIconSize;
            bi.bV5Height = -kIconSize; // Top-down BGRA bitmap.
            bi.bV5Planes = 1;
            bi.bV5BitCount = 32;
            bi.bV5Compression = BI_BITFIELDS;
            bi.bV5RedMask = 0x00FF0000;
            bi.bV5GreenMask = 0x0000FF00;
            bi.bV5BlueMask = 0x000000FF;
            bi.bV5AlphaMask = 0xFF000000;

            void* dibPixels = nullptr;
            HDC screenDc = GetDC(nullptr);
            HDC memDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
            HBITMAP dib = memDc ? CreateDIBSection(memDc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &dibPixels, nullptr, 0) : nullptr;
            HGDIOBJ oldObj = (memDc && dib) ? SelectObject(memDc, dib) : nullptr;
            if (memDc && dib && dibPixels) {
                std::memset(dibPixels, 0, (size_t)kIconSize * (size_t)kIconSize * 4);
                DrawIconEx(memDc, 0, 0, smallIcon, kIconSize, kIconSize, 0, nullptr, DI_NORMAL);

                SDL_Surface* surface = SDL_CreateSurface(kIconSize, kIconSize, SDL_PIXELFORMAT_BGRA32);
                if (surface && surface->pixels) {
                    const size_t rowBytes = (size_t)kIconSize * 4;
                    for (int y = 0; y < kIconSize; ++y) {
                        std::memcpy(static_cast<unsigned char*>(surface->pixels) + (size_t)y * (size_t)surface->pitch,
                                    static_cast<const unsigned char*>(dibPixels) + (size_t)y * rowBytes,
                                    rowBytes);
                    }
                }

                if (oldObj) SelectObject(memDc, oldObj);
                DeleteObject(dib);
                DeleteDC(memDc);
                if (screenDc) ReleaseDC(nullptr, screenDc);
                DestroyIcon(smallIcon);
                if (surface) return surface;
            } else {
                if (oldObj) SelectObject(memDc, oldObj);
                if (dib) DeleteObject(dib);
                if (memDc) DeleteDC(memDc);
                if (screenDc) ReleaseDC(nullptr, screenDc);
                DestroyIcon(smallIcon);
            }
        }
    }

    SDL_Surface* fallback = SDL_CreateSurface(kIconSize, kIconSize, SDL_PIXELFORMAT_RGBA32);
    if (!fallback) return nullptr;
    SDL_FillSurfaceRect(fallback, nullptr, SDL_MapSurfaceRGBA(fallback, 28, 48, 92, 255));
    SDL_Rect inner{2, 2, 12, 12};
    SDL_FillSurfaceRect(fallback, &inner, SDL_MapSurfaceRGBA(fallback, 168, 220, 255, 255));
    SDL_Rect stripe{4, 4, 8, 3};
    SDL_FillSurfaceRect(fallback, &stripe, SDL_MapSurfaceRGBA(fallback, 28, 48, 92, 255));
    stripe = SDL_Rect{4, 9, 5, 3};
    SDL_FillSurfaceRect(fallback, &stripe, SDL_MapSurfaceRGBA(fallback, 28, 48, 92, 255));
    return fallback;
}

std::string updaterStateLabel(const AppState& state) {
    if (state.updaterState.empty() || state.updaterState == "idle") return "Updater: Idle";
    if (state.latestVersion.empty()) return "Updater: " + state.updaterState;
    return "Updater: " + state.updaterState + " -> " + state.latestVersion;
}

void launchViaLauncher(const AppState& state) {
    if (!std::filesystem::exists(state.launcherPath)) {
        MessageBoxW(nullptr, L"df-launcher.exe was not found.", L"Dorfplatformer Tray", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring cmd = quoteWindowsArg(state.launcherPath.wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    const BOOL started = CreateProcessW(
        state.launcherPath.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        state.launcherPath.parent_path().c_str(),
        &si,
        &pi);
    if (!started) {
        std::wstringstream ss;
        ss << L"Could not launch the game.\n\nWin32 error: " << GetLastError();
        MessageBoxW(nullptr, ss.str().c_str(), L"Dorfplatformer Tray", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

void openInstallFolder(const AppState& state) {
    ShellExecuteW(nullptr, L"open", state.rootDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void launchUpdater(const AppState& state) {
    const std::filesystem::path updaterPath = state.rootDir / "df-updater.exe";
    if (!std::filesystem::exists(updaterPath)) {
        MessageBoxW(nullptr, L"df-updater.exe was not found.", L"Dorfplatformer Tray", MB_OK | MB_ICONERROR);
        return;
    }
    const auto gameDir = installedVersionDir(state.rootDir, state.currentVersionId);
    const auto statusFile = updaterStatusPath(state.rootDir, state.currentVersionId);
    if (gameDir.empty() || !std::filesystem::exists(gameDir)) {
        MessageBoxW(nullptr, L"The installed game version folder could not be found.", L"Dorfplatformer Tray", MB_OK | MB_ICONERROR);
        return;
    }
    if (state.windowsUpdateManifestUrl.empty()) {
        MessageBoxW(nullptr, L"No Windows update manifest URL is configured for the installed version.", L"Dorfplatformer Tray", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring cmdLine = quoteWindowsArg(updaterPath.wstring());
    cmdLine += L" --mode check";
    cmdLine += L" --current-version ";
    cmdLine += quoteWindowsArg(utf8ToWide(state.currentVersion.empty() ? state.currentVersionId : state.currentVersion));
    cmdLine += L" --current-version-id ";
    cmdLine += quoteWindowsArg(utf8ToWide(state.currentVersionId));
    cmdLine += L" --app-dir ";
    cmdLine += quoteWindowsArg(gameDir.wstring());
    cmdLine += L" --status-file ";
    cmdLine += quoteWindowsArg(statusFile.wstring());
    cmdLine += L" --manifest-url ";
    cmdLine += quoteWindowsArg(utf8ToWide(state.windowsUpdateManifestUrl));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');
    const BOOL started = CreateProcessW(
        updaterPath.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr,
        state.rootDir.c_str(),
        &si,
        &pi);
    if (!started) {
        std::wstringstream ss;
        ss << L"Could not launch the updater.\n\nWin32 error: " << GetLastError();
        MessageBoxW(nullptr, ss.str().c_str(), L"Dorfplatformer Tray", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

struct TrayContext {
    std::filesystem::path rootDir;
    std::filesystem::path launcherPath;
    AppState state;
    SDL_Tray* tray = nullptr;
    SDL_TrayEntry* gameStatusEntry = nullptr;
    SDL_TrayEntry* updaterStatusEntry = nullptr;
    SDL_TrayEntry* versionEntry = nullptr;
    SDL_TrayEntry* detailEntry = nullptr;
    SDL_TrayEntry* launchEntry = nullptr;
    SDL_TrayEntry* checkUpdatesEntry = nullptr;
    SDL_TrayEntry* closeGameEntry = nullptr;
    SDL_TrayEntry* openFolderEntry = nullptr;
    SDL_TrayEntry* exitEntry = nullptr;
    std::string lastErrorDetail;
    bool running = true;
};

void SDLCALL onLaunchClicked(void* userdata, SDL_TrayEntry*) {
    auto* ctx = static_cast<TrayContext*>(userdata);
    if (!ctx) return;
    launchViaLauncher(ctx->state);
}

void SDLCALL onCheckUpdatesClicked(void* userdata, SDL_TrayEntry*) {
    auto* ctx = static_cast<TrayContext*>(userdata);
    if (!ctx) return;
    launchUpdater(ctx->state);
}

void SDLCALL onCloseGameClicked(void* userdata, SDL_TrayEntry*) {
    auto* ctx = static_cast<TrayContext*>(userdata);
    if (!ctx) return;
    closeProcessesByName(L"platformer.exe");
}

void SDLCALL onOpenFolderClicked(void* userdata, SDL_TrayEntry*) {
    auto* ctx = static_cast<TrayContext*>(userdata);
    if (!ctx) return;
    openInstallFolder(ctx->state);
}

void SDLCALL onExitClicked(void* userdata, SDL_TrayEntry*) {
    auto* ctx = static_cast<TrayContext*>(userdata);
    if (!ctx) return;
    ctx->running = false;
}

bool hasArg(int argc, wchar_t** argv, const wchar_t* expected) {
    if (!expected) return false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && _wcsicmp(argv[i], expected) == 0) {
            return true;
        }
    }
    return false;
}

bool signalExistingTrayShutdown() {
    HANDLE stopEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, kTrayStopEventName);
    if (!stopEvent) return false;
    const BOOL signaled = SetEvent(stopEvent);
    CloseHandle(stopEvent);
    return signaled != FALSE;
}

void refreshTrayUi(TrayContext& ctx) {
    ctx.state = readAppState(ctx.rootDir, ctx.launcherPath);
    SDL_SetTrayEntryLabel(ctx.gameStatusEntry,
                          (std::string("Game: ") +
                           (ctx.state.gameProcessCount > 0 ? ("Running x" + std::to_string(ctx.state.gameProcessCount)) : "Stopped")).c_str());
    SDL_SetTrayEntryLabel(ctx.updaterStatusEntry, updaterStateLabel(ctx.state).c_str());
    SDL_SetTrayEntryLabel(ctx.versionEntry,
                          (std::string("Installed Version ID: ") +
                           (ctx.state.currentVersionId.empty() ? "unknown" : ctx.state.currentVersionId)).c_str());
    SDL_SetTrayEntryLabel(ctx.detailEntry,
                          (std::string("Detail: ") +
                           (ctx.state.updaterDetail.empty() ? "No active message." : ctx.state.updaterDetail)).c_str());
    SDL_SetTrayEntryEnabled(ctx.closeGameEntry, ctx.state.gameProcessCount > 0);
    SDL_SetTrayEntryEnabled(ctx.checkUpdatesEntry, ctx.state.updaterProcessCount == 0 &&
                            (ctx.state.updaterState.empty() || ctx.state.updaterState == "idle"));

    std::string tooltip = "Dorfplatformer";
    tooltip += "\nGame: ";
    tooltip += (ctx.state.gameProcessCount > 0) ? "Running" : "Stopped";
    tooltip += "\nUpdater: ";
    tooltip += ctx.state.updaterState.empty() ? "idle" : ctx.state.updaterState;
    SDL_SetTrayTooltip(ctx.tray, tooltip.c_str());

    if (ctx.state.updaterState == "error" && !ctx.state.updaterDetail.empty() &&
        ctx.state.updaterDetail != ctx.lastErrorDetail) {
        ctx.lastErrorDetail = ctx.state.updaterDetail;
        MessageBoxW(nullptr, utf8ToWide(ctx.state.updaterDetail).c_str(),
                    L"Dorfplatformer Update Error", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool shutdownOnly = argv && (hasArg(argc, argv, L"--shutdown") || hasArg(argc, argv, L"--exit"));
    if (argv) {
        LocalFree(argv);
        argv = nullptr;
    }

    if (shutdownOnly) {
        return signalExistingTrayShutdown() ? 0 : 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kTrayMutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    HANDLE stopEvent = CreateEventW(nullptr, FALSE, FALSE, kTrayStopEventName);
    if (!stopEvent) {
        if (mutex) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
        return 1;
    }

    wchar_t modulePathBuf[MAX_PATH] = {};
    const DWORD moduleLen = GetModuleFileNameW(nullptr, modulePathBuf, (DWORD)std::size(modulePathBuf));
    if (moduleLen == 0 || moduleLen >= std::size(modulePathBuf)) {
        CloseHandle(stopEvent);
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    const std::filesystem::path modulePath(modulePathBuf);
    const std::filesystem::path rootDir = modulePath.parent_path();
    const std::filesystem::path launcherPath = rootDir / "df-launcher.exe";

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        CloseHandle(stopEvent);
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    SDL_Surface* icon = createTrayIconSurface();
    SDL_Tray* tray = SDL_CreateTray(icon, "Dorfplatformer Tray");
    if (icon) SDL_DestroySurface(icon);
    if (!tray) {
        SDL_Quit();
        CloseHandle(stopEvent);
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    TrayContext ctx;
    ctx.rootDir = rootDir;
    ctx.launcherPath = launcherPath;
    ctx.tray = tray;
    ctx.state = readAppState(rootDir, launcherPath);

    SDL_TrayMenu* menu = SDL_CreateTrayMenu(tray);
    ctx.gameStatusEntry = SDL_InsertTrayEntryAt(menu, -1, "Game: ...", SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
    ctx.updaterStatusEntry = SDL_InsertTrayEntryAt(menu, -1, "Updater: ...", SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
    ctx.versionEntry = SDL_InsertTrayEntryAt(menu, -1, "Installed Version ID: ...", SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
    ctx.detailEntry = SDL_InsertTrayEntryAt(menu, -1, "Detail: ...", SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
    SDL_InsertTrayEntryAt(menu, -1, nullptr, SDL_TRAYENTRY_BUTTON);
    ctx.launchEntry = SDL_InsertTrayEntryAt(menu, -1, "Launch Game", SDL_TRAYENTRY_BUTTON);
    ctx.checkUpdatesEntry = SDL_InsertTrayEntryAt(menu, -1, "Check for Updates", SDL_TRAYENTRY_BUTTON);
    ctx.closeGameEntry = SDL_InsertTrayEntryAt(menu, -1, "Close Running Game", SDL_TRAYENTRY_BUTTON);
    ctx.openFolderEntry = SDL_InsertTrayEntryAt(menu, -1, "Open Install Folder", SDL_TRAYENTRY_BUTTON);
    ctx.exitEntry = SDL_InsertTrayEntryAt(menu, -1, "Exit Tray", SDL_TRAYENTRY_BUTTON);

    SDL_SetTrayEntryCallback(ctx.launchEntry, &onLaunchClicked, &ctx);
    SDL_SetTrayEntryCallback(ctx.checkUpdatesEntry, &onCheckUpdatesClicked, &ctx);
    SDL_SetTrayEntryCallback(ctx.closeGameEntry, &onCloseGameClicked, &ctx);
    SDL_SetTrayEntryCallback(ctx.openFolderEntry, &onOpenFolderClicked, &ctx);
    SDL_SetTrayEntryCallback(ctx.exitEntry, &onExitClicked, &ctx);

    refreshTrayUi(ctx);

    Uint64 lastRefresh = SDL_GetTicks();
    while (ctx.running) {
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            ctx.running = false;
            break;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                ctx.running = false;
            }
        }
        const Uint64 now = SDL_GetTicks();
        if (now - lastRefresh >= 1000) {
            lastRefresh = now;
            refreshTrayUi(ctx);
        }
        SDL_UpdateTrays();
        SDL_Delay(50);
    }

    SDL_DestroyTray(tray);
    SDL_Quit();
    CloseHandle(stopEvent);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
