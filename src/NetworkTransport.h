#pragma once

#include "AsyncNetwork.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>

#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif
#if defined(__ANDROID__)
#include <jni.h>
#include <initializer_list>
#endif

// Owned by RunGameApp, after curl initialization and before SDL teardown.
class NetworkRuntime {
public:
    NetworkRuntime();
    ~NetworkRuntime();
    void setWindow(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();
    AsyncNetworkExecutor executor;
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    friend class NetworkBusyScreen;
};

NetworkRuntime* GetNetworkRuntime();

class NetworkBusyScreen {
public:
    explicit NetworkBusyScreen(NetworkRuntime& runtime);
    ~NetworkBusyScreen();
    void frame(std::atomic<bool>& cancelled);
private:
    NetworkRuntime& runtime;
    SDL_Texture* snapshot = nullptr;
    Uint64 started = 0;
    bool captured = false;
};

// Compatibility bridge for operations whose callers require the response now.
// Only the transport runs on a worker. SDL drawing and events stay on main.
template<class F>
auto RunNetworkOperation(F&& task, std::shared_ptr<std::atomic<bool>> cancelled = {})
    -> std::invoke_result_t<F> {
    auto* runtime = GetNetworkRuntime();
    if (!runtime || !SDL_IsMainThread()) return task();
    if (!cancelled) cancelled = std::make_shared<std::atomic<bool>>(false);
    auto result = runtime->executor.submit(std::forward<F>(task));
    try {
        NetworkBusyScreen busy(*runtime);
        while (result.wait_for(std::chrono::milliseconds(16)) != std::future_status::ready) {
            busy.frame(*cancelled);
        }
    } catch (...) {
        // Callers may own curl buffers and JNI references used by the task.
        cancelled->store(true);
        result.wait();
        throw;
    }
    return result.get();
}

// For independent requests (e.g. telemetry): inputs must be captured by value.
template<class F>
auto SubmitNetworkOperation(F&& task) -> std::future<std::invoke_result_t<F>> {
    auto* runtime = GetNetworkRuntime();
    if (!runtime) throw std::runtime_error("Network runtime is unavailable");
    return runtime->executor.submit(std::forward<F>(task));
}

#if defined(HAVE_CURL) && HAVE_CURL
CURLcode PerformNetworkRequest(CURL* curl);
#endif

#if defined(__ANDROID__)
// Local JNI references cannot cross threads; these bridges promote inputs and
// results to global references while the Java call executes on a worker.
jobject PerformJavaNetworkObject(JNIEnv* env, jclass cls, jmethodID method,
                                 std::initializer_list<jstring> strings, jint timeout);
jint PerformJavaNetworkInt(JNIEnv* env, jclass cls, jmethodID method,
                          std::initializer_list<jstring> strings, jint timeout);
#endif
