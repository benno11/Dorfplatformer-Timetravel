#include "SaveFormat.h"
#include <stdexcept>
#include <iostream>
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        const nlohmann::json input{{"active_button_world_areas", {11, 12, 22, 31, 42, 51, 61, 62}},
            {"progression", {{"hardmode_unlocked", true}, {"world_select_unlocked", false},
                {"hardmode_complete", true}, {"hardmode_furthest_level", 20}}},
            {"resume_world", 6}, {"resume_level", 11}, {"player", {{"x", 123.5}}}};
        const auto bytes = SaveFormat::encode(input);
        require(bytes[0] == 3, "One-byte format number");
        require(static_cast<unsigned char>(bytes[1]) == 0x9B, "Worlds 1-4 use two bits each");
        require(static_cast<unsigned char>(bytes[2]) == 0x6D, "Worlds 5-6 low nibble, world ID high nibble");
        require(static_cast<unsigned char>(bytes[3]) == 0x35, "Area high nibble, flags low nibble");
        require(bytes.size() == 4, "Exact packed size, no payload");
        auto expectedDocument = input; expectedDocument.erase("player");
        require(SaveFormat::decode(bytes) == expectedDocument, "Progression and resume point round trip");
        for (int flags = 0; flags < 4; ++flags) {
            auto header = bytes; header[3] = 0x10 | flags;
            const auto result = SaveFormat::decode(header);
            require(result && (*result)["progression"]["hardmode_unlocked"] == bool(flags & 1) &&
                (*result)["progression"]["world_select_unlocked"] == bool(flags & 2), "All unlock combinations");
        }
        for (int level = 1; level <= 20; ++level) {
            auto run = input; run["resume_world"] = 0; run["resume_level"] = level;
            const auto data = SaveFormat::encode(run); const auto decoded = SaveFormat::decode(data);
            require(decoded && (*decoded)["resume_world"] == 0 && (*decoded)["resume_level"] == level,
                "All twenty Hardmode levels fit the level nibble and world group");
            require((static_cast<unsigned char>(data[3]) & 8) != 0, "Hardmode active flag");
        }
        require(SaveFormat::decode(input.dump()) == input, "Legacy JSON readable");
        for (size_t n = 0; n < SaveFormat::headerSize; ++n)
            require(!SaveFormat::decode(bytes.substr(0, n)), "Truncated header rejected");
        auto invalid = bytes; invalid[0] = 4;
        require(!SaveFormat::decode(invalid), "Future versions rejected");
        invalid = bytes; invalid[2] = 0x8D;
        require(!SaveFormat::decode(invalid), "World ID above seven rejected");
        invalid = bytes; invalid[3] = 0;
        require(!SaveFormat::decode(invalid), "Zero level rejected");
        // Every possible combination of the twelve button bits round trips.
        for (unsigned mask = 0; mask < 4096; ++mask) {
            auto sample = input; auto& buttons = sample["active_button_world_areas"];
            buttons = nlohmann::json::array();
            for (int world = 1; world <= 6; ++world) for (int area = 1; area <= 2; ++area)
                if (mask & (1u << ((world - 1) * 2 + area - 1))) buttons.push_back(world * 10 + area);
            const auto encoded = SaveFormat::encode(sample); const auto decoded = SaveFormat::decode(encoded);
            require(decoded && (*decoded)["active_button_world_areas"] == buttons && (*decoded)["resume_world"] == 6,
                "All packed button combinations preserve world ID");
        }
        std::string old("23333334"); old.push_back(1);
        for (const unsigned char state : {3, 2, 1, 2, 1, 3}) old.push_back(state);
        old.push_back(static_cast<char>(0xA5)); old.push_back(static_cast<char>((20 << 3) | 6));
        auto oldExpected = expectedDocument; oldExpected["resume_level"] = 10;
        require(SaveFormat::decode(old) == oldExpected, "Version one saves still load");
        require(SaveFormat::encode(*SaveFormat::decode(old)).size() == 4, "Old saves upgrade to packed format");
        std::string versionTwo("23333334"); versionTwo.push_back(2);
        versionTwo.push_back(static_cast<char>(0x9B)); versionTwo.push_back(static_cast<char>(0x6D));
        versionTwo.push_back(static_cast<char>(0xB5));
        require(SaveFormat::decode(versionTwo) == expectedDocument, "Version two saves still load");
        for (int world = 1; world <= 7; ++world) for (int area = 1; area <= (world == 7 ? 1 : 3); ++area) {
            auto sample = input; sample["resume_world"] = world;
            sample["resume_level"] = (area - 1) * 4 + 4;
            const auto encoded = SaveFormat::encode(sample); const auto decoded = SaveFormat::decode(encoded);
            const int expectedLevel = (area - 1) * 4 + ((world >= 6 || area == 3) ? 3 : 1);
            require((static_cast<unsigned char>(encoded[3]) >> 4) == area && decoded &&
                (*decoded)["resume_level"] == expectedLevel, "Resume resets time period to the area start");
        }
        require(!SaveFormat::decode(bytes + "{}"), "Trailing snapshots rejected");
        std::cout << "Save format layout, nibbles, resume points, legacy and invalid data passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
