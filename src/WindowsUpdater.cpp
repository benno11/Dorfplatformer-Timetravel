#include <windows.h>
#include <shellapi.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Options {
    std::string mode = "check";
    std::string manifestUrl;
    std::string currentVersion;
    std::string currentVersionId;
    std::string installerUrl;
    std::string latestVersion;
    std::string latestVersionId;
    std::string notes;
    std::filesystem::path appDir;
    std::filesystem::path statusFile;
    DWORD waitPid = 0;
};

bool isCheckLikeMode(const std::string& mode) {
    return mode == "check" || mode == "tray";
}

struct Manifest {
    std::string version;
    std::string versionId;
    std::string installerUrl;
    std::string installerUrlX86;
    std::string installerUrlX64;
    std::string notes;
};

std::string getCurrentWindowsArchKey() {
#if defined(_M_IX86)
    return "x86";
#elif defined(_M_X64) || defined(_WIN64)
    return "x64";
#else
    return "";
#endif
}

std::string selectInstallerUrl(const Manifest& manifest) {
    const std::string arch = getCurrentWindowsArchKey();
    if (arch == "x86" && !manifest.installerUrlX86.empty()) {
        return manifest.installerUrlX86;
    }
    if (arch == "x64" && !manifest.installerUrlX64.empty()) {
        return manifest.installerUrlX64;
    }
    if (!manifest.installerUrl.empty()) {
        return manifest.installerUrl;
    }
    return {};
}

struct ParsedVersion {
    std::vector<int> numbers;
    int prereleaseRank = 3;
};

struct DownloadProgressContext {
    std::filesystem::path statusFile;
    std::string currentVersion;
    std::string currentVersionId;
    Manifest manifest;
    int lastPercent = -1;
};

int onDownloadProgress(void* clientp,
                       curl_off_t dltotal,
                       curl_off_t dlnow,
                       curl_off_t ultotal,
                       curl_off_t ulnow);

