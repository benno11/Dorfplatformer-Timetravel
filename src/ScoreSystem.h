#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace ScoreSystem {
inline constexpr int kMaxGainPerSecond = 1200;

// Keep sub-point timing across frames, but never bank unused whole points.
// Pauses and stalls cannot create a burst of accumulated score credit.
class PayoutLimiter {
public:
    int budget(double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0) return 0;
        const double credit = fractional + std::min(seconds, 1.0) * kMaxGainPerSecond;
        const int points = (int)std::floor(credit);
        fractional = credit - points;
        return points;
    }
    void reset() { fractional = 0; }
private:
    double fractional = 0;
};

struct PayoutStep {
    int points = 0;
    bool playSound = false;
};

class PayoutAnimation {
public:
    PayoutStep advance(int pending, double seconds) {
        if (pending <= 0 || !std::isfinite(seconds) || seconds <= 0) return {};
        const double step = std::min(seconds, 1.0);
        soundCooldown = std::max(0.0, soundCooldown - step);
        PayoutStep result;
        result.points = std::min(pending, limiter.budget(step));
        if (result.points > 0 && soundCooldown <= 0) {
            result.playSound = true;
            soundCooldown = 0.08; // A tally tick, not a new overlapping sound every frame.
        }
        return result;
    }
    void reset() { *this = {}; }
private:
    PayoutLimiter limiter;
    double soundCooldown = 0;
};

inline int addPoints(int& score, int points) {
    score = std::max(0, score);
    const int awarded = std::min(std::max(0, points), std::numeric_limits<int>::max() - score);
    score += awarded;
    return awarded;
}
}
