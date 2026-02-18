#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

class InputSystem {
public:
    struct DetectionEvent {
        enum class Type {
            Connected,
            Disconnected
        };
        Type type = Type::Connected;
        SDL_JoystickID id = 0;
        std::string name;
        int connectedCount = 0;
        bool activeChanged = false;
    };

    InputSystem() = default;
    ~InputSystem() { closeAll(); }

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    void scanConnected() {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids) return;
        for (int i = 0; i < count; ++i) {
            openIfNeeded(ids[i]);
        }
        SDL_free(ids);
        ensureActive();
    }

    void handleEvent(const SDL_Event& e) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            openIfNeeded(e.gdevice.which);
            ensureActive();
            return;
        }
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            closeIfPresent(e.gdevice.which);
            ensureActive();
            return;
        }
    }

    bool hasGamepad() const { return activePad_ != nullptr; }
    int connectedCount() const { return (int)pads_.size(); }
    const char* activeGamepadName() const { return activePad_ ? SDL_GetGamepadName(activePad_) : nullptr; }

    bool pollDetectionEvent(DetectionEvent& out) {
        if (pendingDetection_.empty()) return false;
        out = pendingDetection_.front();
        pendingDetection_.pop_front();
        return true;
    }

    float gameplayMoveX() const {
        if (!activePad_) return 0.0f;
        float move = normalizedAxis(SDL_GetGamepadAxis(activePad_, SDL_GAMEPAD_AXIS_LEFTX), kMoveDeadzoneAxis);
        if (move > -kDigitalDeadzone && move < kDigitalDeadzone) {
            if (SDL_GetGamepadButton(activePad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) move = -1.0f;
            if (SDL_GetGamepadButton(activePad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) move = 1.0f;
        }
        return std::clamp(move, -1.0f, 1.0f);
    }

    bool gameplayDownHeld() const {
        if (!activePad_) return false;
        if (SDL_GetGamepadButton(activePad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) return true;
        return normalizedAxis(SDL_GetGamepadAxis(activePad_, SDL_GAMEPAD_AXIS_LEFTY), kMoveDeadzoneAxis) > 0.45f;
    }

    bool gameplayJumpHeld() const {
        if (!activePad_) return false;
        return SDL_GetGamepadButton(activePad_, SDL_GAMEPAD_BUTTON_SOUTH) ||
               SDL_GetGamepadButton(activePad_, SDL_GAMEPAD_BUTTON_EAST);
    }

    bool freeMoveHeld() const {
        if (!activePad_) return false;
        return SDL_GetGamepadButton(activePad_, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    }

    static bool isPauseToggleEvent(const SDL_Event& e) {
        return e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
               (e.gbutton.button == SDL_GAMEPAD_BUTTON_START ||
                e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK);
    }

    static bool isAcceptEvent(const SDL_Event& e) {
        return e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
               (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                e.gbutton.button == SDL_GAMEPAD_BUTTON_START);
    }

    static bool isLeftEvent(const SDL_Event& e) {
        return e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
               e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    }

    static bool isRightEvent(const SDL_Event& e) {
        return e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
               e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    }

private:
    static constexpr Sint16 kMoveDeadzoneAxis = 8000;
    static constexpr float kDigitalDeadzone = 0.15f;

    static float normalizedAxis(Sint16 raw, Sint16 deadzone) {
        const float f = std::clamp(raw / 32767.0f, -1.0f, 1.0f);
        if (std::fabs(f) <= (deadzone / 32767.0f)) return 0.0f;
        return f;
    }

    void openIfNeeded(SDL_JoystickID id) {
        if (pads_.find(id) != pads_.end()) return;
        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (!pad) return;
        const SDL_JoystickID prevActive = activeId_;
        pads_[id] = pad;
        if (!activePad_) {
            activeId_ = id;
            activePad_ = pad;
        }
        DetectionEvent ev;
        ev.type = DetectionEvent::Type::Connected;
        ev.id = id;
        ev.name = SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "";
        ev.connectedCount = (int)pads_.size();
        ev.activeChanged = (prevActive != activeId_);
        pendingDetection_.push_back(std::move(ev));
    }

    void closeIfPresent(SDL_JoystickID id) {
        auto it = pads_.find(id);
        if (it == pads_.end()) return;
        const SDL_JoystickID prevActive = activeId_;
        DetectionEvent ev;
        ev.type = DetectionEvent::Type::Disconnected;
        ev.id = id;
        ev.name = SDL_GetGamepadName(it->second) ? SDL_GetGamepadName(it->second) : "";
        SDL_CloseGamepad(it->second);
        pads_.erase(it);
        if (activeId_ == id) {
            activeId_ = 0;
            activePad_ = nullptr;
        }
        ev.connectedCount = (int)pads_.size();
        ev.activeChanged = (prevActive != activeId_);
        pendingDetection_.push_back(std::move(ev));
    }

    void ensureActive() {
        if (activePad_ && pads_.find(activeId_) != pads_.end()) return;
        if (pads_.empty()) {
            activeId_ = 0;
            activePad_ = nullptr;
            return;
        }
        auto it = pads_.begin();
        activeId_ = it->first;
        activePad_ = it->second;
    }

    void closeAll() {
        for (auto& kv : pads_) SDL_CloseGamepad(kv.second);
        pads_.clear();
        activePad_ = nullptr;
        activeId_ = 0;
    }

    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> pads_;
    std::deque<DetectionEvent> pendingDetection_;
    SDL_JoystickID activeId_ = 0;
    SDL_Gamepad* activePad_ = nullptr;
};

