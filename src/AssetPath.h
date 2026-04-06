#pragma once

#include <string>
#include <vector>

std::string ResolveAssetPath(const std::string& path);
std::string ReadTextFile(const std::string& path);
bool FileExists(const std::string& path);
bool HttpRequestText(const std::string& method,
                     const std::string& url,
                     const std::vector<std::string>& headers,
                     const std::string& body,
                     long* statusCodeOut,
                     std::string* responseBodyOut,
                     std::string* errorOut,
                     int timeoutMs);
std::string GetAppSaveRootPath();
void SetLevelServerUrl(const std::string& url);
std::string GetLevelServerUrl();
void SetLevelServerAuthToken(const std::string& token);
std::string GetLevelServerAuthToken();
void SetLevelServerAccountUsername(const std::string& username);
std::string GetLevelServerAccountUsername();
