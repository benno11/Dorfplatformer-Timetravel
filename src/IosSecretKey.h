#pragma once
#include "SecretSave.h"

namespace SecretData {
enum class KeychainRead { Found, Missing, Error };
inline std::string iosKeyService(const std::string& edition) {
    return "com.Benno111.savedata:ios:v1:" + edition;
}
inline bool legacyIosKeyService(const std::string& service, const std::string& edition) {
    const std::string prefix = "com.Benno111.savedata:";
    if (service.compare(0, prefix.size(), prefix) != 0) return false;
    auto path = std::filesystem::u8path(service.substr(prefix.size())).lexically_normal();
    if (path.filename().empty()) path = path.parent_path();
    return path.is_absolute() && path.filename() == edition;
}

// The backend keeps keys in the OS Keychain. No absolute container path enters
// the stable identity, and an existing save must authenticate a legacy key.
template<class Keychain, class Verify>
bool loadIosKey(const std::string& edition, bool create, Key& key, Keychain& chain, Verify verify) {
    const auto service = iosKeyService(edition);
    const auto status = chain.read(service, key);
    if (status == KeychainRead::Found) return true;
    if (status != KeychainRead::Missing) return false;
    if (!create) {
        const bool found = chain.visitLegacy([&](const std::string& oldService, const Key& candidate) {
            if (!legacyIosKeyService(oldService, edition) || !verify(candidate)) return false;
            key = candidate;
            return true;
        });
        return found && chain.add(service, key);
    }
    return randomBytes(key.data(), key.size()) && chain.add(service, key);
}
} // namespace SecretData
