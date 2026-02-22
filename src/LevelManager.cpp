#include "LevelManager.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "AssetPath.h"
#include "PlayerController.h"

LevelManager::LevelManager()
    : levelNumberList_(loadLevelNumberList("assets/Level Numer List.txt")),
      blockDefs_(loadBlockDefs("assets/blockdefined.txt")) {}

bool LevelManager::setLevelPath(const std::string& path) {
    if (path.empty()) return false;
    levelPath_ = path;
    return true;
}

const std::string& LevelManager::levelPath() const {
    return levelPath_;
}

void LevelManager::reloadLevel(TileMap& map, std::vector<ObjectInstance>& objects, LevelMeta& meta, Player& player) {
    loadLevelBNNLVL(levelPath_, map, objects, meta);
    updateLevelMetadata(map);
    coinCount_ = 0;
    timeWarpId_ = 'N';
    const bool disableEndSignsForArea = (worldId_ >= 1 && worldId_ <= 5 && levelPartId_ == 3);

    if (disableEndSignsForArea) {
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const ObjectInstance& o) {
            return o.id == "67";
        }), objects.end());
    }

    bool hasEndSignObject = false;
    for (const auto& o : objects) {
        if (o.id == "67") {
            hasEndSignObject = true;
            break;
        }
    }
    for (int idx = 0; idx < (int)map.tileIds.size(); ++idx) {
        if (map.tileIds[idx] != 30) continue;
        const int tx = idx % map.w;
        const int ty = idx / map.w;
        setTileAt(map, idx, 2);
        if (!disableEndSignsForArea && !hasEndSignObject) {
            ObjectInstance endSign{};
            endSign.id = "67";
            endSign.x = tx * map.tileSize + map.tileSize * 0.5f;
            endSign.y = ty * map.tileSize + map.tileSize * 0.5f;
            objects.push_back(endSign);
        }
    }

    const int worldAreaKey = worldId_ * 10 + levelPartId_;
    bool hasButtonsInArea = false;
    bool hasInactiveButtonsInArea = false;
    for (int idx = 0; idx < (int)map.tileIds.size(); ++idx) {
        const unsigned short id = map.tileIds[idx];
        if (id == 66 || id == 67) {
            hasButtonsInArea = true;
            if (id == 66) hasInactiveButtonsInArea = true;
        }
    }
    if (hasButtonsInArea && worldId_ > 0 && levelPartId_ > 0) {
        buttonWorldAreasWithButtons_.insert(worldAreaKey);
        if (!hasInactiveButtonsInArea) {
            activeButtonWorldAreas_.insert(worldAreaKey);
        }
    }
    if (activeButtonWorldAreas_.find(worldAreaKey) != activeButtonWorldAreas_.end()) {
        for (int idx = 0; idx < (int)map.tileIds.size(); ++idx) {
            if (map.tileIds[idx] != 66) continue;
            map.tileIds[idx] = 67;
            applyBlockDefAt(map, idx, 67);
        }
    }

    player = Player{};
    bool foundSpawn = false;
    for (const auto& o : objects) {
        if (o.id == "player") {
            player.x = o.x;
            player.y = o.y - 12.0f;
            foundSpawn = true;
            break;
        }
    }

    for (int idx = 0; idx < (int)map.tileIds.size(); ++idx) {
        if (map.tileIds[idx] != 28) continue;
        int tx = idx % map.w;
        int ty = idx / map.w;
        player.x = tx * map.tileSize;
        player.y = ty * map.tileSize;
        foundSpawn = true;
        map.tileIds[idx] = 2;
        applyBlockDefAt(map, idx, 2);
        break;
    }

    if (!foundSpawn) {
        player.x = 3.0f * map.tileSize;
        player.y = (float)(map.h * map.tileSize - player.h) - 3.0f * map.tileSize;
    }

    // Defensive spawn sanitization: malformed level spawn coordinates can place
    // the player outside map bounds and trigger instant death on first update.
    if (map.w > 0 && map.h > 0 && map.tileSize > 0) {
        const float mapWidthPx = (float)(map.w * map.tileSize);
        const float mapHeightPx = (float)(map.h * map.tileSize);
        const float minSpawnX = -(float)player.w;
        const float maxSpawnX = std::max(minSpawnX, mapWidthPx - (float)player.w);
        const float minSpawnY = -(float)map.tileSize * 2.0f;
        const float maxSpawnY = std::max(minSpawnY, mapHeightPx - (float)player.h);
        player.x = std::clamp(player.x, minSpawnX, maxSpawnX);
        player.y = std::clamp(player.y, minSpawnY, maxSpawnY);
    }

    if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
        bool resolved = false;
        const float mapHeightPx = (float)(map.h * map.tileSize);
        const float minSpawnY = -(float)map.tileSize * 2.0f;
        const float maxSpawnY = std::max(minSpawnY, mapHeightPx - (float)player.h);
        const int maxSteps = std::max(1, map.h * map.tileSize);
        for (int step = 1; step <= maxSteps; ++step) {
            float upY = player.y - (float)step;
            if (upY >= minSpawnY && !RectHitsSolid(map, player.x, upY, player.w, player.h)) {
                player.y = upY;
                resolved = true;
                break;
            }
            float downY = player.y + (float)step;
            if (downY <= maxSpawnY && !RectHitsSolid(map, player.x, downY, player.w, player.h)) {
                player.y = downY;
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            player.vx = 0.0f;
            player.vy = 0.0f;
        }
        // Keep final spawn valid even if collision resolution couldn't find a free tile.
        player.y = std::clamp(player.y, minSpawnY, maxSpawnY);
    }

    // Metadata already updated at reload start.
}

