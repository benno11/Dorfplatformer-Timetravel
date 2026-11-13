#pragma once
#include "SandboxUpdateAPI.h"
#include <filesystem>
#include <functional>
#include <future>
#include <string>
namespace SandboxUpdates {
using Fetch = std::function<std::string(const std::string&)>;
struct Result {
    uint32_t state = DF_UPDATE_NOT_CONFIGURED;
    std::string latestGame, latestSandbox, detail, notes, url;
};
Result evaluate(const std::string& body, const std::string& gameId,
                const std::string& sandboxVersion);
class Checker {
public:
    void configure(std::string url, std::string gameId, Fetch fetch);
    bool check();
    DFSandboxUpdateStatus snapshot();
    std::string downloadUrl();
    void shutdown();
private:
    void poll();
    std::string url_,gameId_;
    Fetch fetch_;
    Result result_;
    std::future<Result> pending_;
};
// The runtime implementation is linked into the Windows sandbox EXE, not the
// game DLL. Other platforms keep it in their OS-sandboxed application binary.
void configure(std::string url, std::string gameId, Fetch fetch);
void configureFile(const std::filesystem::path& path, Fetch fetch);
bool request(uint32_t action);
DFSandboxUpdateStatus snapshot();
std::string downloadUrl();
void shutdown();
#ifdef _WIN32
void configureWindows(const std::filesystem::path& root);
#endif
// Called only by the healthy desktop broker after its game child has exited.
// Re-checks trusted configuration; never accepts a game-provided URL or path.
bool handoff();
}
