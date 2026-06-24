#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace UiScale {

constexpr int kMinPercent = 50;
constexpr int kMaxPercent = 400;
constexpr int kStepPercent = 5;
constexpr int kEdgePaddingMin = 0;
constexpr int kEdgePaddingMax = 96;
constexpr int kEdgePaddingStep = 4;

inline int clampPercent(int percent) {
    return std::clamp(percent, kMinPercent, kMaxPercent);
}

inline int clampEdgePadding(int padding) {
    return std::clamp(padding, kEdgePaddingMin, kEdgePaddingMax);
}

inline int stepPercent(int percent, int dir) {
    return clampPercent(percent + dir * kStepPercent);
}

inline int stepEdgePadding(int padding, int dir) {
    return clampEdgePadding(padding + dir * kEdgePaddingStep);
}

inline float multiplier(int percent) {
    return std::clamp(static_cast<float>(clampPercent(percent)) / 100.0f, 0.5f, 4.0f);
}

inline float settingsMultiplier(int percent) {
    return std::clamp(multiplier(percent) * 2.0f, 1.0f, 4.7f);
}

inline int fromSliderX(int x, const SDL_Rect& slider, int minValue, int maxValue) {
    int rel = x - slider.x;
    if (rel < 0) rel = 0;
    if (rel > slider.w) rel = slider.w;
    const float t = rel / static_cast<float>(std::max(1, slider.w));
    return std::clamp(static_cast<int>(std::lround(minValue + t * (maxValue - minValue))), minValue, maxValue);
}

inline int percentFromSliderX(int x, const SDL_Rect& slider) {
    return fromSliderX(x, slider, kMinPercent, kMaxPercent);
}

inline int edgePaddingFromSliderX(int x, const SDL_Rect& slider) {
    return fromSliderX(x, slider, kEdgePaddingMin, kEdgePaddingMax);
}

inline float normalizedPercent(int percent) {
    return (clampPercent(percent) - kMinPercent) / static_cast<float>(kMaxPercent - kMinPercent);
}

inline float normalizedEdgePadding(int padding) {
    return (clampEdgePadding(padding) - kEdgePaddingMin) / static_cast<float>(kEdgePaddingMax - kEdgePaddingMin);
}

inline SDL_Rect scaleRectCentered(const SDL_Rect& in, float scale) {
    if (scale <= 0.0f) return in;
    const float cx = static_cast<float>(in.x) + static_cast<float>(in.w) * 0.5f;
    const float cy = static_cast<float>(in.y) + static_cast<float>(in.h) * 0.5f;
    const int nw = std::max(1, static_cast<int>(std::lround(static_cast<float>(in.w) * scale)));
    const int nh = std::max(1, static_cast<int>(std::lround(static_cast<float>(in.h) * scale)));
    return SDL_Rect{
        static_cast<int>(std::lround(cx - static_cast<float>(nw) * 0.5f)),
        static_cast<int>(std::lround(cy - static_cast<float>(nh) * 0.5f)),
        nw,
        nh
    };
}

}  // namespace UiScale
