#include "LocalLevelEdits.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace fs = std::filesystem;
static std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

int main() {
    TileMap map;
    map.resize(7, 6);
    map.tileIds.assign(42, 2);
    map.tileIds[0] = 66;
    map.tileIds[4 * 7 + 4] = 65535;
    map.tileIds[6] = 67;
    map.tileIds[5 * 7] = 9;
    map.tileIds[5 * 7 + 6] = 0;
    const std::vector<ObjectInstance> objects{{"17", 0, 0}, {"22", 159, 159},
        {"23", 160, 20}, {"24", 20, 160}, {"25", -1, 0},
        {"26", std::numeric_limits<float>::quiet_NaN(), 0}};
    LocalLevelEdits::ResizePlan plan;
    assert(LocalLevelEdits::planResize(map, objects, 5, 5, plan));
    assert(plan.tiles.size() == 25 && plan.tiles[0] == 66 && plan.tiles[24] == 65535);
    assert(plan.croppedTiles == 2 && plan.croppedObjects == 4);
    assert(plan.objects.size() == 2 && plan.objects[1].id == "22" && plan.objects[1].x == 159);
    assert(LocalLevelEdits::planResize(map, {}, 9, 8, plan));
    assert(plan.tiles[6] == 67 && plan.tiles[5 * 9] == 9 && plan.tiles[7 * 9 + 8] == 2);
    assert(plan.croppedTiles == 0);
    assert(LocalLevelEdits::planResize(map, {}, 5, 8, plan));
    assert(plan.croppedTiles == 1 && plan.tiles[7 * 5 + 4] == 2);
    const auto previous = plan.tiles;
    assert(!LocalLevelEdits::planResize(map, {}, 4, 8, plan));
    assert(!LocalLevelEdits::planResize(map, {}, 8, 401, plan));
    assert(plan.tiles == previous);
    assert(LocalLevelEdits::planResize(map, {}, 400, 400, plan));
    map.tileIds.clear();
    assert(!LocalLevelEdits::planResize(map, {}, 5, 5, plan));

    const auto dir = fs::temp_directory_path() / ("df-level-edits-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(dir));
    const auto original = dir / "original.bnnlvl";
    const auto existing = dir / "My_Level.bnnlvl";
    std::ofstream(original) << "level data";
    std::ofstream(existing) << "keep existing";
    std::string renamed, error;
    assert(LocalLevelEdits::renameFile(original.string(), "My Level", renamed, error));
    assert(fs::path(renamed) == dir / "My_Level_1.bnnlvl");
    assert(!fs::exists(original) && read(renamed) == "level data" && read(existing) == "keep existing");
    const auto firstRename = renamed;
    assert(LocalLevelEdits::renameFile(firstRename, "My_Level_1", renamed, error));
    assert(renamed == firstRename);
    assert(!LocalLevelEdits::renameFile(firstRename, "...", renamed, error));
    assert(read(firstRename) == "level data");
    assert(!LocalLevelEdits::renameFile(original.string(), "missing", renamed, error));
    assert(LocalLevelEdits::renameFile(firstRename, "../outside", renamed, error));
    assert(fs::path(renamed).parent_path() == dir && fs::path(renamed).extension() == ".bnnlvl");
    const auto temporary = dir / "replacement.tmp";
    assert(!LocalLevelEdits::replaceFile(temporary.string(), renamed));
    assert(read(renamed) == "level data");
    std::ofstream(temporary) << "resized data";
    assert(LocalLevelEdits::replaceFile(temporary.string(), renamed));
    assert(read(renamed) == "resized data" && !fs::exists(temporary));
    fs::remove(renamed);
    fs::remove(existing);
    assert(fs::remove(dir));
    std::cout << "Local level edit tests passed\n";
}
