#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace IosQuickResume {
using Json = nlohmann::json;
struct SpawnLock { int frames; float x, y; };
inline SpawnLock restoredSpawnLock(const Json& snapshot, int maximumFrames, float playerX, float playerY) {
    // Older snapshots omitted the lock: never inherit a fresh level spawn lock.
    return {std::clamp(snapshot.value("player_spawn_lock_frames", 0), 0, maximumFrames),
        snapshot.value("player_spawn_lock_x", playerX),
        snapshot.value("player_spawn_lock_y", playerY)};
}
inline std::string revision(const std::string& bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    return std::to_string(hash);
}
inline bool finite(const Json& value) {
    if (value.is_number()) return std::isfinite(value.get<double>()) && std::fabs(value.get<double>()) <= 1e9;
    if (value.is_structured()) for (const auto& child : value) if (!finite(child)) return false;
    return true;
}
inline bool validSnapshot(const Json& value) {
    try {
        if (!value.is_object() || !finite(value)) return false;
        const auto path = value.at("level_path").get<std::string>();
        if (path.empty() || path.size() > 4096 || path.find("://") != std::string::npos) return false;
        const auto& map = value.at("map");
        const int w = map.at("w"), h = map.at("h"), tile = map.at("tile_size");
        if (w < 1 || h < 1 || w > 400 || h > 400 || tile < 1 || tile > 256) return false;
        for (const auto* name : {"tile_ids", "bg"}) {
            const auto& layer = map.at(name);
            if (!layer.is_array() || layer.size() != static_cast<size_t>(w * h)) return false;
            for (const auto& id : layer)
                if (!id.is_number_integer() || id < 0 || id > 65535) return false;
        }
        const auto& player = value.at("player");
        for (const auto* name : {"x", "y", "vx", "vy"})
            if (!player.at(name).is_number() || std::fabs(player.at(name).get<double>()) > 1e7) return false;
        for (const auto* name : {"w", "h"}) {
            const int size = player.at(name);
            if (size < 1 || size > 256) return false;
        }
        for (auto it = player.begin(); it != player.end(); ++it) {
            const auto& name = it.key();
            const bool boolean = name == "onGround" || name == "hasShield" || name == "inWater" || name == "freeMove" ||
                name == "jumpHeld" || name == "jumpWasDown" || name == "slideWasDown" || name == "tile12BounceActive";
            if (boolean ? !it.value().is_boolean() : !it.value().is_number()) return false;
        }
        for (const auto* name : {"meta", "boss", "checkpoint", "cycling_blocks"}) {
            if (!value.contains(name)) continue;
            if (!value[name].is_object()) return false;
            for (auto it = value[name].begin(); it != value[name].end(); ++it) {
                const auto& key = it.key();
                const bool string = key == "name" || key == "level_path";
                const bool boolean = key == "active" || key == "wrap_x" || key == "wrap_y" || key == "bouncy_tile_12";
                if (string ? !it.value().is_string() : (boolean ? !it.value().is_boolean() : !it.value().is_number())) return false;
            }
        }
        for (const auto* name : {"secret_fireballs", "secret_explosions", "dropped_coins", "shell_states"}) {
            if (!value.contains(name)) continue;
            if (!value[name].is_array() || value[name].size() > 100000) return false;
            for (const auto& entry : value[name]) {
                if (!entry.is_object()) return false;
                for (auto it = entry.begin(); it != entry.end(); ++it)
                    if (it.key() == "hitPlayer" || it.key() == "hitBoss" || it.key() == "cycling") { if (!it.value().is_boolean()) return false; }
                    else if (!it.value().is_number()) return false;
            }
        }
        if (value.contains("active_button_world_areas")) {
            if (!value["active_button_world_areas"].is_array() || value["active_button_world_areas"].size() > 21) return false;
            for (const auto& key : value["active_button_world_areas"]) if (!key.is_number_integer() || key < 11 || key > 73 || key.get<int>() % 10 < 1 || key.get<int>() % 10 > 3) return false;
        }
        if (value.contains("parameter_contacts")) {
            if (!value["parameter_contacts"].is_array() || value["parameter_contacts"].size() > 100000) return false;
            for (const auto& contact : value["parameter_contacts"]) if (!contact.is_boolean()) return false;
        }
        const auto& objects = value.at("objects");
        if (!objects.is_array() || objects.size() > 100000) return false;
        for (const auto& object : objects) {
            if (!object.is_object() || !object.at("id").is_string() || object.at("id").get<std::string>().size() > 32) return false;
            if (object.contains("layer") && !object["layer"].is_number_integer()) return false;
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (it.key() == "id") continue;
                if (it.key() == "platform_started") { if (!it.value().is_boolean()) return false; }
                else if (!it.value().is_number()) return false;
            }
            for (const auto* name : {"x", "y"})
                if (!object.at(name).is_number() || std::fabs(object.at(name).get<double>()) > 1e7) return false;
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() == "level_path" || it.key() == "custom_level_name") {
                if (!it.value().is_string()) return false;
            } else if (it.key() == "allow_next_level_progression" || it.key() == "hardmode_practice") {
                if (!it.value().is_boolean()) return false;
            } else if (it.key() != "map" && it.key() != "player" && it.key() != "meta" && it.key() != "objects" &&
                       it.key() != "boss" && it.key() != "checkpoint" && it.key() != "cycling_blocks" &&
                       it.key() != "active_button_world_areas" && it.key() != "parameter_contacts" &&
                       it.key() != "dropped_coins" && it.key() != "shell_states" && it.key() != "secret_fireballs" && it.key() != "secret_explosions" &&
                       !it.value().is_number()) return false;
        }
        return true;
    } catch (...) { return false; }
}
class Store {
    std::filesystem::path path_;
    std::string edition_;
    static constexpr size_t maxSize = 16 * 1024 * 1024;
public:
    Store(const std::filesystem::path& root, std::string edition) : path_(root / "quick_resume.cbor"), edition_(std::move(edition)) {}
    void clear() { std::error_code error; std::filesystem::remove(path_, error); }
    std::optional<Json> load() const {
        try {
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path_)) || std::filesystem::file_size(path_) > maxSize) return {};
            std::ifstream file(path_, std::ios::binary);
            const auto size = std::filesystem::file_size(path_);
            if (size > maxSize) return {};
            std::vector<uint8_t> bytes(size);
            file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            if (!file || file.bad()) return {};
            auto value = Json::from_cbor(bytes);
            if (value.at("schema") != 1 || value.at("edition") != edition_ || !value.at("slot").is_number_integer() ||
                value.at("slot") < 0 || value.at("slot") > 2 || !value.at("level_revision").is_string() ||
                !validSnapshot(value.at("snapshot"))) return {};
            return value;
        } catch (...) { return {}; }
    }
    bool save(const Json& snapshot, int slot, const std::string& levelRevision) const {
        if (slot < 0 || slot > 2 || levelRevision.empty() || !validSnapshot(snapshot)) return false;
        auto temporary = path_;
        temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        try {
            const auto bytes = Json::to_cbor(Json{{"schema",1},{"edition",edition_},{"slot",slot},
                {"level_revision",levelRevision},{"snapshot",snapshot}});
            if (bytes.size() > maxSize) return false;
            std::filesystem::create_directories(path_.parent_path());
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                file.flush(); if (!file) throw std::runtime_error("Quick resume write failed");
            }
#ifndef _WIN32
            int descriptor = open(temporary.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
            if (descriptor < 0) throw std::runtime_error("Quick resume sync failed");
            const bool synced = fsync(descriptor) == 0;
            close(descriptor);
            if (!synced) throw std::runtime_error("Quick resume sync failed");
#endif
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Quick resume replacement failed");
#else
            std::filesystem::rename(temporary, path_);
#endif
            return true;
        } catch (...) { std::error_code error; std::filesystem::remove(temporary, error); return false; }
    }
};
}
