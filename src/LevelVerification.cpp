#include "LevelVerification.h"
#include "AssetPath.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

namespace {
bool localPath(const std::string& path) {
    if (path.empty()) return false;
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char ch) { return (char)std::tolower(ch); });
    return lower.rfind("http://", 0) != 0 && lower.rfind("https://", 0) != 0;
}

std::filesystem::path verificationPath(const std::string& levelPath) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(levelPath, ec);
    if (ec) absolute = std::filesystem::path(levelPath);
    auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    std::string key = (ec ? absolute.lexically_normal() : canonical).generic_string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char ch) { return (char)std::tolower(ch); });
#endif
    // A stable filename avoids collisions between identically named levels.
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : key) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    std::ostringstream filename;
    filename << std::hex << std::setfill('0') << std::setw(16) << hash << ".verify.json";
    return std::filesystem::path(GetAppSaveRootPath()) / "level_verification" / filename.str();
}

std::filesystem::path legacyPath(const std::string& levelPath) {
    return std::filesystem::path(levelPath + ".verify.json");
}

std::filesystem::path migrateVerification(const std::string& levelPath) {
    const auto target = verificationPath(levelPath);
    const auto legacy = legacyPath(levelPath);
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) return target;
    if (!std::filesystem::exists(legacy, ec)) return target;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return legacy;
    std::filesystem::rename(legacy, target, ec);
    if (!ec) return target;
    // Levels and app saves may be on different filesystems. Keep the source
    // until its copy succeeds, and retain legacy reads if migration cannot run.
    const std::filesystem::path tmp = target.string() + ".migrate.tmp";
    ec.clear();
    if (std::filesystem::copy_file(legacy, tmp, std::filesystem::copy_options::overwrite_existing, ec)) {
        std::filesystem::rename(tmp, target, ec);
        if (!ec) {
            std::filesystem::remove(legacy, ec);
            return target;
        }
    }
    std::filesystem::remove(tmp, ec);
    return legacy;
}
} // namespace

bool IsLocalLevelVerified(const std::string& levelPath) {
    if (!localPath(levelPath)) return false;
    std::ifstream in(migrateVerification(levelPath), std::ios::binary);
    if (!in.is_open()) return false;
    try {
        const auto j = nlohmann::json::parse(in);
        return j.is_object() && j.value("verified", false);
    } catch (...) {
        return false;
    }
}

void SetLocalLevelVerified(const std::string& levelPath, bool verified) {
    if (!localPath(levelPath)) return;
    const auto target = verificationPath(levelPath);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return;
    const nlohmann::json j = {
        {"verified", verified},
        {"verified_at", verified ? (long long)std::time(nullptr) : 0LL},
        {"level_path", levelPath}
    };
    const std::filesystem::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return;
        out << j.dump(2);
        out.flush();
        if (!out.good()) return;
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        ec.clear();
        std::filesystem::rename(tmp, target, ec);
    }
    if (!ec) std::filesystem::remove(legacyPath(levelPath), ec);
}

void ClearLocalLevelVerification(const std::string& levelPath) {
    if (!localPath(levelPath)) return;
    std::error_code ec;
    std::filesystem::remove(verificationPath(levelPath), ec);
    std::filesystem::remove(legacyPath(levelPath), ec);
}
