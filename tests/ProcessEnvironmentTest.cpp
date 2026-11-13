#include "ProcessEnvironment.h"
#include <cassert>
#include <filesystem>
#include <string>

int main() {
    constexpr const char* name = "DF_TEST_PROCESS_ENVIRONMENT_7D32";
    const auto previous = ProcessEnvironment::get(name);
    auto set = [](const std::string& value) {
#ifdef _WIN32
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        std::wstring wide(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
        const bool changed = SetEnvironmentVariableW(L"DF_TEST_PROCESS_ENVIRONMENT_7D32", wide.c_str()) != FALSE;
        assert(changed);
#else
        const int changed = setenv("DF_TEST_PROCESS_ENVIRONMENT_7D32", value.c_str(), 1);
        assert(changed == 0);
#endif
    };
    auto unset = [] {
#ifdef _WIN32
        SetLastError(ERROR_SUCCESS);
        const bool removed = SetEnvironmentVariableW(L"DF_TEST_PROCESS_ENVIRONMENT_7D32", nullptr) != FALSE;
        assert(removed || GetLastError() == ERROR_ENVVAR_NOT_FOUND);
#else
        const int removed = unsetenv("DF_TEST_PROCESS_ENVIRONMENT_7D32");
        assert(removed == 0);
#endif
    };
    unset();assert(ProcessEnvironment::get(name).empty());
    set("first");assert(ProcessEnvironment::get(name) == "first");
    set("changed");assert(ProcessEnvironment::get(name) == "changed");
    const std::string unicode = u8"Spieler-\u00e4-\u6e38\u620f";
    set(unicode);assert(ProcessEnvironment::get(name) == unicode);
    assert(std::filesystem::u8path(ProcessEnvironment::get(name)).u8string() == unicode);
    const std::string longValue(8192, 'x');set(longValue);assert(ProcessEnvironment::get(name) == longValue);
    set("");assert(ProcessEnvironment::get(name).empty());
    assert(ProcessEnvironment::get(nullptr).empty());assert(ProcessEnvironment::get("").empty());
    unset();if (!previous.empty()) set(previous);
}