void writeStatus(const std::filesystem::path& statusFile,
                 const std::string& state,
                 const std::string& detail,
                 const std::string& currentVersion,
                 const std::string& currentVersionId,
                 const std::string& latestVersion = std::string(),
                 const std::string& latestVersionId = std::string()) {
    if (statusFile.empty()) return;
    std::error_code ec;
    const auto parent = statusFile.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    nlohmann::json json;
    json["state"] = state;
    json["detail"] = detail;
    json["current_version"] = currentVersion;
    json["current_version_id"] = currentVersionId;
    json["latest_version"] = latestVersion;
    json["latest_version_id"] = latestVersionId;
    json["installer_url"] = "";
    json["notes"] = "";
    json["progress"] = 0.0;
    std::ofstream out(statusFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out << json.dump(2);
}

void writeStatus(const std::filesystem::path& statusFile,
                 const std::string& state,
                 const std::string& detail,
                 const std::string& currentVersion,
                 const std::string& currentVersionId,
                 const Manifest& manifest) {
    if (statusFile.empty()) return;
    std::error_code ec;
    const auto parent = statusFile.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    nlohmann::json json;
    json["state"] = state;
    json["detail"] = detail;
    json["current_version"] = currentVersion;
    json["current_version_id"] = currentVersionId;
    json["latest_version"] = manifest.version;
    json["latest_version_id"] = manifest.versionId;
    json["installer_url"] = manifest.installerUrl;
    json["notes"] = manifest.notes;
    json["progress"] = 0.0;
    std::ofstream out(statusFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out << json.dump(2);
}

void writeStatus(const std::filesystem::path& statusFile,
                 const std::string& state,
                 const std::string& detail,
                 const std::string& currentVersion,
                 const std::string& currentVersionId,
                 const Manifest& manifest,
                 double progress01) {
    if (statusFile.empty()) return;
    std::error_code ec;
    const auto parent = statusFile.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    nlohmann::json json;
    json["state"] = state;
    json["detail"] = detail;
    json["current_version"] = currentVersion;
    json["current_version_id"] = currentVersionId;
    json["latest_version"] = manifest.version;
    json["latest_version_id"] = manifest.versionId;
    json["installer_url"] = manifest.installerUrl;
    json["notes"] = manifest.notes;
    json["progress"] = std::clamp(progress01, 0.0, 1.0);
    std::ofstream out(statusFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out << json.dump(2);
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

void showMessage(UINT flags, const std::string& title, const std::string& body) {
    MessageBoxW(nullptr, utf8ToWide(body).c_str(), utf8ToWide(title).c_str(), flags | MB_SETFOREGROUND);
}

size_t writeToString(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<const char*>(contents), total);
    return total;
}

size_t writeToFile(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::ofstream*>(userp);
    out->write(static_cast<const char*>(contents), (std::streamsize)total);
    return out->good() ? total : 0;
}

void setCompatibilityCurlOptions(CURL* curl) {
    if (!curl) return;

    // Older Windows Schannel builds can be temperamental when libcurl negotiates
    // ALPN, so keep updater traffic on plain HTTP/1.1.
    curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
}

bool httpGetText(const std::string& url, std::string& out, std::string& err) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        err = "curl init failed";
        return false;
    }
    long code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DF-New-Updater/1.0");
    setCompatibilityCurlOptions(curl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK || code < 200 || code >= 300) {
        std::ostringstream oss;
        oss << "Request failed (HTTP " << code << ", curl " << (int)rc << ")";
        err = oss.str();
        return false;
    }
    return true;
}

bool httpDownloadFile(const std::string& url,
                      const std::filesystem::path& outPath,
                      std::string& err,
                      DownloadProgressContext* progressCtx = nullptr) {
    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        err = "could not open download target";
        return false;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        err = "curl init failed";
        return false;
    }
    long code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DF-New-Updater/1.0");
    setCompatibilityCurlOptions(curl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    if (progressCtx) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &onDownloadProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progressCtx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }
    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    out.close();
    if (rc != CURLE_OK || code < 200 || code >= 300) {
        std::ostringstream oss;
        oss << "Download failed (HTTP " << code << ", curl " << (int)rc << ")";
        err = oss.str();
        return false;
    }
    return true;
}

BOOL CALLBACK closeWindowForPidProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    const DWORD targetPid = static_cast<DWORD>(lParam);
    if (pid == targetPid) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

void requestCloseProcess(DWORD pid) {
    if (pid == 0) return;
    EnumWindows(&closeWindowForPidProc, static_cast<LPARAM>(pid));
}

void waitForProcessExit(DWORD pid, DWORD timeoutMs) {
    if (pid == 0) return;

    HANDLE proc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    if (!proc) return;

    requestCloseProcess(pid);
    const DWORD waitRc = WaitForSingleObject(proc, timeoutMs);
    if (waitRc == WAIT_TIMEOUT) {
        TerminateProcess(proc, 0);
        WaitForSingleObject(proc, 5000);
    }
    CloseHandle(proc);
}

int onDownloadProgress(void* clientp,
                       curl_off_t dltotal,
                       curl_off_t dlnow,
                       curl_off_t,
                       curl_off_t) {
    auto* ctx = static_cast<DownloadProgressContext*>(clientp);
    if (!ctx) return 0;
    double progress01 = 0.0;
    if (dltotal > 0) {
        progress01 = (double)dlnow / (double)dltotal;
    }
    const int percent = (int)std::lround(std::clamp(progress01, 0.0, 1.0) * 100.0);
    if (percent == ctx->lastPercent && percent != 100) {
        return 0;
    }
    ctx->lastPercent = percent;
    std::ostringstream oss;
    if (dltotal > 0) oss << "Downloading installer... " << percent << "%";
    else oss << "Downloading installer...";
    writeStatus(ctx->statusFile, "downloading", oss.str(),
                ctx->currentVersion, ctx->currentVersionId, ctx->manifest, progress01);
    return 0;
}

