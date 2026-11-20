#pragma once
#include <algorithm>
#include <array>
#include <deque>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Shared by all save slots; serialize in the encrypted global savedata_secret, never a slot.
class Achievements {
public:
    struct Definition { const char* id; const char* title; };
    inline static constexpr std::array<Definition, 13> definitions{{
        {"complete_level", "Complete a level"}, {"beat_boss", "Beat a boss"},
        {"complete_world", "Complete a world"}, {"ending", "Get an ending"},
        {"good_ending", "Get the good ending"},
        {"buttons_world_1", "World 1 buttons"}, {"buttons_world_2", "World 2 buttons"},
        {"buttons_world_3", "World 3 buttons"}, {"buttons_world_4", "World 4 buttons"},
        {"buttons_world_5", "World 5 buttons"}, {"buttons_world_6", "World 6 buttons"},
        {"beat_hardmode", "Beat Hardmode"},
        {"all_campaign_coins", "Collect every campaign coin"}
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
    bool campaignCoin(const std::string& source, const std::set<std::string>& required) {
        if (!required.count(source) || has("all_campaign_coins")) return false;
        const bool changed = campaignCoins_.insert(source).second;
        if (!required.empty() && std::includes(campaignCoins_.begin(), campaignCoins_.end(),
                required.begin(), required.end()))
            return unlock("all_campaign_coins") || changed;
        return changed;
    }
    struct Requirement { std::string label; int current = 0, total = 1; };
    std::vector<Requirement> requirements(const std::string& id,
            const std::set<std::string>& catalog = {}) const {
        if (id == "all_campaign_coins") {
            const auto coinValue = [](const std::string& key) { return key.find(":box:") != std::string::npos ? 5 : 1; };
            int count = 0, total = 0;
            for (const auto& key : catalog) {
                const int value = coinValue(key);
                total += value;
                if (campaignCoins_.count(key)) count += value;
            }
            std::vector<Requirement> result{{"Collect every campaign coin and coin box", count, total}};
            std::string level;
            for (const auto& key : catalog) {
                const auto name = key.substr(0, key.find(':'));
                if (name != level) { result.push_back({name, 0, 0}); level = name; }
                const int value = coinValue(key);
                result.back().total += value;
                if (campaignCoins_.count(key)) result.back().current += value;
            }
            return result;
        }
        if (id.rfind("buttons_world_", 0) == 0) {
            for (int world = 1; world <= 6; ++world) if (id == "buttons_world_" + std::to_string(world))
                return {{"Activate area 1 buttons", buttons_.count(world * 10 + 1) ? 1 : 0, 1},
                        {"Activate area 2 buttons", buttons_.count(world * 10 + 2) ? 1 : 0, 1}};
        }
        const char* instruction = id == "complete_level" ? "Complete one level" :
            id == "beat_boss" ? "Defeat one boss" : id == "complete_world" ? "Complete one campaign world" :
            id == "ending" ? "Reach either ending" : id == "good_ending" ? "Reach the good ending (future 4)" :
            id == "beat_hardmode" ? "Complete the Hardmode campaign" : nullptr;
        return instruction ? std::vector<Requirement>{{instruction, has(id) ? 1 : 0, 1}} : std::vector<Requirement>{};
    }
    std::string requirementSummary(const std::string& id,
            const std::set<std::string>& catalog = {}) const {
        const auto entries = requirements(id, catalog);
        std::string summary;
        const size_t count = id == "all_campaign_coins" ? std::min<size_t>(1, entries.size()) : entries.size();
        for (size_t i = 0; i < count; ++i) {
            if (!summary.empty()) summary += "; ";
            summary += entries[i].label;
            summary += entries[i].total > 0 ? " (" + std::to_string(entries[i].current) + "/" +
                std::to_string(entries[i].total) + ")" : " (progress unavailable)";
        }
        return summary;
    }
    nlohmann::json save() const {
        return {{"unlocked", unlocked_}, {"button_areas", buttons_}, {"campaign_coins", campaignCoins_}};
    }
    void load(const nlohmann::json& j) {
        if (!j.is_object()) return;
        unlocked_.clear(); buttons_.clear(); campaignCoins_.clear(); notifications_.clear(); remaining_ = 4.0f;
        if (j.contains("unlocked") && j["unlocked"].is_array())
            for (const auto& entry : j["unlocked"])
                if (entry.is_string()) for (const auto& definition : definitions)
                    if (entry == definition.id) unlocked_.insert(definition.id);
        if (j.contains("campaign_coins") && j["campaign_coins"].is_array())
            for (const auto& entry : j["campaign_coins"])
                if (entry.is_string()) campaignCoins_.insert(entry.get<std::string>());
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
    std::set<std::string> campaignCoins_;
    std::deque<std::string> notifications_;
    float remaining_ = 4.0f;
};
