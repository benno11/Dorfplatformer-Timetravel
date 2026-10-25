#pragma once
#include "CharacterAppearance.h"
#include <SDL3/SDL.h>

namespace CharacterCreator {
inline std::string exportCharacter(const Appearance& appearance) {
    return nlohmann::json{{"format", "df-character"}, {"version", 1},
        {"experimental", true}, {"character", save(appearance)}}.dump(2);
}

// Validate the interchange format before touching the current character.
inline bool importCharacter(const std::string& text, Appearance& appearance) {
    if (text.size() > 262144) return false;
    const auto document = nlohmann::json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("format") || document["format"] != "df-character" ||
        !document.contains("version") || !document["version"].is_number_integer() || document["version"] != 1 ||
        !document.contains("character") || !document["character"].is_object()) return false;
    const auto& character = document["character"];
    if (!character.contains("version") || !character["version"].is_number_integer() || character["version"] != 1 ||
        !character.contains("enabled") || !character["enabled"].is_boolean()) return false;
    for (const char* key : {"skin", "hair_color", "hair_style", "shirt", "pants", "hat", "accent"})
        if (!character.contains(key) || !character[key].is_number_integer()) return false;
    for (const char* key : {"build", "springiness", "speed", "acceleration", "jump", "gravity"})
        if (character.contains(key) && !character[key].is_number_integer()) return false;
    if (character.contains("hair_pixels")) {
        if (!character["hair_pixels"].is_string()) return false;
        const auto& pixels = character["hair_pixels"].get_ref<const std::string&>();
        if (!pixels.empty() && !validHairPixels(pixels)) return false;
    }
    if (character.contains("uncap_hair_length") && !character["uncap_hair_length"].is_boolean()) return false;
    const bool uncappedHair = character.value("uncap_hair_length", false);
    if (character.contains("hair_shape")) {
        const auto& shape = character["hair_shape"];
        // Five-value files predate the independently configurable side strand.
        if (!shape.is_array() || (shape.size() != 5 && shape.size() != hairShapeIds.size())) return false;
        for (int i = 0; i < (int)shape.size(); ++i)
            if (!shape[i].is_number_integer() || shape[i] < (i == 3 ? 1 : 0) || shape[i] > hairShapeLimit(i, uncappedHair)) return false;
    }
    if (character["hair_style"] == customHairStyle &&
        (!character.contains("hair_pixels") || !validHairPixels(character["hair_pixels"].get_ref<const std::string&>()))) return false;
    if (character.contains("sprite_layers")) {
        const auto& layers = character["sprite_layers"];
        if (!layers.is_object() || !layers.contains("base") || !layers["base"].is_string() ||
            !layers.contains("bangs") || !layers["bangs"].is_string()) return false;
        const auto& base = layers["base"].get_ref<const std::string&>();
        const auto& bangs = layers["bangs"].get_ref<const std::string&>();
        if ((!base.empty() || !bangs.empty()) &&
            (!validPixelData(base, 176 * 28 * 4) || !validPixelData(bangs, 176 * 28 * 4))) return false;
        if (layers.contains("name") && (!layers["name"].is_string() || layers["name"].get_ref<const std::string&>().size() > 80)) return false;
    }
    for (const char* key : {"textures", "texture_pixels"}) {
        if (!character.contains(key)) continue;
        if (!character[key].is_array() || character[key].size() != 6) return false;
        for (const auto& value : character[key]) {
            if (std::string(key) == "textures") {
                if (!value.is_number_integer() || value < 0 || value > 6) return false;
            } else {
                if (!value.is_string()) return false;
                const auto& pixels = value.get_ref<const std::string&>();
                if (!pixels.empty() && !validTexturePixels(pixels)) return false;
            }
        }
    }
    if (character.contains("textures")) for (int i = 0; i < 6; ++i) {
        if (character["textures"][i] == 6 && (!character.contains("texture_pixels") ||
            !validTexturePixels(character["texture_pixels"][i].get_ref<const std::string&>()))) return false;
    }
    appearance = load(character);
    return true;
}

inline bool readCharacterText(const std::string& path, std::string& text) {
    SDL_IOStream* file = SDL_IOFromFile(path.c_str(), "rb");
    if (!file) return false;
    text.clear();
    char buffer[4096];
    bool valid = true;
    for (;;) {
        const size_t count = SDL_ReadIO(file, buffer, sizeof(buffer));
        if (text.size() + count > 262144) { valid = false; break; }
        text.append(buffer, count);
        if (count == 0) {
            valid = SDL_GetIOStatus(file) == SDL_IO_STATUS_EOF;
            break;
        }
    }
    const bool closed = SDL_CloseIO(file);
    return valid && closed;
}
inline bool readCharacterFile(const std::string& path, Appearance& appearance) {
    std::string text;
    return readCharacterText(path, text) && importCharacter(text, appearance);
}

inline bool writeCharacterFile(const std::string& path, const Appearance& appearance) {
    const std::string text = exportCharacter(appearance);
    SDL_IOStream* file = SDL_IOFromFile(path.c_str(), "wb");
    if (!file) return false;
    const bool written = SDL_WriteIO(file, text.data(), text.size()) == text.size();
    const bool closed = SDL_CloseIO(file);
    return written && closed;
}
} // namespace CharacterCreator