std::string LevelManager::nextLevelPath() const {
    int currentLevel = parseLevelIndexFromPath(levelPath_);
    int nextId = nextLevelId(currentLevel);
    if (nextId <= 0 || nextId > (int)levelNumberList_.size()) return "";

    // Non-world-6 route tweak: leaving area 2 with both area-1 and area-2 buttons active
    // advances to the +1 variant for the same target world/area (typically time 3 -> 4).
    if (worldId_ != 6 && levelPartId_ == 2) {
        const bool area1Active = isButtonAreaActive(worldId_, 1);
        const bool area2Active = isButtonAreaActive(worldId_, 2);
        if (area1Active && area2Active) {
            const int altId = nextId + 1;
            if (altId > 0 && altId <= (int)levelNumberList_.size()) {
                const int baseCode = levelNumberList_[nextId - 1];
                const int altCode = levelNumberList_[altId - 1];
                if ((baseCode / 10) == (altCode / 10)) {
                    nextId = altId;
                }
            }
        }
    }

    int nextCode = levelNumberList_[nextId - 1];
    int world = nextCode / 100;
    int area = (nextCode / 10) % 10;
    int time = nextCode % 10;

    // World 6 route tweak: when moving into/through world 6 and all buttons
    // completed so far are active, advance to the +1 variant.
    if (world == 6) {
        const int requiredArea = (worldId_ == 6) ? levelPartId_ : 0;
        const bool allButtonsSoFarActive = (requiredArea <= 0) ? true : areButtonsActiveUpToArea(6, requiredArea);
        if (allButtonsSoFarActive) {
            const int altId = nextId + 1;
            if (altId > 0 && altId <= (int)levelNumberList_.size()) {
                const int altCode = levelNumberList_[altId - 1];
                if ((nextCode / 10) == (altCode / 10)) {
                    nextId = altId;
                    nextCode = altCode;
                    world = nextCode / 100;
                    area = (nextCode / 10) % 10;
                    time = nextCode % 10;
                }
            }
        }
    }

    return levelPathByWorldAreaTime(world, area, time);
}

