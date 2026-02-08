#pragma once

#include <string>
#include <vector>

#include "LevelLoader.h"
#include "Player.h"
#include "TileMap.h"

class LevelManager {
public:
    LevelManager();

    bool setLevelPath(const std::string& path);
    const std::string& levelPath() const;

    void reloadLevel(TileMap& map, std::vector<ObjectInstance>& objects, LevelMeta& meta, Player& player);
    std::string nextLevelPath() const;

    int collectCoinsAtPlayer(TileMap& map, const Player& player);
    void updateTimeWarpIdAtPlayer(const TileMap& map, const Player& player);

    int coinCount() const;
    void resetCoinCount();
    void addCoins(int amount);

    char timeWarpId() const;
    int worldId() const;
    int levelPartId() const;
    int timeId() const;
    std::string musicPath() const;
    bool hasLevelCode(int code) const;
    std::string levelPathByCode(int code) const;

private:
    static std::vector<int> loadLevelNumberList(const std::string& path);
    static int parseLevelIndexFromPath(const std::string& levelPath);
    static int nextLevelId(int currentLevelId);
    static std::string levelPathFromId(int levelId);
    static std::vector<char> loadBlockDefs(const std::string& path);

    void applyBlockDefAt(TileMap& map, int idx, unsigned short tileId) const;
    void updateLevelMetadata(const TileMap& map);

    std::string levelPath_;
    std::vector<int> levelNumberList_;
    std::vector<char> blockDefs_;
    int coinCount_ = 0;
    char timeWarpId_ = 'N';
    int worldId_ = 0;
    int levelPartId_ = 0;
    int timeId_ = 0;
};
