#include "AsyncAssetLoader.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
#include <thread>

int main() {
    assert(SDL_Init(0));
    AsyncAssetLoader loader;
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    const auto mainThread = std::this_thread::get_id();
    std::atomic<int> decoded{0};
    const std::vector<AssetRequest> requests{{"first", "first.plist", true}, {"second", "second.plist", false}};
    loader.start(requests, [&](const AssetRequest& request) {
        assert(std::this_thread::get_id() != mainThread);
        if (++decoded == 1) { entered.set_value(); released.wait(); }
        PreparedAsset asset;
        asset.surface.reset(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA8888));
        assert(asset.surface);
        if (request.colorKey) assert(SDL_SetColorKey(asset.surface.get(), true, SDL_MapSurfaceRGB(asset.surface.get(), 159, 97, 255)));
        asset.frames.push_back({request.imagePath, {{0, 0, 16, 16}, false}});
        return asset;
    });
    entered.get_future().wait();
    assert(loader.pending() && !loader.ready() && decoded == 1);
    release.set_value();
    auto assets = loader.take();
    assert(!loader.pending() && decoded == 2 && loader.progress() == 1.0f);
    assert(assets.size() == 2 && assets[0].frames[0].name == "first" && assets[1].frames[0].name == "second");
    Uint32 key;
    assert(SDL_GetColorKey(assets[0].surface.get(), &key));
    SDL_Surface* output = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA8888);
    assert(output);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(output);
    assert(renderer);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, assets[0].surface.get());
    assert(texture); // CPU preparation survives transfer to a main-thread upload.
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(output);
    assets.clear();

    std::promise<void> cancelEntered, cancelRelease;
    auto cancellationReleased = cancelRelease.get_future().share();
    decoded = 0;
    loader.start(requests, [&](const AssetRequest&) {
        ++decoded;
        cancelEntered.set_value();
        cancellationReleased.wait();
        return PreparedAsset{};
    });
    cancelEntered.get_future().wait();
    loader.cancel();
    cancelRelease.set_value();
    loader.shutdown();
    assert(!loader.pending() && decoded == 1); // Queued jobs never run after cancellation.

    loader.start(requests, [](const AssetRequest&) -> PreparedAsset { throw std::runtime_error("decoder failed"); });
    bool failed = false;
    try { auto unused = loader.take(); } catch (const std::runtime_error&) { failed = true; }
    assert(failed && !loader.pending());
    // Shutdown also consumes exceptions and joins safely without publishing data.
    loader.start(requests, [](const AssetRequest&) -> PreparedAsset { throw std::runtime_error("decoder failed"); });
    loader.shutdown();
    assert(!loader.pending());
    SDL_Quit();
}
