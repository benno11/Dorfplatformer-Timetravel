#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr const wchar_t* kLauncherAppUserModelId = L"Benno111.DorfplatformerTimetravel.Launcher";
constexpr int kControlVersionList = 1001;
constexpr int kControlLaunch = 1002;
constexpr int kControlSetDefault = 1003;
constexpr int kControlCancel = 1004;
constexpr int kControlHeader = 1005;

struct InstalledVersion {
    std::string id;
    std::string version;
    std::filesystem::path dir;
    std::filesystem::path gameExe;
};

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

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string(text.begin(), text.end());
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), needed, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 0) return std::wstring(text.begin(), text.end());
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), needed);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
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
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string readCurrentVersionId(const std::filesystem::path& rootDir) {
    const std::filesystem::path currentVersionFile = rootDir / "current_version.txt";
    std::ifstream in(currentVersionFile);
    if (!in.is_open()) return std::string();
    std::string value;
    std::getline(in, value);
    return trimCopy(value);
}

void writeCurrentVersionId(const std::filesystem::path& rootDir, const std::string& versionId) {
    std::ofstream out(rootDir / "current_version.txt", std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out << versionId;
}

std::string jsonStringField(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return std::string();
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::string();
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return std::string();
    std::string out;
    bool escaped = false;
    for (size_t i = pos + 1; i < text.size(); ++i) {
        const char ch = text[i];
        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') break;
        out.push_back(ch);
    }
    return out;
}

std::string readInstalledVersionLabel(const std::filesystem::path& versionDir) {
    std::string version = jsonStringField(readTextFile(versionDir / "assets" / "config.json"), "version");
    if (version.empty()) {
        version = jsonStringField(readTextFile(versionDir / "config.json"), "version");
    }
    return version;
}

bool tryParseVersionId(const std::string& text, long long& outValue) {
    if (text.empty()) return false;
    char* endPtr = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &endPtr, 10);
    if (!endPtr || *endPtr != '\0') return false;
    outValue = parsed;
    return true;
}

int compareVersionIds(const std::string& a, const std::string& b) {
    long long ai = 0;
    long long bi = 0;
    const bool aNumeric = tryParseVersionId(a, ai);
    const bool bNumeric = tryParseVersionId(b, bi);
    if (aNumeric && bNumeric) {
        if (ai < bi) return -1;
        if (ai > bi) return 1;
        return 0;
    }
    return _stricmp(a.c_str(), b.c_str());
}

std::vector<InstalledVersion> listInstalledVersions(const std::filesystem::path& rootDir) {
    std::vector<InstalledVersion> versions;
    const std::filesystem::path versionsDir = rootDir / "versions";
    if (!std::filesystem::exists(versionsDir)) return versions;

    for (const auto& entry : std::filesystem::directory_iterator(versionsDir)) {
        if (!entry.is_directory()) continue;
        InstalledVersion version;
        version.id = entry.path().filename().string();
        version.dir = entry.path();
        version.gameExe = version.dir / "platformer.exe";
        if (version.id.empty() || !std::filesystem::exists(version.gameExe)) continue;
        version.version = readInstalledVersionLabel(version.dir);
        versions.push_back(version);
    }

    std::sort(versions.begin(), versions.end(), [](const InstalledVersion& a, const InstalledVersion& b) {
        return compareVersionIds(a.id, b.id) > 0;
    });
    return versions;
}

std::string findHighestInstalledVersionId(const std::filesystem::path& rootDir) {
    const std::vector<InstalledVersion> versions = listInstalledVersions(rootDir);
    return versions.empty() ? std::string() : versions.front().id;
}

void showError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"Dorfplatformer Launcher", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
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

std::string argValue(int argc, wchar_t** argv, const wchar_t* expected) {
    if (!expected) return std::string();
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] && _wcsicmp(argv[i], expected) == 0) {
            return wideToUtf8(argv[i + 1] ? argv[i + 1] : L"");
        }
    }
    return std::string();
}

std::vector<std::wstring> forwardedGameArgs(int argc, wchar_t** argv) {
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        if (_wcsicmp(argv[i], L"--version-id") == 0) {
            ++i;
            continue;
        }
        if (_wcsicmp(argv[i], L"--pick-version") == 0 ||
            _wcsicmp(argv[i], L"--tray") == 0) {
            continue;
        }
        args.emplace_back(argv[i]);
    }
    return args;
}

