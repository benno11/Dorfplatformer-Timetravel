#include "GameProgression.h"
#include "SaveFormat.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
static fs::path saveRoot;
std::string GetAppSaveRootPath() { return saveRoot.string(); }
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    const auto temp = fs::temp_directory_path();
    saveRoot = temp / ("df-progression-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path root, temp;
        ~Cleanup() {
            if (root.parent_path() == temp && root.filename().string().rfind("df-progression-test-", 0) == 0) {
                std::error_code ec;
                fs::remove_all(root, ec);
            }
        }
    } cleanup{saveRoot, temp};
    try {
        auto progression = LoadGameProgression();
        require(!progression.worldSelectUnlocked && !progression.hardmodeUnlocked && !progression.hardmodeComplete,
            "New profiles must start locked");
        require(!progression.recordCompletion("custom_levels/final.txt", 7, 1, 4), "Custom levels must not unlock endings");
        require(!progression.recordCompletion("assets/levels/level_001.txt", 1, 1, 1), "Ordinary level completions are not endings");
        require(progression.recordCompletion("assets/levels/level_057.txt", 7, 1, 3), "Bad ending must be recognized");
        require(progression.worldSelectUnlocked && !progression.hardmodeUnlocked, "Any ending unlocks only World Select");
        require(SaveGameProgression(progression), "Save bad ending flags");
        progression = LoadGameProgression();
        require(progression.worldSelectUnlocked && !progression.hardmodeUnlocked, "Ending unlocks must persist");
        require(progression.recordCompletion("assets/levels/level_058.txt", 7, 1, 4), "Good ending must be recognized");
        require(progression.hardmodeUnlocked && !progression.hardmodeComplete, "Good ending unlocks Hardmode");
        progression.recordCompletion("assets/levels/level_057.txt", 7, 1, 3);
        require(progression.hardmodeUnlocked, "Later bad endings must not revoke Hardmode");
        GameProgression practice;
        for (int level = 1; level <= kHardmodeLevelCount; ++level) {
            const auto selection = HardmodePracticeSelection(level);
            require(HardmodePracticeLevel(selection) == level, "Every hardmode level must be selectable for practice");
            require(!practice.recordCompletion(HardmodeLevelPath(level), 7, 1, 4, true),
                "Practice completions must not award ending or hardmode flags");
        }
        require(!practice.worldSelectUnlocked && !practice.hardmodeUnlocked && !practice.hardmodeComplete &&
            practice.hardmodeFurthestLevel == 0, "Practice must leave progression unchanged");
        require(HardmodePracticeSelection(0).empty() && HardmodePracticeSelection(21).empty(),
            "Practice selection is limited to the twenty levels");
        require(HardmodePracticeLevel("__DF_HARDMODE_PRACTICE__:20junk") == 0 &&
            HardmodePracticeLevel(HardmodeLevelPath(20)) == 0, "Only explicit valid selections enable practice");
        for (int level = 1; level <= 20; ++level) {
            const auto path = HardmodeLevelPath(level);
            require(HardmodeLevelFromPath(path) == level, "Hardmode paths must round trip");
            require(HardmodeLevelFromPath((fs::current_path() / path).string()) == level, "Absolute hardmode paths must work");
            require(!IsCampaignLevelPath(path), "Hardmode is separate from the campaign");
            progression.reachHardmodeLevel(level);
            require(HardmodeResumePath(path, false) == path, "An unfinished run can resume its current level");
            require(HardmodeResumePath(path, true) == HardmodeLevelPath(1), "Every hardmode death resumes at level 1");
            progression.reachHardmodeLevel(1);
            require(progression.hardmodeFurthestLevel == level, "Restarting after death must retain the furthest level");
            const bool ending = progression.recordCompletion(path, 7, 1, 4);
            require(ending == (level == 20), "Only the twentieth hardmode clear is a completion");
            require(progression.hardmodeComplete == (level == 20), "Hardmode completion flag must wait for level 20");
        }
        require(SaveGameProgression(progression), "Save hardmode completion");
        progression = LoadGameProgression();
        require(progression.hardmodeComplete && progression.hardmodeFurthestLevel == 20 && progression.hardmodeUnlocked,
            "Hardmode completion and best progress must persist");
        require(HardmodeLevelPath(0).empty() && HardmodeLevelPath(21).empty(), "Hardmode is bounded to twenty levels");
        require(HardmodeLevelFromPath("custom_levels/level_01.txt") == 0, "Custom levels must not start hardmode");
        require(HardmodeLevelFromPath("https://example.test/assets/Hardmode levels/level_01.txt") == 0,
            "Remote levels must not start hardmode");
        require(fs::path(GameSaveSlotPath(2)).filename() == "save_slot_3.json", "Every slot has its own file");
        const nlohmann::json campaign = {{"resume_world", 4}, {"resume_level", 7},
            {"active_button_world_areas", {11, 12, 21, 42}}, {"player", {{"x", 900}}}, {"score_count", 123}};
        const nlohmann::json run = {{"level_path", HardmodeLevelPath(6)}, {"resume_world", 0}, {"resume_level", 6}};
        require(SaveGameSnapshot(0, false, campaign), "Save compact campaign restart point");
        require(fs::file_size(GameSaveSlotPath(0)) == 4, "Save has exactly the requested fields");
        require(HasGameSnapshot(0) && !HasGameSnapshot(0, true), "Campaign is the active mode");
        auto resumed = LoadGameSnapshot(0);
        require(resumed["resume_world"] == 4 && resumed["resume_level"] == 5 &&
            !resumed.contains("player") && !resumed.contains("score_count"), "Resume has only level-start data");
        require(SaveGameSnapshot(0, true, run) && HasGameSnapshot(0, true) && !HasGameSnapshot(0), "Hardmode restart point");
        require(LoadGameSnapshot(0, true)["level_path"] == HardmodeLevelPath(6), "Hardmode starts at the saved level");
        require(LoadGameSnapshot(0, true)["active_button_world_areas"] == campaign["active_button_world_areas"],
            "Hardmode must preserve campaign buttons");
        require(SaveGameSnapshot(0, false, campaign), "Campaign replaces active restart point");
        require(LoadGameProgression(0).hardmodeComplete, "Restart updates preserve completion flags");
        GameProgression otherSlot; otherSlot.recordEnding(false);
        require(SaveGameProgression(otherSlot, 1), "Save different flags in slot two");
        require(LoadGameProgression(1).worldSelectUnlocked && !LoadGameProgression(1).hardmodeUnlocked,
            "Slot unlocks remain isolated");
        fs::remove(GameSaveSlotPath(1));
        require(!LoadGameProgression(1).worldSelectUnlocked, "Deleting a slot deletes its flags");
        { std::ofstream out(GameSaveSlotPath(2));
          out << nlohmann::json{{"level_path", "assets/levels/level_050.txt"},
              {"active_button_world_areas", {11, 52}}, {"player", {{"x", 900}}},
              {"progression", {{"world_select_unlocked", true}}}}; }
        const auto legacy = LoadGameSnapshot(2);
        require(legacy["resume_world"] == 5 && legacy["resume_level"] == 12 && !legacy.contains("player"),
            "Legacy campaign resumes at level start");
        require(SaveGameProgression(LoadGameProgression(2), 2) && fs::file_size(GameSaveSlotPath(2)) == 4,
            "Legacy JSON upgrades to exact compact size");
        require(LoadGameButtonStates(2) == std::vector<int>({11, 52}) && LoadGameSnapshot(2)["resume_level"] == 11,
            "Migration retains buttons and resume level");
        fs::remove(GameSaveSlotPath(2));
        for (int oldVersion : {1, 2}) for (bool saveRestart : {false, true}) {
            std::string oldBytes("23333334"); oldBytes.push_back(oldVersion);
            if (oldVersion == 1) {
                for (const char mask : {1, 0, 0, 0, 2, 0}) oldBytes.push_back(mask);
                oldBytes.push_back(static_cast<char>(0xC2)); oldBytes.push_back(5);
            } else {
                oldBytes.push_back(1); oldBytes.push_back(static_cast<char>(0x52));
                oldBytes.push_back(static_cast<char>(0xC2));
            }
            { std::ofstream out(GameSaveSlotPath(2), std::ios::binary); out.write(oldBytes.data(), oldBytes.size()); }
            const auto flags = LoadGameProgression(2);
            require(flags.worldSelectUnlocked && LoadGameSnapshot(2)["resume_world"] == 5,
                "Older binary save loads before conversion");
            const bool converted = saveRestart ? SaveGameSnapshot(2, false, nlohmann::json::object()) :
                SaveGameProgression(flags, 2);
            require(converted && fs::file_size(GameSaveSlotPath(2)) == 4,
                "Every save entry point automatically converts older binary formats");
            { std::ifstream in(GameSaveSlotPath(2), std::ios::binary);
              require(in.get() == SaveFormat::version, "Converted file starts with current format number"); }
            require(LoadGameProgression(2).worldSelectUnlocked && LoadGameSnapshot(2)["resume_world"] == 5 &&
                LoadGameSnapshot(2)["resume_level"] == 11 && LoadGameButtonStates(2) == std::vector<int>({11, 52}),
                "Conversion preserves unlocks, buttons and area restart point");
            fs::remove(GameSaveSlotPath(2));
        }
        auto future = SaveFormat::encode(campaign); future[0] = 4;
        { std::ofstream out(GameSaveSlotPath(2), std::ios::binary); out.write(future.data(), future.size()); }
        require(!SaveGameProgression(progression, 2) && !SaveGameSnapshot(2, false, campaign), "Never overwrite future saves");
        { std::ifstream in(GameSaveSlotPath(2), std::ios::binary);
          require(std::string(std::istreambuf_iterator<char>(in), {}) == future, "Rejected writes preserve original bytes"); }
        fs::remove(GameSaveSlotPath(2));
        std::ofstream(GameSaveSlotPath(0)) << R"({"progression":{"hardmode_furthest_level":500}})";
        require(LoadGameProgression().hardmodeFurthestLevel == 20, "Loaded progress must remain bounded");
        std::ofstream(GameSaveSlotPath(0)) << "malformed";
        require(!LoadGameProgression().hardmodeUnlocked, "Malformed profiles must not grant unlocks");
        std::ofstream(saveRoot / "progression.json") << R"({"world_select_unlocked":true,"hardmode_unlocked":true})";
        std::ofstream(saveRoot / "saves" / "hardmode_run.json") << run;
        MigrateLegacyGameProgression(1);
        require(LoadGameProgression(1).hardmodeUnlocked && HasGameSnapshot(1, true),
            "Legacy global unlocks and run must migrate into the selected slot");
        MigrateLegacyGameProgression(2);
        require(!LoadGameProgression(2).worldSelectUnlocked && !HasGameSnapshot(2, true),
            "Migration must not duplicate global unlocks into other slots");
        require(fs::exists(saveRoot / "progression.json.migrated") &&
            fs::exists(saveRoot / "saves" / "hardmode_run.json.migrated"), "Legacy files must be archived");
        require(!HasGameSnapshot(1), "An active Hardmode restart must not offer campaign continue");
        std::cout << "Game progression tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
