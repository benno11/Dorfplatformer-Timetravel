#pragma once
#include "LevelLoader.h"
#include "ObjectIds.h"
#include <set>
#include <string>
#include <cstdio>
#include <cmath>

namespace CampaignCoins {
inline std::string pickup(float x, float y) {
    return "pickup:" + std::to_string(x) + ":" + std::to_string(y);
}
inline std::string tile(int index, bool box) {
    return std::string(box ? "box:" : "tile:") + std::to_string(index);
}
// Use original packaged maps, never running maps with consumed pickups.
// Missing campaign data disables completion instead of awarding it early.
inline std::set<std::string> catalog() {
    std::set<std::string> result;
    for (int level = 1; level <= 59; ++level) {
        char name[32]; std::snprintf(name, sizeof(name), "level_%03d.txt", level);
        TileMap map; std::vector<ObjectInstance> objects; LevelMeta meta;
        if (!loadLevelBNNLVL(std::string("assets/levels/") + name, map, objects, meta)) return {};
        for (int index = 0; index < (int)map.tileIds.size(); ++index) {
            const int id = map.tileIds[index];
            if (id != 24 && id != 20) continue;
            bool shield = false;
            if (id == 20) for (const auto& object : objects)
                if (object.id == std::to_string(ObjectIds::Shield) &&
                    (int)std::floor(object.x / map.tileSize) == index % map.w &&
                    (int)std::floor(object.y / map.tileSize) == index / map.w) shield = true;
            if (!shield) result.insert(std::string(name) + ":" + tile(index, id == 20));
        }
        for (const auto& object : objects)
            if (object.id == std::to_string(ObjectIds::Pickup))
                result.insert(std::string(name) + ":" + pickup(object.x, object.y));
    }
    return result;
}
}
