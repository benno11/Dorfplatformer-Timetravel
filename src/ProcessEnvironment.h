#pragma once
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

// Read the live process environment, returning an owned UTF-8 value. Avoid
// deprecated CRT getenv on Windows while preserving non-ASCII save paths.
namespace ProcessEnvironment {
#ifdef _WIN32
inline std::wstring wide(const wchar_t* name) {
    if (!name || !*name) return {};
    DWORD capacity = GetEnvironmentVariableW(name, nullptr, 0);
    for (int attempt = 0; attempt < 4 && capacity > 0 && capacity <= 32768; ++attempt) {
        std::wstring value(capacity, L'\0');
        const DWORD copied = GetEnvironmentVariableW(name, value.data(), capacity);
        if (copied == 0) return {};
        if (copied >= capacity) { capacity = copied; continue; }
        value.resize(copied);
        return value;
    }
    return {};
}
#endif
inline std::string get(const char* name) {
    if (!name || !*name) return {};
#ifdef _WIN32
    const int nameSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
    if (nameSize <= 0) return {};
    std::wstring wideName(nameSize, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wideName.data(), nameSize)) return {};
    const auto value = wide(wideName.c_str());
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr)) return {};
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}
}
