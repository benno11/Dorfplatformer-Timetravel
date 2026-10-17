#pragma once

#include <string>

bool IsLocalLevelVerified(const std::string& levelPath);
void SetLocalLevelVerified(const std::string& levelPath, bool verified);
void ClearLocalLevelVerification(const std::string& levelPath);
