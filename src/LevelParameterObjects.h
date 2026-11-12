#pragma once

#include <array>
#include <limits>
#include <string>
#include <cmath>
#include "LevelLoader.h"
#include "Player.h"

namespace LevelParameters {
struct Definition { int id; const char* key; const char* label; int minimum; int maximum; int initial; };
inline constexpr std::array<Definition, 8> definitions{{
    {1100, "level_id", "Set level ID", 0, std::numeric_limits<int>::max(), 0},
    {1101, "theme_override", "Set theme", 0, 7, 0},
    {1102, "wrap_x", "Set horizontal wrap", 0, 1, 1},
    {1103, "wrap_y", "Set vertical wrap", 0, 1, 1},
    {1104, "world", "Set world", 0, 7, 1},
    {1105, "area", "Set area", 0, 3, 1},
    {1106, "time", "Set time period", 0, 4, 1},
    {1107, "bouncy_tile_12", "Set bouncy tile 12", 0, 1, 1}
}};
inline const Definition* definition(int id) {
    return id >= 1100 && id <= 1107 ? &definitions[id - 1100] : nullptr;
}
inline const Definition* definition(const ObjectInstance& obj) {
    if (obj.id.size() != 4 || obj.id.compare(0, 3, "110") != 0 || obj.id[3] < '0' || obj.id[3] > '7') return nullptr;
    return &definitions[obj.id[3] - '0'];
}
inline bool valid(const ObjectInstance& obj) {
    const auto* d = definition(obj);
    return d && obj.parameterValue && *obj.parameterValue >= d->minimum && *obj.parameterValue <= d->maximum;
}
inline bool apply(const ObjectInstance& obj, LevelMeta& meta) {
    if (!valid(obj)) return false;
    const int value = *obj.parameterValue;
    switch (definition(obj)->id) {
    case 1100: meta.levelId = value; break;
    case 1101: meta.themeOverride = value; break;
    case 1102: meta.wrapX = value != 0; break;
    case 1103: meta.wrapY = value != 0; break;
    case 1104: meta.world = value; break;
    case 1105: meta.area = value; break;
    case 1106: meta.time = value; break;
    case 1107: meta.bouncyTile12 = value != 0; break;
    }
    return true;
}
// Re-entry activates again. List order determines the result of overlapping setters.
inline std::vector<int> update(const Player& player, const TileMap& map,
    const std::vector<ObjectInstance>& objects, std::vector<bool>& contacts,
    LevelMeta& meta, bool wrapX, bool wrapY) {
    contacts.resize(objects.size(), false);
    std::vector<int> activated;
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto& obj = objects[i];
        if (!valid(obj)) { contacts[i] = false; continue; }
        float dx = obj.x - (player.x + player.w * 0.5f);
        float dy = obj.y - (player.y + player.h * 0.5f);
        if (wrapX && map.w > 0) dx = std::remainder(dx, (float)(map.w * map.tileSize));
        if (wrapY && map.h > 0) dy = std::remainder(dy, (float)(map.h * map.tileSize));
        const bool touching = std::abs(dx) < player.w * 0.5f + 16 && std::abs(dy) < player.h * 0.5f + 16;
        if (touching && !contacts[i] && apply(obj, meta)) activated.push_back(definition(obj)->id);
        contacts[i] = touching;
    }
    return activated;
}
}
