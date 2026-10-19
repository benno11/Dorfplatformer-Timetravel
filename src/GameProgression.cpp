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
bool GameProgression::recordCompletion(const std::string& path, int world, int area, int time, bool practice) {
    if (practice) return false;
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

namespace {
using nlohmann::json;
json readDocument(const std::filesystem::path& path) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return json::object();
        auto document = json::parse(in);
        return document.is_object() ? document : json::object();
    } catch (...) { return json::object(); }
}
GameProgression readProgression(const json& j) {
    GameProgression result;
    try {
        result.worldSelectUnlocked = j.value("world_select_unlocked", false);
        result.hardmodeUnlocked = j.value("hardmode_unlocked", false);
        result.hardmodeComplete = j.value("hardmode_complete", false);
        result.hardmodeFurthestLevel = std::clamp(j.value("hardmode_furthest_level", 0), 0, kHardmodeLevelCount);
        if (result.hardmodeComplete) result.completeHardmode();
        if (result.hardmodeUnlocked) result.worldSelectUnlocked = true;
    } catch (...) { return {}; }
    return result;
}
json progressionJson(const GameProgression& progression) {
    return {{"world_select_unlocked", progression.worldSelectUnlocked},
        {"hardmode_unlocked", progression.hardmodeUnlocked}, {"hardmode_complete", progression.hardmodeComplete},
        {"hardmode_furthest_level", progression.hardmodeFurthestLevel}};
}
bool writeDocument(int slotIndex, const json& document) {
    const std::filesystem::path path(GameSaveSlotPath(slotIndex));
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    const auto tmp = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << document.dump(2);
        out.flush();
        if (!out.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // Keep the old save available until the replacement has been fully written.
        const auto backup = std::filesystem::path(path.string() + ".bak");
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(path, backup, ec);
        if (ec) return false;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::error_code restoreError;
            std::filesystem::rename(backup, path, restoreError);
            return false;
        }
        std::filesystem::remove(backup, ec);
    }
    return true;
}
}
std::string GameSaveSlotPath(int slotIndex) {
    return (std::filesystem::path(GetAppSaveRootPath()) / "saves" /
        ("save_slot_" + std::to_string(std::clamp(slotIndex, 0, 2) + 1) + ".json")).string();
}
GameProgression LoadGameProgression(int slotIndex) {
    const auto document = readDocument(GameSaveSlotPath(slotIndex));
    return document.contains("progression") ? readProgression(document["progression"]) : GameProgression{};
}
bool SaveGameProgression(const GameProgression& progression, int slotIndex) {
    auto document = readDocument(GameSaveSlotPath(slotIndex));
    document["progression"] = progressionJson(progression);
    return writeDocument(slotIndex, document);
}
json LoadGameSnapshot(int slotIndex, bool hardmode) {
    auto document = readDocument(GameSaveSlotPath(slotIndex));
    if (hardmode) return document.value("hardmode_run", json::object());
    return document;
}
bool HasGameSnapshot(int slotIndex, bool hardmode) {
    try {
        const auto snapshot = LoadGameSnapshot(slotIndex, hardmode);
        if (!snapshot.is_object()) return false;
        const auto path = snapshot.value("level_path", std::string());
        return hardmode ? HardmodeLevelFromPath(path) > 0 : !path.empty();
    } catch (...) { return false; }
}
bool SaveGameSnapshot(int slotIndex, bool hardmode, const json& snapshot) {
    if (!snapshot.is_object()) return false;
    auto document = readDocument(GameSaveSlotPath(slotIndex));
    if (!document.contains("progression")) document["progression"] = progressionJson({});
    if (hardmode) document["hardmode_run"] = snapshot;
    else {
        auto campaign = snapshot;
        if (document.contains("progression")) campaign["progression"] = document["progression"];
        if (document.contains("hardmode_run")) campaign["hardmode_run"] = document["hardmode_run"];
        document = std::move(campaign);
    }
    return writeDocument(slotIndex, document);
}
bool ClearHardmodeSnapshot(int slotIndex) {
    auto document = readDocument(GameSaveSlotPath(slotIndex));
    document.erase("hardmode_run");
    return writeDocument(slotIndex, document);
}
void MigrateLegacyGameProgression(int slotIndex) {
    const auto root = std::filesystem::path(GetAppSaveRootPath());
    const auto legacyProgression = root / "progression.json";
    const auto legacyRun = root / "saves" / "hardmode_run.json";
    const auto oldProgression = readDocument(legacyProgression);
    const auto oldRun = readDocument(legacyRun);
    if (oldProgression.empty() && oldRun.empty()) return;
    auto document = readDocument(GameSaveSlotPath(slotIndex));
    if (!document.contains("progression") && !oldProgression.empty())
        document["progression"] = progressionJson(readProgression(oldProgression));
    if (!document.contains("hardmode_run") && oldRun.contains("level_path"))
        document["hardmode_run"] = oldRun;
    if (!writeDocument(slotIndex, document)) return;
    // Archive once, so another slot cannot inherit the same global unlocks.
    std::error_code ec;
    if (!oldProgression.empty()) std::filesystem::rename(legacyProgression, legacyProgression.string() + ".migrated", ec);
    if (!oldRun.empty()) std::filesystem::rename(legacyRun, legacyRun.string() + ".migrated", ec);
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
std::string HardmodeResumePath(const std::string& savedPath, bool failed) {
    const int level = HardmodeLevelFromPath(savedPath);
    return level > 0 ? HardmodeLevelPath(failed ? 1 : level) : std::string();
}
std::string HardmodePracticeSelection(int level) {
    return level >= 1 && level <= kHardmodeLevelCount
        ? "__DF_HARDMODE_PRACTICE__:" + std::to_string(level) : std::string();
}
int HardmodePracticeLevel(const std::string& selection) {
    for (int level = 1; level <= kHardmodeLevelCount; ++level)
        if (selection == HardmodePracticeSelection(level)) return level;
    return 0;
}
