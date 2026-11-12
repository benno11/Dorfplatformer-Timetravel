#include "ScoreSystem.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    for (int fps : {30, 60, 144, 1000}) {
        ScoreSystem::PayoutLimiter limiter;
        int total = 0;
        for (int frame = 1; frame <= fps * 10; ++frame) {
            total += limiter.budget(1.0 / fps);
            const double elapsed = (double)frame / fps;
            require(total <= ScoreSystem::kMaxGainPerSecond * elapsed + 0.001,
                "Payout exceeded the per-second rate");
        }
        require(total >= ScoreSystem::kMaxGainPerSecond * 10 - 1,
            "Fractional credit was lost at high frame rates");
    }
    ScoreSystem::PayoutLimiter limiter;
    require(limiter.budget(0) == 0 && limiter.budget(-1) == 0 &&
        limiter.budget(std::numeric_limits<double>::quiet_NaN()) == 0 &&
        limiter.budget(std::numeric_limits<double>::infinity()) == 0,
        "Invalid timing must not award points");
    require(limiter.budget(30) == ScoreSystem::kMaxGainPerSecond,
        "Long stalls must not create a large burst");
    limiter.reset();
    require(limiter.budget(1.0 / 60) <= 20, "Restarting a payout must not grant stored credit");
    int score = 50;
    require(ScoreSystem::addPoints(score, 25) == 25 && score == 75, "Ordinary awards must be preserved");
    require(ScoreSystem::addPoints(score, -20) == 0 && score == 75, "Negative payouts must not remove points");
    score = std::numeric_limits<int>::max() - 5;
    require(ScoreSystem::addPoints(score, 1200) == 5 && score == std::numeric_limits<int>::max(),
        "The score must saturate safely without overflow");
    require(ScoreSystem::addPoints(score, 1200) == 0, "Maximum score must remain stable");
    std::cout << "Score rate and overflow tests passed\n";
}
