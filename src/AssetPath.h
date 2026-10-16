#pragma once

#include <string>
#include <future>

std::string ResolveAssetPath(const std::string& path);
std::string ReadTextFile(const std::string& path);
// Poll the returned future from a game/menu update; do not block on get().
std::future<std::string> ReadTextFileAsync(std::string path);
bool FileExists(const std::string& path);
std::string GetAppSaveRootPath();
void SetLevelServerUrl(const std::string& url);
std::string GetLevelServerUrl();
void SetLevelServerAuthToken(const std::string& token);
std::string GetLevelServerAuthToken();
void SetLevelServerAccountUsername(const std::string& username);
std::string GetLevelServerAccountUsername();

// Best-effort remote revocation before clearing local credentials.
void RevokeLevelServerSession();
