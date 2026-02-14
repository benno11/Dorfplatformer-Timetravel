            frameMsHistoryHead = (frameMsHistoryHead + 1) % (int)frameMsHistory.size();
            if (!paused && !deathSequenceActive) {
                levelTimerSeconds += dt;
                levelReloadTitleTimer = std::max(0.0f, levelReloadTitleTimer - dt);
            }
            if (fastTravelCooldown > 0.0f) {
                fastTravelCooldown = std::max(0.0f, fastTravelCooldown - dt);
            }
            if (timeTravelTriggerCooldown > 0.0f) {
                timeTravelTriggerCooldown = std::max(0.0f, timeTravelTriggerCooldown - dt);
            }
            if (cameraSmoothingSuppressTimer > 0.0f) {
                cameraSmoothingSuppressTimer = std::max(0.0f, cameraSmoothingSuppressTimer - dt);
            }
            if (bossState.hurtFlash > 0.0f) {
                bossState.hurtFlash = std::max(0.0f, bossState.hurtFlash - dt);
            }
            if (bossState.rainbowTimer > 0.0f) {
                bossState.rainbowTimer = std::max(0.0f, bossState.rainbowTimer - dt);
            }
            if (bossState.activationCooldown > 0.0f) {
                bossState.activationCooldown = std::max(0.0f, bossState.activationCooldown - dt);
            }
            if (playerInvincibleTimer > 0.0f) {
                playerInvincibleTimer = std::max(0.0f, playerInvincibleTimer - dt);
            }
            if (levelLoadDeathGraceTimer > 0.0f) {
                levelLoadDeathGraceTimer = std::max(0.0f, levelLoadDeathGraceTimer - dt);
            }
            if (!paused && !deathSequenceActive && !droppedCoins.empty()) {
                auto tileSolidAt = [&](float wx, float wy) -> bool {
                    const int tx = (int)std::floor(wx / map.tileSize);
                    const int ty = (int)std::floor(wy / map.tileSize);
                    if (tx < 0 || ty < 0 || tx >= map.w || ty >= map.h) return true;
                    const int tidx = ty * map.w + tx;
                    return map.solid[tidx] != 0 || map.semisolid[tidx] != 0;
                };
                const float coinR = 8.0f;
                for (auto& c : droppedCoins) {
                    const float prevX = c.x;
                    const float prevY = c.y;
                    c.life -= dt;
                    c.noPickupTimer = std::max(0.0f, c.noPickupTimer - dt);
                    c.vy += 1900.0f * dt;
                    c.vy = std::min(c.vy, 1300.0f);
                    c.x += c.vx * dt;
                    c.y += c.vy * dt;
                    if (tileSolidAt(c.x - coinR, c.y)) {
                        const int gx = (int)std::floor((c.x - coinR) / map.tileSize);
                        c.x = (gx + 1) * map.tileSize + coinR;
                        c.vx = std::fabs(c.vx) * 0.35f;
                    }
                    if (tileSolidAt(c.x + coinR, c.y)) {
                        const int gx = (int)std::floor((c.x + coinR) / map.tileSize);
                        c.x = gx * map.tileSize - coinR;
                        c.vx = -std::fabs(c.vx) * 0.35f;
                    }
                    if (tileSolidAt(c.x, c.y - coinR)) {
                        const int gy = (int)std::floor((c.y - coinR) / map.tileSize);
                        c.y = (gy + 1) * map.tileSize + coinR;
                        c.vy = std::fabs(c.vy) * 0.25f;
                    }
                    if (tileSolidAt(c.x, c.y + coinR)) {
                        int gy = (int)std::floor((c.y + coinR) / map.tileSize);
                        c.y = gy * map.tileSize - coinR;
                        c.vy *= -0.28f;
                        if (std::fabs(c.vy) < 28.0f) c.vy = 0.0f;
                        c.vx *= 0.78f;
                    }
                    // If a coin is still embedded in geometry (e.g. spawned inside a wall),
                    // roll back to the nearest valid previous axis and dampen velocity.
                    if (tileSolidAt(c.x, c.y)) {
                        if (!tileSolidAt(prevX, c.y)) c.x = prevX;
                        else if (!tileSolidAt(c.x, prevY)) c.y = prevY;
                        else {
                            c.x = prevX;
                            c.y = prevY;
                        }
                        c.vx *= -0.25f;
                        c.vy *= -0.25f;
                    }
                    c.vx *= std::exp(-2.0f * dt);
                }
                const float px1 = player.x;
                const float px2 = player.x + player.w;
                const float py1 = player.y;
                const float py2 = player.y + player.h;
                droppedCoins.erase(std::remove_if(droppedCoins.begin(), droppedCoins.end(), [&](const DroppedCoin& c) {
                    if (c.life <= 0.0f) return true;
                    const bool touched = (c.noPickupTimer <= 0.0f) &&
                                         (c.x + coinR > px1 && c.x - coinR < px2 && c.y + coinR > py1 && c.y - coinR < py2);
                    if (!touched) return false;
                    levelManager.addCoins(std::max(1, c.value));
                    audio.playCoinSfx();
                    return true;
                }), droppedCoins.end());
            }
            // Keep level music looping even if backend loop handling stops unexpectedly.
            audio.ensureLevelMusic(paused, deathSequenceActive, levelCompleteActive);
            {
                const float target = levelCompleteActive ? 1.0f : 0.0f;
                const float speed = 4.5f;
                if (levelCompleteUiLerp < target) {
                    levelCompleteUiLerp = std::min(target, levelCompleteUiLerp + speed * dt);
                } else if (levelCompleteUiLerp > target) {
                    levelCompleteUiLerp = std::max(target, levelCompleteUiLerp - speed * dt);
                }
            }
            if (levelCompleteActive && !levelCompleteCounting) {
                if (!audio.isReady() || levelCompleteAudioChannel < 0 || !audio.isChannelPlaying(levelCompleteAudioChannel)) {
                    levelCompleteCounting = true;
                }
            }
            if (levelCompleteActive && levelCompleteCounting && !paused) {
                // Uncapped payout during level complete: process full remaining bonus immediately.
                int payoutPerFrame = std::max(1, levelCompleteCoinBonus + levelCompleteTimeScore);
                int coinStep = std::min(levelCompleteCoinBonus, payoutPerFrame);
                if (coinStep > 0) {
                    levelCompleteCoinBonus -= coinStep;
                    scoreCount += coinStep;
                    levelCompleteAccountedScore += coinStep;
                    audio.playMessageSfx();
                }
                int timeStep = std::min(levelCompleteTimeScore, payoutPerFrame);
                if (timeStep > 0) {
                    levelCompleteTimeScore -= timeStep;
                    scoreCount += timeStep;
                    levelCompleteAccountedScore += timeStep;
                    audio.playMessageSfx();
                }
                if (levelCompleteCoinBonus <= 0 && levelCompleteTimeScore <= 0) {
                    if (!levelCompleteNextPath.empty()) {
                        levelManager.setLevelPath(levelCompleteNextPath);
                        droppedCoins.clear();
                        reloadLevel();
                        continue;
                    }
                    returnToSelect = true;
                    levelRunning = false;
                    continue;
                }
            }

        float inputMove = 0.0f;
        bool inputDown = false;
        int screenW = kBaseScreenW;
        int screenH = kBaseScreenH;
        float uiSize = std::clamp(std::min((float)screenW, (float)screenH) * 0.16f, 110.0f, 190.0f);
        float uiPad = std::clamp(uiSize * 0.22f, 20.0f, 44.0f);
        float uiGap = std::clamp(uiSize * 0.18f, 12.0f, 34.0f);
        SDL_FRect touchLeftBtn{uiPad, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchRightBtn{uiPad + uiSize + uiGap, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchDownBtn{uiPad + (uiSize + uiGap) * 2.0f, screenH - uiPad - uiSize, uiSize, uiSize};
        SDL_FRect touchJumpBtn{screenW - uiPad - uiSize, screenH - uiPad - uiSize, uiSize, uiSize};
        bool touchLeft = false;
        bool touchRight = false;
        bool touchDown = false;
        bool touchJump = false;

        auto computeTouchButtons = [&]() {
            touchLeft = false;
            touchRight = false;
            touchDown = false;
            touchJump = false;
            if (paused) return;
            int winW = 0, winH = 0;
            SDL_GetWindowSize(win, &winW, &winH);
            auto expandRect = [](const SDL_FRect& r, float pad) {
                return SDL_FRect{r.x - pad, r.y - pad, r.w + pad * 2.0f, r.h + pad * 2.0f};
            };
            float hitPad = std::max(8.0f, uiSize * 0.12f);
            SDL_FRect leftHit = expandRect(touchLeftBtn, hitPad);
            SDL_FRect rightHit = expandRect(touchRightBtn, hitPad);
            SDL_FRect downHit = expandRect(touchDownBtn, hitPad);
            SDL_FRect jumpHit = expandRect(touchJumpBtn, hitPad);
            for (const auto& kv : activeTouches) {
                int wx = (int)std::lround(kv.second.x * winW);
                int wy = (int)std::lround(kv.second.y * winH);
                int gx = 0, gy = 0;
                if (!windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) continue;
                float px = (float)gx;
                float py = (float)gy;
                if (pointInRectF(px, py, leftHit)) touchLeft = true;
                if (pointInRectF(px, py, rightHit)) touchRight = true;
                if (pointInRectF(px, py, downHit)) touchDown = true;
                if (pointInRectF(px, py, jumpHit)) touchJump = true;
            }
        };
        computeTouchButtons();

        const SDL_WindowID mainWindowId = SDL_GetWindowID(win);
        const bool embeddedDetailedDebugger = (showDetailedDebugger && debugWin == win && debugRen == ren);
        while (SDL_PollEvent(&e)) {
            input.handleEvent(e);
            {
                InputSystem::DetectionEvent ev;
                while (input.pollDetectionEvent(ev)) {
                    const char* typeStr = (ev.type == InputSystem::DetectionEvent::Type::Connected) ? "connected" : "disconnected";
                    SDL_Log("controller %s: id=%d name=\"%s\" connected=%d", typeStr, (int)ev.id, ev.name.c_str(), ev.connectedCount);
                }
            }
            if (e.type == SDL_QUIT) { running = false; levelRunning = false; }
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && debugWin &&
                e.window.windowID == SDL_GetWindowID(debugWin)) {
                SDL_HideWindow(debugWin);
                showDetailedDebugger = false;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (embeddedDetailedDebugger && e.button.windowID == mainWindowId) {
                    int winW = 0, winH = 0, gx = 0, gy = 0;
                    SDL_GetWindowSize(win, &winW, &winH);
                    if (windowToGamePoint(e.button.x, e.button.y, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                        if (handleDetailedDebuggerTap(gx, gy)) continue;
                    }
                } else if (debugWin && debugRen &&
                           e.button.windowID == SDL_GetWindowID(debugWin)) {
                    const int mx = e.button.x;
                    const int my = e.button.y;
                    (void)handleDetailedDebuggerTap(mx, my);
                }
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowFocused = false;
                    paused = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowFocused = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_MINIMIZED) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowMinimized = true;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_RESTORED) {
                if (e.window.windowID == mainWindowId) {
                    mainWindowMinimized = false;
                }
            }
            if ((e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
                e.window.windowID == mainWindowId) {
                applyDynamicResolutionFromWindow(false);
            }
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                bool consumedByDebugger = false;
                if (embeddedDetailedDebugger &&
                    (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0)) {
                    int winW = 0, winH = 0;
                    SDL_GetWindowSize(win, &winW, &winH);
                    int wx = (int)std::lround(e.tfinger.x * winW);
                    int wy = (int)std::lround(e.tfinger.y * winH);
                    int gx = 0, gy = 0;
                    if (windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                        consumedByDebugger = handleDetailedDebuggerTap(gx, gy);
                    }
                } else if (debugWin && e.tfinger.windowID == SDL_GetWindowID(debugWin)) {
                    int dbgW = 0, dbgH = 0;
                    SDL_GetWindowSize(debugWin, &dbgW, &dbgH);
                    const int mx = (int)std::lround(e.tfinger.x * dbgW);
                    const int my = (int)std::lround(e.tfinger.y * dbgH);
                    consumedByDebugger = handleDetailedDebuggerTap(mx, my);
                }
                if (!consumedByDebugger && e.tfinger.windowID == mainWindowId) {
                    // Touch hotspot (top-right) to toggle detailed debugger on mobile.
                    if (e.tfinger.x >= 0.92f && e.tfinger.y <= 0.10f) {
                        toggleDetailedDebugger();
                        consumedByDebugger = true;
                    }
                }
                if (!consumedByDebugger &&
                    (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0)) {
                    activeTouches[e.tfinger.fingerID] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
                }
            }
            if (e.type == SDL_EVENT_FINGER_MOTION) {
                if (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0 ||
                    activeTouches.find(e.tfinger.fingerID) != activeTouches.end()) {
                    activeTouches[e.tfinger.fingerID] = SDL_FPoint{e.tfinger.x, e.tfinger.y};
                }
            }
            if (e.type == SDL_EVENT_FINGER_UP) {
                if (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0 ||
                    activeTouches.find(e.tfinger.fingerID) != activeTouches.end()) {
                    activeTouches.erase(e.tfinger.fingerID);
                }
            }
            if (e.type == SDL_EVENT_FINGER_CANCELED) {
                if (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0 ||
                    activeTouches.find(e.tfinger.fingerID) != activeTouches.end()) {
                    activeTouches.erase(e.tfinger.fingerID);
                }
            }
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                if (e.key.key == SDLK_F11) {
#if !defined(__ANDROID__)
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen);
#endif
                }
                if (e.key.key == SDLK_F12) {
                    showHitboxes = !showHitboxes;
                }
                if (e.key.key == SDLK_F8) {
                    const bool next = !(showHitboxes && showPlayerHitbox && showDebugView);
                    showHitboxes = next;
                    showPlayerHitbox = next;
                    showDebugView = next;
                }
                if (e.key.key == SDLK_F7) {
                    showDebugView = !showDebugView;
                }
                if (e.key.key == SDLK_p) {
                    showDemoPath = !showDemoPath;
                }
                if (e.key.key == SDLK_F6) {
                    showFpsCounter = !showFpsCounter;
                }
                if (e.key.key == SDLK_F10) {
                    clampCamX = !clampCamX;
                }
                if (e.key.key == SDLK_F5) {
                    toggleDetailedDebugger();
                }
                if (e.key.key == SDLK_F4) {
                    detailedDebugSubmenu = (detailedDebugSubmenu + 1) % 4;
                }
                if (showDetailedDebugger && detailedDebugSubmenu == 1) {
                    if (e.key.key == SDLK_UP) detailedDebugObjectIndex--;
                    if (e.key.key == SDLK_DOWN) detailedDebugObjectIndex++;
                }
                if (e.key.key == SDLK_F9) {
                    if (allowNextLevelProgression) {
                        std::string nextPath = levelManager.nextLevelPath();
                        if (!nextPath.empty()) {
                            levelManager.setLevelPath(nextPath);
                            droppedCoins.clear();
                            reloadLevel();
                        }
                    }
                }
                if (e.key.key == SDLK_F3) {
                    demoState.enabled = !demoState.enabled;
                    demoState.jumpCooldown = 0.0f;
                    demoState.jumpHoldTimer = 0.0f;
                    demoState.stuckTime = 0.0f;
                    demoState.lastX = player.x;
                    demoState.repathTimer = 0.0f;
                    demoState.waypointIndex = 0;
                    demoState.pathTiles.clear();
                    demoState.startTileSet = false;
                    SDL_Log("demo autoplay: %s", demoState.enabled ? "enabled" : "disabled");
                }
                if (e.key.key == SDLK_F2) {
                    if (replayRecorder.enabled) {
                        stopReplayRecording("user_toggle_off");
                        SDL_Log("replay recording: disabled");
                    } else {
                        startReplayRecording();
                        SDL_Log("replay recording: enabled (%s)", replayRecorder.path.c_str());
                    }
                }
                if (e.key.key == SDLK_F1) {
                    if (replayPlayback.active) {
                        stopReplayPlayback();
                        SDL_Log("replay playback: disabled");
                    } else {
                        std::string replayPath;
                        {
                            std::ifstream latest((replayDirPath / "latest_replay.txt").string(), std::ios::binary);
                            if (latest.is_open()) std::getline(latest, replayPath);
                        }
                        if (!replayPath.empty()) {
                            std::string replayLevelPath;
                            if (loadReplayForPlayback(replayPath, replayLevelPath)) {
                                stopReplayRecording("playback_started");
                                if (!replayLevelPath.empty() && replayLevelPath != levelManager.levelPath()) {
                                    levelManager.setLevelPath(replayLevelPath);
                                    droppedCoins.clear();
                                    reloadLevel();
                                }
                                SDL_Log("replay playback: enabled (%s, frames=%d)",
                                        replayPath.c_str(), (int)replayPlayback.frames.size());
                            } else {
                                SDL_Log("replay playback: failed to load %s", replayPath.c_str());
                            }
                        } else {
                            SDL_Log("replay playback: latest replay path not found");
                        }
                    }
                }
                if (!levelCompleteActive && (e.key.key == SDLK_AC_BACK || e.key.key == SDL_GetKeyFromScancode(keybinds.pause, SDL_KMOD_NONE, false))) {
                    paused = !paused;
                }
                if (paused) {
                    if (e.key.key == SDLK_LEFT || e.key.key == SDLK_a) {
                        pauseSelection = std::max(0, pauseSelection - 1);
                    }
                    if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_d) {
                        pauseSelection = std::min(2, pauseSelection + 1);
                    }
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                        handlePauseSelect(pauseSelection);
                    }
                }
            }
            if (!levelCompleteActive && InputSystem::isPauseToggleEvent(e)) {
                paused = !paused;
            }
            if (paused && InputSystem::isLeftEvent(e)) {
                pauseSelection = std::max(0, pauseSelection - 1);
            }
            if (paused && InputSystem::isRightEvent(e)) {
                pauseSelection = std::min(2, pauseSelection + 1);
            }
            if (paused && InputSystem::isAcceptEvent(e)) {
                handlePauseSelect(pauseSelection);
            }
            if (paused && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int winW = 0, winH = 0, gx = 0, gy = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                if (windowToGamePoint(e.button.x, e.button.y, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                    SDL_Point pt{gx, gy};
                    if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                    else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                    else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
                }
            }
            if (paused && e.type == SDL_EVENT_FINGER_DOWN &&
                (e.tfinger.windowID == mainWindowId || e.tfinger.windowID == 0)) {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(win, &winW, &winH);
                int wx = (int)(e.tfinger.x * winW);
                int wy = (int)(e.tfinger.y * winH);
                int gx = 0, gy = 0;
                if (windowToGamePoint(wx, wy, winW, winH, kBaseScreenW, kBaseScreenH, gx, gy, 1.0f)) {
                    SDL_Point pt{gx, gy};
                    if (SDL_PointInRect(&pt, &pauseBtnContinue)) handlePauseSelect(0);
                    else if (SDL_PointInRect(&pt, &pauseBtnRestart)) handlePauseSelect(1);
                    else if (SDL_PointInRect(&pt, &pauseBtnExit)) handlePauseSelect(2);
                }
            }
        }
        computeTouchButtons();

        if (deathSequenceActive && !paused) {
            deathTimer += dt;
            if (!deathLifeDeducted && deathTimer >= 0.12f) {
                livesCount = std::max(0, livesCount - 1);
                deathLifeDeducted = true;
            }
            if (deathLifeDeducted && deathTimer >= 0.90f) {
                deathSequenceActive = false;
                deathTimer = 0.0f;
                if (livesCount > 0) {
                    droppedCoins.clear();
                    reloadLevel();
                    continue;
                } else {
                    returnToSelect = true;
                    levelRunning = false;
                    continue;
                }
            }
        }

            bool replayPlaybackDrivingThisFrame = false;
            if (replayPlayback.active && !paused && !deathSequenceActive && !levelCompleteActive) {
                if (replayPlayback.nextFrame < replayPlayback.frames.size()) {
                    const ReplayFrameSample& s = replayPlayback.frames[replayPlayback.nextFrame++];
                    player.x = s.x;
                    player.y = s.y;
                    player.vx = s.vx;
                    player.vy = s.vy;
                    player.w = s.w;
                    player.h = s.h;
                    player.onGround = s.onGround;
                    player.inWater = s.inWater;
                    player.facing = s.facing;
                    player.freeMove = s.freeMove;
                    player.anim = s.anim;
                    player.animTime = s.animTime;
                    player.jumpHeld = s.jumpHeld;
                    player.jumpHoldTime = s.jumpHoldTime;
                    player.jumpWasDown = s.jumpWasDown;
                    player.jumpBufferTime = s.jumpBufferTime;
                    inputMove = s.inputMove;
                    inputDown = s.inputDown;
                    replayPlaybackDrivingThisFrame = true;
                } else {
                    stopReplayPlayback();
                }
            }

            if (!paused && !deathSequenceActive && !replayPlaybackDrivingThisFrame) {
            const float frameStartX = player.x;
            const float frameStartY = player.y;
            enum MovementReasonMask : unsigned int {
