#pragma once
#include <string>
#include <vector>
#include "TileMap.h"

struct ObjectInstance {
    std::string id;
    float x=0,y=0;
};

struct LevelMeta {
    std::string name;
    int levelId = 0;
    int themeOverride = 0;
    bool wrapX = false;
    bool wrapY = false;
    std::vector<int> entitySpawnPos;
    std::vector<int> entitySpawnType;
};

bool loadLevelBNNLVL(const std::string& path, TileMap& map, std::vector<ObjectInstance>& objects, LevelMeta& meta);
