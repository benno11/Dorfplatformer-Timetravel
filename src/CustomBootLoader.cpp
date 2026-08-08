#include "CustomBootLoader.h"

#include <SDL3/SDL_power.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace CustomBoot {
namespace {

constexpr int kMinimumBootBatteryPercent = 5;

struct Partition {
    std::string id;
    std::string path;
    bool recovery = false;
};

struct BootConfig {
    int inputTimeoutMs = 1200;
    std::string startupExecutable;
    std::string kernel;
    std::string initScript;
    std::string defaultPartition;
    std::string recoveryPartition;
    std::vector<std::string> bootOptions;
    std::vector<Partition> partitions;
    nlohmann::json trustedComponents = nlohmann::json::object();
};

struct StartupKeys {
    bool ctrlBPressed = false;
    std::vector<std::string> ctrlLetters;
    std::string requestedPartition;
};

static void logStage(const std::string& stage, const std::string& message) {
    std::cerr << "[custom-boot] " << stage << ": " << message << "\n";
}

static std::string fatal(const std::string& code, const std::string& message, const std::string& detail) {
    std::ostringstream out;
    out << code << " " << message;
    if (!detail.empty()) out << " (" << detail << ")";
    return out.str();
}

static std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::uint64_t fnv1a64File(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    std::uint64_t hash = 14695981039346656037ull;
    char buffer[8192];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

static std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

static std::filesystem::path executablePath(char** argv) {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(buffer);
    }
#endif
    if (argv && argv[0] && *argv[0]) {
        return std::filesystem::absolute(argv[0]);
    }
    return {};
}

static bool parseBoolEnv(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return false;
    std::string text(value);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

static BootConfig defaultConfig() {
    BootConfig config;
    config.startupExecutable = "<self>";
    config.kernel = "assets/textures.json";
    config.initScript = "assets/config.json";
    config.defaultPartition = "system";
    config.recoveryPartition = "recovery";
    config.bootOptions = {"normal"};
    config.partitions.push_back({"system", ".", false});
    config.partitions.push_back({"recovery", ".", true});
    return config;
}

static bool readBootConfiguration(BootConfig& config, std::string& error) {
    const std::filesystem::path configPath = "assets/boot_config.json";
    const std::string text = readTextFile(configPath);
    if (text.empty()) {
        config = defaultConfig();
        logStage("CONFIG-0001", "assets/boot_config.json missing; using built-in recovery configuration");
        return true;
    }

    try {
        const auto json = nlohmann::json::parse(text);
        config.inputTimeoutMs = json.value("input_timeout_ms", config.inputTimeoutMs);
        config.startupExecutable = json.value("startup_executable", config.startupExecutable);
        config.kernel = json.value("kernel", config.kernel);
        config.initScript = json.value("init_script", config.initScript);
        config.defaultPartition = json.value("default_partition", config.defaultPartition);
        config.recoveryPartition = json.value("recovery_partition", config.recoveryPartition);
        config.bootOptions = json.value("boot_options", config.bootOptions);
        config.trustedComponents = json.value("trusted_components", nlohmann::json::object());
        config.partitions.clear();
        for (const auto& item : json.value("partitions", nlohmann::json::array())) {
            config.partitions.push_back({
                item.value("id", ""),
                item.value("path", "."),
                item.value("recovery", false),
            });
        }
        if (config.partitions.empty()) {
            error = "no partitions are configured";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

static StartupKeys collectStartupKeys(int argc, char** argv) {
    StartupKeys keys;
    keys.ctrlBPressed = parseBoolEnv("DF_BOOT_CTRL_B");
    const char* envPartition = std::getenv("DF_BOOT_PARTITION");
    if (envPartition && *envPartition) {
        keys.ctrlBPressed = true;
        keys.requestedPartition = envPartition;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--boot-select" || arg == "--ctrl-b") {
            keys.ctrlBPressed = true;
        } else if (arg.rfind("--boot-partition=", 0) == 0) {
            keys.ctrlBPressed = true;
            keys.requestedPartition = arg.substr(17);
        } else if (arg.rfind("--boot-option=", 0) == 0) {
            keys.ctrlLetters.push_back(arg.substr(14));
        } else if (arg.rfind("--ctrl-", 0) == 0 && arg.size() == 8) {
            keys.ctrlLetters.push_back(arg.substr(7));
        }
    }

    const char* envOptions = std::getenv("DF_BOOT_OPTIONS");
    if (envOptions && *envOptions) {
        std::stringstream ss(envOptions);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) keys.ctrlLetters.push_back(item);
        }
    }
    return keys;
}

static bool applyValidatedShortcuts(BootConfig& config, const std::vector<std::string>& shortcuts, std::string& error) {
    const std::unordered_set<std::string> allowed = {"normal", "safe", "offline", "diagnostic", "recovery"};
    for (std::string shortcut : shortcuts) {
        std::transform(shortcut.begin(), shortcut.end(), shortcut.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (shortcut == "s") shortcut = "safe";
        if (shortcut == "o") shortcut = "offline";
        if (shortcut == "d") shortcut = "diagnostic";
        if (shortcut == "r") shortcut = "recovery";
        if (!allowed.count(shortcut)) {
            error = shortcut;
            return false;
        }
        if (std::find(config.bootOptions.begin(), config.bootOptions.end(), shortcut) == config.bootOptions.end()) {
            config.bootOptions.push_back(shortcut);
        }
    }
    return true;
}

static const Partition* findPartition(const BootConfig& config, const std::string& id) {
    for (const auto& partition : config.partitions) {
        if (partition.id == id) return &partition;
    }
    return nullptr;
}

static const Partition* resolveBootPartition(const BootConfig& config, const StartupKeys& keys) {
    if (!keys.requestedPartition.empty()) {
        if (const Partition* requested = findPartition(config, keys.requestedPartition)) return requested;
    }
    if (const Partition* selected = findPartition(config, config.defaultPartition)) return selected;
    if (!config.partitions.empty()) return &config.partitions.front();
    return nullptr;
}

static std::filesystem::path resolveComponentPath(const Partition& partition, const std::string& component, char** argv) {
    if (component == "<self>") {
        return executablePath(argv);
    }
    return std::filesystem::path(partition.path) / component;
}

static bool verifyComponent(const char* type,
                            const std::string& trustName,
                            const std::filesystem::path& path,
                            const nlohmann::json& trusted,
                            std::string& error) {
    if (path.empty() || !std::filesystem::exists(path)) {
        error = "missing " + std::string(type) + ": " + path.string();
        return false;
    }

    const std::string key = path.generic_string();
    const bool hasLogicalTrust = trusted.is_object() && trusted.contains(trustName);
    const bool hasPathTrust = trusted.is_object() && trusted.contains(key);
    if (!hasLogicalTrust && !hasPathTrust) {
        logStage(type, "no trusted digest configured for " + trustName + "; existence check passed");
        return true;
    }

    const std::string expected = hasLogicalTrust ? trusted.value(trustName, "") : trusted.value(key, "");
    const std::string actual = hex64(fnv1a64File(path));
    if (!expected.empty() && expected != actual) {
        error = trustName + " digest mismatch expected=" + expected + " actual=" + actual;
        return false;
    }
    return true;
}

static bool checkBattery(std::string& warning) {
    int seconds = -1;
    int percent = -1;
    SDL_PowerState state = SDL_GetPowerInfo(&seconds, &percent);
    if (state == SDL_POWERSTATE_UNKNOWN || percent < 0) {
        warning = "battery state is not reported";
        return true;
    }

    while (state == SDL_POWERSTATE_ON_BATTERY && percent >= 0 && percent < kMinimumBootBatteryPercent) {
        logStage("BATTERY-FLAT", "battery below 5%; waiting for charge before continuing");
        SDL_Delay(1000);
        seconds = -1;
        percent = -1;
        state = SDL_GetPowerInfo(&seconds, &percent);
    }
    return true;
}

} // namespace

Result Run(int argc, char** argv) {
    Result result;
    logStage("FIRMWARE", "UEFI Secure Boot is expected to authenticate the EFI loader before this executable runs");
    logStage("EFI-LOADER", "custom boot loader initialized");

    std::string batteryWarning;
    if (checkBattery(batteryWarning) && !batteryWarning.empty()) {
        logStage("BATTERY-0002", batteryWarning);
    }

    BootConfig config = defaultConfig();
    std::string configError;
    if (!readBootConfiguration(config, configError)) {
        std::cerr << fatal("CONFIG-0002", "No valid boot configuration is available.", configError) << "\n";
        return result;
    }

    StartupKeys keys = collectStartupKeys(argc, argv);
    if (keys.ctrlBPressed && keys.requestedPartition.empty()) {
        keys.requestedPartition = config.recoveryPartition.empty() ? config.defaultPartition : config.recoveryPartition;
        logStage("PARTITION-SELECT", "Ctrl+B requested partition selector; selected " + keys.requestedPartition);
    }

    if (!keys.ctrlLetters.empty()) {
        std::string inputError;
        if (!applyValidatedShortcuts(config, keys.ctrlLetters, inputError)) {
            logStage("INPUT-0001", "invalid startup option ignored: " + inputError);
        }
    }

    const Partition* partition = resolveBootPartition(config, keys);
    if (!partition) {
        std::cerr << fatal("PARTITION-0002", "No usable startup partition was found.", "") << "\n";
        return result;
    }

    std::string verifyError;
    const auto startupPath = resolveComponentPath(*partition, config.startupExecutable, argv);
    if (!verifyComponent("STARTUP-0002", "startup_executable", startupPath, config.trustedComponents, verifyError)) {
        std::cerr << fatal("STARTUP-0002", "The startup executable is not trusted.", verifyError) << "\n";
        return result;
    }

    const auto kernelPath = resolveComponentPath(*partition, config.kernel, argv);
    if (!verifyComponent("KERNEL-0001", "kernel", kernelPath, config.trustedComponents, verifyError)) {
        std::cerr << fatal("KERNEL-0001", "The kernel/runtime component is not trusted.", verifyError) << "\n";
        return result;
    }

    const auto initPath = resolveComponentPath(*partition, config.initScript, argv);
    if (!verifyComponent("INIT-0001", "init_script", initPath, config.trustedComponents, verifyError)) {
        std::cerr << fatal("INIT-0001", "The init script/configuration is not trusted.", verifyError) << "\n";
        return result;
    }

    result.ok = true;
    result.exitCode = 0;
    result.selectedPartition = partition->id;
    result.bootOptions = config.bootOptions;
    logStage("EXECUTE", "startup executable verified; transferring control to game runtime");
    return result;
}

} // namespace CustomBoot
