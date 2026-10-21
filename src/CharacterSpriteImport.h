#pragma once
#include "CharacterFile.h"
#include "CharacterTextures.h"
#include <filesystem>
#include <functional>
#include <vector>

namespace CharacterCreator {
inline bool readSpritePack(const std::string& manifestPath, std::vector<Appearance>& characters,
    const std::function<SDL_Surface*(const char*)>& loadImage) {
    std::string text;
    if (!readCharacterText(manifestPath, text)) return false;
    const auto manifest = nlohmann::json::parse(text, nullptr, false);
    if (!manifest.is_object() || !manifest.contains("characters") || !manifest["characters"].is_object() ||
        manifest["characters"].empty() || manifest["characters"].size() > 16) return false;
    std::vector<Appearance> imported;
    for (auto it = manifest["characters"].begin(); it != manifest["characters"].end(); ++it) {
        const auto& character = it.value();
        if (!character.is_object() || !character.contains("frame_width") || character["frame_width"] != 16 ||
            !character.contains("frame_height") || character["frame_height"] != 28 ||
            !character.contains("order") || !character["order"].is_array() || character["order"].size() != 11) return false;
        std::array<int, 11> order{};
        for (int frame = 0; frame < 11; ++frame) {
            int matches = 0;
            for (int source = 0; source < 11; ++source) if (character["order"][source] == spriteFrameNames[frame]) {
                order[frame] = source;
                ++matches;
            }
            if (matches != 1) return false;
        }
        Appearance appearance;
        appearance.enabled = true;
        appearance.sprites.name = it.key().substr(0, 80);
        for (const char* key : {"hair_base", "bangs_overlay"}) {
            if (!character.contains(key) || !character[key].is_string()) return false;
            const auto filename = std::filesystem::u8path(character[key].get<std::string>());
            if (filename.empty() || filename.has_parent_path() || filename.is_absolute()) return false;
            const auto path = std::filesystem::u8path(manifestPath).parent_path() / filename;
            SDL_Surface* surface = loadImage(path.u8string().c_str());
            if (!surface) return false;
            const std::string pixels = surface->w == 176 && surface->h == 28 ? pixelsFromSurface(surface, 176, 28) : std::string();
            SDL_DestroySurface(surface);
            if (pixels.empty()) return false;
            std::string canonical;
            canonical.reserve(pixels.size());
            for (int y = 0; y < 28; ++y) for (int frame = 0; frame < 11; ++frame)
                canonical.append(pixels, (y * 176 + order[frame] * 16) * 8, 16 * 8);
            if (std::string(key) == "hair_base") appearance.sprites.base = std::move(canonical);
            else appearance.sprites.bangs = std::move(canonical);
        }
        imported.push_back(std::move(appearance));
    }
    characters = std::move(imported);
    return true;
}
} // namespace CharacterCreator
