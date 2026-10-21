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
enum class Part { Skin, Hair, Shirt, Pants, Hat, Accent, None };
inline constexpr std::array<const char*, 6> partIds{{"skin", "hair", "shirt", "pants", "hat", "accent"}};
inline constexpr std::array<const char*, 6> partNames{{"Skin", "Hair", "Shirt", "Pants", "Headwear", "Accent"}};
inline constexpr std::array<const char*, 7> textureNames{{"Solid", "Stripes", "Checks", "Dots", "Denim", "Metal", "Imported Image"}};
inline constexpr std::array<int, 5> statPercent{{75, 90, 100, 110, 125}};
inline constexpr std::array<const char*, 3> builds{{"Slim", "Standard", "Broad"}};

inline int partIndex(const std::string& id) {
    for (int i = 0; i < (int)partIds.size(); ++i) if (id == partIds[i]) return i;
    return -1;
}
inline bool validPixelData(const std::string& pixels, size_t bytes) {
    if (pixels.size() != bytes * 2) return false;
    return std::all_of(pixels.begin(), pixels.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
inline bool validTexturePixels(const std::string& pixels) { return validPixelData(pixels, 8 * 8 * 4); }
inline constexpr std::array<const char*, 11> spriteFrameNames{{"idle", "Walk-1", "Walk-2", "Walk-3", "Walk-4",
    "Jump", "Falling", "Crouched(unused)", "Skiding", "Hurt", "Death"}};
struct SpriteLayers {
    std::string base, bangs, name;
    bool valid() const { return validPixelData(base, 176 * 28 * 4) && validPixelData(bangs, 176 * 28 * 4); }
};

struct Appearance {
    bool enabled = false;
    int skin = 0;
    int hairColor = 0;
    int hairStyle = 0;
    int shirt = 7;
    int pants = 0;
    int hat = 0;
    int accent = 4;
    std::array<int, 6> textures{};
    std::array<std::string, 6> texturePixels{};
    int build = 1;
    int springiness = 2;
    int speed = 2;
    int acceleration = 2;
    int jump = 2;
    int gravity = 2;
    SpriteLayers sprites;

    void normalize() {
        if (!sprites.valid()) sprites = {};
        if (sprites.name.size() > 80) sprites.name.resize(80);
        skin = std::clamp(skin, 0, (int)skinColors.size() - 1);
        hairColor = std::clamp(hairColor, 0, (int)colors.size() - 1);
        hairStyle = std::clamp(hairStyle, 0, (int)hairStyles.size() - 1);
        shirt = std::clamp(shirt, 0, (int)colors.size() - 1);
        pants = std::clamp(pants, 0, (int)colors.size() - 1);
        hat = std::clamp(hat, 0, (int)hats.size() - 1);
        accent = std::clamp(accent, 0, (int)colors.size() - 1);
        build = std::clamp(build, 0, 2);
        for (int* stat : {&springiness, &speed, &acceleration, &jump, &gravity}) *stat = std::clamp(*stat, 0, 4);
        for (int i = 0; i < 6; ++i) {
            if (!validTexturePixels(texturePixels[i])) texturePixels[i].clear();
            textures[i] = std::clamp(textures[i], 0, 6);
            if (textures[i] == 6 && texturePixels[i].empty()) textures[i] = 0;
        }
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
        {"shirt", a.shirt}, {"pants", a.pants}, {"hat", a.hat}, {"accent", a.accent},
        {"textures", a.textures}, {"texture_pixels", a.texturePixels}, {"build", a.build},
        {"springiness", a.springiness}, {"speed", a.speed}, {"acceleration", a.acceleration},
        {"jump", a.jump}, {"gravity", a.gravity},
        {"sprite_layers", {{"base", a.sprites.base}, {"bangs", a.sprites.bangs}, {"name", a.sprites.name}}}};
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
    a.build = index("build", a.build);
    a.springiness = index("springiness", a.springiness);
    a.speed = index("speed", a.speed);
    a.acceleration = index("acceleration", a.acceleration);
    a.jump = index("jump", a.jump);
    a.gravity = index("gravity", a.gravity);
    if (json.contains("sprite_layers") && json["sprite_layers"].is_object()) {
        const auto& layers = json["sprite_layers"];
        if (layers.contains("base") && layers["base"].is_string() && layers.contains("bangs") && layers["bangs"].is_string()) {
            const auto& base = layers["base"].get_ref<const std::string&>();
            const auto& bangs = layers["bangs"].get_ref<const std::string&>();
            if (validPixelData(base, 176 * 28 * 4) && validPixelData(bangs, 176 * 28 * 4)) {
                a.sprites.base = base;
                a.sprites.bangs = bangs;
                if (layers.contains("name") && layers["name"].is_string()) a.sprites.name = layers["name"].get<std::string>().substr(0, 80);
            }
        }
    }
    for (int i = 0; i < 6; ++i) {
        if (json.contains("textures") && json["textures"].is_array() && i < (int)json["textures"].size() &&
            json["textures"][i].is_number_integer())
            a.textures[i] = (int)std::clamp(json["textures"][i].get<double>(), 0.0, 6.0);
        if (json.contains("texture_pixels") && json["texture_pixels"].is_array() && i < (int)json["texture_pixels"].size() &&
            json["texture_pixels"][i].is_string()) {
            const auto& pixels = json["texture_pixels"][i].get_ref<const std::string&>();
            if (validTexturePixels(pixels)) a.texturePixels[i] = pixels;
        }
    }
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
    if (part.rfind("texture:", 0) == 0) {
        const int i = partIndex(part.substr(8));
        if (i < 0) return false;
        a.textures[i] = (a.textures[i] + 1) % (a.texturePixels[i].empty() ? 6 : 7);
        return true;
    }
    if (part == "enabled") a.enabled = !a.enabled;
    else if (part == "reset") a = Appearance{};
    else if (part == "skin") a.skin = (a.skin + 1) % skinColors.size();
    else if (part == "hair_color") a.hairColor = (a.hairColor + 1) % colors.size();
    else if (part == "hair_style") a.hairStyle = (a.hairStyle + 1) % hairStyles.size();
    else if (part == "shirt") a.shirt = (a.shirt + 1) % colors.size();
    else if (part == "pants") a.pants = (a.pants + 1) % colors.size();
    else if (part == "hat") a.hat = (a.hat + 1) % hats.size();
    else if (part == "accent") a.accent = (a.accent + 1) % colors.size();
    else if (part == "build") a.build = (a.build + 1) % builds.size();
    else if (part == "springiness") a.springiness = (a.springiness + 1) % statPercent.size();
    else if (part == "speed") a.speed = (a.speed + 1) % statPercent.size();
    else if (part == "acceleration") a.acceleration = (a.acceleration + 1) % statPercent.size();
    else if (part == "jump") a.jump = (a.jump + 1) % statPercent.size();
    else if (part == "gravity") a.gravity = (a.gravity + 1) % statPercent.size();
    else if (part == "reset_attributes") { a.build = 1; a.springiness = a.speed = a.acceleration = a.jump = a.gravity = 2; }
    else if (part == "clear_textures") { a.textures = {}; a.texturePixels = {}; }
    else if (part == "procedural_parts") a.sprites = {};
    else return false;
    return true;
}
} // namespace CharacterCreator
