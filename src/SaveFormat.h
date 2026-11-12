#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace SaveFormat {
inline constexpr std::string_view magic = "23333334";
inline constexpr unsigned char version = 3;
inline constexpr size_t headerSize = 4;
// Four bytes: format number, buttons 1..4, buttons 5..6/world ID, flags/area ID.
// Pairs start at the least-significant bits; world and area occupy high nibbles.
inline std::string encode(const nlohmann::json& document) {
    std::array<unsigned char, 6> buttons{};
    if (document.contains("active_button_world_areas") && document["active_button_world_areas"].is_array())
        for (const auto& value : document["active_button_world_areas"])
            if (value.is_number_integer() && value >= 11 && value <= 63) {
                const int key = value.get<int>(), world = key / 10, area = key % 10;
                if (area >= 1 && area <= 2) buttons[world - 1] |= 1u << (area - 1);
            }
    const auto flags = document.value("progression", nlohmann::json::object());
    std::string result;
    result.push_back(static_cast<char>(version));
    const int world = std::clamp(document.value("resume_world", 1), 0, 7);
    const int level = std::clamp(document.value("resume_level", 1), 1, world == 0 ? 20 : 12);
    const bool hardmode = world == 0;
    const int storedWorld = hardmode ? (level - 1) / 10 + 1 : world;
    const int storedLevel = hardmode ? (level - 1) % 10 + 1 : (level - 1) / 4 + 1;
    result.push_back(static_cast<char>(buttons[0] | (buttons[1] << 2) | (buttons[2] << 4) | (buttons[3] << 6)));
    result.push_back(static_cast<char>(buttons[4] | (buttons[5] << 2) | (storedWorld << 4)));
    result.push_back(static_cast<char>((flags.value("hardmode_unlocked", false) ? 1 : 0) |
        (flags.value("world_select_unlocked", false) ? 2 : 0) |
        (flags.value("hardmode_complete", false) ? 4 : 0) | (hardmode ? 8 : 0) | (storedLevel << 4)));
    return result;
}
inline std::optional<nlohmann::json> decode(std::string_view bytes) {
    try {
        const bool compact = bytes.size() == headerSize && static_cast<unsigned char>(bytes[0]) == version;
        if (!compact && bytes.substr(0, magic.size()) != magic) {
            auto legacy = nlohmann::json::parse(bytes);
            if (legacy.is_object()) return legacy;
            return std::nullopt;
        }
        if (!compact && bytes.size() < 9) return std::nullopt;
        const auto fileVersion = compact ? version : static_cast<unsigned char>(bytes[8]);
        const bool legacy = fileVersion == 1;
        if (!compact && ((legacy && bytes.size() != 17) || (!legacy && (fileVersion != 2 || bytes.size() != 12))))
            return std::nullopt;
        const auto unlocks = static_cast<unsigned char>(bytes[compact ? 3 : (legacy ? 15 : 11)]);
        const auto worldByte = static_cast<unsigned char>(bytes[compact ? 2 : (legacy ? 16 : 10)]);
        const bool hardmode = (unlocks & 8) != 0;
        const int storedWorld = legacy ? worldByte & 7 : worldByte >> 4;
        const int storedLevel = unlocks >> 4;
        const int world = hardmode ? 0 : storedWorld;
        const int level = hardmode ? (storedWorld - 1) * 10 + storedLevel :
            (compact ? (storedLevel - 1) * 4 + ((world >= 6 || storedLevel == 3) ? 3 : 1) : storedLevel);
        const int furthest = legacy ? worldByte >> 3 : ((unlocks & 4) ? 20 : (hardmode ? level : 0));
        if (furthest > 20 || storedWorld < 1 || storedLevel < 1 ||
            (hardmode ? (storedWorld > 2 || storedLevel > 10) : (storedWorld > 7 || storedLevel > (compact ? 3 : 12) || (compact && storedWorld == 7 && storedLevel != 1)))) return std::nullopt;
        auto document = nlohmann::json::object();
        if (!document.is_object()) return std::nullopt;
        auto& flags = document["progression"];
        if (flags.is_null()) flags = nlohmann::json::object();
        if (!flags.is_object()) return std::nullopt;
        flags["hardmode_unlocked"] = (unlocks & 1) != 0;
        flags["world_select_unlocked"] = (unlocks & 2) != 0;
        flags["hardmode_complete"] = (unlocks & 4) != 0;
        flags["hardmode_furthest_level"] = furthest;
        document["resume_world"] = world;
        document["resume_level"] = level;
        auto& areas = document["active_button_world_areas"];
        areas = nlohmann::json::array();
        for (int world = 1; world <= 6; ++world) {
            const auto state = legacy ? static_cast<unsigned char>(bytes[8 + world]) :
                (static_cast<unsigned char>(bytes[compact ? (world <= 4 ? 1 : 2) : (world <= 4 ? 9 : 10)]) >> (2 * ((world - 1) % 4))) & 3u;
            if (legacy && (state & ~7u)) return std::nullopt;
            for (int area = 1; area <= (legacy ? 3 : 2); ++area)
                if (state & (1u << (area - 1))) areas.push_back(world * 10 + area);
        }
        return document;
    } catch (...) { return std::nullopt; }
}
}
