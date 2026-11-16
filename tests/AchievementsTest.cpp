#include "Achievements.h"
#include <stdexcept>
#include <iostream>
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        Achievements a;
        require(a.unlock("complete_level"), "first unlock");
        require(!a.unlock("complete_level"), "duplicate unlock");
        require(!a.unlock("invalid"), "unknown achievement");
        require(a.button(1, 1), "partial button progress saved");
        require(!a.has("buttons_world_1"), "requires both areas");
        Achievements secondSession;
        secondSession.load(nlohmann::json::parse(a.save().dump()));
        require(secondSession.has("complete_level"), "global unlock survives restart");
        require(!secondSession.toast(), "loading does not replay toasts");
        require(!secondSession.unlock("complete_level"), "another save cannot unlock twice");
        require(secondSession.button(1, 2) && secondSession.has("buttons_world_1"), "button progress shared across sessions/saves");
        require(secondSession.button(6, 1) && !secondSession.has("buttons_world_6"), "world six partial progress");
        require(secondSession.button(6, 2) && secondSession.has("buttons_world_6"), "world six complete");
        require(!secondSession.button(7, 1) && !secondSession.button(1, 3), "invalid button areas");
        require(secondSession.toast() && *secondSession.toast() == "World 1 buttons", "queued toast order");
        for (int i = 0; i < 42; ++i) secondSession.tick(0.1f);
        require(secondSession.toast() && *secondSession.toast() == "World 6 buttons", "next toast gets own duration");
        secondSession.tick(1000.0f);
        require(secondSession.toast(), "suspend does not discard toast");
        const std::set<std::string> required{"level_001.txt:tile:1", "level_002.txt:box:2"};
        Achievements coins;
        require(!coins.campaignCoin("custom.txt:tile:1", required), "unknown coin source ignored");
        require(coins.campaignCoin(*required.begin(), required), "first source recorded");
        require(!coins.campaignCoin(*required.begin(), required), "replayed coins cannot inflate progress");
        require(!coins.has("all_campaign_coins"), "partial collection cannot unlock");
        Achievements resumedCoins;
        resumedCoins.load(nlohmann::json::parse(coins.save().dump()));
        require(!resumedCoins.toast(), "partial progress loads without toast");
        require(resumedCoins.campaignCoin(*required.rbegin(), required), "progress resumes across sessions");
        require(resumedCoins.has("all_campaign_coins"), "all unique sources unlock achievement");
        require(!resumedCoins.campaignCoin(*required.rbegin(), required), "completion only notifies once");
        require(!coins.campaignCoin("any", {}), "missing campaign catalog cannot unlock");
        const auto partialCoins = coins.requirements("all_campaign_coins", required);
        require(partialCoins.front().current == 1 && partialCoins.front().total == 2, "Coin requirement progress uses saved unique sources");
        const auto completeCoins = resumedCoins.requirements("all_campaign_coins", required);
        require(completeCoins.front().current == 2 && completeCoins.front().total == 2, "Complete coin requirement progress");
        const auto worldButtons = a.requirements("buttons_world_1");
        require(worldButtons.size() == 2 && worldButtons[0].current == 1 && worldButtons[1].current == 0, "Button requirements distinguish completed areas");
        require(a.requirements("complete_level").front().current == 1, "Completed requirement shown");
        require(a.requirements("good_ending").front().current == 0, "Incomplete requirement shown");
        require(a.requirementSummary("complete_level") == "Complete one level (1/1)", "Achievement row includes earned requirement progress");
        require(a.requirementSummary("good_ending").find("(0/1)") != std::string::npos, "Locked row includes incomplete progress");
        const auto buttonSummary = a.requirementSummary("buttons_world_1");
        require(buttonSummary.find("area 1") != std::string::npos && buttonSummary.find("area 2") != std::string::npos,
            "Button row includes both requirements");
        require(coins.requirementSummary("all_campaign_coins", required).find("(1/2)") != std::string::npos,
            "Coin row includes saved unique source progress");
        require(coins.requirementSummary("all_campaign_coins", required).find("level_001") == std::string::npos,
            "Coin summary does not include the entire per-level breakdown");
        Achievements malformed;
        malformed.load({{"unlocked", {false, "invalid", "beat_boss"}}, {"button_areas", {"bad", -1, 999999999999ULL, 13, 61}}});
        require(malformed.has("beat_boss") && !malformed.has("invalid"), "validate persisted entries");
        require(malformed.button(6, 2) && malformed.has("buttons_world_6"), "validated progress");
        std::cout << "Achievement persistence, deduplication, button progress and toast queue passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
