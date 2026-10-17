#pragma once
#include <string>
#include <vector>
#include "TileMap.h"

struct ObjectInstance {
    std::string id;
    float x=0,y=0;
};

struct LevelMeta {
    int formatVersion = 0;
    std::string name;
    int levelId = 0;
    int themeOverride = 0;
    bool wrapX = false;
    bool wrapY = false;
    int world = 0; // Zero preserves the legacy/path-derived setting.
    int area = 0;
    int time = 0;
    bool bouncyTile12 = false;
    std::vector<int> entitySpawnPos;
    std::vector<int> entitySpawnType;
};

bool loadLevelBNNLVL(const std::string& path, TileMap& map, std::vector<ObjectInstance>& objects, LevelMeta& meta);
std::string SerializeLevelMetaHeader(const LevelMeta& meta);
