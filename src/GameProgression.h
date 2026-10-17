#pragma once
#include <string>

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

GameProgression LoadGameProgression();
bool SaveGameProgression(const GameProgression& progression);
std::string HardmodeLevelPath(int level);
int HardmodeLevelFromPath(const std::string& path);
bool IsCampaignLevelPath(const std::string& path);
std::string HardmodeSavePath();
std::string HardmodeResumePath(const std::string& savedPath, bool failed);
std::string HardmodePracticeSelection(int level);
int HardmodePracticeLevel(const std::string& selection);
