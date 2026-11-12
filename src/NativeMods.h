#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../sdk/include/df_mod.h"

namespace NativeMods {
struct Info { std::string id, name, version, status; bool enabled = false, loaded = false; };
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
    void tick(double seconds, DFPlayerState& player, const DFLevelState& level, uint64_t generation);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
