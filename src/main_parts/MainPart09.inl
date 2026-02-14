                                } else {
                                    targetTime = 2;
                                }
                            } else if (ot == 2) {
                                targetTime = 3;
                            } else {
                                targetTime = 2;
                            }
                            std::string pitPath = levelManager.levelPathByCode(world * 100 + area * 10 + targetTime);
                            if (!pitPath.empty()) {
                                    levelManager.setLevelPath(pitPath);
                                    droppedCoins.clear();
                                    reloadLevel();
                                    continue;
                            }
                        }
                    }
                }
                deathSequenceActive = true;
                deathLifeDeducted = false;
                deathTimer = 0.0f;
                audio.haltAllChannels();
                audio.haltMusic();
                audio.playLoseSfx();
                continue;
            }

            if (upd == PlayerUpdateResult::RenderOnly) {
                updatePlayerAnimState(inputMove, inputDown, dt);
                goto RENDER_ONLY;
            }

            if (!bossState.active && bossState.activationCooldown <= 0.0f &&
                playerTouchesTileId(map, player, 68, 68, gameplayWrapX, gameplayWrapY)) {
                bossState.active = true;
                removeTimewarpObjectsAndExit();
            }

            if (bossState.active && bossState.activationCooldown <= 0.0f && !levelCompleteActive) {
                auto applyBossContactDamageToPlayer = [&]() -> bool {
                    const bool hasCoins = levelManager.coinCount() > 0;
                    if (hasCoins) {
                        dropPlayerCoins(player.x + player.w * 0.5f, player.y + player.h * 0.5f);
                        playerInvincibleTimer = kPlayerInvincibleDuration;
                        player.anim = ANIM_HURT;
                        player.animTime = 0.0f;
                        const float playerCenterX = player.x + player.w * 0.5f;
                        const float pushDir = (playerCenterX < bossState.x) ? -1.0f : 1.0f;
                        player.vx = pushDir * 520.0f;
                        player.vy = -760.0f;
                        player.onGround = false;
                        audio.playLoseSfx();
                        return false;
                    }
                    deathSequenceActive = true;
                    deathLifeDeducted = false;
                    deathTimer = 0.0f;
                    audio.haltAllChannels();
                    audio.haltMusic();
                    audio.playLoseSfx();
                    return true;
                };

                const bool bossUsesFinalAnimation = (bossState.sourceWorld == 7);
                const float bossBaseSize = bossUsesFinalAnimation ? 56.0f : 28.0f; // normal-animation bosses are 50% smaller
                const float bossW = bossBaseSize;
                const float bossH = bossBaseSize;
                const float halfW = bossW * 0.5f;
                const float halfH = bossH * 0.5f;
                float arenaLeft = (float)map.tileSize * 2.0f;
                float arenaTop = (float)map.tileSize * 2.0f;
                float arenaRight = (float)(map.w * map.tileSize) - (float)map.tileSize * 2.0f;
                float arenaBottom = (float)(map.h * map.tileSize) - (float)map.tileSize * 2.0f;
                if (bossState.sourceWorld == 3) {
                    const float playerCenterX = player.x + player.w * 0.5f;
                    const float playerCenterY = player.y + player.h * 0.5f;
                    if (bossState.phase == 0) {
                        if (!bossState.replayPath.empty()) {
                            bossState.x = bossState.replayPath[0].x;
                            bossState.y = bossState.replayPath[0].y;
                        }
                        const float dx = playerCenterX - bossState.x;
                        const float dy = playerCenterY - bossState.y;
                        if (dx * dx + dy * dy <= 260.0f * 260.0f) {
                            bossState.phase = 1;
                            bossState.replayIndex = 0;
                            bossState.replayFrameAcc = 0.0f;
                            SDL_Log("boss.w3: phase 0 -> 1 (start replay)");
                        }
                    } else if (bossState.phase == 1) {
                        if (!bossState.replayPath.empty()) {
                            bossState.replayFrameAcc += dt * 24.0f;
                            while (bossState.replayFrameAcc >= 1.0f && bossState.replayIndex + 1 < bossState.replayPath.size()) {
                                bossState.replayFrameAcc -= 1.0f;
                                bossState.replayIndex++;
                            }
                            const SDL_FPoint& p = bossState.replayPath[std::min(bossState.replayIndex, bossState.replayPath.size() - 1)];
                            bossState.x = p.x;
                            bossState.y = p.y;
                            if (bossState.replayIndex + 1 >= bossState.replayPath.size()) {
                                bossState.phase = 2;
                                SDL_Log("boss.w3: phase 1 -> 2 (replay done)");
                            }
                        } else {
                            bossState.phase = 2;
                        }
                    } else if (bossState.phase == 2) {
                        const float dx = playerCenterX - bossState.x;
                        const float dy = playerCenterY - bossState.y;
                        if (dx * dx + dy * dy <= 300.0f * 300.0f) {
                            bossState.phase = 3;
                            bossState.health = bossState.maxHealth;
                            bossState.vx = 280.0f;
                            bossState.vy = 220.0f;
                            removeTimewarpObjectsAndExit();
                            SDL_Log("boss.w3: phase 2 -> 3 (fight active)");
                        }
                    }
                }
                const bool hasBossCameraLock =
                    (bossState.sourceWorld == 1 || bossState.sourceWorld == 2) ||
                    (bossState.sourceWorld == 4) ||
                    (bossState.sourceWorld == 5) ||
                    (bossState.sourceWorld == 7) ||
                    (bossState.sourceWorld == 3 && bossState.phase == 3);
                if (hasBossCameraLock) {
                    float lockCx = 1170.0f;
                    float lockCy = 132.0f;
                    if (bossState.sourceWorld == 2) {
                        lockCx = 1887.0f;
                        lockCy = 744.0f;
                    } else if (bossState.sourceWorld == 4) {
                        lockCx = 1315.0f + kGameplayViewW * 0.5f;
                        lockCy = 202.0f + kGameplayViewH * 0.5f;
                    } else if (bossState.sourceWorld == 5) {
                        lockCx = 5728.0f + kGameplayViewW * 0.5f;
                        lockCy = 2832.0f + kGameplayViewH * 0.5f;
                    } else if (bossState.sourceWorld == 7) {
                        lockCx = 96.0f + kGameplayViewW * 0.5f;
                        lockCy = 32.0f + kGameplayViewH * 0.5f;
                    } else if (bossState.sourceWorld == 3) {
                        lockCx = 2528.0f + kGameplayViewW * 0.5f;
                        lockCy = 976.0f + kGameplayViewH * 0.5f;
                    }
                    const float lockLeft = lockCx - kGameplayViewW * 0.5f + halfW + 8.0f;
                    const float lockRight = lockCx + kGameplayViewW * 0.5f - halfW - 8.0f;
                    const float lockTop = lockCy - kGameplayViewH * 0.5f + halfH + 8.0f;
                    const float lockBottom = lockCy + kGameplayViewH * 0.5f - halfH - 8.0f;
                    arenaLeft = std::max(arenaLeft, lockLeft);
                    arenaRight = std::min(arenaRight, lockRight);
                    arenaTop = std::max(arenaTop, lockTop);
                    arenaBottom = std::min(arenaBottom, lockBottom);
                }
                if (arenaLeft > arenaRight) std::swap(arenaLeft, arenaRight);
                if (arenaTop > arenaBottom) std::swap(arenaTop, arenaBottom);
                const bool bossCanMoveAndTakeDamage = !(bossState.sourceWorld == 3 && bossState.phase != 3);
                const bool world4RainbowActive = (bossState.sourceWorld == 4 && bossState.rainbowTimer > 0.0f);
                if (bossCanMoveAndTakeDamage && !world4RainbowActive) {
                    if (bossState.sourceWorld == 7) {
                        bossState.vy += bossGravity * dt;
                        bossState.y += bossState.vy * dt;
                        if (bossState.y + halfH < arenaTop) {
                            const float minX = arenaLeft + halfW;
                            const float maxX = std::max(minX, arenaRight - halfW);
                            const float rx = (float)std::rand() / (float)RAND_MAX;
                            bossState.x = minX + (maxX - minX) * rx;
                            bossState.y = arenaBottom + halfH + 24.0f;
                            bossState.vy = -std::fabs(bossState.vy);
                        }
                    } else {
                        bossState.vy += bossGravity * dt;
                        bossState.x += bossState.vx * dt;
                        bossState.y += bossState.vy * dt;
                        if (bossState.x - halfW < arenaLeft) {
                            bossState.x = arenaLeft + halfW;
                            bossState.vx = std::fabs(bossState.vx);
                        }
                        if (bossState.x + halfW > arenaRight) {
                            bossState.x = arenaRight - halfW;
                            bossState.vx = -std::fabs(bossState.vx);
                        }
                        if (bossState.y - halfH < arenaTop) {
                            bossState.y = arenaTop + halfH;
                            bossState.vy = std::fabs(bossState.vy);
                        }
                        if (bossState.y + halfH > arenaBottom) {
                            bossState.y = arenaBottom - halfH;
                            bossState.vy = -std::fabs(bossState.vy);
                        }
                    }
                }

                const float bx = bossState.x - halfW;
                const float by = bossState.y - halfH;
                float testBx = bx;
                float testBy = by;
                const bool overlap = overlapPlayerWithWrappedRect(bx, by, bossW, bossH, testBx, testBy);
                if (overlap && playerInvincibleTimer <= 0.0f && levelLoadDeathGraceTimer <= 0.0f) {
                    if (!bossCanMoveAndTakeDamage) {
                        if (applyBossContactDamageToPlayer()) continue;
                    } else {
                        const float py2 = player.y + (float)player.h;
                        const bool stomp = (player.vy > 80.0f) && (py2 <= testBy + bossH * 0.55f);
                        if (stomp) {
                            player.y = testBy - (float)player.h;
                            player.vy = -1000.0f;
                            player.onGround = false;
                            playerInvincibleTimer = 0.12f;
                            bossState.health = std::max(0, bossState.health - 1);
                            bossState.hurtFlash = 0.18f;
                            if (bossState.sourceWorld == 4) {
                                bossState.rainbowTimer = 3.0f;
                                const float pad = 20.0f;
                                const float minX = std::min(arenaLeft + halfW + pad, arenaRight - halfW - pad);
                                const float maxX = std::max(arenaLeft + halfW + pad, arenaRight - halfW - pad);
                                const float minY = std::min(arenaTop + halfH + pad, arenaBottom - halfH - pad);
                                const float maxY = std::max(arenaTop + halfH + pad, arenaBottom - halfH - pad);
                                const float rx = (float)std::rand() / (float)RAND_MAX;
                                const float ry = (float)std::rand() / (float)RAND_MAX;
                                bossState.x = minX + (maxX - minX) * rx;
                                bossState.y = minY + (maxY - minY) * ry;
                            } else if (bossState.sourceWorld == 7) {
                                const float minX = arenaLeft + halfW;
                                const float maxX = std::max(minX, arenaRight - halfW);
                                const float rx = (float)std::rand() / (float)RAND_MAX;
                                bossState.x = minX + (maxX - minX) * rx;
                                bossState.y = arenaBottom + halfH + 24.0f;
                                bossState.vy = -std::fabs(bossState.vy);
                            }
                            audio.playBumperSfx();
                            if (bossState.health <= 0) {
                                bossState.active = false;
                                if (bossState.world == 1) {
                                    clampCamX = false;
                                }
                                startLevelCompleteSequence();
                            }
                        } else {
                            if (applyBossContactDamageToPlayer()) continue;
                        }
                    }
                }
            }

            if (!paused && !deathSequenceActive && !levelCompleteActive && player.onGround) {
                const float footX = player.x + player.w * 0.5f;
                const float footY = player.y + (float)player.h + 1.0f;
                int tx = (int)std::floor(footX / (float)map.tileSize);
                int ty = (int)std::floor(footY / (float)map.tileSize);
                if (gameplayWrapX && map.w > 0) {
                    tx %= map.w;
                    if (tx < 0) tx += map.w;
                }
                if (gameplayWrapY && map.h > 0) {
                    ty %= map.h;
                    if (ty < 0) ty += map.h;
                }
                if (tx >= 0 && tx < map.w && ty >= 0 && ty < map.h) {
                    const int idx = ty * map.w + tx;
                    const int standingTileId = (int)map.tileIds[idx];
                    if (standingTileId == 20) {
                        levelManager.setTileAt(map, idx, 21);
                        levelManager.addCoins(5);
                        audio.playCoinSfx();
                    } else if (standingTileId == 13 && playerInvincibleTimer <= 0.0f && levelLoadDeathGraceTimer <= 0.0f) {
                        const bool hasCoins = levelManager.coinCount() > 0;
                        if (hasCoins) {
                            dropPlayerCoins(player.x + player.w * 0.5f, player.y + player.h * 0.5f);
                            playerInvincibleTimer = kPlayerInvincibleDuration;
                            player.anim = ANIM_HURT;
                            player.animTime = 0.0f;
                            player.vx = (player.facing < 0) ? 320.0f : -320.0f;
                            player.vy = -760.0f;
                            player.onGround = false;
                            audio.playLoseSfx();
                        } else {
                            deathSequenceActive = true;
                            deathLifeDeducted = false;
                            deathTimer = 0.0f;
                            audio.haltAllChannels();
                            audio.haltMusic();
                            audio.playLoseSfx();
                            continue;
                        }
                    }
                }
            }

            int collectedNow = levelManager.collectCoinsAtPlayer(map, player, gameplayWrapX, gameplayWrapY);
            if (collectedNow > 0) {
                audio.playCoinSfx();
            }
            if (timeTravelTriggerCooldown <= 0.0f) {
                levelManager.updateTimeWarpIdAtPlayer(map, player, gameplayWrapX, gameplayWrapY);
            }

            // Spring objects (id 31): bounce player upward on top contact.
            for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
                const auto& obj = objects[objIdx];
                if (obj.id != "31") continue;
                const float sx = obj.x - 16.0f;
                const float sy = obj.y - 16.0f;
                const float sw = 32.0f;
                const float sh = 32.0f;
                float testSx = sx;
                float testSy = sy;
                if (!overlapPlayerWithWrappedRect(sx, sy, sw, sh, testSx, testSy)) continue;
                const float px1 = player.x;
                const float px2 = player.x + (float)player.w;
                const float py2 = player.y + (float)player.h;
                const bool xOverlap = (px2 > testSx) && (px1 < testSx + sw);
                const bool nearTop = (py2 >= testSy) && (py2 <= testSy + sh * 0.75f);
                if (xOverlap && nearTop && player.vy >= 0.0f) {
                    player.y = testSy - (float)player.h;
                    player.vy = -1800.0f;
                    player.onGround = false;
                    addMovementReason(MR_SPRING);
                    break;
                }
            }
            // Bumper objects (id 46): vertical bounce, up/down depending approach.
            auto applyBumperBounce = [&](float targetY, float launchVy) -> bool {
                const float oldY = player.y;
                player.y = targetY;

                // Keep player out of solids near bumpers placed against floor/ceiling.
                const float nudgeDir = (launchVy < 0.0f) ? 1.0f : -1.0f;
                int nudgeSteps = 0;
                while (RectHitsSolid(map, player.x, player.y, player.w, player.h) && nudgeSteps < 64) {
                    player.y += nudgeDir;
                    ++nudgeSteps;
                }
                if (RectHitsSolid(map, player.x, player.y, player.w, player.h)) {
                    player.y = oldY;
                    return false;
                }

                // If launch direction is immediately blocked, don't force into ceiling/floor.
                const float probeY = player.y + ((launchVy < 0.0f) ? -2.0f : 2.0f);
                if (RectHitsSolid(map, player.x, probeY, player.w, player.h)) {
                    player.vy = 0.0f;
                    player.onGround = (launchVy > 0.0f);
                    return false;
                }

                player.vy = launchVy;
                player.onGround = false;
                return true;
            };
            for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
                const auto& obj = objects[objIdx];
                if (obj.id != "46") continue;
                const float bx = obj.x - 16.0f;
                const float by = obj.y - 16.0f;
                const float bw = 32.0f;
                const float bh = 32.0f;
                float testBx = bx;
                float testBy = by;
                const bool overlap = overlapPlayerWithWrappedRect(bx, by, bw, bh, testBx, testBy);
                if (!overlap) continue;

                const float playerCY = player.y + player.h * 0.5f;
                const float bumperCY = testBy + bh * 0.5f;
                if (playerCY <= bumperCY && player.vy >= 0.0f) {
                    if (applyBumperBounce(testBy - (float)player.h, -1200.0f)) {
                        addMovementReason(MR_BUMPER);
                        activeBumperIndices.insert(objIdx);
                        audio.playBumperSfx();
                    }
                    break;
                }
                if (playerCY > bumperCY && player.vy <= 0.0f) {
                    if (applyBumperBounce(testBy + bh, 1200.0f)) {
                        addMovementReason(MR_BUMPER);
                        activeBumperIndices.insert(objIdx);
                        audio.playBumperSfx();
                    }
                    break;
                }
            }
            if (showDebugView && (std::fabs(player.x - frameStartX) > 0.01f || std::fabs(player.y - frameStartY) > 0.01f)) {
                std::string reasonText;
                if (movementReasons & MR_FAST_TRAVEL) reasonText += (reasonText.empty() ? "" : ",") + std::string("fast_travel");
                if (movementReasons & MR_NORMAL_MOVEMENT) reasonText += (reasonText.empty() ? "" : ",") + std::string("normal_movement");
                if (movementReasons & MR_WORLD_WRAP) reasonText += (reasonText.empty() ? "" : ",") + std::string("world_wrap");
                if (movementReasons & MR_SPRING) reasonText += (reasonText.empty() ? "" : ",") + std::string("spring");
                if (movementReasons & MR_BUMPER) reasonText += (reasonText.empty() ? "" : ",") + std::string("bumper");
                if (reasonText.empty()) reasonText = "unknown";
                SDL_Log("player move: from=(%.2f, %.2f) to=(%.2f, %.2f) delta=(%.2f, %.2f) reason=%s",
                        frameStartX, frameStartY,
                        player.x, player.y,
                        player.x - frameStartX, player.y - frameStartY,
                        reasonText.c_str());
            }

            if (!levelCompleteActive && endSignState.present && !endSignState.triggered) {
                const float playerCenterX = player.x + player.w * 0.5f;
                const float prevPlayerCenterX = frameStartX + player.w * 0.5f;
                int triggerIdx = -1;
                float triggerX = 0.0f;
                float triggerY = 0.0f;
                float bestDxNow = 1e30f;
                for (int i = 0; i < (int)objects.size(); ++i) {
                    if (objects[i].id != "67") continue;
                    const float signX = objects[i].x;
                    const float signY = objects[i].y;
                    const float dxNow = signX - playerCenterX;
                    const float aboveDelta = player.y - signY;
                    const bool crossedSignX = prevPlayerCenterX < signX && playerCenterX >= signX;
                    const bool signNotFarLeft = signX >= prevPlayerCenterX - 8.0f;
                    const bool signNotFarAbove = aboveDelta <= 220.0f;
                    const bool signLikelyVisible = dxNow <= (float)kGameplayViewW * 0.95f;
                    if (!crossedSignX || !signNotFarLeft || !signNotFarAbove || !signLikelyVisible) continue;
                    if (dxNow < bestDxNow) {
                        bestDxNow = dxNow;
                        triggerIdx = i;
                        triggerX = signX;
                        triggerY = signY;
                    }
                }
                if (triggerIdx >= 0) {
                    endSignState.objectIndex = triggerIdx;
                    endSignState.objectX = triggerX;
                    endSignState.objectY = triggerY;
                    endSignState.triggered = true;
                    endSignState.phase = EndSignPhase::SignForward;
                    endSignState.frameTimer = 0.0f;
                    endSignCameraLocked = false; // recapture stable X lock at trigger time
                    cameraSmoothingSuppressTimer = std::max(cameraSmoothingSuppressTimer, 0.12f);
                }
            }

            // Fallback for levels without end_sign object.
            if (!levelCompleteActive && !endSignState.present &&
                playerTouchesTileId(map, player, 30, 30, gameplayWrapX, gameplayWrapY)) {
                startLevelCompleteSequence();
            }
            updatePlayerAnimState(inputMove, inputDown, dt);
        }

        RENDER_ONLY:
        {
            const int targetPresentIntervalMs =
                (powerManagementEnabled && mainWindowMinimized) ? 250 :
                (powerManagementEnabled && (!mainWindowFocused || paused)) ? 33 : 16;
            const Uint32 renderNow = SDL_GetTicks();
            if (renderNow < nextPresentTicks) {
                // Skip rendering on this tick so simulation updates can run independently.
                SDL_Delay(1);
                continue;
            }
        }

        const bool renderWrapX = true;
        const bool renderWrapY = true;
        const int renderLevelId = currentLevelId;
        const bool cameraWrapX = (renderLevelId == 39 || renderLevelId == 40);
        bool cameraWrapY = false;
        if (((renderLevelId == 29 &&
            player.x > 1250.0f) || (renderLevelId == 30 &&
            player.x > 1250.0f) || renderLevelId == 39 ||
             renderLevelId == 40 || renderLevelId == 53 || renderLevelId == 54)) {
            cameraWrapY = true;
        }
        if ((renderLevelId == 21 || renderLevelId == 22 || renderLevelId == 23 || renderLevelId == 24) &&
            player.x > 3211.0f && player.x < 4559.0f) {
            cameraWrapY = true;
        }

        const int worldViewW = kGameplayViewW;
        const int worldViewH = kGameplayViewH;
        const bool lockCameraToEndSign =
            endSignState.triggered &&
            endSignState.objectIndex >= 0 &&
            endSignState.phase != EndSignPhase::Done;
        const bool forceBossCamera =
            bossState.active &&
            (bossState.sourceWorld == 1 ||
             bossState.sourceWorld == 2 ||
             bossState.sourceWorld == 4 ||
             bossState.sourceWorld == 5 ||
             bossState.sourceWorld == 7 ||
             (bossState.sourceWorld == 3 && bossState.phase == 3));
        float forcedBossCameraX = 1170.0f;
        float forcedBossCameraY = 132.0f;
        if (bossState.sourceWorld == 2) {
            forcedBossCameraX = 1887.0f;
            forcedBossCameraY = 744.0f;
        } else if (bossState.sourceWorld == 4) {
            forcedBossCameraX = 1315.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 202.0f + kGameplayViewH * 0.5f;
        } else if (bossState.sourceWorld == 5) {
            forcedBossCameraX = 5728.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 2832.0f + kGameplayViewH * 0.5f;
        } else if (bossState.sourceWorld == 7) {
            forcedBossCameraX = 96.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 32.0f + kGameplayViewH * 0.5f;
        } else if (bossState.sourceWorld == 3) {
            forcedBossCameraX = 2528.0f + kGameplayViewW * 0.5f;
            forcedBossCameraY = 976.0f + kGameplayViewH * 0.5f;
