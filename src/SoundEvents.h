#pragma once
#include <array>
#include <atomic>
namespace SoundEvents {
enum class Event : unsigned { Sign, Checkpoint, Bounce, Hover, Click, Error, Count };
inline constexpr std::array<const char*,6> paths{{
    "assets/Audio/sfx/signflip.mp3", "assets/Audio/sfx/max.wav",
    "assets/Audio/sfx/SuperBounce.mp3", "assets/Audio/sfx/Hint.mp3",
    "assets/Audio/sfx/Click5.mp3", "assets/Audio/sfx/Error3.mp3"}};
// Workers enqueue notifications only; playback is drained on the SDL main thread.
inline std::atomic<unsigned>& pending(){static std::atomic<unsigned> value{0};return value;}
inline void emit(Event event){pending().fetch_or(1u<<static_cast<unsigned>(event),std::memory_order_relaxed);}
inline unsigned take(){return pending().exchange(0,std::memory_order_relaxed);}
}
