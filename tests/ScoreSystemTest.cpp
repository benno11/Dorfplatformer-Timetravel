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
            if (frame % fps == 0)
                require(total == ScoreSystem::kMaxGainPerSecond * (frame / fps),
                    "Every complete second must award exactly 1200 points");
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
    for (int fps : {10, 15, 30, 60, 144}) {
        ScoreSystem::PayoutAnimation animation;
        int pending = 10000, paid = 0, ticks = 0;
        double lastTick = -1;
        for (int frame = 1; pending > 0 && frame <= fps * 20; ++frame) {
            const auto payout = animation.advance(pending, 1.0 / fps);
            require(payout.points >= 0 && payout.points <= pending, "Animation overpaid a bonus");
            require(payout.points <= (int)std::ceil(1200.0 / fps), "Animation exceeded the score cap");
            paid += payout.points;
            pending -= payout.points;
            const double elapsed = (double)frame / fps;
            const double expected = std::min(10000.0, 1200 * elapsed);
            require(std::fabs(paid - expected) <= 1.001, "Tally must advance at a constant real-time rate");
            if (frame % fps == 0 && pending > 0)
                require(paid == 1200 * (frame / fps), "Animated payout must award exactly 1200 per second");
            if (payout.playSound) {
                require(payout.points > 0, "Silent frames must not trigger tally sounds");
                require(lastTick < 0 || elapsed - lastTick >= 0.08 - 0.001,
                    "Tally sounds must not overlap every frame");
                lastTick = elapsed;
                ++ticks;
            }
        }
        require(paid == 10000 && pending == 0, "Animation must preserve all bonus points");
        require(ticks > 10, "Payout needs paced audible ticks");
        animation.reset();
        require(!animation.advance(1000, 0).playSound && animation.advance(1000, 0).points == 0,
            "Pausing must stop scoring and sounds");
        require(animation.advance(10000, 5).points <= 1200, "Long stalls must respect the one-second burst limit");
    }
    ScoreSystem::PayoutAnimation variableFrames;
    int variablePaid = 0;
    double variableElapsed = 0;
    for (int frame = 0; frame < 50; ++frame) {
        const double frameTimes[]{0.01, 0.027, 0.05, 0.11, 0.016};
        const double seconds = frameTimes[frame % 5];
        variableElapsed += seconds;
        variablePaid += variableFrames.advance(100000, seconds).points;
        require(std::fabs(variablePaid - 1200 * variableElapsed) <= 1.001,
            "Changing frame times must not change the payout speed");
    }
    ScoreSystem::PayoutAnimation smallBonus;
    int pending = 1, ticks = 0;
    for (int i = 0; i < 30 && pending; ++i) {
        const auto payout = smallBonus.advance(pending, 1.0 / 60);
        pending -= payout.points;
        ticks += payout.playSound;
    }
    require(pending == 0 && ticks == 1, "Small bonuses must complete with one sound");
    std::cout << "Score rate, animation, audio cadence, and overflow tests passed\n";
}
