#pragma once
#include "LevelLoader.h"
#include <string>

namespace LocalLevelEdits {
struct ResizePlan {
    std::vector<unsigned short> tiles;
    std::vector<ObjectInstance> objects;
    size_t croppedTiles = 0, croppedObjects = 0;
};
bool planResize(const TileMap& map, const std::vector<ObjectInstance>& objects, int width, int height, ResizePlan& plan);
bool renameFile(const std::string& path, const std::string& name, std::string& renamedPath, std::string& error);
bool replaceFile(const std::string& temporary, const std::string& target);
}
