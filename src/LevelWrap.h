#pragma once

#include <vector>

#include "LevelLoader.h"
#include "Player.h"

inline bool LevelHasWrapTrigger(const std::vector<int>& objectIds, int triggerId) {
    for (int id : objectIds) {
        if (id == triggerId) return true;
    }
    return false;
}

inline bool LevelHasWrapControlTrigger(const std::vector<int>& objectIds,
                                       int enableTriggerId,
                                       int disableTriggerId) {
    for (int id : objectIds) {
        if (id == enableTriggerId || id == disableTriggerId) return true;
    }
    return false;
}

inline bool PlayerTouchesLevelWrapTrigger(const Player& player,
                                          const std::vector<ObjectInstance>& objects,
                                          const std::vector<int>& objectIds,
                                          int triggerId) {
    const float playerRight = player.x + (float)player.w;
    const float playerBottom = player.y + (float)player.h;
    for (int i = 0; i < (int)objects.size(); ++i) {
        if (i >= (int)objectIds.size() || objectIds[i] != triggerId) continue;
        const float left = objects[i].x - 16.0f;
        const float top = objects[i].y - 16.0f;
        if (playerRight > left && player.x < left + 32.0f &&
            playerBottom > top && player.y < top + 32.0f) {
            return true;
        }
    }
    return false;
}

inline bool UpdateLevelWrapTriggerState(const Player& player,
                                        const std::vector<ObjectInstance>& objects,
                                        const std::vector<int>& objectIds,
                                        int enableTriggerId,
                                        int disableTriggerId,
                                        bool active) {
    if (PlayerTouchesLevelWrapTrigger(player, objects, objectIds, disableTriggerId)) {
        return false;
    }
    if (PlayerTouchesLevelWrapTrigger(player, objects, objectIds, enableTriggerId)) {
        return true;
    }
    return active;
}
