#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <nlohmann/json.hpp>

namespace CharacterCreator {
struct Color {
    unsigned char r, g, b;
    const char* name;
};
inline constexpr std::array<Color, 6> skinColors{{
    {255, 219, 172, "Peach"}, {230, 177, 126, "Sand"}, {195, 134, 85, "Warm"},
    {145, 92, 58, "Brown"}, {97, 61, 44, "Deep"}, {70, 43, 35, "Dark"}
}};
inline constexpr std::array<Color, 10> colors{{
    {45, 48, 65, "Ink"}, {248, 241, 216, "Cream"}, {223, 75, 80, "Red"},
    {245, 155, 67, "Orange"}, {246, 213, 91, "Gold"}, {88, 178, 112, "Green"},
    {76, 190, 201, "Teal"}, {81, 127, 218, "Blue"}, {161, 111, 210, "Violet"},
    {230, 139, 182, "Pink"}
}};
inline constexpr std::array<const char*, 4> hairStyles{{"Short", "Spiky", "Long", "Shaved"}};
inline constexpr std::array<const char*, 4> hats{{"None", "Cap", "Beanie", "Crown"}};

struct Appearance {
    bool enabled = false;
    int skin = 0;
    int hairColor = 0;
    int hairStyle = 0;
    int shirt = 7;
    int pants = 0;
    int hat = 0;
    int accent = 4;

    void normalize() {
        skin = std::clamp(skin, 0, (int)skinColors.size() - 1);
        hairColor = std::clamp(hairColor, 0, (int)colors.size() - 1);
        hairStyle = std::clamp(hairStyle, 0, (int)hairStyles.size() - 1);
        shirt = std::clamp(shirt, 0, (int)colors.size() - 1);
        pants = std::clamp(pants, 0, (int)colors.size() - 1);
        hat = std::clamp(hat, 0, (int)hats.size() - 1);
        accent = std::clamp(accent, 0, (int)colors.size() - 1);
    }
};

struct Preset {
    const char* id;
    const char* name;
    Appearance appearance;
};
inline const std::array<Preset, 6> placeholderCharacters{{
    {"explorer", "Explorer", {true, 0, 0, 0, 7, 0, 1, 4}},
    {"runner", "Runner", {true, 2, 0, 1, 2, 0, 0, 1}},
    {"mechanic", "Mechanic", {true, 4, 0, 0, 3, 7, 2, 6}},
    {"ranger", "Ranger", {true, 1, 3, 2, 5, 0, 0, 4}},
    {"royal", "Royal", {true, 5, 1, 2, 8, 0, 3, 4}},
    {"frost", "Frost", {true, 3, 1, 3, 6, 7, 2, 1}}
}};

inline nlohmann::json save(const Appearance& appearance) {
    Appearance a = appearance;
    a.normalize();
    return {{"version", 1}, {"experimental", true}, {"enabled", a.enabled},
        {"skin", a.skin}, {"hair_color", a.hairColor}, {"hair_style", a.hairStyle},
        {"shirt", a.shirt}, {"pants", a.pants}, {"hat", a.hat}, {"accent", a.accent}};
}

inline Appearance load(const nlohmann::json& json) {
    Appearance a;
    if (!json.is_object()) return a;
    // Future formats fall back to the default character rather than guessing.
    if (json.contains("version") && (!json["version"].is_number_integer() || json["version"] != 1)) return a;
    auto index = [&](const char* name, int fallback) {
        const auto it = json.find(name);
        if (it == json.end() || !it->is_number_integer()) return fallback;
        // Bound before converting to int, including very large unsigned values.
        const double value = it->get<double>();
        return (int)std::clamp(value, 0.0, 100.0);
    };
    if (json.contains("enabled") && json["enabled"].is_boolean()) a.enabled = json["enabled"].get<bool>();
    a.skin = index("skin", a.skin);
    a.hairColor = index("hair_color", a.hairColor);
    a.hairStyle = index("hair_style", a.hairStyle);
    a.shirt = index("shirt", a.shirt);
    a.pants = index("pants", a.pants);
    a.hat = index("hat", a.hat);
    a.accent = index("accent", a.accent);
    a.normalize();
    return a;
}

inline bool change(Appearance& a, const std::string& part) {
    if (part.rfind("preset:", 0) == 0) {
        for (const auto& preset : placeholderCharacters) {
            if (part == std::string("preset:") + preset.id) {
                a = preset.appearance;
                return true;
            }
        }
        return false;
    }
    a.normalize();
    if (part == "enabled") a.enabled = !a.enabled;
    else if (part == "reset") a = Appearance{};
    else if (part == "skin") a.skin = (a.skin + 1) % skinColors.size();
    else if (part == "hair_color") a.hairColor = (a.hairColor + 1) % colors.size();
    else if (part == "hair_style") a.hairStyle = (a.hairStyle + 1) % hairStyles.size();
    else if (part == "shirt") a.shirt = (a.shirt + 1) % colors.size();
    else if (part == "pants") a.pants = (a.pants + 1) % colors.size();
    else if (part == "hat") a.hat = (a.hat + 1) % hats.size();
    else if (part == "accent") a.accent = (a.accent + 1) % colors.size();
    else return false;
    return true;
}
} // namespace CharacterCreator
