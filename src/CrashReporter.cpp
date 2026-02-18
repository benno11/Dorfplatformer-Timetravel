#include "CrashReporter.h"

#include <sdl3/SDL.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

namespace CrashReporter {
static std::atomic<bool> running{false};
static std::atomic<bool> pending{false};
static std::atomic<bool> handled{false};
static std::atomic<int> pendingSignal{0};
static std::thread worker;
static std::mutex msgMutex;
static std::string pendingMessage;
static std::terminate_handler prevTerminate = nullptr;

static void appendCrashLog(const std::string& msg) {
    std::FILE* f = std::fopen("build/crash.log", "a");
    if (!f) return;
    std::fprintf(f, "%s\n", msg.c_str());
    std::fclose(f);
}

static void workerMain() {
    while (running.load(std::memory_order_relaxed)) {
        if (!pending.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        int sig = pendingSignal.exchange(0, std::memory_order_relaxed);
        std::string msg;
        {
            std::lock_guard<std::mutex> lock(msgMutex);
            msg = pendingMessage;
            pendingMessage.clear();
        }
        if (sig > 0) {
            msg = std::string("FATAL SIGNAL: ") + std::to_string(sig);
        } else if (msg.empty()) {
            msg = "FATAL: unknown terminate";
        }
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", msg.c_str());
        appendCrashLog(msg);
        handled.store(true, std::memory_order_relaxed);
        pending.store(false, std::memory_order_relaxed);
    }
}

static void signalHandler(int sig) {
    pendingSignal.store(sig, std::memory_order_relaxed);
    pending.store(true, std::memory_order_relaxed);
    std::fprintf(stderr, "FATAL SIGNAL captured\n");
    std::fflush(stderr);
    for (volatile int i = 0; i < 5000000; ++i) {}
    std::_Exit(128 + sig);
}

static void terminateHandler() {
    std::string msg = "FATAL: std::terminate called";
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            msg = std::string("FATAL terminate exception: ") + e.what();
        } catch (...) {
            msg = "FATAL terminate with non-std exception";
        }
    }
    {
        std::lock_guard<std::mutex> lock(msgMutex);
        pendingMessage = msg;
    }
    pending.store(true, std::memory_order_relaxed);
    for (int i = 0; i < 80 && !handled.load(std::memory_order_relaxed); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (prevTerminate) prevTerminate();
    std::abort();
}

void start() {
    handled.store(false, std::memory_order_relaxed);
    pending.store(false, std::memory_order_relaxed);
    pendingSignal.store(0, std::memory_order_relaxed);
    running.store(true, std::memory_order_relaxed);
    worker = std::thread(workerMain);
    prevTerminate = std::set_terminate(terminateHandler);
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
#if defined(SIGBUS)
    std::signal(SIGBUS, signalHandler);
#endif
}

void stop() {
    running.store(false, std::memory_order_relaxed);
    if (worker.joinable()) worker.join();
}
} // namespace CrashReporter
