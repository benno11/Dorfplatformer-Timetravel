#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>

namespace UpdaterMenu {
struct State {
    std::string status, detail, latestVersion, notes;
    int percent = 0;
    bool available = false;

    bool busy() const {
        for (const char* prefix : {"CHECKING", "DOWNLOADING", "STARTING INSTALLER", "WAITING FOR WINDOWS PERMISSION",
            "INSTALLING UPDATE", "INSTALLER STARTED", "RESTARTING INTO UPDATE"})
            if (status.rfind(prefix, 0) == 0) return true;
        return false;
    }
    bool canAct() const { return available && status != "NOT CONFIGURED" && !busy(); }
    std::string actionLabel() const {
        if (!available) return "Updater Unavailable";
        if (status == "NOT CONFIGURED") return "Updater Not Configured";
        if (busy()) return status.rfind("CHECKING", 0) == 0 ? "Checking for Updates..." : "Update in Progress...";
        if (status.find("UPDATE FOUND") != std::string::npos) {
#if defined(_WIN32)
            return "Install Update";
#else
            return "Open Update Download";
#endif
        }
        return "Check for Updates";
    }
    bool operator!=(const State& other) const {
        return std::tie(status, detail, latestVersion, notes, percent, available) !=
            std::tie(other.status, other.detail, other.latestVersion, other.notes, other.percent, other.available);
    }
};

inline int progressPercent(float progress) {
    return std::isfinite(progress) ? (int)std::lround(std::clamp(progress, 0.0f, 1.0f) * 100) : 0;
}
} // namespace UpdaterMenu
