#include "EditionPolicy.h"
#include "GameProgression.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iostream>

static std::filesystem::path saveRoot;
std::string GetAppSaveRootPath() { return saveRoot.string(); }
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    saveRoot = std::filesystem::temp_directory_path() /
        ("df-edition-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { ~Cleanup() { std::filesystem::remove_all(saveRoot); } } cleanup;
    require(Edition::scoresEnabled == !Edition::lite, "Lite must disable scoring");
    int worlds = 0;
    for (int world = 1; world <= 7; ++world) worlds += Edition::worldSelectable(world);
    require(worlds == (Edition::lite ? 3 : 6), "Wrong World Select contents");
    require(Edition::worldSelectable(1) && Edition::worldSelectable(5), "Available campaign worlds must be selectable");
    require(!Edition::worldSelectable(7), "World 7 must be hidden and rejected in every edition");
    require(!Edition::worldSelectable(0) && !Edition::worldSelectable(8), "Invalid worlds must be rejected");
    for (const auto* menu : {"settingsaccount", "mainedit", "onlinelevels", "locallevels", "hardmode", "charactercreator"})
        require(Edition::menuAllowed(menu) == !Edition::lite, "Restricted menu policy mismatch");
    for (const auto* command : {"account_login", "upload_level", "custom", "create_level", "hardmode", "hardmode_practice", "get"})
        require(Edition::commandAllowed(command, "") == !Edition::lite, "Restricted command policy mismatch");
    require(Edition::commandAllowed("toggle", "show_experimental_features") == !Edition::lite, "Experiments must stay disabled");
    require(Edition::commandAllowed("toggle", "level_select_enabled") == !Edition::lite, "Lite mode must be forced");
    require(Edition::menuAllowed("future_menu") == !Edition::lite, "Future menus need Lite opt-in");
    require(Edition::commandAllowed("future_command", "") == !Edition::lite, "Future commands need Lite opt-in");
    require(Edition::commandAllowed("toggle", "fullscreen"), "Display settings must remain usable");
    auto progression = LoadGameProgression();
    require(progression.hardmodeUnlocked == Edition::lite, "Lite must retain an active Hardmode flag");
    require(SaveGameProgression({}, 0), "Cannot save progression");
    require(LoadGameProgression().hardmodeUnlocked == Edition::lite, "Hardmode flag must survive saves");
    const nlohmann::json snapshot{{"level_path", "assets/levels/level_001.txt"}, {"score_count", 1234}};
    require(SaveGameSnapshot(0, false, snapshot) && HasGameSnapshot(0), "Campaign saves must work");
    require(SaveGameSnapshot(0, true, {{"level_path", HardmodeLevelPath(1)}}) == !Edition::lite,
        "Lite must reject Hardmode snapshots");
    require(HasGameSnapshot(0, true) == !Edition::lite, "Lite must not load Hardmode runs");
    require(std::filesystem::path(GameSaveSlotPath(0)).parent_path().filename() == (Edition::lite ? "lite-saves" : "saves"),
        "Edition saves must be isolated");
    std::ifstream in(GameSaveSlotPath(0));
    auto persisted = nlohmann::json::parse(in);
    require(persisted["progression"]["hardmode_unlocked"] == Edition::lite, "Persisted unlock flag is incorrect");
    require(persisted.contains("score_count") == Edition::scoresEnabled,
        "Lite must never persist a score field");
    require(LoadGameSnapshot(0).contains("score_count") == Edition::scoresEnabled,
        "Lite must never restore score state");
    if (Edition::scoresEnabled) require(persisted["score_count"] == 1234, "Full saves must retain score state");
    if (!Edition::scoresEnabled) {
        persisted["score_count"] = 9876; // A save from an older Lite build.
        { std::ofstream out(GameSaveSlotPath(0)); out << persisted; }
        require(!LoadGameSnapshot(0).contains("score_count"), "Legacy Lite scores must be ignored");
        require(SaveGameProgression(progression), "Cannot update Lite progression");
        std::ifstream migrated(GameSaveSlotPath(0));
        require(!nlohmann::json::parse(migrated).contains("score_count"), "Legacy Lite scores must be removed on save");
    }
    std::cout << (Edition::lite ? "Lite" : "Full") << " edition policy tests passed\n";
}
