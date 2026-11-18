#include "IosQuickResume.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <iostream>

int main() {
    using namespace IosQuickResume;
    const auto root = std::filesystem::temp_directory_path() / ("df-quick-resume-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto normal = root / "save_slot_1.json";
    { std::ofstream file(normal); file << "normal save unchanged"; }
    Json state = {{"level_path","assets/levels/level_001.txt"},
        {"map",{{"w",2},{"h",2},{"tile_size",32},{"tile_ids",{2,48,4,2}},{"bg",{0,0,0,0}}}},
        {"player",{{"x",31.5},{"y",42.25},{"vx",90.0},{"vy",-40.0},{"w",30},{"h",56},{"hasShield",true}}},
        {"objects",Json::array({{{"id","12"},{"x",64.0},{"y",32.0},{"platform_elapsed",2.5}}})},
        {"coin_count",9},{"active_button_world_areas",{11,12,51}},
        {"checkpoint",{{"active",true},{"level_path","assets/levels/level_001.txt"},{"tile_x",1},{"tile_y",1}}}};
    auto lock = restoredSpawnLock(state, 6, state["player"]["x"], state["player"]["y"]);
    assert(lock.frames == 0 && lock.x == 31.5f && lock.y == 42.25f);
    // The first resumed gameplay frame must not snap to the level spawn.
    float x = state["player"]["x"], y = state["player"]["y"];
    if (lock.frames > 0) { x = lock.x; y = lock.y; }
    assert(x == 31.5f && y == 42.25f);
    state["player_spawn_lock_frames"] = 3;
    state["player_spawn_lock_x"] = 31.5f; state["player_spawn_lock_y"] = 42.25f;
    lock = restoredSpawnLock(state, 6, state["player"]["x"], state["player"]["y"]);
    assert(lock.frames == 3 && lock.x == x && lock.y == y);
    Store store(root,"full");
    assert(!store.load());
    const auto stamp = revision("original level");
    assert(stamp != revision("updated level"));
    assert(store.save(state,2,stamp));
    Store relaunched(root,"full");
    const auto loaded = relaunched.load();
    assert(loaded && loaded->at("snapshot") == state && loaded->at("slot") == 2);
    assert(!Store(root,"lite").load());
    state["coin_count"] = 10;
    assert(store.save(state,2,stamp));
    assert(store.load()->at("snapshot").at("coin_count") == 10);
    auto bad = state; bad["map"]["tile_ids"] = {2};
    assert(!store.save(bad,2,stamp));
    bad = state; bad["player"]["x"] = std::numeric_limits<double>::infinity();
    assert(!store.save(bad,2,stamp));
    bad = state; bad["player"]["onGround"] = "invalid";
    assert(!store.save(bad,2,stamp));
    bad = state; bad["boss"] = {{"health","invalid"}};
    assert(!store.save(bad,2,stamp));
    bad = state; bad["coin_count"] = Json::object();
    assert(!store.save(bad,2,stamp));
    bad = state; bad["objects"][0]["platform_started"] = "invalid";
    assert(!store.save(bad,2,stamp));
    assert(!store.save(state,3,stamp));
    assert(store.load()->at("snapshot").at("coin_count") == 10);
    { std::ofstream file(root/"quick_resume.cbor",std::ios::binary|std::ios::trunc); file << "broken"; }
    assert(!store.load());
    store.clear(); assert(!store.load());
    std::ifstream original(normal); std::string contents; std::getline(original,contents);
    assert(contents == "normal save unchanged");
    std::filesystem::remove_all(root);
    std::cout << "Quick resume relaunch, atomic replacement, corruption, revision and edition checks passed\n";
}
