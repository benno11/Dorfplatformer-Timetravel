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
};

bool loadLevelBNNLVL(const std::string& path, TileMap& map, std::vector<ObjectInstance>& objects, LevelMeta& meta);
