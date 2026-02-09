#include "AssetPath.h"

#include <sdl3/SDL.h>

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
    SDL_IOStream* io = SDL_IOFromFile(resolved.c_str(), "rb");
    if (!io) return {};

    const Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) {
        SDL_CloseIO(io);
        return {};
    }

    std::vector<char> data(static_cast<size_t>(sz));
    const size_t got = static_cast<size_t>(SDL_ReadIO(io, data.data(), data.size()));
    SDL_CloseIO(io);
    if (got != data.size()) return {};

    return std::string(data.begin(), data.end());
}

bool FileExists(const std::string& path) {
    const std::string resolved = ResolveAssetPath(path);
    SDL_IOStream* io = SDL_IOFromFile(resolved.c_str(), "rb");
    if (!io) return false;
    SDL_CloseIO(io);
    return true;
}
