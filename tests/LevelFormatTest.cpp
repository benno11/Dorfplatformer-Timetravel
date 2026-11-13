#include "AssetPath.h"
#include "LevelLoader.h"
#include "LevelParameterObjects.h"
#include <sstream>
#include "LevelManager.h"
#include "ObjectIds.h"
#include "GameProgression.h"
#include "SaveFormat.h"
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
    // Camera exclusion is a regular object, so editor saves keep its position/layer.
    ObjectInstance exclusion; exclusion.id = std::to_string(ObjectIds::CameraExclusion);
    exclusion.x = 48; exclusion.y = 80; exclusion.layer = 4;
    files["camera_exclusion.bin"] = "DFLVL4 1 1\n2\n" + SerializeLevelObjects({exclusion});
    { TileMap cameraMap; LevelMeta cameraMeta; std::vector<ObjectInstance> cameraObjects;
      assert(loadLevelBNNLVL("camera_exclusion.bin", cameraMap, cameraObjects, cameraMeta));
      assert(cameraObjects.size() == 1 && cameraObjects[0].id == "99" && cameraObjects[0].x == 48 &&
          cameraObjects[0].y == 80 && cameraObjects[0].layer == 4); }

    // Boss barriers survive level loading and runtime edits without becoming
    // player solids; their collision is derived from the saved tile ID.
    files["boss_barrier.bin"] = "DFLVL4 3 1\n2 98 2\nOBJ 0\n";
    {
        TileMap map;
        LevelMeta metadata;
        std::vector<ObjectInstance> entities;
        assert(loadLevelBNNLVL("boss_barrier.bin", map, entities, metadata));
        assert(map.tileIds[1] == TileIds::BossBarrier);
        assert(!map.getSolid(1, 0) && map.getBossSolid(1, 0));
        LevelManager edits;
        edits.setTileAt(map, 1, 2);
        assert(!map.getBossSolid(1, 0));
        edits.setTileAt(map, 1, TileIds::BossBarrier);
        assert(!map.getSolid(1, 0) && map.getBossSolid(1, 0));
    }
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
    manager.reloadLevel(map, objects, loaded, player, true);
    assert(!areaActive(21) && areaActive(22) && map.tileIds[5] == 66);
    player.x = 32; player.y = 32; player.w = player.h = 30;
    assert(manager.activateButtonsAtPlayer(map, player) == 1 && areaActive(21));
    assert(map.tileIds[9] == 66); // The first press sets the flag with another button untouched.
    // Death reloads keep the shared area flag and pressed visuals; alternatives
    // must not award another press after respawning.
    manager.reloadLevel(map, objects, loaded, player, true);
    assert(areaActive(21) && areaActive(22));
    assert(map.tileIds[5] == 67 && map.tileIds[6] == 67 && map.tileIds[9] == 67);
    player.x = 32; player.y = 32; player.w = player.h = 30;
    assert(manager.activateButtonsAtPlayer(map, player) == 0);
    manager.reloadLevel(map, objects, loaded, player);
    player.x = 32; player.y = 32; player.w = player.h = 30;
    assert(manager.activateButtonsAtPlayer(map, player) == 1);
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
    // Parameter values live after the unchanged triples, so old readers can
    // still enumerate the reserved object IDs without misreading positions.
    std::vector<ObjectInstance> setters;
    for (const auto& d : LevelParameters::definitions)
        setters.push_back({std::to_string(d.id), 16.25f, 48.5f, d.maximum});
    const std::string setterStream = SerializeLevelObjects(setters);
    files["setters.bin"] = "DFLVL4 2 2\n2 2 2 2\n" + setterStream;
    assert(loadLevelBNNLVL("setters.bin", map, objects, loaded));
    assert(objects.size() == setters.size());
    assert(objects[0].x == 16.25f && objects[0].y == 48.5f);
    for (size_t i = 0; i < setters.size(); ++i) {
        assert(objects[i].id == setters[i].id);
        assert(objects[i].parameterValue == setters[i].parameterValue);
        assert(LevelParameters::apply(objects[i], loaded));
    }
    assert(loaded.levelId == std::numeric_limits<int>::max() && loaded.themeOverride == 7);
    assert(loaded.world == 7 && loaded.area == 3 && loaded.time == 4);
    assert(loaded.wrapX && loaded.wrapY && loaded.bouncyTile12);
    assert(SerializeLevelObjects(objects) == setterStream);
    std::istringstream legacy(setterStream);
    std::string section;
    int count = 0;
    legacy >> section >> count;
    assert(section == "OBJ" && count == 8);
    for (int i = 0; i < count; ++i) {
        int id; float x, y;
        assert(legacy >> id >> x >> y);
        assert(id == 1100 + i && x == 16.25f && y == 48.5f);
    }
    legacy >> section;
    assert(section == "OBJPARAM1");
    // Unsupported payload versions and malformed/invalid values fail safely.
    for (const auto& extension : {"OBJPARAM2 1\n0 1\n", "OBJPARAM1 1\n2 1\n",
        "OBJPARAM1 2\n0 1\n0 0\n", "OBJPARAM1 1\n0 -1\n", "OBJPARAM1 1\n0 2\n",
        "OBJPARAM1 1\n0 9999999999999999999999\n", "OBJPARAM1 1\n0\n"}) {
        files["invalid_setter.bin"] = std::string("DFLVL4 1 1\n2\nOBJ 1\n1102 16 16\n") + extension;
        assert(!loadLevelBNNLVL("invalid_setter.bin", map, objects, loaded));
        for (const auto& obj : objects) assert(!obj.parameterValue); // No partial extension is applied.
    }
    files["old_resave.bin"] = "DFLVL4 1 1\n2\nOBJ 1\n1102 16 16\n";
    assert(loadLevelBNNLVL("old_resave.bin", map, objects, loaded));
    assert(!objects[0].parameterValue && !LevelParameters::apply(objects[0], loaded));
    // Unknown future IDs keep their values on a new-client round trip.
    files["future_setter.bin"] = "DFLVL4 1 1\n2\nOBJ 1\n1200 16 16\nOBJPARAM1 1\n0 42\n";
    assert(loadLevelBNNLVL("future_setter.bin", map, objects, loaded));
    assert(objects[0].parameterValue == 42 && !LevelParameters::apply(objects[0], loaded));
    assert(SerializeLevelObjects(objects).find("0 42") != std::string::npos);
    // Layers round-trip alongside parameters; both extension orders are accepted.
    for (const auto& sections : {"OBJPARAM1 1\n0 3\nOBJLAYER1 1\n0 7\n", "OBJLAYER1 1\n0 7\nOBJPARAM1 1\n0 3\n"}) {
        files["layered.bin"] = std::string("DFLVL4 1 1\n2\nOBJ 1\n1106 16 16\n") + sections;
        assert(loadLevelBNNLVL("layered.bin", map, objects, loaded));
        assert(objects[0].layer == 7 && objects[0].parameterValue == 3);
        files["layered_roundtrip.bin"] = "DFLVL4 1 1\n2\n" + SerializeLevelObjects(objects);
        assert(loadLevelBNNLVL("layered_roundtrip.bin", map, objects, loaded));
        assert(objects[0].layer == 7 && objects[0].parameterValue == 3);
    }
    for (const auto& section : {"OBJLAYER2 1\n0 1\n", "OBJLAYER1 1\n0 -1\n", "OBJLAYER1 1\n0 10\n",
        "OBJLAYER1 1\n1 0\n", "OBJLAYER1 1\n0\n", "OBJLAYER1 0\nOBJLAYER1 0\n"}) {
        files["bad_layer.bin"] = std::string("DFLVL4 1 1\n2\nOBJ 1\n1106 16 16\nOBJPARAM1 1\n0 3\n") + section;
        assert(!loadLevelBNNLVL("bad_layer.bin", map, objects, loaded));
        assert(!objects[0].parameterValue && objects[0].layer == 0); // Atomic extension loading.
    }
    assert(loadLevelBNNLVL("configured.bin", map, objects, loaded));
    files["configured_upgrade.bin"] = SerializeLevelBNNLVL(map, objects, loaded);
    assert(loadLevelBNNLVL("configured_upgrade.bin", map, objects, loaded));
    assert(loaded.levelId == original.levelId && loaded.themeOverride == original.themeOverride);
    assert(loaded.world == original.world && loaded.area == original.area && loaded.time == original.time);
    assert(loaded.wrapX == original.wrapX && loaded.wrapY == original.wrapY && loaded.bouncyTile12 == original.bouncyTile12);
    // Upgrade all supported encodings through one canonical writer. Upgrading
    // again is byte-stable and never invents missing trigger values.
    for (int version : {2, 3, 4}) {
        files["upgrade_source.bin"] = "DFLVL" + std::to_string(version) +
            " 3 1\n0 98 65535\nOBJ 2\n1106 16.25 16\n62 48 16\nOBJLAYER1 1\n0 2\n";
        assert(loadLevelBNNLVL("upgrade_source.bin", map, objects, loaded));
        assert(LevelNeedsUpgrade(loaded) == (version < 4));
        const auto upgraded = SerializeLevelBNNLVL(map, objects, loaded);
        assert(!upgraded.empty());
        files["upgrade_output.bin"] = upgraded;
        assert(loadLevelBNNLVL("upgrade_output.bin", map, objects, loaded));
        assert(!LevelNeedsUpgrade(loaded) && loaded.formatVersion == kCurrentLevelFormatVersion);
        assert((map.tileIds == std::vector<unsigned short>{0, 98, 65535}));
        assert(objects.size() == 2 && objects[0].x == 16.25f && objects[0].layer == 2);
        assert(!objects[0].parameterValue);
        if (version < 4) assert(loaded.wrapX);
        assert(SerializeLevelBNNLVL(map, objects, loaded) == upgraded);
    }
    // The original chunk-encoded campaign level is upgraded from its decoded
    // geometry, preventing coordinate adjustments from being applied twice.
    assert(loadLevelBNNLVL("assets/levels/level_001.txt", map, objects, loaded));
    assert(loaded.legacyEncoding && LevelNeedsUpgrade(loaded));
    const auto originalTiles = map.tileIds;
    const auto originalObjects = objects;
    files["campaign_upgraded.bin"] = SerializeLevelBNNLVL(map, objects, loaded);
    assert(loadLevelBNNLVL("campaign_upgraded.bin", map, objects, loaded));
    assert(!loaded.legacyEncoding && !LevelNeedsUpgrade(loaded));
    assert(map.tileIds == originalTiles && objects.size() == originalObjects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        assert(objects[i].id == originalObjects[i].id);
        assert(objects[i].x == originalObjects[i].x && objects[i].y == originalObjects[i].y);
    }
    const auto stableCampaign = SerializeLevelBNNLVL(map, objects, loaded);
    assert(stableCampaign == files["campaign_upgraded.bin"]);
    loaded.formatVersion = 5;
    assert(SerializeLevelBNNLVL(map, objects, loaded).empty());
    for (const auto& malformed : {"DFLVL2 2 2\n2 2\n", "DFLVL3 1 1\n2\nOBJ 1\n31 16\n",
        "DFLVL4 1 1\n2\nOBJ x\n", "DFLVL4 1 1\n2\nOBJ 0\nOBJFUTURE1 1\n0 123\n"}) {
        files["cannot_upgrade.bin"] = malformed;
        assert(!loadLevelBNNLVL("cannot_upgrade.bin", map, objects, loaded));
    }
    // Filtered legacy entries must not shift the value indices in saved files.
    assert(SerializeLevelObjects({{"player", 1, 2}, {"1102", 16, 16, 0}}) ==
        "OBJ 1\n1102 16 16\nOBJPARAM1 1\n0 0\n");
    // Entering applies once, staying does not undo another change, re-entry applies again.
    map.resize(4, 4);
    Player triggerPlayer;
    triggerPlayer.x = triggerPlayer.y = 0;
    std::vector<bool> contacts;
    objects = {{"1106", 16, 16, 3}};
    loaded = LevelMeta{};
    assert(LevelParameters::update(triggerPlayer, map, objects, contacts, loaded, false, false).size() == 1);
    assert(loaded.time == 3);
    loaded.time = 2;
    assert(LevelParameters::update(triggerPlayer, map, objects, contacts, loaded, false, false).empty());
    assert(loaded.time == 2);
    triggerPlayer.x = 300;
    LevelParameters::update(triggerPlayer, map, objects, contacts, loaded, false, false);
    triggerPlayer.x = 0;
    assert(LevelParameters::update(triggerPlayer, map, objects, contacts, loaded, false, false).size() == 1);
    assert(loaded.time == 3);
    // Seam contacts use the current wrap state.
    contacts.clear(); objects[0].x = 120; triggerPlayer.x = -20;
    assert(LevelParameters::update(triggerPlayer, map, objects, contacts, loaded, false, false).empty());
    assert(LevelParameters::update(triggerPlayer, map, objects, contacts, loaded, true, false).size() == 1);

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
    // Loading a save must select the qualified World 6 variant before spawning.
    manager.setButtonAreaActiveForDebug(6, 1, true);
    manager.setButtonAreaActiveForDebug(6, 2, true);
    for (int mask = 0; mask < 4; ++mask) {
        std::vector<int> savedButtons{11, 12}; // Other worlds never satisfy World 6.
        if (mask & 1) savedButtons.push_back(61);
        if (mask & 2) savedButtons.push_back(62);
        for (int area = 1; area <= 3; ++area) {
            const bool qualified = area == 1 || (area == 2 ? (mask & 1) != 0 : mask == 3);
            const int time = qualified ? 4 : 3;
            const auto path = manager.levelPathForSavedArea(6, area, savedButtons);
            assert(!path.empty() && path == manager.levelPathByCode(600 + area * 10 + time));
            const auto bytes = SaveFormat::encode({{"resume_world", 6}, {"resume_level", (area - 1) * 4 + 3},
                {"active_button_world_areas", savedButtons}});
            const auto decoded = SaveFormat::decode(bytes);
            assert(decoded && manager.levelPathForSavedArea((*decoded)["resume_world"],
                ((*decoded)["resume_level"].get<int>() - 1) / 4 + 1,
                (*decoded)["active_button_world_areas"].get<std::vector<int>>()) == path);
        }
    }
    assert(manager.levelPathForSavedArea(1, 1, {61, 62}) == manager.levelPathByCode(111));
    assert(manager.levelPathForSavedArea(0, 1, {}).empty());
    assert(manager.levelPathForSavedArea(7, 2, {}).empty());
    assert(manager.levelPathByCode(613) == "assets/levels/level_051.txt");
    assert(manager.levelPathByCode(713) == "assets/levels/level_057.txt");
    return 0;
}
