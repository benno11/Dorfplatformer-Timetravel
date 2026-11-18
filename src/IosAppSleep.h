#pragma once
#include "Platform.h"
#include <SDL3/SDL.h>
#include <functional>
#include <utility>
#include <vector>

namespace IosAppSleep {
inline bool sleeping = false;
inline std::function<void(bool)> callback;
// Nested screens can save their own state in addition to the app-level hook.
struct Hook {
    std::function<void(bool)> previous;
    explicit Hook(std::function<void(bool)> fn) : previous(callback) {
        callback = [parent = previous, fn = std::move(fn)](bool asleep) {
            if (parent) parent(asleep);
            fn(asleep);
        };
    }
    ~Hook() { callback = std::move(previous); }
};
inline void observe(const SDL_Event& event) {
    const bool background = event.type == SDL_EVENT_WILL_ENTER_BACKGROUND || event.type == SDL_EVENT_DID_ENTER_BACKGROUND;
    const bool foreground = event.type == SDL_EVENT_DID_ENTER_FOREGROUND;
    if ((background && !sleeping) || (foreground && sleeping)) {
        sleeping = background;
        if (callback) callback(sleeping);
        if (!sleeping) SDL_ResetKeyboard();
    }
}
inline void waitForForeground(SDL_Event* event) {
    const SDL_Event background = *event;
    std::vector<SDL_Event> deferred;
    while (sleeping) {
        if (!SDL_WaitEvent(event)) { SDL_Delay(50); continue; }
        observe(*event);
        if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_TERMINATING) return;
        if (event->type == SDL_EVENT_RENDER_TARGETS_RESET || event->type == SDL_EVENT_RENDER_DEVICE_RESET ||
            event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event->type == SDL_EVENT_WINDOW_RESIZED)
            deferred.push_back(*event);
    }
    // Owners still receive background then foreground in order, after the
    // blocking wait, so they can reset frame clocks and clear held controls.
    SDL_PushEvent(event);
    for (auto& pending : deferred) SDL_PushEvent(&pending);
    *event = background;
}
inline bool pollEvent(SDL_Event* event, bool enabled = PLATFORMER_IOS != 0) {
    if (!enabled) return SDL_PollEvent(event);
    if (!SDL_PollEvent(event)) return false;
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_TERMINATING) return true;
    observe(*event);
    if (sleeping) waitForForeground(event);
    return true;
}
}
