#include "UpdateManifest.h"
#include <iostream>

int main() {
    using namespace AppUpdate;
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        if (!ok) { std::cerr << label << '\n'; ++failures; }
    };
    const auto manifest = nlohmann::json::parse(R"({
        "version": "2.3", "version_id": 24,
        "installer_url": "https://example.com/legacy.exe",
        "installer_urls": {
            "windows": {"x86": "https://example.com/x86.exe", "arm64": "https://example.com/arm64.exe"},
            "linux": {"x64": "https://example.com/linux.tar.gz"},
            "macos": {"default": "https://example.com/universal.dmg"},
            "android": "https://example.com/app.apk",
            "ios": "https://example.com/ios-install"
        }
    })");
    check(installerUrl(manifest, "windows", "x86") == "https://example.com/x86.exe", "Windows arch overrides generic");
    check(installerUrl(manifest, "windows", "arm64") == "https://example.com/arm64.exe", "Windows ARM64");
    check(installerUrl(manifest, "linux", "x64") == "https://example.com/linux.tar.gz", "Linux");
    check(installerUrl(manifest, "macos", "arm64") == "https://example.com/universal.dmg", "Universal macOS");
    check(installerUrl(manifest, "android", "arm") == "https://example.com/app.apk", "Android");
    check(installerUrl(manifest, "ios", "arm64") == "https://example.com/ios-install", "iOS installation page");
    try { installerUrl(manifest, "linux", "arm64"); check(false, "Must reject missing Linux arch"); }
    catch (const std::runtime_error&) {}
    auto legacy = nlohmann::json::parse(R"({"installer_urls":{"x64":"https://example.com/old.exe"}})");
    check(installerUrl(legacy, "windows", "x64") == "https://example.com/old.exe", "Legacy Windows manifest");
    try { installerUrl(legacy, "android", "x64"); check(false, "Must never select Windows package for Android"); }
    catch (const std::runtime_error&) {}
    auto unsafe = nlohmann::json::parse(R"({"installer_urls":{"linux":"file:///tmp/update"}})");
    try { installerUrl(unsafe, "linux", "x64"); check(false, "Reject non-web downloads"); }
    catch (const std::runtime_error&) {}
    check(versionId(manifest) == "24", "Numeric build ID");
    check(versionId(nlohmann::json{{"version", "2.3"}}) == "2.3", "Version fallback");
    check(compareVersions("23", "24") < 0, "New build");
    check(compareVersions("24", "24") == 0, "Current build");
    check(compareVersions("25", "24") > 0, "Never downgrade");
    check(compareVersions("2.10", "2.9") > 0, "Numeric version comparison");
    check(compareVersions("2.3.0", "2.3") == 0, "Trailing zero");
    check(compareVersions("2.3-beta", "2.3") < 0, "Prerelease");
    check(compareVersions("999999999999999999999", "1000000000000000000000") < 0, "Large build IDs");
    return failures ? 1 : 0;
}
