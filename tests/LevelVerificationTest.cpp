#include "LevelVerification.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
static fs::path saveRoot;
std::string GetAppSaveRootPath() { return saveRoot.string(); }

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void write(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << data;
    require(out.good(), "Write test fixture");
}
static int recordCount() {
    int count = 0;
    if (fs::exists(saveRoot / "level_verification"))
        for (const auto& entry : fs::directory_iterator(saveRoot / "level_verification"))
            if (entry.is_regular_file()) ++count;
    return count;
}

int main() {
    const auto temp = fs::temp_directory_path();
    const auto root = temp / ("df-level-verification-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path root, temp;
        ~Cleanup() {
            if (root.parent_path() == temp && root.filename().string().rfind("df-level-verification-test-", 0) == 0) {
                std::error_code ec;
                fs::remove_all(root, ec);
            }
        }
    } cleanup{root, temp};
    try {
        saveRoot = root / "saves";
        const auto first = root / "levels-a" / "same.txt";
        const auto second = root / "levels-b" / "same.txt";
        write(first, "level");
        write(second, "level");
        require(!IsLocalLevelVerified(first.string()), "New levels start unverified");
        SetLocalLevelVerified(first.string(), true);
        require(IsLocalLevelVerified(first.string()), "Verification must be readable");
        require(recordCount() == 1, "Records must be stored in the separate verification folder");
        require(!fs::exists(first.string() + ".verify.json"), "New writes must not create sidecars");
        require(!IsLocalLevelVerified(second.string()), "Same-named levels must not share verification");
        SetLocalLevelVerified(second.string(), true);
        require(recordCount() == 2, "Same-named levels need separate records");
        require(IsLocalLevelVerified((first.parent_path() / "." / first.filename()).string()),
            "Equivalent paths must share a verification record");
        SetLocalLevelVerified(first.string(), false);
        require(!IsLocalLevelVerified(first.string()), "Reverification must support false");
        ClearLocalLevelVerification(first.string());
        require(recordCount() == 1 && !IsLocalLevelVerified(first.string()), "Clear must remove new records");
        const auto legacy = fs::path(first.string() + ".verify.json");
        write(legacy, R"({"verified":true,"verified_at":123})");
        require(IsLocalLevelVerified(first.string()), "Existing sidecars must retain verification during migration");
        require(!fs::exists(legacy) && recordCount() == 2, "Migration must move the old record into the new folder");
        bool timestampPreserved = false;
        for (const auto& entry : fs::directory_iterator(saveRoot / "level_verification")) {
            std::ifstream in(entry.path());
            const auto metadata = nlohmann::json::parse(in);
            if (metadata.value("verified_at", 0LL) == 123) timestampPreserved = true;
        }
        require(timestampPreserved, "Migration must preserve existing verification metadata");
        SetLocalLevelVerified(first.string(), false);
        write(legacy, R"({"verified":true})");
        require(!IsLocalLevelVerified(first.string()), "Old sidecars must not override newer verification");
        ClearLocalLevelVerification(first.string());
        require(!fs::exists(legacy) && !IsLocalLevelVerified(first.string()), "Clear must remove both locations");
        write(legacy, "invalid JSON");
        require(!IsLocalLevelVerified(first.string()), "Malformed verification remains unverified");
        ClearLocalLevelVerification(first.string());
        write(legacy, R"({"verified":true})");
        SetLocalLevelVerified(first.string(), true);
        require(!fs::exists(legacy), "New writes must clean up old sidecars");
        const int beforeRemote = recordCount();
        SetLocalLevelVerified("https://example.test/level", true);
        ClearLocalLevelVerification("https://example.test/level");
        require(!IsLocalLevelVerified("https://example.test/level") && recordCount() == beforeRemote,
            "Remote levels must not create local verification records");
        // Failed migration must leave the original readable rather than lose it.
        saveRoot = root / "blocked-saves";
        write(saveRoot, "not a directory");
        write(legacy, R"({"verified":true})");
        require(IsLocalLevelVerified(first.string()) && fs::exists(legacy),
            "Migration failure must preserve old verification");
        std::cout << "Level verification tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
