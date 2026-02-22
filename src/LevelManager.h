#pragma once

#include <string>
#include <unordered_set>
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

    int collectCoinsAtPlayer(TileMap& map, const Player& player, bool wrapX = false, bool wrapY = false);
    int activateButtonsAtPlayer(TileMap& map, const Player& player, bool wrapX = false, bool wrapY = false);
    void updateTimeWarpIdAtPlayer(const TileMap& map, const Player& player, bool wrapX = false, bool wrapY = false);
    void setTileAt(TileMap& map, int idx, unsigned short tileId) const;

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
    std::string levelPathByWorldAreaTime(int world, int area, int time) const;
    std::vector<int> activeButtonWorldAreas() const;
    void setButtonAreaActiveForDebug(int world, int area, bool active);
    void clearButtonAreasForDebug();

private:
    static std::vector<int> loadLevelNumberList(const std::string& path);
    static int parseLevelIndexFromPath(const std::string& levelPath);
    static int nextLevelId(int currentLevelId);
    static std::string levelPathFromId(int levelId);
    static std::vector<char> loadBlockDefs(const std::string& path);
    bool isButtonAreaActive(int worldId, int areaId) const;
    bool areButtonsActiveUpToArea(int worldId, int areaId) const;

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
    std::unordered_set<int> activeButtonWorldAreas_;
    std::unordered_set<int> buttonWorldAreasWithButtons_;
};

