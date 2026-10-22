#include "Bumper.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        TileMap map;
        map.resize(64, 64);
        Player player;
        player.x = 128;
        Bumper::Impact effect;
        require(Bumper::bounce(player, map, 100, -1200), "First contact must bounce upward");
        require(player.vy == -1200 && !player.onGround && player.y == 100, "Upward launch must set player movement");
        require(effect.trigger(), "First successful bounce must trigger feedback");
        effect.update(0.1f);
        const float animationRemaining = effect.remaining;
        const float cooldownRemaining = effect.cooldownRemaining;
        require(Bumper::bounce(player, map, 200, 1200), "Returning below the bumper during cooldown must still bounce downward");
        require(player.vy == 1200 && !player.onGround && player.y == 200, "Cooldown must not suppress a downward launch");
        require(!effect.trigger() && effect.remaining == animationRemaining && effect.cooldownRemaining == cooldownRemaining,
            "Repeated contacts must not restart animation or extend the feedback cooldown");
        require(Bumper::bounce(player, map, 100, -1200), "Returning above the bumper during cooldown must still bounce upward");
        require(player.vy == -1200 && !effect.trigger(), "Gameplay bounce must work without repeated feedback");
        Bumper::Impact otherEffect;
        require(otherEffect.trigger(), "Each bumper must have independent feedback");
        effect.update(0);
        effect.update(std::numeric_limits<float>::quiet_NaN());
        require(effect.cooldownRemaining == cooldownRemaining, "Paused or invalid time must not advance the cooldown");
        effect.update(0.16f);
        require(effect.remaining > 0 && effect.cooldownRemaining == 0, "Animation may outlast its feedback cooldown");
        require(Bumper::bounce(player, map, 200, 1200) && effect.trigger(),
            "Feedback may trigger again after cooldown even while the old animation remains active");
        effect.update(1);
        require(effect.remaining == 0 && effect.cooldownRemaining == 0, "Finished timers must clamp to zero");

        // Preserve the existing solid-safety checks when the launch is obstructed.
        player.x = 64;
        map.setSolid(2, 2, 1);
        require(!Bumper::bounce(player, map, 96, -1200) && player.vy == 0,
            "A ceiling immediately above the launch must stop the bounce");
        require(!RectHitsSolid(map, player.x, player.y, player.w, player.h), "Blocked launches must leave the player outside solids");
        std::fill(map.solid.begin(), map.solid.end(), 1);
        const float oldY = player.y;
        require(!Bumper::bounce(player, map, 100, -1200) && player.y == oldY,
            "An embedded bumper must reject the launch and restore the original position");
        std::cout << "Bumper tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
