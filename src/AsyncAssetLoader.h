#pragma once
#include "GameSupport.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>

struct AssetRequest {
    std::string imagePath;
    std::string plistPath;
    bool colorKey = false;
};
struct PreparedAsset {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, SDL_DestroySurface};
    std::vector<FrameEntry> frames;
};

// Workers prepare CPU data only. The caller owns renderer uploads and lookup pointers.
class AsyncAssetLoader {
public:
    using Decoder = std::function<PreparedAsset(const AssetRequest&)>;
    ~AsyncAssetLoader() { shutdown(); }
    AsyncAssetLoader() = default;
    AsyncAssetLoader(const AsyncAssetLoader&) = delete;
    AsyncAssetLoader& operator=(const AsyncAssetLoader&) = delete;

    void start(std::vector<AssetRequest> requests, Decoder decode) {
        shutdown();
        canceled_ = false;
        completed_ = 0;
        total_ = requests.size();
        pending_ = std::async(std::launch::async, [this, requests = std::move(requests), decode = std::move(decode)] {
            std::vector<PreparedAsset> assets;
            assets.reserve(requests.size());
            for (const auto& request : requests) {
                if (canceled_) break;
                assets.push_back(decode(request));
                ++completed_;
            }
            return assets;
        });
    }
    bool pending() const { return pending_.valid(); }
    bool ready() const {
        return pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    float progress() const { return total_ ? (float)completed_.load() / total_ : 1.0f; }
    std::vector<PreparedAsset> take() { return pending_.get(); }
    void cancel() { canceled_ = true; }
    void shutdown() {
        cancel();
        if (pending_.valid()) {
            // Join and release surfaces before SDL or its decoders are shut down.
            try { auto discarded = pending_.get(); } catch (...) {}
        }
    }
private:
    std::atomic<bool> canceled_{false};
    std::atomic<size_t> completed_{0};
    size_t total_ = 0;
    std::future<std::vector<PreparedAsset>> pending_;
};
