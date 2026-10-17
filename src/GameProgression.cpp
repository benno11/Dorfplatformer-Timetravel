#include "GameProgression.h"
#include "AssetPath.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

void GameProgression::recordEnding(bool goodEnding) {
    worldSelectUnlocked = true;
    hardmodeUnlocked = hardmodeUnlocked || goodEnding;
}
void GameProgression::reachHardmodeLevel(int level) {
    if (level >= 1 && level <= kHardmodeLevelCount)
        hardmodeFurthestLevel = std::max(hardmodeFurthestLevel, level);
}
void GameProgression::completeHardmode() {
    hardmodeComplete = worldSelectUnlocked = hardmodeUnlocked = true;
    hardmodeFurthestLevel = kHardmodeLevelCount;
}
bool GameProgression::recordCompletion(const std::string& path, int world, int area, int time) {
    if (HardmodeLevelFromPath(path) == kHardmodeLevelCount) {
        completeHardmode();
        return true;
    }
    if (IsCampaignLevelPath(path) && world == 7 && area == 1 && (time == 3 || time == 4)) {
        recordEnding(time == 4);
        return true;
    }
    return false;
}

GameProgression LoadGameProgression() {
    GameProgression result;
    try {
        std::ifstream in(std::filesystem::path(GetAppSaveRootPath()) / "progression.json");
        if (!in.is_open()) return result;
        const auto j = nlohmann::json::parse(in);
        result.worldSelectUnlocked = j.value("world_select_unlocked", false);
        result.hardmodeUnlocked = j.value("hardmode_unlocked", false);
        result.hardmodeComplete = j.value("hardmode_complete", false);
        result.hardmodeFurthestLevel = std::clamp(j.value("hardmode_furthest_level", 0), 0, kHardmodeLevelCount);
        if (result.hardmodeComplete) result.completeHardmode();
        if (result.hardmodeUnlocked) result.worldSelectUnlocked = true;
    } catch (...) { return {}; }
    return result;
}
bool SaveGameProgression(const GameProgression& progression) {
    const auto path = std::filesystem::path(GetAppSaveRootPath()) / "progression.json";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    const auto tmp = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        const nlohmann::json j = {{"world_select_unlocked", progression.worldSelectUnlocked},
            {"hardmode_unlocked", progression.hardmodeUnlocked}, {"hardmode_complete", progression.hardmodeComplete},
            {"hardmode_furthest_level", progression.hardmodeFurthestLevel}};
        out << j.dump(2);
        out.flush();
        if (!out.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}
std::string HardmodeLevelPath(int level) {
    if (level < 1 || level > kHardmodeLevelCount) return {};
    std::ostringstream path;
    path << "assets/Hardmode levels/level_" << std::setw(2) << std::setfill('0') << level << ".txt";
    return path.str();
}
namespace {
bool inAssetFolder(const std::string& path, const std::string& folder) {
    if (path.find("://") != std::string::npos) return false;
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const std::string parent = std::filesystem::path(normalized).lexically_normal().parent_path().generic_string();
    const std::string expected = "assets/" + folder;
    return parent == expected || (parent.size() > expected.size() &&
        parent.compare(parent.size() - expected.size() - 1, expected.size() + 1, "/" + expected) == 0);
}
}
int HardmodeLevelFromPath(const std::string& path) {
    if (!inAssetFolder(path, "Hardmode levels")) return 0;
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const auto filename = std::filesystem::path(normalized).filename();
    for (int level = 1; level <= kHardmodeLevelCount; ++level)
        if (filename == std::filesystem::path(HardmodeLevelPath(level)).filename()) return level;
    return 0;
}
bool IsCampaignLevelPath(const std::string& path) { return inAssetFolder(path, "levels"); }
std::string HardmodeSavePath() {
    return (std::filesystem::path(GetAppSaveRootPath()) / "saves" / "hardmode_run.json").string();
}
std::string HardmodeResumePath(const std::string& savedPath, bool failed) {
    const int level = HardmodeLevelFromPath(savedPath);
    return level > 0 ? HardmodeLevelPath(failed ? 1 : level) : std::string();
}
