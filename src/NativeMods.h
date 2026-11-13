#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../sdk/include/df_mod.h"

namespace NativeMods {
struct Info { std::string id, name, version, status, icon; bool enabled = false, loaded = false; };
struct MenuItem { int type = DF_MENU_TEXT; std::string label, command, argument; int originalIndex = -1; };
struct Menu { std::string id, title; std::vector<MenuItem> items; };
struct TileAccess {
    std::function<int(int, int)> get;
    std::function<void(int, int, int)> set;
};
class Loader {
public:
    explicit Loader(const std::string& saveRoot);
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;
    void start();
    void shutdown();
    void rescan();
    std::vector<Info> list() const;
    bool toggle(const std::string& id, std::string& status);
    const std::string& folder() const;
    static std::string platform();
    void tick(double seconds, DFPlayerState& player, const DFLevelState& level, uint64_t generation,
              DFRunState* run = nullptr, const TileAccess* tiles = nullptr);
    void editMenu(Menu& menu);
    void menuAction(const std::string& menu, const std::string& action);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
