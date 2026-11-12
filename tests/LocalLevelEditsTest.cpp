#include "LocalLevelEdits.h"
#include "EditorObjectTools.h"
#include "EditorBrush.h"
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
    assert(EditorBrush::cells(10, 10, 5, 5, 1).size() == 1);
    assert(EditorBrush::cells(10, 10, 5, 5, 3).size() == 9);
    assert(EditorBrush::cells(10, 10, 0, 0, 3).size() == 4);
    assert(EditorBrush::cells(5, 4, 2, 2, 9).size() == 20);
    for (const auto& cell : EditorBrush::cells(5, 4, 0, 0, 9))
        assert(cell.first >= 0 && cell.first < 5 && cell.second >= 0 && cell.second < 4);

    std::vector<ObjectInstance> layered;
    EditorObjects::replace(layered, {"31", 48, 48, {}, 0});
    EditorObjects::replace(layered, {"46", 48, 48, {}, 2});
    assert(layered.size() == 2);
    // Repeated placement/overrides never toggle an object off or touch other layers.
    EditorObjects::replace(layered, {"31", 48, 48, {}, 0});
    assert(layered.size() == 2);
    EditorObjects::replace(layered, {"1106", 48, 48, 3, 0});
    assert(layered.size() == 2 && layered[0].parameterValue == 3 && layered[1].id == "46");
    assert(!EditorObjects::erase(layered, 1, 1, 0, false) && layered.size() == 2);
    assert(EditorObjects::find(layered, 1, 1, 2) == 1);
    assert(EditorObjects::erase(layered, 1, 1, 0, true));
    assert(layered.size() == 1 && layered[0].layer == 2);
    EditorObjects::replace(layered, {"97", 80, 48, {}, 0});
    EditorObjects::replace(layered, {"96", 112, 48, {}, 2});
    const auto order = EditorObjects::renderOrder(layered);
    assert((order == std::vector<int>{1, 0, 2})); // Stable within one layer.

    TileMap map;
    map.resize(7, 6);
    map.tileIds.assign(42, 2);
    map.tileIds[0] = 66;
    map.tileIds[4 * 7 + 4] = 65535;
    map.tileIds[6] = 67;
    map.tileIds[5 * 7] = 9;
    map.tileIds[5 * 7 + 6] = 0;
    const std::vector<ObjectInstance> objects{{"17", 0, 0, {}, 4}, {"22", 159, 159},
        {"23", 160, 20}, {"24", 20, 160}, {"25", -1, 0},
        {"26", std::numeric_limits<float>::quiet_NaN(), 0}};
    LocalLevelEdits::ResizePlan plan;
    assert(LocalLevelEdits::planResize(map, objects, 5, 5, plan));
    assert(plan.tiles.size() == 25 && plan.tiles[0] == 66 && plan.tiles[24] == 65535);
    assert(plan.croppedTiles == 2 && plan.croppedObjects == 4);
    assert(plan.objects.size() == 2 && plan.objects[1].id == "22" && plan.objects[1].x == 159);
    assert(plan.objects[0].layer == 4);
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
    std::string upgradeBackup;
    assert(LocalLevelEdits::backupForUpgrade(renamed, upgradeBackup, error));
    assert(read(upgradeBackup) == "level data" && read(renamed) == "level data");
    std::string secondBackup;
    assert(LocalLevelEdits::backupForUpgrade(renamed, secondBackup, error));
    assert(secondBackup != upgradeBackup && read(secondBackup) == "level data");
    const std::string secondBackupFile = secondBackup;
    assert(!LocalLevelEdits::backupForUpgrade(original.string(), secondBackup, error));
    assert(secondBackup.empty() && !error.empty());
    const auto temporary = dir / "replacement.tmp";
    assert(!LocalLevelEdits::replaceFile(temporary.string(), renamed));
    assert(read(renamed) == "level data");
    std::ofstream(temporary) << "resized data";
    assert(LocalLevelEdits::replaceFile(temporary.string(), renamed));
    assert(read(renamed) == "resized data" && !fs::exists(temporary));
    fs::remove(renamed);
    fs::remove(existing);
    fs::remove(upgradeBackup);
    fs::remove(secondBackupFile);
    assert(fs::remove(dir));
    std::cout << "Local level edit tests passed\n";
}