int LevelManager::collectCoinsAtPlayer(TileMap& map, const Player& player, bool wrapX, bool wrapY) {
    int t = map.tileSize;
    int left = (int)std::floor(player.x / t);
    int right = (int)std::floor((player.x + player.w - 1) / t);
    int top = (int)std::floor(player.y / t);
    int bottom = (int)std::floor((player.y + player.h - 1) / t);

    int collected = 0;
    for (int ty = top; ty <= bottom; ++ty) {
        int qy = ty;
        if (wrapY && map.h > 0) {
            qy %= map.h;
            if (qy < 0) qy += map.h;
        } else if (ty < 0 || ty >= map.h) {
            continue;
        }
        for (int tx = left; tx <= right; ++tx) {
            int qx = tx;
            if (wrapX && map.w > 0) {
                qx %= map.w;
                if (qx < 0) qx += map.w;
            } else if (tx < 0 || tx >= map.w) {
                continue;
            }
            int idx = qy * map.w + qx;
            if (map.tileIds[idx] != 24) continue;
            map.tileIds[idx] = 2;
            applyBlockDefAt(map, idx, 2);
            collected++;
        }
    }

    coinCount_ += collected;
    return collected;
}

int LevelManager::activateButtonsAtPlayer(TileMap& map, const Player& player, bool wrapX, bool wrapY) {
    const int worldAreaKey = worldId_ * 10 + levelPartId_;

    int t = map.tileSize;
    int left = (int)std::floor(player.x / t);
    int right = (int)std::floor((player.x + player.w - 1) / t);
    int top = (int)std::floor(player.y / t);
    int bottom = (int)std::floor((player.y + player.h - 1) / t);

    int activated = 0;
    for (int ty = top; ty <= bottom; ++ty) {
        int qy = ty;
        if (wrapY && map.h > 0) {
            qy %= map.h;
            if (qy < 0) qy += map.h;
        } else if (ty < 0 || ty >= map.h) {
            continue;
        }
        for (int tx = left; tx <= right; ++tx) {
            int qx = tx;
            if (wrapX && map.w > 0) {
                qx %= map.w;
                if (qx < 0) qx += map.w;
            } else if (tx < 0 || tx >= map.w) {
                continue;
            }
            const int idx = qy * map.w + qx;
            if (map.tileIds[idx] != 66) continue;
            map.tileIds[idx] = 67;
            applyBlockDefAt(map, idx, 67);
            activated++;
        }
    }
    if (activated > 0 && worldId_ > 0 && levelPartId_ > 0) {
        buttonWorldAreasWithButtons_.insert(worldAreaKey);
        bool hasRemainingInactiveButton = false;
        for (int i = 0; i < (int)map.tileIds.size(); ++i) {
            if (map.tileIds[i] == 66) {
                hasRemainingInactiveButton = true;
                break;
            }
        }
        if (!hasRemainingInactiveButton) {
            activeButtonWorldAreas_.insert(worldAreaKey);
        }
    }
    return activated;
}

void LevelManager::updateTimeWarpIdAtPlayer(const TileMap& map, const Player& player, bool wrapX, bool wrapY) {
    int t = map.tileSize;
    int left = (int)std::floor(player.x / t);
    int right = (int)std::floor((player.x + player.w - 1) / t);
    int top = (int)std::floor(player.y / t);
    int bottom = (int)std::floor((player.y + player.h - 1) / t);

    char next = timeWarpId_;
    for (int ty = top; ty <= bottom; ++ty) {
        int qy = ty;
        if (wrapY && map.h > 0) {
            qy %= map.h;
            if (qy < 0) qy += map.h;
        } else if (ty < 0 || ty >= map.h) {
            continue;
        }
        for (int tx = left; tx <= right; ++tx) {
            int qx = tx;
            if (wrapX && map.w > 0) {
                qx %= map.w;
                if (qx < 0) qx += map.w;
            } else if (tx < 0 || tx >= map.w) {
                continue;
            }
            unsigned short id = map.tileIds[qy * map.w + qx];
            if (id == 43) next = '1';
            if (id == 45) next = '2';
        }
    }
    if (next != timeWarpId_) {
        SDL_Log("timeWarpId changed: %c -> %c (%s)", timeWarpId_, next, levelPath_.c_str());
        timeWarpId_ = next;
    }
}

