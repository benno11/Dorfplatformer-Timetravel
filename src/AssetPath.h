#pragma once

#include <string>

std::string ResolveAssetPath(const std::string& path);
std::string ReadTextFile(const std::string& path);
bool FileExists(const std::string& path);