std::optional<ParsedVersion> parseVersion(const std::string& text) {
    ParsedVersion out;
    bool foundNumber = false;
    int value = -1;
    std::string lower;
    lower.reserve(text.size());
    for (unsigned char ch : text) lower.push_back((char)std::tolower(ch));
    for (char ch : lower) {
        if (std::isdigit((unsigned char)ch)) {
            foundNumber = true;
            if (value < 0) value = 0;
            value = value * 10 + (ch - '0');
        } else if (value >= 0) {
            out.numbers.push_back(value);
            value = -1;
        }
    }
    if (value >= 0) out.numbers.push_back(value);
    if (!foundNumber) return std::nullopt;
    if (lower.find("alpha") != std::string::npos) out.prereleaseRank = 0;
    else if (lower.find("beta") != std::string::npos) out.prereleaseRank = 1;
    else if (lower.find("rc") != std::string::npos) out.prereleaseRank = 2;
    else out.prereleaseRank = 3;
    return out;
}

int compareVersions(const std::string& lhs, const std::string& rhs) {
    const auto a = parseVersion(lhs);
    const auto b = parseVersion(rhs);
    if (!a && !b) return _stricmp(lhs.c_str(), rhs.c_str());
    if (!a) return -1;
    if (!b) return 1;
    const size_t count = std::max(a->numbers.size(), b->numbers.size());
    for (size_t i = 0; i < count; ++i) {
        const int av = (i < a->numbers.size()) ? a->numbers[i] : 0;
        const int bv = (i < b->numbers.size()) ? b->numbers[i] : 0;
        if (av != bv) return (av < bv) ? -1 : 1;
    }
    if (a->prereleaseRank != b->prereleaseRank) {
        return (a->prereleaseRank < b->prereleaseRank) ? -1 : 1;
    }
    return 0;
}

bool parseArgs(int argc, wchar_t** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        auto nextValue = [&](std::string& target) -> bool {
            if (i + 1 >= argc || !argv[i + 1]) return false;
            const std::wstring value = argv[++i];
            const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 0) {
                target.clear();
                target.reserve(value.size());
                for (wchar_t ch : value) {
                    target.push_back((ch >= 0 && ch <= 0x7f) ? (char)ch : '?');
                }
                return true;
            }
            std::string utf8((size_t)needed, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), needed, nullptr, nullptr);
            if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
            target = utf8;
            return true;
        };
        if (arg == L"--manifest-url") {
            if (!nextValue(out.manifestUrl)) return false;
        } else if (arg == L"--mode") {
            if (!nextValue(out.mode)) return false;
        } else if (arg == L"--current-version") {
            if (!nextValue(out.currentVersion)) return false;
        } else if (arg == L"--current-version-id") {
            if (!nextValue(out.currentVersionId)) return false;
        } else if (arg == L"--installer-url") {
            if (!nextValue(out.installerUrl)) return false;
        } else if (arg == L"--latest-version") {
            if (!nextValue(out.latestVersion)) return false;
        } else if (arg == L"--latest-version-id") {
            if (!nextValue(out.latestVersionId)) return false;
        } else if (arg == L"--notes") {
            if (!nextValue(out.notes)) return false;
        } else if (arg == L"--wait-pid") {
            if (i + 1 >= argc || !argv[i + 1]) return false;
            out.waitPid = (DWORD)_wtoi(argv[++i]);
        } else if (arg == L"--app-dir") {
            if (i + 1 >= argc || !argv[i + 1]) return false;
            out.appDir = std::filesystem::path(argv[++i]);
        } else if (arg == L"--status-file") {
            if (i + 1 >= argc || !argv[i + 1]) return false;
            out.statusFile = std::filesystem::path(argv[++i]);
        }
    }
    return true;
}