void LevelManager::setTileAt(TileMap& map, int idx, unsigned short tileId) const {
    if (idx < 0 || idx >= (int)map.tileIds.size()) return;
    map.tileIds[idx] = tileId;
    applyBlockDefAt(map, idx, tileId);
}

int LevelManager::coinCount() const {
    return coinCount_;
}

void LevelManager::resetCoinCount() {
    coinCount_ = 0;
}

void LevelManager::addCoins(int amount) {
    coinCount_ += amount;
}

char LevelManager::timeWarpId() const {
    return timeWarpId_;
}

int LevelManager::worldId() const {
    return worldId_;
}

int LevelManager::levelPartId() const {
    return levelPartId_;
}

int LevelManager::timeId() const {
    return timeId_;
}

std::string LevelManager::musicPath() const {
    if (worldId_ <= 0) return "";
    if (worldId_ >= 6 && worldId_ <= 7) {
        return "assets/Audio/Music/" + std::to_string(worldId_) + "." + std::to_string(levelPartId_) + ".mp3";
    }
    return "assets/Audio/Music/" + std::to_string(worldId_) + "." + std::to_string(timeId_) + ".mp3";
}

bool LevelManager::hasLevelCode(int code) const {
    for (int i = 0; i < (int)levelNumberList_.size(); ++i) {
        if (levelNumberList_[i] != code) continue;
        std::string p = levelPathFromId(i + 1);
        if (!p.empty()) return true;
    }
    return false;
}

std::string LevelManager::levelPathByCode(int code) const {
    for (int i = 0; i < (int)levelNumberList_.size(); ++i) {
        if (levelNumberList_[i] != code) continue;
        std::string p = levelPathFromId(i + 1);
        if (!p.empty()) return p;
    }
    return "";
}

std::string LevelManager::levelPathByWorldAreaTime(int world, int area, int time) const {
    if (world <= 0 || area <= 0 || time <= 0) return "";
    int routedTime = time;
    const bool areaButtonActive = isButtonAreaActive(world, area);
    if (time == 3 && areaButtonActive) {
        routedTime = 4;
    }

    std::string path = levelPathByCode(world * 100 + area * 10 + routedTime);
    if (path.empty() && routedTime != time) {
        path = levelPathByCode(world * 100 + area * 10 + time);
    }
    return path;
}

std::vector<int> LevelManager::activeButtonWorldAreas() const {
    std::vector<int> out;
    out.reserve(activeButtonWorldAreas_.size());
    for (int key : activeButtonWorldAreas_) out.push_back(key);
    std::sort(out.begin(), out.end());
    return out;
}

void LevelManager::setButtonAreaActiveForDebug(int world, int area, bool active) {
    if (world <= 0 || area <= 0) return;
    const int key = world * 10 + area;
    if (active) {
        buttonWorldAreasWithButtons_.insert(key);
        activeButtonWorldAreas_.insert(key);
    } else {
        activeButtonWorldAreas_.erase(key);
    }
}

void LevelManager::clearButtonAreasForDebug() {
    activeButtonWorldAreas_.clear();
}

std::vector<int> LevelManager::loadLevelNumberList(const std::string& path) {
    const std::string text = ReadTextFile(path);
    if (text.empty()) return {};
    std::istringstream f(text);
    std::vector<int> out;
    std::string line;
    while (std::getline(f, line)) {
        std::string digits;
        for (char ch : line) {
            if (std::isdigit((unsigned char)ch)) digits.push_back(ch);
        }
        if (digits.size() < 3) continue;
        int v = 0;
        try { v = std::stoi(digits.substr(0, 3)); } catch (...) { continue; }
        out.push_back(v);
    }
    return out;
}

