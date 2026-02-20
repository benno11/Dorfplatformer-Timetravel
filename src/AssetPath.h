#pragma once

#include <string>

std::string ResolveAssetPath(const std::string& path);
std::string ReadTextFile(const std::string& path);
bool FileExists(const std::string& path);
std::string GetAppSaveRootPath();
void SetLevelServerUrl(const std::string& url);
std::string GetLevelServerUrl();
void SetLevelServerAuthToken(const std::string& token);
std::string GetLevelServerAuthToken();
void SetLevelServerAccountUsername(const std::string& username);
std::string GetLevelServerAccountUsername();
