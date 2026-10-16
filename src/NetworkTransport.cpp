#include "NetworkTransport.h"
#include "TextRenderer.h"

#include <algorithm>
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#endif

namespace {
// Accessed on the main thread. Workers do not depend on the UI's lifetime.
thread_local NetworkRuntime* currentRuntime = nullptr;

struct RenderState {
    SDL_Renderer* ren;
    SDL_Texture* target;
    SDL_Rect viewport{}, clip{};
    float scaleX = 1.0f, scaleY = 1.0f;
    int logicalW = 0, logicalH = 0;
    SDL_RendererLogicalPresentation presentation{};
    SDL_BlendMode blend{};
    Uint8 r = 0, g = 0, b = 0, a = 255;
    bool clipped = false;

    explicit RenderState(SDL_Renderer* renderer) : ren(renderer), target(SDL_GetRenderTarget(ren)) {
        SDL_GetRenderScale(ren, &scaleX, &scaleY);
        SDL_GetRenderViewport(ren, &viewport);
        SDL_GetRenderClipRect(ren, &clip);
        clipped = SDL_RenderClipEnabled(ren);
        SDL_GetRenderLogicalPresentation(ren, &logicalW, &logicalH, &presentation);
        SDL_GetRenderDrawBlendMode(ren, &blend);
        SDL_GetRenderDrawColor(ren, &r, &g, &b, &a);
    }
    ~RenderState() {
        SDL_SetRenderTarget(ren, target);
        SDL_SetRenderLogicalPresentation(ren, logicalW, logicalH, presentation);
        SDL_SetRenderScale(ren, scaleX, scaleY);
        SDL_SetRenderViewport(ren, &viewport);
        SDL_SetRenderClipRect(ren, clipped ? &clip : nullptr);
        SDL_SetRenderDrawBlendMode(ren, blend);
        SDL_SetRenderDrawColor(ren, r, g, b, a);
    }
};
}

NetworkRuntime::NetworkRuntime() { currentRuntime = this; }
NetworkRuntime::~NetworkRuntime() {
    shutdown();
    currentRuntime = nullptr;
}
void NetworkRuntime::setWindow(SDL_Window* win, SDL_Renderer* ren) {
    window = win;
    renderer = ren;
}
void NetworkRuntime::shutdown() { executor.shutdown(); }
NetworkRuntime* GetNetworkRuntime() { return currentRuntime; }

NetworkBusyScreen::NetworkBusyScreen(NetworkRuntime& owner) : runtime(owner), started(SDL_GetTicks()) {}
NetworkBusyScreen::~NetworkBusyScreen() {
    if (snapshot) SDL_DestroyTexture(snapshot);
}

