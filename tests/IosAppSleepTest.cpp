#include "IosAppSleep.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <thread>
#include <vector>

int main() {
    assert(SDL_Init(SDL_INIT_EVENTS));
    std::vector<bool> transitions;
    IosAppSleep::Hook app([&](bool value) { transitions.push_back(value); });
    SDL_Event background{};
    background.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
    SDL_PushEvent(&background);
    std::thread foreground([] {
        SDL_Delay(100);
        SDL_Event event{};
        event.type = SDL_EVENT_DID_ENTER_BACKGROUND;
        SDL_PushEvent(&event);
        event.type = SDL_EVENT_RENDER_TARGETS_RESET;
        SDL_PushEvent(&event);
        event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        SDL_PushEvent(&event);
    });
    SDL_Event event{};
    const auto started = SDL_GetTicks();
    assert(IosAppSleep::pollEvent(&event, true));
    assert(SDL_GetTicks() - started >= 80);
    assert(event.type == SDL_EVENT_WILL_ENTER_BACKGROUND);
    assert((transitions == std::vector<bool>{true, false}));
    assert(!IosAppSleep::sleeping);
    bool resumed = false, restored = false;
    const auto deadline = SDL_GetTicks() + 1000;
    while (SDL_GetTicks() < deadline && (!resumed || !restored)) {
        if (IosAppSleep::pollEvent(&event, true)) {
            if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) resumed = true;
            if (event.type == SDL_EVENT_RENDER_TARGETS_RESET) restored = true;
        } else SDL_Delay(1);
    }
    assert(resumed && restored);
    foreground.join();
    SDL_Quit();
}
