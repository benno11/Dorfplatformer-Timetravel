#include "UpdaterMenuState.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        UpdaterMenu::State state;
        state.status = "READY";
        require(!state.canAct(), "A missing updater callback must disable its action");
        state.available = true;
        require(state.canAct() && state.actionLabel() == "Check for Updates", "Ready must allow checking");
        state.status = "UPDATE FOUND - PRESS ENTER";
        require(state.canAct(), "A found release must allow an explicit update action");
#if defined(_WIN32)
        require(state.actionLabel() == "Install Update", "Windows must offer installation");
#else
        require(state.actionLabel() == "Open Update Download", "Other platforms must offer their download page");
#endif
        for (const char* busy : {"CHECKING", "DOWNLOADING 42% -> 2.0", "STARTING INSTALLER -> 2.0",
            "WAITING FOR WINDOWS PERMISSION", "INSTALLING UPDATE", "INSTALLER STARTED", "RESTARTING INTO UPDATE"}) {
            state.status = busy;
            require(state.busy() && !state.canAct(), "Busy updater states must prevent duplicate launches");
        }
        for (const char* retry : {"UP TO DATE", "ERROR", "STATUS ERROR", "CANCELLED"}) {
            state.status = retry;
            require(state.canAct() && state.actionLabel() == "Check for Updates", "Completed and failed checks must allow retry");
        }
        state.status = "NOT CONFIGURED";
        require(!state.canAct(), "Unconfigured updates must stay disabled");
        auto changed = state;
        require(!(state != changed), "Unchanged state must not reload the menu");
        changed.percent = 42;
        require(state != changed, "Progress changes must refresh the menu");
        changed = state;
        changed.notes = "New release notes";
        require(state != changed, "New release notes must refresh the menu");
        require(UpdaterMenu::progressPercent(-1) == 0 && UpdaterMenu::progressPercent(2) == 100 &&
            UpdaterMenu::progressPercent(0.42f) == 42 &&
            UpdaterMenu::progressPercent(std::numeric_limits<float>::quiet_NaN()) == 0,
            "Progress must remain finite and within 0-100 percent");
        std::cout << "Updater menu state tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