void NetworkBusyScreen::frame(std::atomic<bool>& cancelled) {
    SDL_PumpEvents();
    SDL_Event event;
    if (SDL_PeepEvents(&event, 1, SDL_PEEKEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0) {
        cancelled.store(true);
    }
    SDL_Event closeEvents[16];
    const int closeCount = SDL_PeepEvents(closeEvents, 16, SDL_PEEKEVENT,
        SDL_EVENT_WINDOW_CLOSE_REQUESTED, SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    for (int i = 0; i < closeCount; ++i) {
        if (runtime.window && closeEvents[i].window.windowID == SDL_GetWindowID(runtime.window)) {
            cancelled.store(true);
        }
    }
    SDL_Event keys[32];
    const int keyCount = SDL_PeepEvents(keys, 32, SDL_PEEKEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_DOWN);
    for (int i = 0; i < keyCount; ++i) {
        if (keys[i].key.key == SDLK_ESCAPE || keys[i].key.key == SDLK_AC_BACK) cancelled.store(true);
    }
    // Preserve releases so held gameplay/touch controls cannot get stuck.
    // Keep Escape queued to cancel any fallback request and reach the owner loop.
    SDL_FilterEvents(+[](void*, SDL_Event* queued) -> bool {
        if (queued->type == SDL_EVENT_KEY_DOWN) {
            return queued->key.key == SDLK_ESCAPE || queued->key.key == SDLK_AC_BACK;
        }
        return queued->type != SDL_EVENT_MOUSE_BUTTON_DOWN && queued->type != SDL_EVENT_FINGER_DOWN;
    }, nullptr);
    // Leave quit events queued for the owning game/menu loop.
    if (!runtime.renderer || SDL_GetTicks() - started < 100) return;
    SDL_Renderer* ren = runtime.renderer;
    if (!captured) {
        captured = true;
        SDL_Surface* surface = SDL_RenderReadPixels(ren, nullptr);
        if (surface) {
            snapshot = SDL_CreateTextureFromSurface(ren, surface);
            SDL_DestroySurface(surface);
        }
    }
    const RenderState original(ren);
    SDL_SetRenderTarget(ren, nullptr);
    const RenderState windowState(ren);
    SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderViewport(ren, nullptr);
    SDL_SetRenderClipRect(ren, nullptr);
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
    int width = 0, height = 0;
    SDL_GetCurrentRenderOutputSize(ren, &width, &height);
    SDL_SetRenderDrawColor(ren, 20, 26, 34, 255);
    SDL_RenderClear(ren);
    if (snapshot) {
        float textureW = 0.0f, textureH = 0.0f;
        SDL_GetTextureSize(snapshot, &textureW, &textureH);
        if (textureW > 0.0f && textureH > 0.0f) {
            const float fit = std::min(width / textureW, height / textureH);
            const SDL_FRect dst{(width - textureW * fit) / 2, (height - textureH * fit) / 2,
                                textureW * fit, textureH * fit};
            SDL_RenderTexture(ren, snapshot, nullptr, &dst);
        }
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
    const SDL_FRect strip{0.0f, (float)height - 64.0f, (float)width, 64.0f};
    SDL_RenderFillRect(ren, &strip);
    const std::string label = cancelled.load() ? "Cancelling..." :
        "Connecting" + std::string((SDL_GetTicks() / 250) % 4, '.') + "  (Esc to cancel)";
    DrawText(ren, 20, std::max(0, height - 46), 2, label);
    SDL_RenderPresent(ren);

}

#if defined(HAVE_CURL) && HAVE_CURL
CURLcode PerformNetworkRequest(CURL* curl) {
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    return RunNetworkOperation([curl, cancelled] {
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
            +[](void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return static_cast<std::atomic<bool>*>(data)->load() ? 1 : 0;
            });
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelled.get());
        const CURLcode result = curl_easy_perform(curl);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, nullptr);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, nullptr);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        return result;
    }, cancelled);
}
#endif

#if defined(__ANDROID__)
namespace {
struct JavaNetworkArgs {
    JNIEnv* owner;
    jclass cls;
    std::vector<jvalue> args;
    JavaNetworkArgs(JNIEnv* env, jclass source, std::initializer_list<jstring> strings, jint timeout)
        : owner(env), cls(static_cast<jclass>(env->NewGlobalRef(source))) {
        for (auto string : strings) {
            jvalue value{};
            value.l = env->NewGlobalRef(string);
            args.push_back(value);
        }
        jvalue value{};
        value.i = timeout;
        args.push_back(value);
    }
    ~JavaNetworkArgs() {
        for (size_t i = 0; i + 1 < args.size(); ++i) owner->DeleteGlobalRef(args[i].l);
        owner->DeleteGlobalRef(cls);
    }
};
}

jobject PerformJavaNetworkObject(JNIEnv* env, jclass cls, jmethodID method,
                                 std::initializer_list<jstring> strings, jint timeout) {
    JavaNetworkArgs inputs(env, cls, strings, timeout);
    if (env->ExceptionCheck()) return nullptr;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    jobject global = RunNetworkOperation([&inputs, method]() -> jobject {
        auto* worker = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!worker) return nullptr;
        jobject local = worker->CallStaticObjectMethodA(inputs.cls, method, inputs.args.data());
        if (worker->ExceptionCheck()) {
            worker->ExceptionClear();
            if (local) worker->DeleteLocalRef(local);
            return nullptr;
        }
        jobject result = local ? worker->NewGlobalRef(local) : nullptr;
        if (worker->ExceptionCheck()) worker->ExceptionClear();
        if (local) worker->DeleteLocalRef(local);
        return result;
    }, cancelled);
    jobject result = global && !cancelled->load() ? env->NewLocalRef(global) : nullptr;
    if (global) env->DeleteGlobalRef(global);
    return result;
}

jint PerformJavaNetworkInt(JNIEnv* env, jclass cls, jmethodID method,
                          std::initializer_list<jstring> strings, jint timeout) {
    JavaNetworkArgs inputs(env, cls, strings, timeout);
    if (env->ExceptionCheck()) return -1;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    const jint result = RunNetworkOperation([&inputs, method]() -> jint {
        auto* worker = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!worker) return -1;
        const jint result = worker->CallStaticIntMethodA(inputs.cls, method, inputs.args.data());
        if (worker->ExceptionCheck()) {
            worker->ExceptionClear();
            return -1;
        }
        return result;
    }, cancelled);
    return cancelled->load() ? -1 : result;
}
#endif
