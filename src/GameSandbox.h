#pragma once
#include <filesystem>
#include <string>
namespace GameSandbox {
// Must run before SDL creates threads or any native mod is loaded.
bool install(const std::filesystem::path& gameRoot, const std::filesystem::path& saveRoot, std::string& status);
bool nativeModsAllowed();
// Close access to new local IPC channels after SDL connects to display/audio.
bool sealRuntime();
// Desktop parent stays outside the game to report a fatal exit safely.
int supervise(int argc, char** argv, int (*entry)(int,char**));
std::string saveRootOverride();
}