bool loadManifest(const std::string& manifestUrl, Manifest& out, std::string& err) {
    std::string body;
    if (!httpGetText(manifestUrl, body, err)) return false;
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (const std::exception& ex) {
        err = std::string("Invalid manifest JSON: ") + ex.what();
        return false;
    }
    if (!json.contains("version") || !json["version"].is_string()) {
        err = "Manifest must contain a string field: version";
        return false;
    }
    const bool hasInstallerUrl = json.contains("installer_url") && json["installer_url"].is_string();
    const bool hasInstallerUrls = json.contains("installer_urls") && json["installer_urls"].is_object();
    if (!hasInstallerUrl && !hasInstallerUrls) {
        err = "Manifest must contain installer_url or installer_urls";
        return false;
    }
    auto jsonValueToString = [](const nlohmann::json& value) -> std::string {
        if (value.is_string()) return value.get<std::string>();
        if (value.is_number_integer()) return std::to_string(value.get<long long>());
        if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
        if (value.is_number_float()) {
            std::ostringstream oss;
            oss << value.get<double>();
            return oss.str();
        }
        return std::string();
    };
    out.version = json["version"].get<std::string>();
    if (json.contains("version_id")) {
        out.versionId = jsonValueToString(json["version_id"]);
        if (out.versionId.empty()) out.versionId = out.version;
    } else {
        out.versionId = out.version;
    }
    out.installerUrl = hasInstallerUrl ? json["installer_url"].get<std::string>() : std::string();
    if (hasInstallerUrls) {
        const auto& urls = json["installer_urls"];
        if (urls.contains("x86") && urls["x86"].is_string()) {
            out.installerUrlX86 = urls["x86"].get<std::string>();
        }
        if (urls.contains("x64") && urls["x64"].is_string()) {
            out.installerUrlX64 = urls["x64"].get<std::string>();
        }
    }
    if (json.contains("notes") && json["notes"].is_string()) {
        out.notes = json["notes"].get<std::string>();
    }
    if (out.installerUrl.empty()) {
        out.installerUrl = selectInstallerUrl(out);
    }
    if (out.installerUrl.empty()) {
        err = "Manifest does not provide an installer URL for this build architecture.";
        return false;
    }
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        showMessage(MB_ICONERROR, "Updater Error", "Could not read command line arguments.");
        return 1;
    }

    Options options;
    const bool argsOk = parseArgs(argc, argv, options);
    LocalFree(argv);
    if (!argsOk) {
        return 1;
    }
    if (options.currentVersionId.empty()) {
        options.currentVersionId = options.currentVersion;
    }

    if (isCheckLikeMode(options.mode)) {
        if (options.manifestUrl.empty()) {
            writeStatus(options.statusFile, "not_configured", "No update manifest URL was provided.",
                        options.currentVersion, options.currentVersionId);
            return 0;
        }
        writeStatus(options.statusFile, "checking", "Checking for updates...",
                    options.currentVersion, options.currentVersionId);

        curl_global_init(CURL_GLOBAL_DEFAULT);

        Manifest manifest;
        std::string err;
        if (!loadManifest(options.manifestUrl, manifest, err)) {
            writeStatus(options.statusFile, "error", "Could not load update manifest.",
                        options.currentVersion, options.currentVersionId);
            curl_global_cleanup();
            return 1;
        }

        if (compareVersions(options.currentVersionId, manifest.versionId) >= 0) {
            writeStatus(options.statusFile, "up_to_date", "Already on the latest version.",
                        options.currentVersion, options.currentVersionId, manifest);
            curl_global_cleanup();
            return 0;
        }

        if (options.mode == "tray") {
            options.installerUrl = manifest.installerUrl;
            options.latestVersion = manifest.version;
            options.latestVersionId = manifest.versionId;
            options.notes = manifest.notes;
        } else {
            writeStatus(options.statusFile, "update_available", "Update found.",
                        options.currentVersion, options.currentVersionId, manifest);
            curl_global_cleanup();
            return 0;
        }
    }

    if (options.mode != "install" && options.mode != "tray") {
        writeStatus(options.statusFile, "error", "Unknown updater mode.",
                    options.currentVersion, options.currentVersionId);
        return 1;
    }

    Manifest manifest;
    manifest.version = options.latestVersion;
    manifest.versionId = options.latestVersionId.empty() ? options.latestVersion : options.latestVersionId;
    manifest.installerUrl = options.installerUrl;
    manifest.notes = options.notes;

    if (manifest.installerUrl.empty()) {
        writeStatus(options.statusFile, "error", "No installer URL was provided.",
                    options.currentVersion, options.currentVersionId, manifest.version, manifest.versionId);
        return 1;
    }
    if (manifest.versionId.empty()) {
        manifest.versionId = manifest.version;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    const std::filesystem::path tempRoot =
        std::filesystem::temp_directory_path() / "df-new-updater";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot, ec);
    const std::filesystem::path installerPath = tempRoot / "df-platformer-update.exe";
    writeStatus(options.statusFile, "downloading", "Downloading installer...",
                options.currentVersion, options.currentVersionId, manifest, 0.0);
    std::string err;
    DownloadProgressContext progressCtx{
        options.statusFile,
        options.currentVersion,
        options.currentVersionId,
        manifest,
        -1
    };
    if (!httpDownloadFile(manifest.installerUrl, installerPath, err, &progressCtx)) {
        writeStatus(options.statusFile, "error", "Could not download the new installer.",
                    options.currentVersion, options.currentVersionId, manifest);
        curl_global_cleanup();
        return 1;
    }

    writeStatus(options.statusFile, "install_ready", "Installer downloaded. Starting installer...",
                options.currentVersion, options.currentVersionId, manifest, 1.0);
    std::wstring installerArgs = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = nullptr;
    sei.lpVerb = L"open";
    sei.lpFile = installerPath.c_str();
    sei.lpParameters = installerArgs.c_str();
    sei.lpDirectory = options.appDir.empty() ? nullptr : options.appDir.c_str();
    sei.nShow = SW_HIDE;
    const BOOL started = ShellExecuteExW(&sei);
    curl_global_cleanup();
    if (!started) {
        const DWORD launchErr = GetLastError();
        if (launchErr == ERROR_CANCELLED) {
            writeStatus(options.statusFile, "cancelled", "Installer launch was cancelled.",
                        options.currentVersion, options.currentVersionId, manifest, 1.0);
            return 0;
        }
        writeStatus(options.statusFile, "error", "The installer was downloaded but could not be started.",
                    options.currentVersion, options.currentVersionId, manifest);
        return 1;
    }
    writeStatus(options.statusFile, "installing", "Installing update...",
                options.currentVersion, options.currentVersionId, manifest, 1.0);
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD installerExitCode = 1;
    GetExitCodeProcess(sei.hProcess, &installerExitCode);
    CloseHandle(sei.hProcess);
    if (installerExitCode != 0) {
        writeStatus(options.statusFile, "error", "Installer failed.",
                    options.currentVersion, options.currentVersionId, manifest, 1.0);
        return 1;
    }
    if (options.waitPid != 0) {
        writeStatus(options.statusFile, "installing", "Update installed. Closing running game...",
                    options.currentVersion, options.currentVersionId, manifest, 1.0);
        waitForProcessExit(options.waitPid, 10000);
        writeStatus(options.statusFile, "relaunch_ready", "Update installed. Relaunching game...",
                    options.currentVersion, options.currentVersionId, manifest, 1.0);
    } else {
        writeStatus(options.statusFile, "up_to_date", "Update installed.",
                    options.currentVersion, options.currentVersionId, manifest);
    }
    return 0;
}