void launchTrayHelperIfNeeded(const std::filesystem::path& rootDir) {
    HANDLE existingMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\DFNewTrayAppMutex");
    if (existingMutex) {
        CloseHandle(existingMutex);
        return;
    }

    std::string versionId = readCurrentVersionId(rootDir);
    if (versionId.empty()) {
        versionId = findHighestInstalledVersionId(rootDir);
    }
    if (versionId.empty()) {
        return;
    }

    const std::filesystem::path trayExe = rootDir / "versions" / utf8ToWide(versionId) / "df-tray.exe";
    if (!std::filesystem::exists(trayExe)) {
        return;
    }

    std::wstring cmdLine = quoteWindowsArg(trayExe.wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');
    const BOOL started = CreateProcessW(
        trayExe.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr,
        trayExe.parent_path().c_str(),
        &si,
        &pi);
    if (!started) {
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

std::wstring displayLabel(const InstalledVersion& version, const std::string& currentVersionId) {
    std::wstring label = L"Version ID " + utf8ToWide(version.id);
    if (!version.version.empty()) {
        label += L"  -  v" + utf8ToWide(version.version);
    }
    if (version.id == currentVersionId) {
        label += L"  (default)";
    }
    return label;
}

struct PickerState {
    const std::filesystem::path* rootDir = nullptr;
    const std::vector<InstalledVersion>* versions = nullptr;
    std::string currentVersionId;
    int selected = -1;
    bool accepted = false;
    bool setDefault = false;
};

void layoutPicker(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int pad = 14;
    const int buttonW = 112;
    const int buttonH = 30;
    const int buttonY = std::max(pad, rc.bottom - pad - buttonH);
    HWND header = GetDlgItem(hwnd, kControlHeader);
    HWND list = GetDlgItem(hwnd, kControlVersionList);
    HWND launch = GetDlgItem(hwnd, kControlLaunch);
    HWND setDefault = GetDlgItem(hwnd, kControlSetDefault);
    HWND cancel = GetDlgItem(hwnd, kControlCancel);
    MoveWindow(header, pad, pad, std::max(1, rc.right - pad * 2), 34, TRUE);
    MoveWindow(list, pad, pad + 38, std::max(1, rc.right - pad * 2), std::max(1, buttonY - pad * 2 - 38), TRUE);
    MoveWindow(launch, rc.right - pad - buttonW * 3 - 16, buttonY, buttonW, buttonH, TRUE);
    MoveWindow(setDefault, rc.right - pad - buttonW * 2 - 8, buttonY, buttonW, buttonH, TRUE);
    MoveWindow(cancel, rc.right - pad - buttonW, buttonY, buttonW, buttonH, TRUE);
}

void updatePickerButtons(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, kControlVersionList);
    const LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
    const BOOL enabled = sel != LB_ERR;
    EnableWindow(GetDlgItem(hwnd, kControlLaunch), enabled);
    EnableWindow(GetDlgItem(hwnd, kControlSetDefault), enabled);
}

LRESULT CALLBACK pickerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PickerState* state = reinterpret_cast<PickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<PickerState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CreateWindowW(L"STATIC",
            L"Choose which installed version to launch. Pin the launcher shortcut to keep one stable taskbar entry.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>((INT_PTR)kControlHeader), GetModuleHandleW(nullptr), nullptr);
        HWND list = CreateWindowW(L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>((INT_PTR)kControlVersionList), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"Launch",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>((INT_PTR)kControlLaunch), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"Set Default",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>((INT_PTR)kControlSetDefault), GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>((INT_PTR)kControlCancel), GetModuleHandleW(nullptr), nullptr);
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (int id : {kControlHeader, kControlVersionList, kControlLaunch, kControlSetDefault, kControlCancel}) {
            SendMessageW(GetDlgItem(hwnd, id), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        int defaultIndex = 0;
        if (state && state->versions) {
            for (int i = 0; i < (int)state->versions->size(); ++i) {
                const InstalledVersion& version = (*state->versions)[i];
                const std::wstring label = displayLabel(version, state->currentVersionId);
                SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                if (version.id == state->currentVersionId) defaultIndex = i;
            }
        }
        SendMessageW(list, LB_SETCURSEL, defaultIndex, 0);
        updatePickerButtons(hwnd);
        layoutPicker(hwnd);
        return 0;
    }
    case WM_SIZE:
        layoutPicker(hwnd);
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == kControlVersionList && code == LBN_SELCHANGE) {
            updatePickerButtons(hwnd);
            return 0;
        }
        if (id == kControlVersionList && code == LBN_DBLCLK) {
            id = kControlLaunch;
        }
        if (id == kControlLaunch || id == kControlSetDefault) {
            HWND list = GetDlgItem(hwnd, kControlVersionList);
            const LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (state && state->versions && sel != LB_ERR && sel >= 0 && sel < (LRESULT)state->versions->size()) {
                state->selected = (int)sel;
                state->accepted = true;
                state->setDefault = id == kControlSetDefault;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        if (id == kControlCancel) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::string pickInstalledVersionId(const std::filesystem::path& rootDir,
                                   const std::vector<InstalledVersion>& versions,
                                   const std::string& currentVersionId) {
    if (versions.empty()) return std::string();

    PickerState state;
    state.rootDir = &rootDir;
    state.versions = &versions;
    state.currentVersionId = currentVersionId;

    WNDCLASSW wc{};
    wc.lpfnWndProc = pickerWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DFNewLauncherVersionPicker";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"Dorfplatformer Launcher",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        560,
        360,
        nullptr,
        nullptr,
        wc.hInstance,
        &state);
    if (!hwnd) return std::string();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (!state.accepted || state.selected < 0 || state.selected >= (int)versions.size()) {
        return std::string();
    }
    if (state.setDefault) {
        writeCurrentVersionId(rootDir, versions[state.selected].id);
    }
    return versions[state.selected].id;
}

const InstalledVersion* findInstalledVersion(const std::vector<InstalledVersion>& versions, const std::string& versionId) {
    for (const InstalledVersion& version : versions) {
        if (version.id == versionId) return &version;
    }
    return nullptr;
}

bool launchTrayOnly(const std::filesystem::path& rootDir) {
    HANDLE existingMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\DFNewTrayAppMutex");
    if (existingMutex) {
        CloseHandle(existingMutex);
        return true;
    }

    std::string versionId = readCurrentVersionId(rootDir);
    if (versionId.empty()) {
        versionId = findHighestInstalledVersionId(rootDir);
    }
    if (versionId.empty()) {
        showError(L"No installed tray companion version was found.");
        return false;
    }

    const std::filesystem::path trayExe = rootDir / "versions" / utf8ToWide(versionId) / "df-tray.exe";
    if (!std::filesystem::exists(trayExe)) {
        std::wstring msg = L"The tray companion could not be launched:\n\n" + trayExe.wstring();
        showError(msg);
        return false;
    }

    std::wstring cmdLine = quoteWindowsArg(trayExe.wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');
    const BOOL started = CreateProcessW(
        trayExe.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr,
        trayExe.parent_path().c_str(),
        &si,
        &pi);
    if (!started) {
        std::wstringstream ss;
        ss << L"Could not launch the tray companion.\n\nWin32 error: " << GetLastError();
        showError(ss.str());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetCurrentProcessExplicitAppUserModelID(kLauncherAppUserModelId);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    wchar_t modulePathBuf[MAX_PATH] = {};
    const DWORD moduleLen = GetModuleFileNameW(nullptr, modulePathBuf, (DWORD)std::size(modulePathBuf));
    if (moduleLen == 0 || moduleLen >= std::size(modulePathBuf)) {
        if (argv) LocalFree(argv);
        showError(L"Could not resolve the launcher path.");
        return 1;
    }

    const std::filesystem::path launcherPath(modulePathBuf);
    const std::filesystem::path rootDir = launcherPath.parent_path();

    const bool trayOnly = argv && hasArg(argc, argv, L"--tray");
    const bool forcePicker = argv && hasArg(argc, argv, L"--pick-version");
    const std::string requestedVersionId = argv ? argValue(argc, argv, L"--version-id") : std::string();
    const std::vector<std::wstring> gameArgs = argv ? forwardedGameArgs(argc, argv) : std::vector<std::wstring>();
    if (trayOnly) {
        if (argv) LocalFree(argv);
        return launchTrayOnly(rootDir) ? 0 : 1;
    }

    const std::vector<InstalledVersion> installedVersions = listInstalledVersions(rootDir);
    if (installedVersions.empty()) {
        if (argv) LocalFree(argv);
        showError(L"No installed game version was found.");
        return 1;
    }

    std::string versionId = requestedVersionId;
    if (findInstalledVersion(installedVersions, versionId) == nullptr) {
        versionId = readCurrentVersionId(rootDir);
    }
    if (findInstalledVersion(installedVersions, versionId) == nullptr) {
        versionId = installedVersions.front().id;
    }
    if (forcePicker || requestedVersionId.empty()) {
        const std::string pickedVersionId = pickInstalledVersionId(rootDir, installedVersions, versionId);
        if (pickedVersionId.empty()) {
            if (argv) LocalFree(argv);
            return 0;
        }
        versionId = pickedVersionId;
    }

    launchTrayHelperIfNeeded(rootDir);

    const InstalledVersion* selectedVersion = findInstalledVersion(installedVersions, versionId);
    if (!selectedVersion) {
        if (argv) LocalFree(argv);
        showError(L"The selected game version is no longer installed.");
        return 1;
    }

    const std::filesystem::path gameDir = selectedVersion->dir;
    const std::filesystem::path gameExe = selectedVersion->gameExe;
    if (!std::filesystem::exists(gameExe)) {
        std::wstring msg = L"The selected game version could not be launched:\n\n" + gameExe.wstring();
        if (argv) LocalFree(argv);
        showError(msg);
        return 1;
    }

    std::wstring cmdLine = quoteWindowsArg(gameExe.wstring());
    for (const std::wstring& arg : gameArgs) {
        cmdLine += L" ";
        cmdLine += quoteWindowsArg(arg);
    }
    if (argv) LocalFree(argv);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');
    const BOOL started = CreateProcessW(
        gameExe.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        gameDir.c_str(),
        &si,
        &pi);
    if (!started) {
        std::wstringstream ss;
        ss << L"Could not start the selected game version.\n\nWin32 error: " << GetLastError();
        showError(ss.str());
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
