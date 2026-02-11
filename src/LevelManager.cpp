#include "LevelManager.h"

#include <sdl3/SDL.h>
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

    if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
        bool resolved = false;
        const int maxSteps = std::max(1, map.h * map.tileSize);
        for (int step = 1; step <= maxSteps; ++step) {
            float upY = player.y - (float)step;
            if (!RectHitsSolid(map, player.x, upY, player.w, player.h)) {
                player.y = upY;
                resolved = true;
                break;
            }
            float downY = player.y + (float)step;
            if (!RectHitsSolid(map, player.x, downY, player.w, player.h)) {
                player.y = downY;
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            player.vx = 0.0f;
            player.vy = 0.0f;
        }
    }

    // Metadata already updated at reload start.
}

std::string LevelManager::nextLevelPath() const {
    int currentLevel = parseLevelIndexFromPath(levelPath_);
    return levelPathFromId(nextLevelId(currentLevel));
}

int LevelManager::collectCoinsAtPlayer(TileMap& map, const Player& player) {
    int t = map.tileSize;
    int left = (int)std::floor(player.x / t);
    int right = (int)std::floor((player.x + player.w - 1) / t);
    int top = (int)std::floor(player.y / t);
    int bottom = (int)std::floor((player.y + player.h - 1) / t);

    int collected = 0;
    for (int ty = top; ty <= bottom; ++ty) {
        if (ty < 0 || ty >= map.h) continue;
        for (int tx = left; tx <= right; ++tx) {
            if (tx < 0 || tx >= map.w) continue;
            int idx = ty * map.w + tx;
            if (map.tileIds[idx] != 24) continue;
            map.tileIds[idx] = 2;
            applyBlockDefAt(map, idx, 2);
            collected++;
        }
    }

    coinCount_ += collected;
    return collected;
}

void LevelManager::updateTimeWarpIdAtPlayer(const TileMap& map, const Player& player) {
    int t = map.tileSize;
    int left = (int)std::floor(player.x / t);
    int right = (int)std::floor((player.x + player.w - 1) / t);
    int top = (int)std::floor(player.y / t);
    int bottom = (int)std::floor((player.y + player.h - 1) / t);

    char next = timeWarpId_;
    for (int ty = top; ty <= bottom; ++ty) {
        if (ty < 0 || ty >= map.h) continue;
        for (int tx = left; tx <= right; ++tx) {
            if (tx < 0 || tx >= map.w) continue;
            unsigned short id = map.tileIds[ty * map.w + tx];
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
