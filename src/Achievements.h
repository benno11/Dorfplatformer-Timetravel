#pragma once
#include <algorithm>
#include <array>
#include <deque>
#include <set>
#include <string>
#include <nlohmann/json.hpp>

// Shared by all save slots; serialize in the global client settings, never a slot.
class Achievements {
public:
    struct Definition { const char* id; const char* title; };
    inline static constexpr std::array<Definition, 12> definitions{{
        {"complete_level", "Complete a level"}, {"beat_boss", "Beat a boss"},
        {"complete_world", "Complete a world"}, {"ending", "Get an ending"},
        {"good_ending", "Get the good ending"},
        {"buttons_world_1", "World 1 buttons"}, {"buttons_world_2", "World 2 buttons"},
        {"buttons_world_3", "World 3 buttons"}, {"buttons_world_4", "World 4 buttons"},
        {"buttons_world_5", "World 5 buttons"}, {"buttons_world_6", "World 6 buttons"},
        {"beat_hardmode", "Beat Hardmode"}
    }};
    bool unlock(const std::string& id) {
        for (const auto& definition : definitions) {
            if (id == definition.id && unlocked_.insert(id).second) {
                notifications_.push_back(definition.title);
                return true;
            }
        }
        return false;
    }
    bool has(const std::string& id) const { return unlocked_.count(id) != 0; }
    bool button(int world, int area) {
        // Each campaign world has button flags in areas 1 and 2.
        if (world < 1 || world > 6 || area < 1 || area > 2) return false;
        const bool changed = buttons_.insert(world * 10 + area).second;
        for (int a = 1; a <= 2; ++a)
            if (!buttons_.count(world * 10 + a)) return changed;
        return unlock("buttons_world_" + std::to_string(world)) || changed;
    }
    nlohmann::json save() const {
        return {{"unlocked", unlocked_}, {"button_areas", buttons_}};
    }
    void load(const nlohmann::json& j) {
        if (!j.is_object()) return;
        unlocked_.clear(); buttons_.clear(); notifications_.clear(); remaining_ = 4.0f;
        if (j.contains("unlocked") && j["unlocked"].is_array())
            for (const auto& entry : j["unlocked"])
                if (entry.is_string()) for (const auto& definition : definitions)
                    if (entry == definition.id) unlocked_.insert(definition.id);
        if (j.contains("button_areas") && j["button_areas"].is_array())
            for (const auto& entry : j["button_areas"])
                if (entry.is_number_integer() && entry >= 11 && entry <= 63) {
                    const int key = entry.get<int>();
                    if (key % 10 >= 1 && key % 10 <= 2) buttons_.insert(key);
                }
    }
    const std::string* toast() const { return notifications_.empty() ? nullptr : &notifications_.front(); }
    void tick(float dt) {
        if (notifications_.empty() || !(dt > 0.0f)) return;
        remaining_ -= std::min(dt, 0.1f); // Resuming after a suspension must not discard notifications.
        if (remaining_ <= 0.0f) { notifications_.pop_front(); remaining_ = 4.0f; }
    }
private:
    std::set<std::string> unlocked_;
    std::set<int> buttons_;
    std::deque<std::string> notifications_;
    float remaining_ = 4.0f;
};
