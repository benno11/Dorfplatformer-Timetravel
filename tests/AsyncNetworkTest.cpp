#include "AsyncNetwork.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        AsyncNetworkExecutor executor(2);
        const auto mainThread = std::this_thread::get_id();
        auto thread = executor.submit([] { return std::this_thread::get_id(); });
        require(thread.get() != mainThread, "Request ran on the caller thread");

        std::promise<void> release;
        auto gate = release.get_future().share();
        std::promise<void> firstStarted, secondStarted;
        auto firstReady = firstStarted.get_future();
        auto secondReady = secondStarted.get_future();
        auto first = executor.submit([gate, &firstStarted] { firstStarted.set_value(); gate.wait(); return 1; });
        auto second = executor.submit([gate, &secondStarted] { secondStarted.set_value(); gate.wait(); return 2; });
        const bool concurrent = firstReady.wait_for(std::chrono::seconds(3)) == std::future_status::ready &&
                                secondReady.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        // Release even on failure so executor shutdown cannot hang the test.
        release.set_value();
        require(concurrent, "Slow requests prevented independent requests from starting");
        require(first.get() == 1 && second.get() == 2, "Response values were lost");

        auto failure = executor.submit([]() -> int { throw std::runtime_error("request failed"); });
        bool propagated = false;
        try { (void)failure.get(); }
        catch (const std::runtime_error& e) { propagated = std::string(e.what()) == "request failed"; }
        require(propagated, "Request exceptions were not delivered to the future");
        auto owned = executor.submit([value = std::make_unique<int>(42)] { return *value; });
        require(owned.get() == 42, "Worker did not recover after a failed request");

        std::atomic<int> finished{0};
        for (int i = 0; i < 100; ++i) {
            // Discarding a future must neither wait nor discard the request.
            (void)executor.submit([&finished] { finished.fetch_add(1); });
        }
        executor.shutdown();
        require(finished.load() == 100, "Shutdown dropped accepted requests");
        bool rejected = false;
        try { (void)executor.submit([] {}); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Requests were accepted after shutdown");
        executor.shutdown(); // Repeated shutdown is safe.
        std::cout << "Async network executor tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
