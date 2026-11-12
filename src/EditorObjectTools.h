#pragma once
#include <algorithm>
#include <cmath>
#include "LevelLoader.h"

namespace EditorObjects {
inline constexpr int maxLayer = 9;
inline bool at(const ObjectInstance& object, int x, int y, int layer) {
    return object.layer == layer && (int)std::floor(object.x / 32.0f) == x && (int)std::floor(object.y / 32.0f) == y;
}
inline int find(const std::vector<ObjectInstance>& objects, int x, int y, int layer) {
    for (int i = (int)objects.size() - 1; i >= 0; --i) if (at(objects[i], x, y, layer)) return i;
    return -1;
}
inline bool erase(std::vector<ObjectInstance>& objects, int x, int y, int layer, bool deleteActive) {
    if (!deleteActive) return false;
    const size_t before = objects.size();
    objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const auto& obj) { return at(obj, x, y, layer); }), objects.end());
    return before != objects.size();
}
inline void replace(std::vector<ObjectInstance>& objects, const ObjectInstance& object) {
    const int x = (int)std::floor(object.x / 32.0f), y = (int)std::floor(object.y / 32.0f);
    const int index = find(objects, x, y, object.layer);
    if (index >= 0) objects[index] = object;
    else objects.push_back(object);
}
inline std::vector<int> renderOrder(const std::vector<ObjectInstance>& objects) {
    std::vector<int> indices(objects.size());
    for (int i = 0; i < (int)indices.size(); ++i) indices[i] = i;
    std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) { return objects[a].layer < objects[b].layer; });
    return indices;
}
}
