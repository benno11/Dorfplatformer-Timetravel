#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace BuildPair {
constexpr size_t sealSize = 256;
constexpr size_t digestSize = 64;
using Bytes = std::vector<uint8_t>;
using Seal = std::array<uint8_t, sealSize>;
using PublicKey = std::array<uint8_t, 32>;
struct Image {
    size_t sealOffset = 0;
    size_t checksumOffset = 0;
    uint16_t machine = 0;
    bool dll = false;
};
bool inspect(const Bytes& file, Image& image, std::string& error);
std::array<uint8_t, digestSize> digest(const Bytes& file, const Image& image);
Seal getSeal(const Bytes& file, const Image& image);
// Validate both complete PE files before loading code. Only the fixed seal slot
// and non-executable PE checksum are normalized to break the signature cycle.
bool verify(const Bytes& exe, const Bytes& dll, const PublicKey& key,
            uint32_t edition, std::string& error, const Seal* loadedExeSeal = nullptr);
// Build tooling only; no private signing key is present in the game/launcher.
bool seal(Bytes& exe, Bytes& dll, const std::array<uint8_t,32>& seed,
          const std::array<uint8_t,32>& buildId, uint32_t edition, std::string& error);
}
