                        auto tryRelax = [&](int fromIdx, int nx, int ny, int stepCost) {
                        if (nx < 0 || ny < 0 || nx >= map.w || ny >= map.h) return;
                        if (!standableTile(nx, ny)) return;
                        const int ni = indexOf(nx, ny);
                        if (nodes[ni].closed) return;
                        const int ng = nodes[fromIdx].g + stepCost + tileRisk[ni];
                        if (ng >= nodes[ni].g) return;
                        nodes[ni].g = ng;
                        nodes[ni].f = ng + heuristic(nx, ny);
                        nodes[ni].parent = fromIdx;
                        open.push_back(ni);
                        };

                        const int startIdx = indexOf(start.x, start.y);
                        nodes[startIdx].g = 0;
                        nodes[startIdx].f = heuristic(start.x, start.y);
                        open.push_back(startIdx);
                        int expandedAttempt = 0;
                        int guardAttempt = 0;
                        while (!open.empty() && guardAttempt++ < guardLimit) {
                        int bestOpenPos = 0;
                        int bestNode = open[0];
                        for (int i = 1; i < (int)open.size(); ++i) {
                            if (nodes[open[i]].f < nodes[bestNode].f) {
                                bestOpenPos = i;
                                bestNode = open[i];
                            }
                        }
                        open[bestOpenPos] = open.back();
                        open.pop_back();
                        if (nodes[bestNode].closed) continue;
                        nodes[bestNode].closed = true;
                        expandedAttempt++;
                        const int cx = bestNode % map.w;
                        const int cy = bestNode / map.w;
                        if (cx == target.x && cy == target.y) {
                            foundIdx = bestNode;
                            expanded = expandedAttempt;
                            guard = guardAttempt;
                            usedAttempt = attempt;
                            break;
                        }

                        tryRelax(bestNode, cx + 1, cy, forwardCost);
                        tryRelax(bestNode, cx - 1, cy, backCost);
                        // Prefer longer stable strides when ground permits.
                        tryRelax(bestNode, cx + 2, cy, forwardCost + 4);
                        tryRelax(bestNode, cx - 2, cy, backCost + 6);
                        // Allow straight drops when directly above a safe standable tile.
                        for (int dy = 1; dy <= downMax + 1; ++dy) {
                            tryRelax(bestNode, cx, cy + dy, 10 + dy * 3);
                        }
                        for (int dy = 1; dy <= downMax; ++dy) {
                            // Descents are allowed but carry slight risk cost.
                            tryRelax(bestNode, cx + 1, cy + dy, forwardCost + 4 + dy * 3);
                            tryRelax(bestNode, cx - 1, cy + dy, backCost + 8 + dy * 4);
                            tryRelax(bestNode, cx + 2, cy + dy, forwardCost + 7 + dy * 4);
                            tryRelax(bestNode, cx - 2, cy + dy, backCost + 11 + dy * 5);
                        }
                        for (int jx = 1; jx <= jumpMax; ++jx) {
                            for (int jy = 1; jy <= jumpMax; ++jy) {
                                int nx = cx + jx;
                                int ny = cy - jy;
                                if (simulateJumpReachable(cx, cy, nx, ny)) tryRelax(bestNode, nx, ny, jumpForwardBase + jx * 2 + jy * 4);
                                nx = cx - jx;
                                if (simulateJumpReachable(cx, cy, nx, ny)) tryRelax(bestNode, nx, ny, jumpBackBase + jx * 4 + jy * 6);
                            }
                        }
                    }
                        if (foundIdx < 0) {
                            SDL_Log("demo.path: attempt %d failed (expanded=%d guard=%d)", attempt, expandedAttempt, guardAttempt);
                        } else {
                            // Reconstruct using nodes from successful attempt.
                            demoState.pathTiles.clear();
                            demoState.waypointIndex = 0;
                            std::vector<SDL_Point> reversePath;
                            for (int at = foundIdx; at >= 0; at = nodes[at].parent) {
                                reversePath.push_back(SDL_Point{at % map.w, at / map.w});
                                if (at == startIdx) break;
                            }
                            for (int i = (int)reversePath.size() - 1; i >= 0; --i) {
                                demoState.pathTiles.push_back(reversePath[(size_t)i]);
                            }
                        }
                    }

                    if (foundIdx < 0) {
                        SDL_Log("demo.path: rebuild failed (unreachable target, expanded=%d, guard=%d)", expanded, guard);
                        demoState.pathTiles.clear();
                        demoState.waypointIndex = 0;
                        return;
                    }
                    if (demoState.pathTiles.size() >= 3) {
                        std::vector<SDL_Point> smoothed;
                        smoothed.reserve(demoState.pathTiles.size());
                        smoothed.push_back(demoState.pathTiles.front());
                        for (size_t i = 1; i + 1 < demoState.pathTiles.size(); ++i) {
                            const SDL_Point& a = smoothed.back();
                            const SDL_Point& b = demoState.pathTiles[i];
                            const SDL_Point& c = demoState.pathTiles[i + 1];
                            const int abx = b.x - a.x;
                            const int aby = b.y - a.y;
                            const int bcx = c.x - b.x;
                            const int bcy = c.y - b.y;
                            const bool sameDirection = (abx == bcx) && (aby == bcy);
                            if (!sameDirection) smoothed.push_back(b);
                        }
                        smoothed.push_back(demoState.pathTiles.back());
                        demoState.pathTiles.swap(smoothed);
                    }
                    SDL_Log("demo.path: rebuild ok (attempt=%d expanded=%d, guard=%d, path_len=%d)", usedAttempt, expanded, guard, (int)demoState.pathTiles.size());
                };

                if (demoState.repathTimer <= 0.0f || demoState.pathTiles.empty() || demoState.waypointIndex >= demoState.pathTiles.size()) {
                    rebuildDemoPath();
                    demoState.repathTimer = 0.85f;
                }

                const float probeNearX = (demoState.dir > 0.0f) ? (player.x + player.w + 6.0f) : (player.x - 6.0f);
                const float probeFarX = (demoState.dir > 0.0f) ? (player.x + player.w + t * 1.3f) : (player.x - t * 1.3f);
                const int txNear = (int)std::floor(probeNearX / (float)t);
                const int txFar = (int)std::floor(probeFarX / (float)t);
                const int topTy = (int)std::floor((player.y + 2.0f) / (float)t);
                const int footTy = (int)std::floor((player.y + player.h + 1.0f) / (float)t);
                bool wallAhead = false;
                for (int ty = topTy; ty <= footTy - 1; ++ty) {
                    if (map.getSolid(txNear, ty) || map.getSolid(txFar, ty)) {
                        wallAhead = true;
                        break;
                    }
                }
                auto hasGroundAt = [&](int tx, int ty) -> bool {
                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return false;
                    return map.getSolid(tx, ty) || map.getSemiSolid(tx, ty);
                };
                auto holeDepthAt = [&](int tx, int startTy, int maxDepth) -> int {
                    for (int d = 0; d <= maxDepth; ++d) {
                        const int ty = startTy + d;
                        if (ty >= map.h) return maxDepth + 1;
                        if (hasGroundAt(tx, ty)) return d;
                    }
                    return maxDepth + 1;
                };
                const int txFar2 = (int)std::floor(((demoState.dir > 0.0f) ? (player.x + player.w + t * 2.1f) : (player.x - t * 2.1f)) / (float)t);
                const int txFar3 = (int)std::floor(((demoState.dir > 0.0f) ? (player.x + player.w + t * 2.8f) : (player.x - t * 2.8f)) / (float)t);
                const int probeCols[4] = {txNear, txFar, txFar2, txFar3};
                int longestGapRun = 0;
                int gapRun = 0;
                int minLandingDepth = 9999;
                for (int i = 0; i < 4; ++i) {
                    const int depth = holeDepthAt(probeCols[i], footTy, 6);
                    if (depth == 0) {
                        gapRun = 0;
                    } else {
                        gapRun++;
                        longestGapRun = std::max(longestGapRun, gapRun);
                    }
                    minLandingDepth = std::min(minLandingDepth, depth);
                }
                const bool gapAhead = (longestGapRun >= 2) || (minLandingDepth >= 3);
                // Consider landing "safe" when one of the forward probes can be reached
                // with at most a short drop.
                const bool safeLandingAhead =
                    (holeDepthAt(txFar, footTy, 6) <= 2) ||
                    (holeDepthAt(txFar2, footTy, 6) <= 2) ||
                    (holeDepthAt(txFar3, footTy, 6) <= 2);

                if (!demoState.pathTiles.empty() && demoState.waypointIndex < demoState.pathTiles.size()) {
                    // Track progress on a fixed start->finish path by snapping to nearest upcoming node.
                    const float playerCenterX = player.x + player.w * 0.5f;
                    const float playerCenterY = player.y + player.h * 0.5f;
                    const size_t prevWaypointIndex = demoState.waypointIndex;
                    size_t nearestIdx = demoState.waypointIndex;
                    float nearestD2 = std::numeric_limits<float>::max();
                    const size_t lookEnd = std::min(demoState.pathTiles.size(), demoState.waypointIndex + 28);
                    for (size_t i = demoState.waypointIndex; i < lookEnd; ++i) {
                        const SDL_Point p = demoState.pathTiles[i];
                        const float px = (p.x + 0.5f) * (float)t;
                        const float py = (p.y + 0.5f) * (float)t;
                        const float dx = px - playerCenterX;
                        const float dy = py - playerCenterY;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < nearestD2) {
                            nearestD2 = d2;
                            nearestIdx = i;
                        }
                    }
                    demoState.waypointIndex = nearestIdx;
                    if (demoState.waypointIndex != prevWaypointIndex) {
                        SDL_Log("demo.path: waypoint %d -> %d", (int)prevWaypointIndex, (int)demoState.waypointIndex);
                    }
                }
                if (!demoState.pathTiles.empty() && demoState.waypointIndex < demoState.pathTiles.size()) {
                    while (demoState.waypointIndex < demoState.pathTiles.size() &&
                           !standableTile(demoState.pathTiles[demoState.waypointIndex].x,
                                          demoState.pathTiles[demoState.waypointIndex].y)) {
                        demoState.waypointIndex++;
                    }
                    if (demoState.waypointIndex >= demoState.pathTiles.size()) {
                        demoState.repathTimer = 0.0f;
                    }
                }
                if (!demoState.pathTiles.empty() && demoState.waypointIndex < demoState.pathTiles.size()) {
                    const SDL_Point wp = demoState.pathTiles[demoState.waypointIndex];
                    const float waypointX = (wp.x + 0.5f) * (float)t;
                    const float waypointY = (wp.y + 0.5f) * (float)t;
                    const float playerCenterX = player.x + player.w * 0.5f;
                    const float playerCenterY = player.y + player.h * 0.5f;
                    const float dxWp = waypointX - playerCenterX;
                    const float dyWp = waypointY - playerCenterY;
                    if (std::fabs(dxWp) < 8.0f && std::fabs(dyWp) < (float)t * 0.55f) {
                        if (demoState.waypointIndex + 1 < demoState.pathTiles.size()) {
                            demoState.waypointIndex++;
                        }
                    }
                    const float steer = (dxWp > 4.0f) ? 1.0f : ((dxWp < -10.0f) ? -1.0f : 1.0f);
                    demoState.dir = steer;
                    touchMove = steer;
                    if (player.onGround && demoState.jumpCooldown <= 0.0f && dyWp < -10.0f) {
                        demoState.jumpCooldown = 0.18f;
                        demoState.jumpHoldTimer = 0.11f;
                    }
                }

                bool bumperAhead = false;
                bool springAhead = false;
                bool fastTravelAhead = false;
                int fastTravelAheadId = -1;
                float nearestFastTravelDist = std::numeric_limits<float>::max();
                float nearestFastTravelCenterX = 0.0f;
                float nearestSpringDist = std::numeric_limits<float>::max();
                float nearestSpringCenterX = 0.0f;
                const float playerCenterX = player.x + player.w * 0.5f;
                const float playerCenterY = player.y + player.h * 0.5f;
                for (const auto& obj : objects) {
                    int objId = 0;
                    try { objId = std::stoi(obj.id); } catch (...) { continue; }
                    const float objCenterX = obj.x;
                    const float objCenterY = obj.y;
                    const float dx = objCenterX - playerCenterX;
                    const float dy = objCenterY - playerCenterY;
                    const float aheadDist = dx * ((demoState.dir > 0.0f) ? 1.0f : -1.0f);
                    if (objId == 46) {
                        if (aheadDist >= -8.0f && aheadDist <= (float)t * 3.0f && std::fabs(dy) <= (float)t * 1.5f) {
                            bumperAhead = true;
                        }
                        continue;
                    }
                    if (objId == 31) {
                        if (aheadDist >= -10.0f && aheadDist <= (float)t * 4.0f && std::fabs(dy) <= (float)t * 1.8f) {
                            if (aheadDist < nearestSpringDist) {
                                nearestSpringDist = aheadDist;
                                nearestSpringCenterX = objCenterX;
                                springAhead = true;
                            }
                        }
                        continue;
                    }
                    if (objId < 57 || objId > 61) continue;
                    if (aheadDist < -20.0f || aheadDist > (float)t * 7.0f) continue;
                    if (std::fabs(dy) > (float)t * 2.5f) continue;
                    if (aheadDist < nearestFastTravelDist) {
                        nearestFastTravelDist = aheadDist;
                        nearestFastTravelCenterX = objCenterX;
                        fastTravelAheadId = objId;
                        fastTravelAhead = true;
                    }
                }

                bool shouldJump = false;
                if (player.onGround && demoState.jumpCooldown <= 0.0f) {
                    shouldJump = wallAhead || (gapAhead && safeLandingAhead) || bumperAhead;
                }

                if (springAhead) {
                    // Line up with spring and let spring impulse handle the vertical movement.
                    const float springDeltaX = nearestSpringCenterX - playerCenterX;
                    if (std::fabs(springDeltaX) > 4.0f) {
                        touchMove = (springDeltaX > 0.0f) ? 1.0f : -1.0f;
                        demoState.dir = touchMove;
                    }
                    if (nearestSpringDist <= (float)t * 1.6f) {
                        shouldJump = false;
                        demoState.jumpHoldTimer = 0.0f;
                    }
                    // If terrain demands vertical gain and a spring is nearby, trust spring pathing.
                    if (wallAhead || gapAhead) {
                        shouldJump = false;
                    }
                }

                if (fastTravelAhead) {
                    touchMove = demoState.dir;
                    const float targetXDelta = nearestFastTravelCenterX - playerCenterX;
                    if (std::fabs(targetXDelta) > 6.0f) {
                        touchMove = (targetXDelta > 0.0f) ? 1.0f : -1.0f;
                        demoState.dir = touchMove;
                    }
                    if (fastTravelAheadId == 58) {
                        demoDown = true;
                    }
                    if (fastTravelAheadId == 57 || fastTravelAheadId == 61) {
                        shouldJump = false;
                    }
                    // Safety: avoid entering nearby fast-travel if already close to route end-sign zone.
                    if (endSignState.present && std::fabs(endSignState.objectX - playerCenterX) < (float)t * 10.0f) {
                        fastTravelAhead = false;
                        demoDown = false;
                        touchMove = demoState.dir;
                    }
                }

                if (bumperAhead && !wallAhead && player.onGround) {
                    // Prefer avoiding bumper collisions when there is space to sidestep/back off.
                    shouldJump = false;
                    if (!gapAhead) touchMove = -demoState.dir;
                }

                if (shouldJump) {
                    demoState.jumpCooldown = bumperAhead ? 0.14f : 0.22f;
                    demoState.jumpHoldTimer = bumperAhead ? 0.14f : 0.10f;
                }
                demoJump = (demoState.jumpHoldTimer > 0.0f);
                const float dx = player.x - demoState.lastX;
                if (std::fabs(dx) < 2.0f) demoState.stuckTime += dt;
                else demoState.stuckTime = 0.0f;
                if (player.onGround && demoState.stuckTime > 0.9f) {
                    demoState.dir = -demoState.dir;
                    demoState.stuckTime = 0.0f;
                    demoState.jumpCooldown = 0.0f;
                    demoState.jumpHoldTimer = 0.12f;
                    demoJump = true;
                }
                demoState.lastX = player.x;
                if (!fastTravelAhead) {
                    touchMove = demoState.dir;
                } else {
                    touchMove = std::clamp(touchMove, -1.0f, 1.0f);
                }
                touchDown = demoDown;
                touchJump = demoJump;
            }
            const bool forceRightMovement = endSignState.lockPlayerRight;
            float gamepadMove = forceRightMovement ? 1.0f : input.gameplayMoveX();
            bool gamepadDown = forceRightMovement ? false : input.gameplayDownHeld();
            bool gamepadJump = forceRightMovement ? false : input.gameplayJumpHeld();
            bool gamepadFreeMove = input.freeMoveHeld();
            const bool fastTravelEnabled = fastTravelTriggered;
            if (playerSpawnLockFrames > 0) {
                playerSpawnLockFrames--;
                player.x = playerSpawnLockX;
                player.y = playerSpawnLockY;
                player.vx = 0.0f;
                player.vy = 0.0f;
                player.jumpHeld = false;
                player.jumpWasDown = false;
                player.jumpHoldTime = 0.0f;
                player.jumpBufferTime = 0.0f;
                touchMove = 0.0f;
                touchDown = false;
                touchJump = false;
                inputMove = 0.0f;
                inputDown = false;
                gamepadMove = 0.0f;
                gamepadDown = false;
                gamepadJump = false;
                gamepadFreeMove = false;
            }
            if (forceRightMovement) {
                touchMove = 1.0f;
                touchDown = false;
                touchJump = false;
                inputMove = 1.0f;
                inputDown = false;
            }
            PlayerUpdateResult upd = PlayerUpdateResult::RenderOnly;
            const float beforeNormalX = player.x;
            const float beforeNormalY = player.y;
                if (!fastTravelEnabled) {
                    upd = UpdatePlayerMovement(
                        player, map, dt, jumpBufferMax, movementCfg,
                        touchMove, touchDown, touchJump,
                        gamepadMove, gamepadDown, gamepadJump, gamepadFreeMove,
                        keybinds,
                        inputMove, inputDown
                    );
                    if (std::fabs(player.x - beforeNormalX) > 0.01f || std::fabs(player.y - beforeNormalY) > 0.01f) {
                        addMovementReason(MR_NORMAL_MOVEMENT);
                    }
            } else {
                // Fast-travel mode disables normal movement/physics.
                inputMove = 0.0f;
                inputDown = false;
            }
            replayInput.touchMove = touchMove;
            replayInput.touchDown = touchDown;
            replayInput.touchJump = touchJump;
            replayInput.gamepadMove = gamepadMove;
            replayInput.gamepadDown = gamepadDown;
            replayInput.gamepadJump = gamepadJump;
            replayInput.gamepadFreeMove = gamepadFreeMove;
            replayInput.inputMove = inputMove;
            replayInput.inputDown = inputDown;
            replayInput.forceRightMovement = forceRightMovement;
            replayInput.fastTravelEnabled = fastTravelEnabled;
            replayInput.demoEnabled = demoState.enabled;
            {
                const float beforeWrapX = player.x;
                const float beforeWrapY = player.y;
                bool wrappedX = false;
                bool wrappedY = false;
                const bool horizontalWrapActive = gameplayWrapX;
                const bool verticalWrapNow = isVerticalWrapEnabledAtX(player.x);
                verticalWrapActive = verticalWrapNow;
                if (verticalWrapNow) {
                    const float mapHeightPx = (float)(map.h * map.tileSize);
                    while (player.y + (float)player.h < 0.0f) {
                        player.y += mapHeightPx;
                        wrappedY = true;
                    }
                    while (player.y >= mapHeightPx) {
                        player.y -= mapHeightPx;
                        wrappedY = true;
                    }
                }
                if (horizontalWrapActive) {
                    const float mapWidthPx = (float)(map.w * map.tileSize);
                    while (player.x + (float)player.w < 0.0f) {
                        player.x += mapWidthPx;
                        wrappedX = true;
                    }
                    while (player.x >= mapWidthPx) {
                        player.x -= mapWidthPx;
                        wrappedX = true;
                    }
                }
                if ((wrappedX || wrappedY) && RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                    const float wrappedPosX = player.x;
                    const float wrappedPosY = player.y;
                    bool foundSafe = false;
                    auto tryPlace = [&](float nx, float ny) -> bool {
                        if (RectHitsSolid(map, nx, ny, player.w, player.h)) return false;
                        player.x = nx;
                        player.y = ny;
                        foundSafe = true;
                        return true;
                    };
                    float preferX = 0.0f;
                    float preferY = 0.0f;
                    if (wrappedX) {
                        if (player.vx > 0.01f) preferX = -1.0f;
                        else if (player.vx < -0.01f) preferX = 1.0f;
                        else preferX = (beforeWrapX <= player.x) ? 1.0f : -1.0f;
                    }
                    if (wrappedY) {
                        if (player.vy > 0.01f) preferY = -1.0f;
                        else if (player.vy < -0.01f) preferY = 1.0f;
                        else preferY = (beforeWrapY <= player.y) ? 1.0f : -1.0f;
                    }
                    for (int up = 0; up <= 72 && !foundSafe; up += 2) {
                        for (int side = 0; side <= 36 && !foundSafe; side += 2) {
                            const float sx = (side == 0) ? 0.0f : ((preferX == 0.0f) ? (float)side : preferX * (float)side);
                            const float uy = (up == 0) ? 0.0f : ((preferY == 0.0f) ? -(float)up : preferY * (float)up);
                            if (tryPlace(wrappedPosX + sx, wrappedPosY + uy)) break;
                            if (side > 0 && tryPlace(wrappedPosX - sx, wrappedPosY + uy)) break;
                            if (up > 0 && tryPlace(wrappedPosX + sx, wrappedPosY - uy)) break;
                        }
                    }
                    if (!foundSafe) {
                        player.x = beforeWrapX;
                        player.y = beforeWrapY;
                        if (wrappedX) player.vx = 0.0f;
                        if (wrappedY) player.vy = 0.0f;
                    } else if (std::fabs(player.y - wrappedPosY) > 0.01f) {
                        player.onGround = false;
                    }
                }
                if (std::fabs(player.x - beforeWrapX) > 0.01f || std::fabs(player.y - beforeWrapY) > 0.01f) {
                    addMovementReason(MR_WORLD_WRAP);
                }
            }
            if (upd == PlayerUpdateResult::Reloaded) {
                if (levelLoadDeathGraceTimer > 0.0f) {
                    continue;
                }
                const float pitResetY = (float)((map.h + 7) * map.tileSize);
                const bool bottomlessPit = (player.y >= pitResetY);
                if (bottomlessPit) {
                    // Bottomless pit routing, based on world/area/time variant logic.
                    const char tw = levelManager.timeWarpId();
                    const int world = levelManager.worldId();
                    const int area = levelManager.levelPartId();
                    const int ot = levelManager.timeId();
                    if (tw != 'F' && world > 0 && area > 0) {
                        // In normal world ('N'), pit falls should trigger death flow.
                        if (tw != 'N') {
                            int targetTime = 2;
                            if (ot == 1) {
                                if (tw == '2') {
                                    targetTime = 3;
