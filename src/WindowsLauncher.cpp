#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
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

std::string readCurrentVersionId(const std::filesystem::path& rootDir) {
    const std::filesystem::path currentVersionFile = rootDir / "current_version.txt";
    std::ifstream in(currentVersionFile);
    if (!in.is_open()) return std::string();
    std::string value;
    std::getline(in, value);
    return trimCopy(value);
}

bool tryParseVersionId(const std::string& text, long long& outValue) {
    if (text.empty()) return false;
    char* endPtr = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &endPtr, 10);
    if (!endPtr || *endPtr != '\0') return false;
    outValue = parsed;
    return true;
}

std::string findHighestInstalledVersionId(const std::filesystem::path& rootDir) {
    const std::filesystem::path versionsDir = rootDir / "versions";
    if (!std::filesystem::exists(versionsDir)) return std::string();

    bool foundAny = false;
    long long bestValue = 0;
    std::string bestText;
    for (const auto& entry : std::filesystem::directory_iterator(versionsDir)) {
        if (!entry.is_directory()) continue;
        long long parsed = 0;
        const std::string name = entry.path().filename().string();
        if (!tryParseVersionId(name, parsed)) continue;
        if (!foundAny || parsed > bestValue) {
            foundAny = true;
            bestValue = parsed;
            bestText = name;
        }
    }
    return bestText;
}

void showError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"Dorfplatformer Launcher", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

void launchTrayHelperIfNeeded(const std::filesystem::path& rootDir) {
    HANDLE existingMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\DFNewTrayAppMutex");
    if (existingMutex) {
        CloseHandle(existingMutex);
        return;
    }

    const std::filesystem::path trayExe = rootDir / "df-tray.exe";
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
        rootDir.c_str(),
        &si,
        &pi);
    if (!started) {
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t modulePathBuf[MAX_PATH] = {};
    const DWORD moduleLen = GetModuleFileNameW(nullptr, modulePathBuf, (DWORD)std::size(modulePathBuf));
    if (moduleLen == 0 || moduleLen >= std::size(modulePathBuf)) {
        showError(L"Could not resolve the launcher path.");
        return 1;
    }

    const std::filesystem::path launcherPath(modulePathBuf);
    const std::filesystem::path rootDir = launcherPath.parent_path();

    std::string versionId = readCurrentVersionId(rootDir);
    if (versionId.empty()) {
        versionId = findHighestInstalledVersionId(rootDir);
    }
    if (versionId.empty()) {
        showError(L"No installed game version was found.");
        return 1;
    }

    launchTrayHelperIfNeeded(rootDir);

    const std::filesystem::path gameDir = rootDir / "versions" / utf8ToWide(versionId);
    const std::filesystem::path gameExe = gameDir / "platformer.exe";
    if (!std::filesystem::exists(gameExe)) {
        std::wstring msg = L"The selected game version could not be launched:\n\n" + gameExe.wstring();
        showError(msg);
        return 1;
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring cmdLine = quoteWindowsArg(gameExe.wstring());
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            cmdLine += L" ";
            cmdLine += quoteWindowsArg(argv[i] ? argv[i] : L"");
        }
        LocalFree(argv);
    }

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
