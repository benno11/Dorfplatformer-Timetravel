#pragma once

#include "Platform.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace AppUpdate {
inline std::string platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__ANDROID__)
    return "android";
#elif PLATFORMER_IOS
    return "ios";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

inline std::string architecture() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_ARM) || defined(__arm__)
    return "arm";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

inline bool isWebUrl(const std::string& url) {
    return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

// A platform map must never fall back to another operating system's package.
inline std::string installerUrl(const nlohmann::json& manifest,
                                const std::string& os = platform(),
                                const std::string& arch = architecture()) {
    auto read = [](const nlohmann::json& value) -> std::string {
        return value.is_string() ? value.get<std::string>() : std::string();
    };
    std::string url;
    const auto urls = manifest.find("installer_urls");
    if (urls != manifest.end() && urls->is_object()) {
        const auto entry = urls->find(os);
        if (entry != urls->end()) {
            if (entry->is_object()) {
                url = read(entry->value(arch, nlohmann::json()));
                if (url.empty()) url = read(entry->value("default", nlohmann::json()));
            } else url = read(*entry);
        }
        // Original manifests contain Windows architecture keys directly.
        if (os == "windows" && entry == urls->end())
            url = read(urls->value(arch, nlohmann::json()));
    }
    if (url.empty() && os == "windows")
        url = read(manifest.value("installer_url", nlohmann::json()));
    if (url.empty()) throw std::runtime_error("No update download is available for " + os + "/" + arch + ".");
    if (!isWebUrl(url)) throw std::runtime_error("Update downloads must use an HTTP or HTTPS URL.");
    return url;
}

inline std::string versionId(const nlohmann::json& manifest) {
    const auto& value = manifest.contains("version_id") ? manifest.at("version_id") : manifest.at("version");
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return value.dump();
    throw std::runtime_error("Update version_id must be a string or integer.");
}

inline int compareVersions(std::string a, std::string b) {
    auto lower = [](std::string& s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    lower(a); lower(b);
    // Compare digit runs without integer conversion, including large build IDs.
    auto numbers = [](const std::string& s) {
        std::vector<std::string> out;
        for (size_t i = 0; i < s.size();) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; continue; }
            size_t end = i;
            while (end < s.size() && std::isdigit(static_cast<unsigned char>(s[end]))) ++end;
            auto n = s.substr(i, end - i);
            const auto first = n.find_first_not_of('0');
            out.push_back(first == std::string::npos ? "0" : n.substr(first));
            i = end;
        }
        return out;
    };
    auto an = numbers(a), bn = numbers(b);
    if (an.empty() && bn.empty()) return a.compare(b);
    an.resize(std::max(an.size(), bn.size()), "0");
    bn.resize(an.size(), "0");
    for (size_t i = 0; i < an.size(); ++i) {
        if (an[i].size() != bn[i].size()) return an[i].size() < bn[i].size() ? -1 : 1;
        if (an[i] != bn[i]) return an[i] < bn[i] ? -1 : 1;
    }
    auto rank = [](const std::string& s) {
        if (s.find("alpha") != std::string::npos) return 0;
        if (s.find("beta") != std::string::npos) return 1;
        if (s.find("rc") != std::string::npos) return 2;
        return 3;
    };
    return rank(a) - rank(b);
}
} // namespace AppUpdate
