#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>

inline constexpr int kHardmodeLevelCount = 20;
inline constexpr const char* kStartHardmodeToken = "__DF_HARDMODE_START__";
inline constexpr const char* kContinueHardmodeToken = "__DF_HARDMODE_CONTINUE__";

struct GameProgression {
    bool worldSelectUnlocked = false;
    bool hardmodeUnlocked = false;
    bool hardmodeComplete = false;
    int hardmodeFurthestLevel = 0;

    void recordEnding(bool goodEnding);
    void reachHardmodeLevel(int level);
    void completeHardmode();
    bool recordCompletion(const std::string& path, int world, int area, int time, bool practice = false);
};

GameProgression LoadGameProgression(int slotIndex = 0);
bool SaveGameProgression(const GameProgression& progression, int slotIndex = 0);
std::string GameSaveSlotPath(int slotIndex);
nlohmann::json LoadGameSnapshot(int slotIndex, bool hardmode = false);
bool SaveGameSnapshot(int slotIndex, bool hardmode, const nlohmann::json& snapshot);
bool HasGameSnapshot(int slotIndex, bool hardmode = false);
bool ClearHardmodeSnapshot(int slotIndex);
void MigrateLegacyGameProgression(int slotIndex);
std::string HardmodeLevelPath(int level);
int HardmodeLevelFromPath(const std::string& path);
bool IsCampaignLevelPath(const std::string& path);
std::string HardmodeResumePath(const std::string& savedPath, bool failed);
std::string HardmodePracticeSelection(int level);
int HardmodePracticeLevel(const std::string& selection);
