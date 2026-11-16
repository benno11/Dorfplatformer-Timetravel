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
        Achievements malformed;
        malformed.load({{"unlocked", {false, "invalid", "beat_boss"}}, {"button_areas", {"bad", -1, 999999999999ULL, 13, 61}}});
        require(malformed.has("beat_boss") && !malformed.has("invalid"), "validate persisted entries");
        require(malformed.button(6, 2) && malformed.has("buttons_world_6"), "validated progress");
        std::cout << "Achievement persistence, deduplication, button progress and toast queue passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
