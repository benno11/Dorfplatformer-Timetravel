#include "AssetPath.h"

#include <SDL2/SDL.h>

#include <vector>

std::string ResolveAssetPath(const std::string& path) {
#if defined(__ANDROID__)
    constexpr const char* kPrefix = "assets/";
    if (path.rfind(kPrefix, 0) == 0) {
        return path.substr(7);
    }
#endif
    return path;
}

std::string ReadTextFile(const std::string& path) {
    const std::string resolved = ResolveAssetPath(path);
    SDL_RWops* rw = SDL_RWFromFile(resolved.c_str(), "rb");
    if (!rw) return {};

    const Sint64 sz = SDL_RWsize(rw);
    if (sz <= 0) {
        SDL_RWclose(rw);
        return {};
    }

    std::vector<char> data(static_cast<size_t>(sz));
    const size_t got = SDL_RWread(rw, data.data(), 1, data.size());
    SDL_RWclose(rw);
    if (got != data.size()) return {};

    return std::string(data.begin(), data.end());
}

bool FileExists(const std::string& path) {
    const std::string resolved = ResolveAssetPath(path);
    SDL_RWops* rw = SDL_RWFromFile(resolved.c_str(), "rb");
    if (!rw) return false;
    SDL_RWclose(rw);
    return true;
}
