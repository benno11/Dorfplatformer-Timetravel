#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Tasks must own their inputs; deliver game-state changes on the main thread.
class AsyncNetworkExecutor {
public:
    explicit AsyncNetworkExecutor(unsigned workerCount = 2) {
        try {
            for (unsigned i = 0; i < workerCount; ++i) {
                workers.emplace_back([this] { work(); });
            }
            if (workers.empty()) throw std::invalid_argument("Network executor needs a worker");
        } catch (...) {
            shutdown();
            throw;
        }
    }

    AsyncNetworkExecutor(const AsyncNetworkExecutor&) = delete;
    AsyncNetworkExecutor& operator=(const AsyncNetworkExecutor&) = delete;
    ~AsyncNetworkExecutor() { shutdown(); }

    template<class F>
    auto submit(F&& fn) -> std::future<std::invoke_result_t<F>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(fn));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) throw std::runtime_error("Network executor is shutting down");
            tasks.emplace_back([task] { (*task)(); });
        }
        ready.notify_one();
        return result;
    }

    // Drain all accepted work before network libraries or SDL are torn down.
    // Called by the owning thread, never by a worker.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        ready.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }
        workers.clear();
    }

private:
    void work() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [this] { return stopping || !tasks.empty(); });
                if (tasks.empty()) return;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            task(); // packaged_task sends exceptions to its future.
        }
    }

    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::function<void()>> tasks;
    std::vector<std::thread> workers;
    bool stopping = false;
};
