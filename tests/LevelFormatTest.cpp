#include "AssetPath.h"
#include "LevelLoader.h"
#include "LevelManager.h"
#include "ObjectIds.h"
#include "GameProgression.h"
#include "Shield.h"

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
    // Attribute decoding must agree when loading and changing a tile at runtime.
    // Blank/unknown definitions are air; #, =, and ^ mean solid, platform, water.
    files["assets/blockdefined.txt"] = std::string(100, '\n') + "\n#\n=\n^\n?\n";
    LevelMeta savedWorld;
    savedWorld.world = savedWorld.area = savedWorld.time = 1;
    files["attributes.bin"] = SerializeLevelMetaHeader(savedWorld) + "DFLVL4 6 1\n100 101 102 103 104 65535\nOBJ 0\n";
    {
        TileMap attributes;
        LevelMeta metadata;
        std::vector<ObjectInstance> entities;
        assert(loadLevelBNNLVL("attributes.bin", attributes, entities, metadata));
        assert(metadata.world == 1 && metadata.area == 1 && metadata.time == 1);
        const unsigned short ids[]{100, 101, 102, 103, 104, 65535};
        auto check = [&](int index, unsigned short id) {
            assert(attributes.solid[index] == (id == 101));
            assert(attributes.semisolid[index] == (id == 102));
            assert(attributes.water[index] == (id == 103));
        };
        for (int i = 0; i < 6; ++i) check(i, ids[i]);
        LevelManager edits;
        const auto savedTiles = attributes.tileIds;
        const auto solid = attributes.solid, semi = attributes.semisolid, water = attributes.water;
        // Continue restores all saved tile IDs through setTileAt, unlike Restart.
        for (int i = 0; i < 6; ++i) edits.setTileAt(attributes, i, savedTiles[i]);
        assert(attributes.solid == solid && attributes.semisolid == semi && attributes.water == water);
        for (auto from : ids) for (auto to : ids) {
            edits.setTileAt(attributes, 0, from);
            edits.setTileAt(attributes, 0, to);
            check(0, to);
            for (int i = 1; i < 6; ++i) check(i, ids[i]);
        }
    }
    files.erase("assets/blockdefined.txt");
    files.erase("attributes.bin");
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

    // Auto must resolve to playable defaults for custom and renamed levels.
    for (const std::string path : {"custom.bin", "custom_99999.bin"}) {
        files[path] = SerializeLevelMetaHeader({}) + "DFLVL4 " + tiles;
        manager.setLevelPath(path);
        manager.reloadLevel(map, objects, loaded, player);
        assert(manager.worldId() == 1 && manager.levelPartId() == 1 && manager.timeId() == 1);
        assert(loaded.world == 0 && loaded.area == 0 && loaded.time == 0);
        assert(manager.musicPath() == "assets/Audio/Music/1.1.mp3");
        manager.applyLevelConfig(original);
        manager.applyLevelConfig({});
        assert(manager.worldId() == 1 && manager.levelPartId() == 1 && manager.timeId() == 1);
    }
    // Switching individual overrides back to Auto restores the path-derived value.
    manager.setLevelPath("assets/levels/level_052.txt");
    manager.applyLevelConfig(original);
    LevelMeta partial;
    partial.area = 3;
    manager.applyLevelConfig(partial);
    assert(manager.worldId() == 6 && manager.levelPartId() == 3 && manager.timeId() == 4);
    manager.applyLevelConfig({});
    assert(manager.worldId() == 6 && manager.levelPartId() == 1 && manager.timeId() == 4);

    // Invalid automatic source fields fall back independently.
    files["assets/Level Numer List.txt"] = "809\n";
    LevelManager invalidAuto;
    invalidAuto.setLevelPath("level_001.txt");
    invalidAuto.applyLevelConfig({});
    assert(invalidAuto.worldId() == 1 && invalidAuto.levelPartId() == 1 && invalidAuto.timeId() == 1);
    files.erase("assets/Level Numer List.txt");

    LevelMeta buttonMeta;
    buttonMeta.world = 2;
    buttonMeta.area = 1;
    buttonMeta.time = 1;
    files["button_start.bin"] = SerializeLevelMetaHeader(buttonMeta) +
        "DFLVL4 4 4\n2 2 2 2\n2 66 66 2\n2 66 2 2\n2 2 2 2\nOBJ 0\n";
    auto areaActive = [&](int key) {
        const auto active = manager.activeButtonWorldAreas();
        return std::find(active.begin(), active.end(), key) != active.end();
    };
    manager.setButtonAreaActiveForDebug(2, 1, true);
    manager.setButtonAreaActiveForDebug(2, 2, true);
    manager.setLevelPath("button_start.bin");
    manager.reloadLevel(map, objects, loaded, player);
    assert(map.tileIds[5] == 66 && map.tileIds[6] == 66 && map.tileIds[9] == 66);
    assert(!areaActive(21) && areaActive(22));
    player.x = 32; player.y = 32; player.w = player.h = 30;
    assert(manager.activateButtonsAtPlayer(map, player) == 1 && areaActive(21));
    assert(map.tileIds[9] == 66); // The first press sets the flag with another button untouched.
    player.y = 64;
    assert(manager.activateButtonsAtPlayer(map, player) == 1 && areaActive(21));
    manager.reloadLevel(map, objects, loaded, player);
    assert(map.tileIds[5] == 66 && map.tileIds[6] == 66 && map.tileIds[9] == 66 && !areaActive(21));
    for (int button : {5, 6, 9}) {
        manager.reloadLevel(map, objects, loaded, player);
        player.x = (button % map.w) * map.tileSize;
        player.y = (button / map.w) * map.tileSize;
        player.w = player.h = 30;
        assert(manager.activateButtonsAtPlayer(map, player) == 1 && areaActive(21));
        assert(map.tileIds[button == 5 ? 9 : 5] == 66);
        assert(manager.activateButtonsAtPlayer(map, player) == 0 && areaActive(21));
        assert(areaActive(22));
    }
    // A mixed authored state also sets the flag from just one active button.
    files["button_mixed.bin"] = SerializeLevelMetaHeader(buttonMeta) +
        "DFLVL4 4 4\n2 2 2 2\n2 66 67 2\n2 66 2 2\n2 2 2 2\nOBJ 0\n";
    manager.setLevelPath("button_mixed.bin");
    manager.reloadLevel(map, objects, loaded, player);
    assert(map.tileIds[5] == 66 && map.tileIds[6] == 67 && map.tileIds[9] == 66 && areaActive(21));
    buttonMeta.time = 4;
    files["button_alternate.bin"] = SerializeLevelMetaHeader(buttonMeta) +
        "DFLVL4 4 4\n2 2 2 2\n2 67 67 2\n2 67 2 2\n2 2 2 2\nOBJ 0\n";
    manager.setLevelPath("button_alternate.bin");
    manager.reloadLevel(map, objects, loaded, player);
    assert(map.tileIds[5] == 67 && map.tileIds[6] == 67 && map.tileIds[9] == 67 && areaActive(21));
    manager.setLevelPath("button_start.bin");
    manager.reloadLevel(map, objects, loaded, player);
    // Saved maps and saved area flags cannot override authored states on Continue.
    manager.setTileAt(map, 5, 2);
    manager.setTileAt(map, 6, 67);
    manager.setTileAt(map, 9, 67);
    manager.setButtonAreaActiveForDebug(2, 1, true);
    manager.resetButtonsForLevelStart(map);
    assert(map.tileIds[5] == 66 && map.tileIds[6] == 66 && map.tileIds[9] == 66);
    assert(!areaActive(21) && areaActive(22));

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
    const std::string objectTiles = "2 2\n2 2 2 2\nOBJ 6\n" +
        std::to_string(ObjectIds::Pickup) + " 16 32\n" +
        std::to_string(ObjectIds::Goal) + " 48 32\n95 80 32\n97 112 32\n" +
        std::to_string(ObjectIds::ShellChraker) + " 144 32\n" +
        std::to_string(ObjectIds::Shield) + " 48 48\n";
    for (int version : {2, 3, 4}) {
        files["object_ids.bin"] = "DFLVL" + std::to_string(version) + " " + objectTiles;
        assert(loadLevelBNNLVL("object_ids.bin", map, objects, loaded));
        assert(objects.size() == 6);
        assert(objects[0].id == std::to_string(ObjectIds::Pickup));
        assert(objects[1].id == std::to_string(ObjectIds::Goal));
        assert(objects[2].id == "95" && objects[3].id == "97");
        assert(objects[4].id == std::to_string(ObjectIds::ShellChraker));
        assert(objects[5].id == std::to_string(ObjectIds::Shield));
    }
    map.resize(4, 4);
    objects = {{std::to_string(ObjectIds::Shield), 48, 80}};
    manager.resetCoinCount();
    manager.addCoins(7);
    player = Player{};
    assert(!manager.openCoinBox(map, 9, objects, player));
    assert(!player.hasShield && manager.coinCount() == 7); // Standalone marker does nothing.
    manager.setTileAt(map, 9, 20);
    assert(manager.openCoinBox(map, 9, objects, player));
    assert(player.hasShield && manager.coinCount() == 7 && map.tileIds[9] == 21);
    assert(!manager.openCoinBox(map, 9, objects, player)); // Empty boxes cannot award another shield.
    player.vx = 123;
    player.vy = -75;
    assert(Shield::absorbDamage(player));
    assert(!player.hasShield && manager.coinCount() == 7 && player.vx == 123 && player.vy == -75);
    assert(!Shield::absorbDamage(player));
    manager.setTileAt(map, 10, 20);
    assert(manager.openCoinBox(map, 10, objects, player));
    assert(!player.hasShield && manager.coinCount() == 12); // Other boxes still award coins.
    objects = {{std::to_string(ObjectIds::Shield), -16, 16}};
    manager.setTileAt(map, 3, 20);
    assert(manager.openCoinBox(map, 3, objects, player, true, false));
    assert(player.hasShield && manager.coinCount() == 12);
    manager.setLevelPath("configured.bin");
    manager.reloadLevel(map, objects, loaded, player);
    assert(player.hasShield); // Restart preserves the shield.
    manager.setLevelPath(HardmodeLevelPath(1));
    manager.reloadLevel(map, objects, loaded, player);
    assert(player.hasShield); // Moving to a different level also preserves it.
    player.hasShield = false; // Death consumes it before reload.
    manager.reloadLevel(map, objects, loaded, player);
    assert(!player.hasShield);
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
