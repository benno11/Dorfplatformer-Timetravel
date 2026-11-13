#pragma once
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>
namespace SecretData {
using Key = std::array<unsigned char, 32>;
using KeyProvider = std::function<bool(bool, Key&)>;
bool randomBytes(unsigned char* out, size_t size);
bool deviceKey(const std::filesystem::path& root, bool create, Key& key);
class Store {
public:
    enum class Result { Missing, Loaded, Error };
    Store(std::filesystem::path root, std::string context, KeyProvider provider = {});
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Result load(nlohmann::json& value);
    bool save(const nlohmann::json& value);
private:
    bool keys(bool create);
    bool decode(const std::string& bytes, nlohmann::json& value);
    std::filesystem::path root_, path_;
    std::string context_;
    KeyProvider provider_;
    Key encryption_{};
    std::array<unsigned char,64> signing_{};
    Key public_{};
    bool ready_ = false, blocked_ = false;
};
// Remove only legacy private values; preserve unrelated settings.
void scrubLegacy(nlohmann::json& value);
}