int LevelManager::parseLevelIndexFromPath(const std::string& levelPath) {
    const std::string name = std::filesystem::path(levelPath).stem().string();
    std::string digits;
    for (char ch : name) {
        if (std::isdigit((unsigned char)ch)) digits.push_back(ch);
    }
    if (digits.empty()) return -1;
    try { return std::stoi(digits); } catch (...) { return -1; }
}

int LevelManager::nextLevelId(int currentLevelId) {
    if (currentLevelId >= 1 && currentLevelId <= 50) {
        int world = (currentLevelId - 1) / 10 + 1;
        int inWorldLevel = (currentLevelId - 1) % 10 + 1;
        if (inWorldLevel >= 1 && inWorldLevel <= 4) return (world - 1) * 10 + 5;
        if (inWorldLevel >= 5 && inWorldLevel <= 8) return (world - 1) * 10 + 9;
        if (inWorldLevel >= 9 && inWorldLevel <= 10) return world * 10 + 1;
    }
    if (currentLevelId == 51 || currentLevelId == 52) return 53;
    if (currentLevelId == 53 || currentLevelId == 54) return 55;
    if (currentLevelId == 55) return 57;
    if (currentLevelId == 56) return 58;
    return -1;
}

bool LevelManager::areButtonsActiveUpToArea(int worldId, int areaId) const {
    if (worldId <= 0 || areaId <= 0) return false;
    for (int a = 1; a <= areaId; ++a) {
        const int key = worldId * 10 + a;
        if (buttonWorldAreasWithButtons_.find(key) == buttonWorldAreasWithButtons_.end()) {
            continue;
        }
        if (!isButtonAreaActive(worldId, a)) {
            return false;
        }
    }
    return true;
}

bool LevelManager::isButtonAreaActive(int worldId, int areaId) const {
    if (worldId <= 0 || areaId <= 0) return false;
    const int key = worldId * 10 + areaId;
    if (activeButtonWorldAreas_.find(key) != activeButtonWorldAreas_.end()) {
        return true;
    }
    if (buttonWorldAreasWithButtons_.find(key) == buttonWorldAreasWithButtons_.end()) {
        return false;
    }
    return false;
}

std::string LevelManager::levelPathFromId(int levelId) {
    if (levelId <= 0) return "";
    std::ostringstream ss;
    ss << "assets/levels/level_" << std::setw(3) << std::setfill('0') << levelId << ".txt";
    std::string path = ss.str();
    if (!FileExists(path)) return "";
    return path;
}

std::vector<char> LevelManager::loadBlockDefs(const std::string& path) {
    const std::string text = ReadTextFile(path);
    if (text.empty()) return {};
    std::istringstream f(text);
    std::vector<char> defs;
    std::string line;
    while (std::getline(f, line)) {
        char c = 0;
        for (char ch : line) {
            if (!std::isspace((unsigned char)ch)) { c = ch; break; }
        }
        defs.push_back(c);
    }
    return defs;
}

void LevelManager::applyBlockDefAt(TileMap& map, int idx, unsigned short tileId) const {
    char d = (tileId < blockDefs_.size()) ? blockDefs_[tileId] : 0;
    map.semisolid[idx] = (d == '=') ? 1 : 0;
    map.water[idx]     = (d == '^') ? 1 : 0;
    map.solid[idx]     = (d != '=' && d != '^' && d != '#') ? 0 : 1;
}

void LevelManager::updateLevelMetadata(const TileMap& map) {
    int levelIndex = parseLevelIndexFromPath(levelPath_);
    if (levelIndex > 0 && levelIndex <= (int)levelNumberList_.size()) {
        int code = levelNumberList_[levelIndex - 1];
        worldId_ = code / 100;
        levelPartId_ = (code / 10) % 10;
        timeId_ = code % 10;
    } else {
        worldId_ = 0;
        levelPartId_ = 0;
        timeId_ = 0;
    }
}


