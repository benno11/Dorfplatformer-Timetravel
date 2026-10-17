#pragma once
#include "Player.h"

namespace Shield {
inline bool absorbDamage(Player& player) {
    if (!player.hasShield) return false;
    player.hasShield = false;
    return true;
}
} // namespace Shield
