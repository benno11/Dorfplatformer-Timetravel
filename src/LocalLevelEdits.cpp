#include "LocalLevelEdits.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace LocalLevelEdits {
bool planResize(const TileMap& map, const std::vector<ObjectInstance>& objects, int width, int height, ResizePlan& plan) {
    if (width < 5 || height < 5 || width > 400 || height > 400 || map.w <= 0 || map.h <= 0 || map.tileSize <= 0 ||
        map.tileIds.size() != (size_t)map.w * (size_t)map.h) return false;
    ResizePlan result;
    result.tiles.assign((size_t)width * height, 2);
    for (int y = 0; y < map.h; ++y) for (int x = 0; x < map.w; ++x) {
        const auto tile = map.tileIds[(size_t)y * map.w + x];
        if (x < width && y < height) result.tiles[(size_t)y * width + x] = tile;
        else if (tile != 0 && tile != 2) ++result.croppedTiles;
    }
    for (const auto& object : objects) {
        if (!std::isfinite(object.x) || !std::isfinite(object.y) || object.x < 0 || object.y < 0 ||
            object.x >= (float)width * map.tileSize || object.y >= (float)height * map.tileSize) ++result.croppedObjects;
        else result.objects.push_back(object);
    }
    plan = std::move(result);
    return true;
}

bool renameFile(const std::string& path, const std::string& name, std::string& renamedPath, std::string& error) {
    renamedPath.clear();
    error.clear();
    std::string base;
    for (unsigned char ch : name) {
        if (base.size() >= 64) break;
        base += ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') ? (char)ch : '_';
    }
    if (base.empty() || base.find_first_not_of('_') == std::string::npos) { error = "Enter a name with letters or numbers."; return false; }
    const std::filesystem::path source(path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec) || ec) { error = "The local level is missing or read-only."; return false; }
    auto destination = source.parent_path() / (base + source.extension().string());
    for (int suffix = 1; destination != source && std::filesystem::exists(destination, ec) && !ec && suffix <= 10000; ++suffix)
        destination = source.parent_path() / (base + "_" + std::to_string(suffix) + source.extension().string());
    if (destination == source) { renamedPath = path; return true; }
    if (ec || std::filesystem::exists(destination, ec) || ec) { error = "Could not find an unused level name."; return false; }
    std::filesystem::rename(source, destination, ec);
    if (ec) { error = "Could not rename this local level."; return false; }
    renamedPath = destination.string();
    return true;
}

bool replaceFile(const std::string& temporary, const std::string& target) {
#ifdef _WIN32
    return MoveFileExW(std::filesystem::path(temporary).c_str(), std::filesystem::path(target).c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    return !ec;
#endif
}
}
