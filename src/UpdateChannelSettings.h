#pragma once
#include "UpdateManifest.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>

namespace AppUpdate {
inline std::filesystem::path channelSettingsPath(const nlohmann::json& config) {
    const bool lite = config.value("update_edition", std::string(Edition::lite ? "lite" : "full")) == "lite";
    char* pref = SDL_GetPrefPath("Benno111", lite ? "DorfplatformerTimetravelLite" : "DorfplatformerTimetravel");
    if (!pref) throw std::runtime_error("Could not open update channel preferences.");
    auto path = std::filesystem::u8path(pref) / "update-channel.txt";
    SDL_free(pref);
    return path;
}
inline void applySavedChannel(nlohmann::json& config) {
    std::ifstream input(channelSettingsPath(config));
    std::string channel;
    if (input >> channel) {
        if (channel == "stable" || channel == "prerelease" || channel == "nightly")
            config["update_channel"] = channel;
    }
}
inline bool saveChannel(const nlohmann::json& config, const std::string& channel) {
    const auto path = channelSettingsPath(config);
    auto temporary = path; temporary += ".tmp";
    { std::ofstream output(temporary); output << channel << '\n'; if (!output) return false; }
    // Windows rename cannot replace an existing file. Keep the previous choice
    // intact if writing the temporary file failed.
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear(); std::filesystem::rename(temporary, path, error);
    return !error;
}
}
