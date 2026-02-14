                MR_FAST_TRAVEL = 1u << 0,
                MR_NORMAL_MOVEMENT = 1u << 1,
                MR_WORLD_WRAP = 1u << 2,
                MR_SPRING = 1u << 3,
                MR_BUMPER = 1u << 4,
            };
            unsigned int movementReasons = 0u;
            auto addMovementReason = [&](unsigned int reasonBit) {
                movementReasons |= reasonBit;
            };
            const float mapWrapW = (float)(map.w * map.tileSize);
            const float mapWrapH = (float)(map.h * map.tileSize);
            auto wrapCoordNear = [&](float value, float size, float anchor, float wrapSize, bool wrapEnabled) -> float {
                if (!wrapEnabled || wrapSize <= 0.0f) return value;
                float wrapped = value;
                const float centerOffset = size * 0.5f;
                while ((wrapped + centerOffset - anchor) < -wrapSize * 0.5f) wrapped += wrapSize;
                while ((wrapped + centerOffset - anchor) > wrapSize * 0.5f) wrapped -= wrapSize;
                return wrapped;
            };
            auto overlapPlayerWithWrappedRect = [&](float rx, float ry, float rw, float rh, float& outRx, float& outRy) -> bool {
                const float playerCX = player.x + (float)player.w * 0.5f;
                const float playerCY = player.y + (float)player.h * 0.5f;
                outRx = wrapCoordNear(rx, rw, playerCX, mapWrapW, gameplayWrapX);
                outRy = wrapCoordNear(ry, rh, playerCY, mapWrapH, gameplayWrapY);
                const float px1 = player.x;
                const float px2 = player.x + (float)player.w;
                const float py1 = player.y;
                const float py2 = player.y + (float)player.h;
                return (px2 > outRx) && (px1 < outRx + rw) && (py2 > outRy) && (py1 < outRy + rh);
            };
            bool fastTravelReload = false;
            bool fastTravelTriggered = false;
            auto updateEndSignState = [&]() {
                if (!endSignState.present || levelCompleteActive) return;
                const float kFrameTime = 0.0166667f;
                if (endSignState.phase == EndSignPhase::Idle) return;
                const float animStepDt = std::clamp(dt, 0.0f, 0.05f);
                endSignState.frameTimer += animStepDt;
                while (endSignState.frameTimer >= kFrameTime) {
                    endSignState.frameTimer -= kFrameTime;
                    if (endSignState.phase == EndSignPhase::SignForward) {
                        if (endSignState.signFrame < 15) {
                            endSignState.signFrame++;
                        } else {
                            endSignState.phase = EndSignPhase::SignBackward;
                        }
                    } else if (endSignState.phase == EndSignPhase::SignBackward) {
                        if (endSignState.signFrame > 9) {
                            endSignState.signFrame--;
                        } else {
                            endSignState.signLoopCount++;
                            if (endSignState.signLoopCount >= 3) {
                                endSignState.phase = EndSignPhase::PlayerForward;
                                endSignState.lockPlayerRight = true;
                            } else {
                                endSignState.phase = EndSignPhase::SignForward;
                            }
                        }
                    } else if (endSignState.phase == EndSignPhase::PlayerForward) {
                        if (endSignState.playerFrame < 7) {
                            endSignState.playerFrame++;
                        } else {
                            endSignState.phase = EndSignPhase::PlayerBackward;
                        }
                    } else if (endSignState.phase == EndSignPhase::PlayerBackward) {
                        if (endSignState.playerFrame > 1) {
                            endSignState.playerFrame--;
                        } else {
                            endSignState.playerLoopCount++;
                            if (endSignState.playerLoopCount >= 3) {
                                endSignState.phase = EndSignPhase::TriggerComplete;
                            } else {
                                endSignState.phase = EndSignPhase::PlayerForward;
                            }
                        }
                    } else if (endSignState.phase == EndSignPhase::TriggerComplete) {
                        endSignState.phase = EndSignPhase::Done;
                        startLevelCompleteSequence();
                    }
                }
            };
            updateEndSignState();
            auto ejectFromFastTravel = [&](int dirHint) {
                const bool startedInsideSolid = RectHitsSolid(map, player.x, player.y, player.w, player.h);
                float ex = 0.0f;
                if (dirHint == FT_LEFT) ex = -1.0f;
                else if (dirHint == FT_RIGHT) ex = 1.0f;
                const float horizontalRange = 28.0f;
                const float upwardRange = startedInsideSolid ? 96.0f : 30.0f;
                const float stepSize = 2.0f;
                const float startX = player.x;
                const float startY = player.y;

                auto sweepMoveNoClip = [&](float dx, float dy) {
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const int steps = std::max(1, (int)std::ceil(dist / stepSize));
                    const float sx = dx / (float)steps;
                    const float sy = dy / (float)steps;
                    for (int i = 0; i < steps; ++i) {
                        const float nx = player.x + sx;
                        const float ny = player.y + sy;
                        if (RectHitsSolid(map, nx, ny, player.w, player.h)) break;
                        player.x = nx;
                        player.y = ny;
                    }
                };

                if (startedInsideSolid) {
                    bool foundEscape = false;
                    for (int up = 4; up <= (int)upwardRange && !foundEscape; up += 4) {
                        for (int side = 0; side <= (int)horizontalRange; side += 4) {
                            const float nxA = startX + (ex * side);
                            const float nyA = startY - (float)up;
                            if (!RectHitsSolid(map, nxA, nyA, player.w, player.h)) {
                                player.x = nxA;
                                player.y = nyA;
                                foundEscape = true;
                                break;
                            }
                            if (side == 0) continue;
                            const float nxB = startX - (ex * side);
                            const float nyB = startY - (float)up;
                            if (!RectHitsSolid(map, nxB, nyB, player.w, player.h)) {
                                player.x = nxB;
                                player.y = nyB;
                                foundEscape = true;
                                break;
                            }
                        }
                    }
                    if (!foundEscape) {
                        player.x = startX;
                        player.y = startY;
                    }
                } else {
                    // Keep ejection collision-safe when already in free space.
                    sweepMoveNoClip(ex * horizontalRange, 0.0f);
                    sweepMoveNoClip(0.0f, -upwardRange);
                }

                const float ejectSpeed = 620.0f;
                const float verticalDir = startedInsideSolid ? -1.0f : -0.55f;
                player.vx = ex * ejectSpeed;
                player.vy = verticalDir * ejectSpeed;
                player.onGround = false;
            };
            if (timeTravelTriggerCooldown <= 0.0f) {
                int overlapDir = -1;
                for (const auto& obj : objects) {
                    int objId = 0;
                    try { objId = std::stoi(obj.id); } catch (...) { continue; }
                    if (objId < 57 || objId > 61) continue;
                    if (fastTravelCooldown > 0.0f) break;

                    const float ox = obj.x - 16.0f;
                    const float oy = obj.y - 16.0f;
                    const float ow = 32.0f;
                    const float oh = 32.0f;
                    float testOx = ox;
                    float testOy = oy;
                    const bool overlap = overlapPlayerWithWrappedRect(ox, oy, ow, oh, testOx, testOy);
                    if (!overlap) continue;

                    const int dir = fastTravelDirForObjectId(objId);
                    if (dir < 0) continue;
                    if (dir == FT_EXIT && fastTravelActiveDir < 0) continue;
                    const bool isHighestPriority = (objId == 61);
                    if (isHighestPriority) {
                        temp1TouchedThisFrame = true;
                        overlapDir = dir;
                        break;
                    }
                    if (overlapDir < 0) {
                        overlapDir = dir;
                    }
                }
                if (overlapDir >= 0) {
                    // No delay; velocity blending provides smooth transitions.
                    setFastTravelActiveDir(overlapDir, "set_overlap");
                }
                fastTravelOverlapWasActive = (overlapDir >= 0);
            }
            if (fastTravelActiveDir >= 0) {
                const float eps = 0.01f;
                const float mapW = (float)(map.w * map.tileSize);
                const float mapH = (float)(map.h * map.tileSize);
                const bool insideSolid = RectHitsSolid(map, player.x, player.y, player.w, player.h);
                const float oldX = player.x;
                const float oldY = player.y;
                bool positionChanged = false;
                const float fastTravelSpeed = 900.0f;
                const float fastTravelBlendSpeed = 21.0f;
                float targetVx = 0.0f;
                float targetVy = 0.0f;

                if (fastTravelActiveDir == FT_UP) {
                    targetVy = -fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_DOWN) {
                    targetVy = fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_LEFT) {
                    targetVx = -fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_RIGHT) {
                    targetVx = fastTravelSpeed;
                } else if (fastTravelActiveDir == FT_EXIT && fastTravelCooldown <= 0.0f) {
                    // Keep fast travel active while embedded in solids.
                    if (!insideSolid) {
                        const int previousDir = fastTravelActiveDir;
                        ejectFromFastTravel(previousDir);
                        setFastTravelActiveDir(-1, "exit_mode");
                        fastTravelOverlapWasActive = false;
                        fastTravelBlendVx = 0.0f;
                        fastTravelBlendVy = 0.0f;
                        fastTravelCooldown = 0.2f;
                    }
                }

                // Frame-rate independent smoothing for consistent response.
                const float blendT = 1.0f - std::exp(-fastTravelBlendSpeed * std::max(0.0f, dt));
                fastTravelBlendVx += (targetVx - fastTravelBlendVx) * blendT;
                fastTravelBlendVy += (targetVy - fastTravelBlendVy) * blendT;

                if (fastTravelActiveDir != FT_EXIT) {
                    player.x += fastTravelBlendVx * dt;
                    player.y += fastTravelBlendVy * dt;
                    while (player.y + (float)player.h < 0.0f) player.y += mapH;
                    while (player.y >= mapH) player.y -= mapH;
                    while (player.x + (float)player.w < 0.0f) player.x += mapW;
                    while (player.x >= mapW) player.x -= mapW;
                }

                positionChanged = (std::fabs(player.x - oldX) > eps) || (std::fabs(player.y - oldY) > eps);
                if (positionChanged) addMovementReason(MR_FAST_TRAVEL);
                player.vx = fastTravelBlendVx;
                player.vy = fastTravelBlendVy;
                player.onGround = false;
                if (positionChanged || fastTravelReload) {
                    fastTravelTriggered = true;
                }
                if (positionChanged && showDebugView) {
                    SDL_Log("fastTravel move: dir=%d from=(%.2f, %.2f) to=(%.2f, %.2f) delta=(%.2f, %.2f)",
                            fastTravelActiveDir,
                            oldX, oldY,
                            player.x, player.y,
                            player.x - oldX, player.y - oldY);
                }
            }
            if (temp1TouchedThisFrame && fastTravelActiveDir >= 0 &&
                !RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                const int previousDir = fastTravelActiveDir;
                ejectFromFastTravel(previousDir);
                setFastTravelActiveDir(-1, "temp1_disable");
                fastTravelOverlapWasActive = false;
                fastTravelBlendVx = 0.0f;
                fastTravelBlendVy = 0.0f;
                fastTravelCooldown = std::max(fastTravelCooldown, 0.2f);
            }
            if (fastTravelReload) {
                droppedCoins.clear();
                reloadLevel();
                continue;
            }

            float touchMove = 0.0f;
            if (touchLeft) touchMove -= 1.0f;
            if (touchRight) touchMove += 1.0f;
            const bool demoControlsActive = demoState.enabled && !paused && !deathSequenceActive && !levelCompleteActive;
            bool demoDown = false;
            bool demoJump = false;
            if (demoControlsActive) {
                demoState.jumpCooldown = std::max(0.0f, demoState.jumpCooldown - dt);
                demoState.jumpHoldTimer = std::max(0.0f, demoState.jumpHoldTimer - dt);
                demoState.repathTimer = std::max(0.0f, demoState.repathTimer - dt);
                const float mapWidthPx = (float)(map.w * map.tileSize);
                const float edgePad = (float)map.tileSize * 1.5f;
                const int t = map.tileSize;
                const int playerTilesH = std::max(1, (player.h + t - 1) / t);
                if (player.x <= edgePad) demoState.dir = 1.0f;
                if (player.x + player.w >= mapWidthPx - edgePad) demoState.dir = -1.0f;

                auto passableTile = [&](int tx, int ty) -> bool {
                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return false;
                    return !map.getSolid(tx, ty) && !map.getSemiSolid(tx, ty);
                };
                auto standableTile = [&](int tx, int footTy) -> bool {
                    if (tx < 0 || tx >= map.w) return false;
                    if (footTy + 1 < 0 || footTy + 1 >= map.h) return false;
                    const int headTy = footTy - (playerTilesH - 1);
                    if (headTy < 0) return false;
                    for (int ty = headTy; ty <= footTy; ++ty) {
                        if (!passableTile(tx, ty)) return false;
                    }
                    return map.getSolid(tx, footTy + 1) || map.getSemiSolid(tx, footTy + 1);
                };
                auto findNearestStandable = [&](int cx, int cy, int radius) -> SDL_Point {
                    SDL_Point best{-1, -1};
                    int bestDist = 1e9;
                    for (int r = 0; r <= radius; ++r) {
                        for (int dy = -r; dy <= r; ++dy) {
                            for (int dx = -r; dx <= r; ++dx) {
                                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                                const int tx = cx + dx;
                                const int ty = cy + dy;
                                if (!standableTile(tx, ty)) continue;
                                const int dist = std::abs(dx) + std::abs(dy);
                                if (dist < bestDist) {
                                    bestDist = dist;
                                    best = SDL_Point{tx, ty};
                                }
                            }
                        }
                        if (best.x >= 0) break;
                    }
                    return best;
                };
                auto rebuildDemoPath = [&]() {
                    SDL_Log("demo.path: rebuild start");
                    if (!demoState.startTileSet) {
                        const int startTx = (int)std::floor((player.x + player.w * 0.5f) / (float)t);
                        const int startTy = (int)std::floor((player.y + player.h - 1.0f) / (float)t);
                        SDL_Point seeded = findNearestStandable(startTx, startTy, 24);
                        if (seeded.x >= 0) {
                            demoState.startTile = seeded;
                            demoState.startTileSet = true;
                        }
                    }
                    SDL_Point start = demoState.startTileSet ? demoState.startTile : SDL_Point{-1, -1};
                    if (start.x < 0 || !standableTile(start.x, start.y)) {
                        const int startTx = (int)std::floor((player.x + player.w * 0.5f) / (float)t);
                        const int startTy = (int)std::floor((player.y + player.h - 1.0f) / (float)t);
                        start = findNearestStandable(startTx, startTy, 24);
                        if (start.x >= 0) {
                            demoState.startTile = start;
                            demoState.startTileSet = true;
                        }
                    }
                    if (start.x < 0) {
                        SDL_Log("demo.path: rebuild failed (no valid start)");
                        demoState.pathTiles.clear();
                        demoState.waypointIndex = 0;
                        return;
                    }

                    SDL_Point target{-1, -1};
                    int bestTargetX = std::numeric_limits<int>::min();
                    for (const auto& obj : objects) {
                        if (obj.id != "67") continue;
                        const int ex = (int)std::floor(obj.x / (float)t);
                        const int ey = (int)std::floor((obj.y + 12.0f) / (float)t);
                        SDL_Point candidate = findNearestStandable(ex, ey, 10);
                        if (candidate.x < 0) continue;
                        if (candidate.x > bestTargetX) {
                            bestTargetX = candidate.x;
                            target = candidate;
                        }
                    }
                    if (target.x < 0) {
                        for (int x = map.w - 2; x >= 1 && target.x < 0; --x) {
                            for (int y = 1; y < map.h - 2; ++y) {
                                if (standableTile(x, y)) {
                                    target = SDL_Point{x, y};
                                    break;
                                }
                            }
                        }
                    }
                    if (target.x < 0) {
                        SDL_Log("demo.path: rebuild failed (no valid target)");
                        demoState.pathTiles.clear();
                        demoState.waypointIndex = 0;
                        return;
                    }
                    SDL_Log("demo.path: start=(%d,%d) target=(%d,%d)", start.x, start.y, target.x, target.y);
                    auto indexOf = [&](int tx, int ty) { return ty * map.w + tx; };
                    // Hazard-aware costs: steer the route away from bumpers/time-warp objects.
                    std::vector<int> tileRisk((size_t)map.w * (size_t)map.h, 0);
                    for (const auto& obj : objects) {
                        int objId = 0;
                        try { objId = std::stoi(obj.id); } catch (...) { continue; }
                        int basePenalty = 0;
                        int radiusX = 0;
                        int radiusY = 0;
                        if (objId == 46) { // bumper
                            basePenalty = 26;
                            radiusX = 2;
                            radiusY = 1;
                        } else if (objId >= 57 && objId <= 61) { // fast-travel trigger
                            basePenalty = 16;
                            radiusX = 2;
                            radiusY = 2;
                        } else {
                            continue;
                        }
                        const int ox = (int)std::floor(obj.x / (float)t);
                        const int oy = (int)std::floor(obj.y / (float)t);
                        for (int dy = -radiusY; dy <= radiusY; ++dy) {
                            for (int dx = -radiusX; dx <= radiusX; ++dx) {
                                const int tx = ox + dx;
                                const int ty = oy + dy;
                                if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) continue;
                                const int manhattan = std::abs(dx) + std::abs(dy);
                                const int risk = std::max(1, basePenalty - manhattan * 6);
                                tileRisk[indexOf(tx, ty)] = std::max(tileRisk[indexOf(tx, ty)], risk);
                            }
                        }
                    }

                    struct Node {
                        int g = std::numeric_limits<int>::max();
                        int f = std::numeric_limits<int>::max();
                        int parent = -1;
                        bool closed = false;
                    };
                    int foundIdx = -1;
                    int expanded = 0;
                    int guard = 0;
                    int usedAttempt = -1;
                    const int attemptCount = 3;
                    for (int attempt = 0; attempt < attemptCount && foundIdx < 0; ++attempt) {
                        const int heuristicXWeight = (attempt == 0) ? 3 : ((attempt == 1) ? 2 : 1);
                        const int forwardCost = (attempt == 0) ? 6 : ((attempt == 1) ? 8 : 10);
                        const int backCost = (attempt == 0) ? 14 : ((attempt == 1) ? 11 : 9);
                        const int downMax = (attempt == 0) ? 3 : ((attempt == 1) ? 4 : 5);
                        const int jumpMax = (attempt == 0) ? 3 : ((attempt == 1) ? 4 : 5);
                        const int jumpForwardBase = (attempt == 0) ? 20 : ((attempt == 1) ? 17 : 14);
                        const int jumpBackBase = (attempt == 0) ? 36 : ((attempt == 1) ? 26 : 20);
                        const int guardLimit = (attempt == 0) ? 30000 : 60000;
                        const float runMul = (attempt == 0) ? 0.78f : ((attempt == 1) ? 0.92f : 1.0f);
                        SDL_Log("demo.path: attempt %d (hX=%d jumpMax=%d downMax=%d)", attempt, heuristicXWeight, jumpMax, downMax);

                        const int total = map.w * map.h;
                        std::vector<Node> nodes(total);
                        std::vector<int> open;
                        open.reserve(4096);
                        auto heuristic = [&](int tx, int ty) {
                            return std::abs(tx - target.x) * heuristicXWeight + std::abs(ty - target.y);
                        };
                        auto simulateJumpReachable = [&](int fromX, int fromY, int toX, int toY) -> bool {
                        const float tile = (float)t;
                        const float startFootX = (fromX + 0.5f) * tile;
                        const float startFootY = (fromY + 1.0f) * tile;
                        const float targetFootX = (toX + 0.5f) * tile;
                        const float targetFootY = (toY + 1.0f) * tile;
                        const float dir = (targetFootX >= startFootX) ? 1.0f : -1.0f;
                        const float runSpeed = std::max(220.0f, movementCfg.maxSpeedGround * runMul);
                        float px = startFootX - (float)player.w * 0.5f;
                        float py = startFootY - (float)player.h;
                        float vx = dir * runSpeed;
                        float vy = -movementCfg.jumpSpeed;
                        const float g = movementCfg.gravityGround;
                        const float dtSim = 1.0f / 90.0f;
                        const float maxSimT = 1.35f;

                        auto rectHitsSolidOnly = [&](float x, float y, int w, int h) -> bool {
                            int left = (int)std::floor(x / tile);
                            int right = (int)std::floor((x + w - 1) / tile);
                            int top = (int)std::floor(y / tile);
                            int bottom = (int)std::floor((y + h - 1) / tile);
                            for (int ty = top; ty <= bottom; ++ty) {
                                for (int tx = left; tx <= right; ++tx) {
                                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return true;
                                    if (map.getSolid(tx, ty)) return true;
                                }
                            }
                            return false;
                        };

                        for (float simT = 0.0f; simT <= maxSimT; simT += dtSim) {
                            float nx = px + vx * dtSim;
                            float ny = py + vy * dtSim;
                            vy += g * dtSim;

                            if (!rectHitsSolidOnly(nx, py, player.w, player.h)) px = nx;
                            else vx = 0.0f;
                            if (!rectHitsSolidOnly(px, ny, player.w, player.h)) py = ny;
                            else {
                                if (vy > 0.0f) {
                                    const float footY = py + (float)player.h;
                                    const float dX = (px + player.w * 0.5f) - targetFootX;
                                    const float dY = footY - targetFootY;
                                    if (std::fabs(dX) <= tile * 0.70f && std::fabs(dY) <= tile * 0.60f &&
                                        standableTile(toX, toY)) {
                                        return true;
                                    }
                                }
                                break;
                            }

                            if (vy > 0.0f) {
                                const float footY = py + (float)player.h;
                                const float dX = (px + player.w * 0.5f) - targetFootX;
                                const float dY = footY - targetFootY;
                                if (std::fabs(dX) <= tile * 0.65f && std::fabs(dY) <= tile * 0.55f &&
                                    standableTile(toX, toY)) {
                                    return true;
                                }
                            }
                        }
                        return false;
                        };
