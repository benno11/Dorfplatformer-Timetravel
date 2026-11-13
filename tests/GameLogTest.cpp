#include "GameLog.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <iostream>
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    SDL_Init(0);
    const auto root = std::filesystem::temp_directory_path() / ("df-log-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "logs");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    std::atomic<int> forwarded{0};
    SDL_SetLogOutputFunction([](void* context, int, SDL_LogPriority, const char*) { ++*static_cast<std::atomic<int>*>(context); }, &forwarded);
    try {
        { std::ofstream old(root / "logs/game.log", std::ios::binary); old.seekp(8 * 1024 * 1024); old.put('x'); }
        {
            GameLog log(root.string(), false);
            SDL_Log("must stay hidden"); require(log.lines().empty() && forwarded == 0, "Logging starts disabled");
            log.setEnabled(true); SDL_Log("visible diagnostic");
            require(log.lines().size() == 1 && log.lines()[0].find("visible diagnostic") != std::string::npos, "In-game log stores messages");
            require(std::filesystem::exists(root / "logs/game.log.previous"), "Large log rotates");
            { std::ifstream in(log.path()); std::string text((std::istreambuf_iterator<char>(in)), {});
              require(text.find("visible diagnostic") != std::string::npos, "Log flushes to file"); }
            log.setEnabled(false); SDL_Log("hidden again"); require(log.lines().size() == 1, "Disable takes effect immediately");
            log.setEnabled(true);
            std::vector<std::thread> writers;
            for (int i = 0; i < 4; ++i) writers.emplace_back([] { for (int row = 0; row < 100; ++row) SDL_Log("threaded diagnostic %d", row); });
            for (auto& thread : writers) thread.join();
            require(log.lines().size() == 200 && forwarded == 401, "Recent history is bounded and thread safe");
        }
        SDL_Log("restored output"); require(forwarded == 402, "Shutdown restores the previous callback for a game restart");
        SDL_SetLogOutputFunction(nullptr, nullptr); SDL_Quit();
        std::cout << "Game logging enable/disable, file rotation, threads and restart checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
