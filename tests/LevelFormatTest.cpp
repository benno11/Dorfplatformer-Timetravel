#include "AssetPath.h"
#include "LevelLoader.h"
#include "LevelManager.h"
#include "ObjectIds.h"
#include "GameProgression.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>

// The loader reads fixtures through the same asset boundary as real levels.
static std::unordered_map<std::string, std::string> files;
std::string GetAppSaveRootPath() { return "."; }
std::string ReadTextFile(const std::string& path) {
    const auto found = files.find(path);
    if (found != files.end()) return found->second;
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
bool FileExists(const std::string& path) {
    return files.count(path) != 0 || std::filesystem::exists(path);
}

int main() {
    LevelMeta original;
    original.levelId = 71;
    original.themeOverride = 5;
    original.wrapX = original.wrapY = true;
    original.world = 6;
    original.area = 2;
    original.time = 4;
    original.bouncyTile12 = true;
    const std::string tiles = "4 4\n2 2 2 2\n2 83 86 2\n2 2 2 2\n12 12 12 12\nOBJ 1\n62 32 64\n";
    files["configured.bin"] = SerializeLevelMetaHeader(original) + "DFLVL4 " + tiles;
    TileMap map;
    LevelMeta loaded;
    std::vector<ObjectInstance> objects;
    assert(loadLevelBNNLVL("configured.bin", map, objects, loaded));
    assert(loaded.formatVersion == 4 && loaded.levelId == 71 && loaded.themeOverride == 5);
    assert(loaded.world == 6 && loaded.area == 2 && loaded.time == 4);
    assert(loaded.wrapX && loaded.wrapY && loaded.bouncyTile12 && map.bouncyTile12);
    assert(map.w == 4 && map.h == 4 && map.tileIds[5] == 83 && map.tileIds[6] == 86);
    assert(objects.size() == 1 && objects[0].id == "62" && objects[0].x == 32 && objects[0].y == 64);
    // Re-saving metadata must retain every configured field.
    files["resaved.bin"] = SerializeLevelMetaHeader(loaded) + "DFLVL4 " + tiles;
    assert(loadLevelBNNLVL("resaved.bin", map, objects, loaded));
    assert(loaded.world == 6 && loaded.area == 2 && loaded.time == 4 && map.bouncyTile12);

    LevelManager manager;
    Player player;
    manager.setLevelPath("configured.bin");
    manager.reloadLevel(map, objects, loaded, player);
    assert(manager.worldId() == 6 && manager.levelPartId() == 2 && manager.timeId() == 4);
    assert(manager.musicPath() == "assets/Audio/Music/6.2.mp3");

    for (int version : {2, 3}) {
        files["legacy.bin"] = "DFLVL" + std::to_string(version) + " " + tiles;
        assert(loadLevelBNNLVL("legacy.bin", map, objects, loaded));
        assert(loaded.formatVersion == version);
        assert(loaded.world == 0 && loaded.area == 0 && loaded.time == 0);
        assert(!loaded.bouncyTile12 && !map.bouncyTile12);
        assert(map.tileIds[5] == 83 && map.tileIds[6] == 86);
    }
    LevelMeta invalid;
    invalid.world = 999;
    invalid.area = -3;
    invalid.time = 50;
    files["bounds.bin"] = SerializeLevelMetaHeader(invalid) + "DFLVL4 " + tiles;
    assert(loadLevelBNNLVL("bounds.bin", map, objects, loaded));
    assert(loaded.world == 7 && loaded.area == 0 && loaded.time == 4);
    files["unsupported.bin"] = "DFLVL5 " + tiles;
    assert(!loadLevelBNNLVL("unsupported.bin", map, objects, loaded));
    // Custom IDs must survive loading without reinterpreting colliding legacy IDs.
    const std::string objectTiles = "2 2\n2 2 2 2\nOBJ 4\n" +
        std::to_string(ObjectIds::Pickup) + " 16 32\n" +
        std::to_string(ObjectIds::Goal) + " 48 32\n95 80 32\n97 112 32\n";
    for (int version : {2, 3, 4}) {
        files["object_ids.bin"] = "DFLVL" + std::to_string(version) + " " + objectTiles;
        assert(loadLevelBNNLVL("object_ids.bin", map, objects, loaded));
        assert(objects.size() == 4);
        assert(objects[0].id == std::to_string(ObjectIds::Pickup));
        assert(objects[1].id == std::to_string(ObjectIds::Goal));
        assert(objects[2].id == "95" && objects[3].id == "97");
    }
    for (int level = 1; level <= kHardmodeLevelCount; ++level) {
        const auto path = HardmodeLevelPath(level);
        assert(loadLevelBNNLVL(path, map, objects, loaded));
        const bool hasEndTile = std::find(map.tileIds.begin(), map.tileIds.end(), 30) != map.tileIds.end();
        manager.setLevelPath(path);
        manager.reloadLevel(map, objects, loaded, player);
        assert(manager.nextLevelPath() == HardmodeLevelPath(level + 1));
        if (hasEndTile) {
            assert(std::any_of(objects.begin(), objects.end(), [](const ObjectInstance& object) { return object.id == "67"; }));
        }
    }
    assert(manager.levelPathByCode(613) == "assets/levels/level_051.txt");
    assert(manager.levelPathByCode(713) == "assets/levels/level_057.txt");
    return 0;
}
