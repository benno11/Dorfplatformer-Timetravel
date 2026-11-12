#include "NativeMods.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
int main(int argc, char** argv) {
    assert(argc == 2 && SDL_Init(0));
    const auto root = fs::temp_directory_path() / ("df-mod-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "mods");
    auto manifest = [&](const std::string& id, int major = 1, int minor = 0, const std::vector<std::string>& dependencies = {}) {
        const auto folder = root / "mods" / id; fs::create_directories(folder);
        const auto lib = fs::path(argv[1]).filename();
        fs::copy_file(argv[1], folder / lib, fs::copy_options::overwrite_existing);
        nlohmann::json j = {{"id", id}, {"name", id}, {"version", "999.42.3"},
            {"game_version", "an older minor game version"},
            {"api", {{"major", major}, {"minor", minor}}},
            {"libraries", {{NativeMods::Loader::platform(), lib.string()}}}, {"dependencies", dependencies}};
        std::ofstream(folder / "mod.json") << j;
    };
    manifest("good"); manifest("minimal.prefix"); manifest("disabled"); manifest("wrong.major", 2); manifest("future.minor", 1, 1);
    manifest("missing.dependency", 1, 0, {"not.installed"});
    manifest("cycle.a", 1, 0, {"cycle.b"}); manifest("cycle.b", 1, 0, {"cycle.a"});
    manifest("overflow.major");
    { auto path = root / "mods/overflow.major/mod.json"; std::ifstream in(path); nlohmann::json j; in >> j;
      j["api"]["major"] = uint64_t{4294967297}; std::ofstream(path) << j; }
    manifest("outside");
    { auto path = root / "mods/outside/mod.json"; std::ifstream in(path); nlohmann::json j; in >> j;
      j["libraries"][NativeMods::Loader::platform()] = "../../outside.so"; std::ofstream(path) << j; }
    std::ofstream(root / "mods/enabled.json") << "{\"disabled\":false}";
    {
        NativeMods::Loader loader(root.string()); loader.start();
        int loaded = 0;
        for (const auto& mod : loader.list()) { if (mod.loaded) { ++loaded; assert(mod.id == "good" || mod.id == "minimal.prefix"); } }
        assert(loaded == 2);
        DFPlayerState p{sizeof(p), 1, 2, 0, 0, 1, 1, 0};
        DFLevelState level{sizeof(level), 42, 1, 1, 3, 30, 20, 32};
        loader.tick(.1, p, level, 1);
        assert(p.x == 11 && p.velocity_y == -600 && !p.on_ground && std::isfinite(p.x));
        loader.tick(.1, p, level, 1); assert(p.x == 11);
        loader.tick(.1, p, level, 2); assert(p.x == 21);
        std::string status;
        assert(loader.toggle("good", status));
        loader.tick(.1, p, level, 2); assert(p.velocity_x > .39f); // Disable waits for restart.
        assert(loader.toggle("wrong.major", status)); // Incompatible installs can still be disabled.
        assert(loader.toggle("disabled", status));
        loader.shutdown();
        assert(fs::is_regular_file(root / "mods/.config/good/unloaded"));
    }
    {
        NativeMods::Loader loader(root.string()); loader.start();
        for (const auto& mod : loader.list()) if (mod.id == "good") assert(!mod.loaded && !mod.enabled);
        else if (mod.id == "disabled") assert(mod.loaded && mod.enabled);
    }
    fs::remove_all(root / "mods");
    manifest("a.dependent", 1, 0, {"z.base"}); manifest("z.base");
    {
        NativeMods::Loader loader(root.string()); loader.start();
        for (const auto& mod : loader.list()) assert(mod.loaded);
        std::ifstream in(root / "mods/.config/load-order");
        std::string base, dependent; std::getline(in, base); std::getline(in, dependent);
        assert(base == "z.base" && dependent == "a.dependent");
        loader.shutdown();
        std::ifstream out(root / "mods/.config/unload-order");
        std::getline(out, dependent); std::getline(out, base);
        assert(base == "z.base" && dependent == "a.dependent");
    }
    fs::remove_all(root);
    SDL_Quit();
}
