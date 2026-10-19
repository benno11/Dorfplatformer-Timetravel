#include "GameProgression.h"
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
        const nlohmann::json campaign = {{"level_path", "assets/levels/level_010.txt"}, {"score_count", 123}};
        const nlohmann::json run = {{"level_path", HardmodeLevelPath(6)}, {"hardmode_failed", false}};
        require(SaveGameSnapshot(0, false, campaign), "Save campaign in slot one");
        require(SaveGameSnapshot(0, true, run), "Save hardmode in the same slot");
        require(HasGameSnapshot(0) && HasGameSnapshot(0, true), "One slot must retain both modes");
        require(LoadGameProgression(0).hardmodeComplete, "Snapshot saves must preserve unlocks");
        require(LoadGameSnapshot(0).value("score_count", 0) == 123, "Hardmode must preserve the campaign snapshot");
        require(LoadGameSnapshot(0, true).value("level_path", std::string()) == HardmodeLevelPath(6),
            "Hardmode must resume from its slot's run");
        require(SaveGameSnapshot(0, false, campaign), "Resave campaign");
        require(HasGameSnapshot(0, true), "Campaign saves must preserve the hardmode run");
        require(!LoadGameProgression(1).worldSelectUnlocked && !HasGameSnapshot(1, true),
            "Fresh slots must not inherit another slot's unlocks or run");
        GameProgression otherSlot;
        otherSlot.recordEnding(false);
        require(SaveGameProgression(otherSlot, 1), "Save a different ending in slot two");
        require(LoadGameProgression(1).worldSelectUnlocked && !LoadGameProgression(1).hardmodeUnlocked,
            "Unlocks must belong to the slot that earned them");
        require(SaveGameProgression(progression, 0), "Resave slot one's unlocks");
        require(HasGameSnapshot(0) && HasGameSnapshot(0, true), "Unlock updates must preserve both snapshots");
        require(ClearHardmodeSnapshot(0) && !HasGameSnapshot(0, true) && HasGameSnapshot(0),
            "Completing hardmode removes only that slot's run");
        require(LoadGameProgression(0).hardmodeComplete, "Completing a run must retain earned unlocks");
        fs::remove(GameSaveSlotPath(1));
        require(!LoadGameProgression(1).worldSelectUnlocked && !HasGameSnapshot(1),
            "Deleting a slot must also delete its unlocks");
        require(LoadGameProgression(0).hardmodeComplete, "Deleting another slot must preserve progression");
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
        require(!HasGameSnapshot(1), "A slot with only unlocks and hardmode must not offer campaign continue");
        std::cout << "Game progression tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
