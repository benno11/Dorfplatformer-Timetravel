                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, resumeBtn.x + (resumeBtn.w - MeasureTextWidth(2, "RESUME")) / 2, resumeBtn.y + (resumeBtn.h - labelLineH) / 2, 2, "RESUME");

                SDL_SetRenderDrawColor(ren, pauseSelection == 1 ? 70 : 45, pauseSelection == 1 ? 120 : 70, pauseSelection == 1 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &restartBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, restartBtn.x + (restartBtn.w - MeasureTextWidth(2, "RESTART")) / 2, restartBtn.y + (restartBtn.h - labelLineH) / 2, 2, "RESTART");

                SDL_SetRenderDrawColor(ren, pauseSelection == 2 ? 120 : 70, pauseSelection == 2 ? 70 : 50, pauseSelection == 2 ? 70 : 60, 255);
                SDL_RenderFillRect(ren, &quitBtn);
                SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
                DrawText(ren, quitBtn.x + (quitBtn.w - MeasureTextWidth(2, "QUIT")) / 2, quitBtn.y + (quitBtn.h - labelLineH) / 2, 2, "QUIT");
            }

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
        if (levelCompleteUiLerp > 0.001f) {
            const int centerY = screenH / 2;
            auto completeSlideInXForY = [&](int y) -> int {
                const float yBias = std::abs((float)(y - centerY)) * 0.45f;
                const float base = (float)screenW + 120.0f + yBias;
                return (int)std::lround((1.0f - levelCompleteUiLerp) * base);
            };
            const std::string titleTop = "DIRK HAS";
            std::string titleBottom = std::string("PASSED AREA ") + std::to_string(levelCompleteAreaId);
            const int titleScale = 3;
            int topW = MeasureTextWidth(titleScale, titleTop);
            int bottomW = MeasureTextWidth(titleScale, titleBottom);
            const int titleTopY = centerY - 130;
            const int titleBottomY = centerY - 110;
            DrawText(ren, screenW / 2 - topW / 2 + completeSlideInXForY(titleTopY), titleTopY, titleScale, titleTop);
            DrawText(ren, screenW / 2 - bottomW / 2 + completeSlideInXForY(titleBottomY), titleBottomY, titleScale, titleBottom);

            const int bonusScale = 2;
            const int bonusLineGap = 18;
            const int bonusStartY = centerY - 50;
            const std::string bonusLine1 = std::string("COIN BONUS: ") + std::to_string(levelCompleteCoinBonus);
            const std::string bonusLine2 = std::string("TIME BONUS: ") + std::to_string(levelCompleteTimeScore);
            const std::string bonusLine3 = std::string("TOTAL SCORE: ") + std::to_string(levelCompleteAccountedScore);
            DrawText(ren, screenW / 2 - MeasureTextWidth(bonusScale, bonusLine1) / 2 + completeSlideInXForY(bonusStartY), bonusStartY, bonusScale, bonusLine1);
            DrawText(ren, screenW / 2 - MeasureTextWidth(bonusScale, bonusLine2) / 2 + completeSlideInXForY(bonusStartY + bonusLineGap), bonusStartY + bonusLineGap, bonusScale, bonusLine2);
            DrawText(ren, screenW / 2 - MeasureTextWidth(bonusScale, bonusLine3) / 2 + completeSlideInXForY(bonusStartY + bonusLineGap * 2), bonusStartY + bonusLineGap * 2, bonusScale, bonusLine3);
        }
        if (showFpsCounter) {
            const std::string ufpsText = std::string("UFPS: ") + std::to_string(updateFpsDisplay);
            const std::string rfpsText = std::string("RFPS: ") + std::to_string(renderFpsDisplay);
            const int fpsScale = 2;
            const int fpsX = screenW - 10 - std::max(MeasureTextWidth(fpsScale, ufpsText), MeasureTextWidth(fpsScale, rfpsText));
            DrawText(ren, fpsX, 10, fpsScale, ufpsText);
            DrawText(ren, fpsX, 34, fpsScale, rfpsText);
        }
        if (demoState.enabled) {
            DrawText(ren, 12, 10, 2, "DEMO");
        }
        if (replayRecorder.enabled) {
            DrawText(ren, 12, 34, 2, "REC");
        }
        if (replayPlayback.active) {
            DrawText(ren, 12, 58, 2, "PBK");
        }
        if (replayRecorder.enabled && replayRecorder.out.is_open()) {
            try {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                nlohmann::json frame;
                frame["type"] = "frame";
                frame["i"] = replayRecorder.frameIndex++;
                frame["ticks_ns"] = (uint64_t)SDL_GetTicksNS();
                frame["dt"] = dt;
                frame["paused"] = paused;
                frame["level_timer"] = levelTimerSeconds;
                frame["level_path"] = levelManager.levelPath();
                frame["world"] = levelManager.worldId();
                frame["area"] = levelManager.levelPartId();
                frame["cam"] = {{"x", camX}, {"y", camY}};
                frame["player"] = {
                    {"x", player.x}, {"y", player.y},
                    {"w", player.w}, {"h", player.h},
                    {"vx", player.vx}, {"vy", player.vy},
                    {"on_ground", player.onGround},
                    {"in_water", player.inWater},
                    {"facing", player.facing},
                    {"free_move", player.freeMove},
                    {"anim", player.anim},
                    {"anim_name", debugAnimName},
                    {"anim_time", player.animTime},
                    {"frame_index", renderAnimFrameIndex},
                    {"frame_name", renderFrameName},
                    {"jump_held", player.jumpHeld},
                    {"jump_hold_time", player.jumpHoldTime},
                    {"jump_was_down", player.jumpWasDown},
                    {"jump_buffer_time", player.jumpBufferTime}
                };
                frame["input_map"] = {
                    {"touch_move", replayInput.touchMove},
                    {"touch_down", replayInput.touchDown},
                    {"touch_jump", replayInput.touchJump},
                    {"gamepad_move", replayInput.gamepadMove},
                    {"gamepad_down", replayInput.gamepadDown},
                    {"gamepad_jump", replayInput.gamepadJump},
                    {"gamepad_free_move", replayInput.gamepadFreeMove},
                    {"input_move", replayInput.inputMove},
                    {"input_down", replayInput.inputDown},
                    {"force_right_movement", replayInput.forceRightMovement},
                    {"fast_travel_enabled", replayInput.fastTravelEnabled},
                    {"demo_enabled", replayInput.demoEnabled},
                    {"keyboard", {
                        {"left", keys[keybinds.moveLeft] || keys[SDL_SCANCODE_LEFT]},
                        {"right", keys[keybinds.moveRight] || keys[SDL_SCANCODE_RIGHT]},
                        {"up", keys[keybinds.jump] || keys[SDL_SCANCODE_UP]},
                        {"down", keys[keybinds.moveDown] || keys[SDL_SCANCODE_DOWN]},
                        {"jump", keys[keybinds.jump]},
                        {"shift", keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]}
                    }}
                };
                replayRecorder.out << frame.dump() << "\n";
                if ((replayRecorder.frameIndex % 120) == 0) replayRecorder.out.flush();
            } catch (...) {
                replayRecorder.enabled = false;
            }
        }

        if (showDebugView) {
            // Debug UI (highest render priority)
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
            SDL_Rect dbgPanel{10, 10, 340, 196};
            SDL_RenderFillRect(ren, &dbgPanel);
            SDL_SetRenderDrawColor(ren, 80, 90, 110, 220);
            SDL_RenderDrawRect(ren, &dbgPanel);

            SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
            DrawText(ren, 18, 18, 2, "DEBUG");
            DrawDebugNumber(ren, 18, 36, 2, "PX", (int)player.x);
            DrawDebugNumber(ren, 18, 50, 2, "PY", (int)player.y);
            DrawDebugNumber(ren, 18, 64, 2, "VX", (int)player.vx);
            DrawDebugNumber(ren, 18, 78, 2, "VY", (int)player.vy);
            DrawDebugNumber(ren, 140, 36, 2, "CAMX", (int)camX);
            DrawDebugNumber(ren, 140, 50, 2, "CAMY", (int)camY);
            DrawDebugNumber(ren, 140, 64, 2, "WTR", player.inWater ? 1 : 0);
            DrawDebugNumber(ren, 140, 78, 2, "DRN", (int)(45.0f - player.drownTimer));
            float maxCamX = std::max(0.0f, (float)(map.h * map.tileSize - screenW));
            float maxCamY = std::max(0.0f, (float)(map.w * map.tileSize - screenH));
            DrawDebugNumber(ren, 18, 92, 2, "BW", map.w);
            DrawDebugNumber(ren, 140, 92, 2, "BH", map.h);
            DrawDebugNumber(ren, 18, 106, 2, "CMINX", 0);
            DrawDebugNumber(ren, 140, 106, 2, "CMAXX", (int)maxCamX);
            DrawDebugNumber(ren, 18, 120, 2, "CMINY", 0);
            DrawDebugNumber(ren, 140, 120, 2, "CMAXY", (int)maxCamY);
            DrawDebugNumber(ren, 18, 134, 2, "UFPS", updateFpsDisplay);
            DrawDebugNumber(ren, 140, 134, 2, "RFPS", renderFpsDisplay);
            DrawText(ren, 18, 148, 2, std::string("ANIM ") + debugAnimName);
            if (!renderFrameName.empty()) {
                std::string id = renderFrameName;
                if (id.size() > 4 && id.substr(id.size() - 4) == ".png") id.resize(id.size() - 4);
                DrawText(ren, 18, 162, 2, std::string("ID ") + id);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            // Camera bounds (level extents)
            SDL_SetRenderDrawColor(ren, 40, 200, 140, 255);
            SDL_FRect bounds{
                -camX,
                -camY,
                (float)(map.w * map.tileSize),
                (float)(map.h * map.tileSize)
            };
            SDL_RenderDrawRectF(ren, &bounds);

            // Tile IDs (debug)
            SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
            for (int y = tileMinY; y <= tileMaxY; y++) {
                for (int x = tileMinX; x <= tileMaxX; x++) {
                    int idx = y * map.w + x;
                    int id = (int)map.tileIds[idx];
                    int screenX = (int)(x * map.tileSize - camX);
                    int screenY = (int)(y * map.tileSize - camY);
                    const std::string idText = std::to_string(id);
                    int idScale = std::clamp(map.tileSize / 14, 1, 3);
                    const int fitW = std::max(4, map.tileSize - 4);
                    const int fitH = std::max(4, map.tileSize - 4);
                    while (idScale > 1 &&
                           (MeasureTextWidth(idScale, idText) > fitW || (10 * idScale) > fitH)) {
                        --idScale;
                    }
                    const int textW = MeasureTextWidth(idScale, idText);
                    const int textH = 10 * idScale;
                    const int textX = screenX + (map.tileSize - textW) / 2;
                    const int textY = screenY + (map.tileSize - textH) / 2;
                    DrawText(ren, textX, textY, idScale, idText);
                }
            }
        }

        if (!paused) {
            int touchDeviceCount = 0;
            SDL_TouchID* touchDevices = SDL_GetTouchDevices(&touchDeviceCount);
            if (touchDevices) SDL_free(touchDevices);
            bool showMobileUi = (touchDeviceCount > 0) || !activeTouches.empty();
            if (showMobileUi) {
                auto drawTouchBtn = [&](const SDL_FRect& r, bool active) {
                    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(ren, active ? 180 : 120, active ? 180 : 120, active ? 180 : 120, active ? 120 : 80);
                    SDL_RenderFillRectF(ren, &r);
                    SDL_SetRenderDrawColor(ren, 230, 230, 230, 180);
                    SDL_RenderDrawRectF(ren, &r);
                };
                drawTouchBtn(touchLeftBtn, touchLeft);
                drawTouchBtn(touchRightBtn, touchRight);
                drawTouchBtn(touchDownBtn, touchDown);
                drawTouchBtn(touchJumpBtn, touchJump);
                SDL_SetRenderDrawColor(ren, 240, 240, 240, 220);
                int touchLabelScale = std::clamp((int)std::lround(uiSize / 44.0f), 2, 4);
                auto drawBtnLabel = [&](const SDL_FRect& btn, const std::string& text) {
                    int tw = MeasureTextWidth(touchLabelScale, text);
                    int tx = (int)std::lround(btn.x + (btn.w - tw) * 0.5f);
                    int ty = (int)std::lround(btn.y + (btn.h * 0.5f) - (10.0f * touchLabelScale));
                    DrawText(ren, tx, ty, touchLabelScale, text);
                };
                drawBtnLabel(touchLeftBtn, "L");
                drawBtnLabel(touchRightBtn, "R");
                drawBtnLabel(touchDownBtn, "DN");
                drawBtnLabel(touchJumpBtn, "JMP");
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            }
        }

        if (embeddedDetailedDebugger) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect panel{8, 6, kBaseScreenW - 16, std::min(360, kBaseScreenH - 12)};
            SDL_SetRenderDrawColor(ren, 10, 12, 16, 210);
            SDL_RenderFillRect(ren, &panel);
            SDL_SetRenderDrawColor(ren, 180, 200, 230, 220);
            SDL_RenderDrawRect(ren, &panel);

            DrawText(ren, 12, 10, 2, "DETAILED DEBUGGER (F5)");
            SDL_Rect tab0{12, 38, 130, 36};
            SDL_Rect tab1{152, 38, 130, 36};
            SDL_Rect tab2{292, 38, 130, 36};
            SDL_Rect tab3{432, 38, 130, 36};
            SDL_Rect tabs[4] = {tab0, tab1, tab2, tab3};
            const char* tabNames[4] = {"OVERVIEW", "OBJECT INDEX", "PERF", "PLAYER"};
            for (int i = 0; i < 4; ++i) {
                SDL_SetRenderDrawColor(ren, i == detailedDebugSubmenu ? 70 : 45, 85, 120, i == detailedDebugSubmenu ? 180 : 120);
                SDL_RenderFillRect(ren, &tabs[i]);
                SDL_SetRenderDrawColor(ren, 180, 200, 230, 255);
                SDL_RenderDrawRect(ren, &tabs[i]);
                DrawText(ren, tabs[i].x + 8, tabs[i].y + 8, 2, tabNames[i]);
            }
            SDL_Rect closeBtn{500, 8, 52, 24};
            SDL_SetRenderDrawColor(ren, 85, 55, 55, 210);
            SDL_RenderFillRect(ren, &closeBtn);
            SDL_SetRenderDrawColor(ren, 220, 180, 180, 255);
            SDL_RenderDrawRect(ren, &closeBtn);
            DrawText(ren, 514, 12, 2, "X");

            int y = 88;
            if (detailedDebugSubmenu == 0) {
                DrawText(ren, 12, y, 2, std::string("UFPS/RFPS: ") + std::to_string(updateFpsDisplay) + "/" + std::to_string(renderFpsDisplay)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Frame ms: ") + std::to_string((int)std::lround(dt * 1000.0f))); y += 20;
                DrawText(ren, 12, y, 2, std::string("Player X/Y: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Player VX/VY: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(ren, 12, y, 2, std::string("OnGround/InWater: ") + (player.onGround ? "1" : "0") + "/" + (player.inWater ? "1" : "0")); y += 20;
                DrawText(ren, 12, y, 2, std::string("Objects: ") + std::to_string((int)objects.size())); y += 20;
            } else if (detailedDebugSubmenu == 1) {
                if (objects.empty()) {
                    detailedDebugObjectIndex = 0;
                    DrawText(ren, 12, y, 2, "No objects in current level");
                } else {
                    detailedDebugObjectIndex = std::clamp(detailedDebugObjectIndex, 0, (int)objects.size() - 1);
                    SDL_Rect prevBtn{12, 92, 120, 34};
                    SDL_Rect nextBtn{142, 92, 120, 34};
                    SDL_SetRenderDrawColor(ren, 55, 70, 95, 180);
                    SDL_RenderFillRect(ren, &prevBtn);
                    SDL_RenderFillRect(ren, &nextBtn);
                    SDL_SetRenderDrawColor(ren, 180, 200, 230, 255);
                    SDL_RenderDrawRect(ren, &prevBtn);
                    SDL_RenderDrawRect(ren, &nextBtn);
                    DrawText(ren, prevBtn.x + 12, prevBtn.y + 7, 2, "PREV");
                    DrawText(ren, nextBtn.x + 12, nextBtn.y + 7, 2, "NEXT");
                    y = 134;
                    const ObjectInstance& sel = objects[detailedDebugObjectIndex];
                    DrawText(ren, 12, y, 2, std::string("Selected: ") + std::to_string(detailedDebugObjectIndex)); y += 20;
                    DrawText(ren, 12, y, 2, std::string("ID: ") + sel.id); y += 20;
                    DrawText(ren, 12, y, 2, std::string("Pos: ") + std::to_string((int)sel.x) + ", " + std::to_string((int)sel.y)); y += 20;
                }
            } else if (detailedDebugSubmenu == 2) {
                DrawText(ren, 12, y, 2, std::string("Frame ms: ") + std::to_string((int)std::lround(dt * 1000.0f))); y += 20;
                DrawText(ren, 12, y, 2, "Target: <16ms (60 FPS)"); y += 20;
                DrawText(ren, 12, y, 2, "Use submenu for full perf details on desktop"); y += 20;
            } else {
                DrawText(ren, 12, y, 2, std::string("Position: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Velocity: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(ren, 12, y, 2, std::string("Facing: ") + (player.facing < 0 ? "LEFT" : "RIGHT")); y += 20;
                DrawText(ren, 12, y, 2, std::string("Anim: ") + std::to_string(player.anim)); y += 20;
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        SDL_SetRenderTarget(ren, nullptr);
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Rect presentDst = computePresentRect(winW, winH, kBaseScreenW, kBaseScreenH, 1.0f);
        SDL_SetRenderDrawColor(ren, 221, 248, 255, 255); // #ddf8ff
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, gameTarget, nullptr, &presentDst);
        SDL_RenderPresent(ren);
        {
            const Uint32 presentedAt = SDL_GetTicks();
            const Uint32 presentDelta = presentedAt - lastPresentTicks;
            const int renderFpsInstant = std::clamp((presentDelta > 0) ? (int)(1000u / presentDelta) : 0, 0, kFpsDisplayMax);
            if (renderFpsSmoothed <= 0.0f) renderFpsSmoothed = (float)renderFpsInstant;
            else renderFpsSmoothed += ((float)renderFpsInstant - renderFpsSmoothed) * 0.20f;
            renderFpsDisplay = std::clamp((int)std::lround(renderFpsSmoothed), 0, kFpsDisplayMax);
            lastPresentTicks = presentedAt;
            // Keep render cadence capped while updates remain independent.
            const int targetPresentIntervalMs =
                (powerManagementEnabled && mainWindowMinimized) ? 250 :
                (powerManagementEnabled && (!mainWindowFocused || paused)) ? 33 : 16;
            nextPresentTicks = presentedAt + (Uint32)targetPresentIntervalMs;
        }
        if (showDetailedDebugger && debugRen && debugWin && !embeddedDetailedDebugger) {
            int dbgW = 0, dbgH = 0;
            SDL_GetWindowSize(debugWin, &dbgW, &dbgH);
            SDL_SetRenderDrawColor(debugRen, 10, 12, 16, 255);
            SDL_RenderClear(debugRen);

            int y = 10;
            DrawText(debugRen, 12, y, 2, "DETAILED DEBUGGER (F5)"); y += 24;
            SDL_Rect tab0{12, 38, 130, 36};
            SDL_Rect tab1{152, 38, 130, 36};
            SDL_Rect tab2{292, 38, 130, 36};
            SDL_Rect tab3{432, 38, 130, 36};
            SDL_Rect tabs[4] = {tab0, tab1, tab2, tab3};
            const char* tabNames[4] = {"OVERVIEW", "OBJECT INDEX", "PERFORMANCE", "PLAYER STATUS"};
            for (int i = 0; i < 4; ++i) {
                SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(debugRen, i == detailedDebugSubmenu ? 70 : 45, 85, 120, i == detailedDebugSubmenu ? 180 : 120);
                SDL_RenderFillRect(debugRen, &tabs[i]);
                SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(debugRen, 180, 200, 230, 255);
                SDL_RenderDrawRect(debugRen, &tabs[i]);
                DrawText(debugRen, tabs[i].x + (tabs[i].w - MeasureTextWidth(2, tabNames[i])) / 2, tabs[i].y + 8, 2, tabNames[i]);
            }
            y = 84;
            const char* submenuNames[4] = {"OVERVIEW", "OBJECT INDEX", "PERFORMANCE", "PLAYER STATUS"};
            DrawText(debugRen, 12, y, 2, std::string("SUBMENU (F4): ") + submenuNames[detailedDebugSubmenu]); y += 20;
            if (detailedDebugSubmenu == 0) {
                long rssKB = -1, vmKB = -1;
                readProcessMemoryKB(rssKB, vmKB);
                DrawText(debugRen, 12, y, 2, std::string("UFPS: ") + std::to_string(updateFpsDisplay)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("RFPS: ") + std::to_string(renderFpsDisplay)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Frame ms: ") + std::to_string((int)std::lround(dt * 1000.0f))); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Memory RSS MB: ") + (rssKB >= 0 ? std::to_string((int)(rssKB / 1024)) : "N/A")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Memory VM MB: ") + (vmKB >= 0 ? std::to_string((int)(vmKB / 1024)) : "N/A")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Player X/Y: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Player VX/VY: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("OnGround/InWater: ") + (player.onGround ? "1" : "0") + "/" + (player.inWater ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Cam X/Y: ") + std::to_string((int)camX) + ", " + std::to_string((int)camY)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Map W/H: ") + std::to_string(map.w) + " x " + std::to_string(map.h)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("TileSize: ") + std::to_string(map.tileSize)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Coins/Score/Lives: ") + std::to_string(levelManager.coinCount()) + "/" + std::to_string(scoreCount) + "/" + std::to_string(livesCount)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Objects: ") + std::to_string((int)objects.size())); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Level IDs (W/P/T): ") + std::to_string(levelManager.worldId()) + "/" + std::to_string(levelManager.levelPartId()) + "/" + std::to_string(levelManager.timeId())); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("TimeWarpId: ") + std::string(1, levelManager.timeWarpId())); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Flags H/P/D/FPS: ") +
                                            (showHitboxes ? "1" : "0") + "/" +
                                            (showPlayerHitbox ? "1" : "0") + "/" +
                                            (showDebugView ? "1" : "0") + "/" +
                                            (showFpsCounter ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Paused/Death/Complete: ") +
                                            (paused ? "1" : "0") + "/" +
                                            (deathSequenceActive ? "1" : "0") + "/" +
                                            (levelCompleteActive ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, "Guideline: target <16ms for 60 FPS"); y += 20;
                DrawText(debugRen, 12, y, 2, "Guideline: keep RSS stable during play"); y += 20;
            } else if (detailedDebugSubmenu == 1) {
                if (objects.empty()) {
                    detailedDebugObjectIndex = 0;
                    DrawText(debugRen, 12, y, 2, "No objects in current level");
                } else {
                    if (detailedDebugObjectIndex < 0) detailedDebugObjectIndex = 0;
                    if (detailedDebugObjectIndex >= (int)objects.size()) detailedDebugObjectIndex = (int)objects.size() - 1;
                    SDL_Rect prevBtn{12, 92, 120, 34};
                    SDL_Rect nextBtn{142, 92, 120, 34};
                    SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(debugRen, 55, 70, 95, 180);
                    SDL_RenderFillRect(debugRen, &prevBtn);
                    SDL_RenderFillRect(debugRen, &nextBtn);
                    SDL_SetRenderDrawBlendMode(debugRen, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(debugRen, 180, 200, 230, 255);
                    SDL_RenderDrawRect(debugRen, &prevBtn);
                    SDL_RenderDrawRect(debugRen, &nextBtn);
                    DrawText(debugRen, prevBtn.x + 12, prevBtn.y + 7, 2, "PREV");
                    DrawText(debugRen, nextBtn.x + 12, nextBtn.y + 7, 2, "NEXT");
                    y = 134;
                    DrawText(debugRen, 12, y, 2, "Use UP/DOWN to select object index"); y += 20;
                    const ObjectInstance& sel = objects[detailedDebugObjectIndex];
                    DrawText(debugRen, 12, y, 2, std::string("Selected Index: ") + std::to_string(detailedDebugObjectIndex)); y += 20;
                    DrawText(debugRen, 12, y, 2, std::string("ID: ") + sel.id); y += 20;
                    DrawText(debugRen, 12, y, 2, std::string("Pos X/Y: ") + std::to_string((int)sel.x) + ", " + std::to_string((int)sel.y)); y += 20;
                    DrawText(debugRen, 12, y, 2, std::string("Screen X/Y: ") + std::to_string((int)(sel.x - camX)) + ", " + std::to_string((int)(sel.y - camY))); y += 24;

                    // Decoded object map from level data.
                    const int mapPanelX = 360;
                    const int mapPanelY = 92;
                    const int mapPanelW = std::max(180, dbgW - mapPanelX - 12);
                    const int mapPanelH = std::max(150, dbgH - mapPanelY - 12);
                    SDL_Rect objMapRect{mapPanelX, mapPanelY, mapPanelW, mapPanelH};
                    SDL_SetRenderDrawColor(debugRen, 36, 44, 58, 255);
                    SDL_RenderDrawRect(debugRen, &objMapRect);
                    DrawText(debugRen, mapPanelX + 8, mapPanelY + 8, 2, "OBJECT MAP (DECODED)");

                    const float worldW = std::max(1.0f, (float)(map.w * map.tileSize));
                    const float worldH = std::max(1.0f, (float)(map.h * map.tileSize));
                    const float plotW = (float)(mapPanelW - 16);
                    const float plotH = (float)(mapPanelH - 28);
                    const int plotX = mapPanelX + 8;
                    const int plotY = mapPanelY + 20;

                    SDL_Rect plotRect{plotX, plotY, (int)plotW, (int)plotH};
                    SDL_SetRenderDrawColor(debugRen, 26, 32, 42, 255);
                    SDL_RenderFillRect(debugRen, &plotRect);
                    SDL_SetRenderDrawColor(debugRen, 70, 90, 120, 255);
                    SDL_RenderDrawRect(debugRen, &plotRect);

                    for (int i = 0; i < (int)objects.size(); ++i) {
                        const ObjectInstance& o = objects[i];
                        int px = plotX + (int)std::lround((std::clamp(o.x, 0.0f, worldW) / worldW) * (plotW - 1.0f));
                        int py = plotY + (int)std::lround((std::clamp(o.y, 0.0f, worldH) / worldH) * (plotH - 1.0f));
                        bool selected = (i == detailedDebugObjectIndex);
                        SDL_SetRenderDrawColor(debugRen, selected ? 255 : 130, selected ? 230 : 190, selected ? 90 : 140, 255);
                        SDL_Rect dot{px - (selected ? 2 : 1), py - (selected ? 2 : 1), selected ? 5 : 3, selected ? 5 : 3};
                        SDL_RenderFillRect(debugRen, &dot);
                    }

                    int ppx = plotX + (int)std::lround((std::clamp(player.x, 0.0f, worldW) / worldW) * (plotW - 1.0f));
                    int ppy = plotY + (int)std::lround((std::clamp(player.y, 0.0f, worldH) / worldH) * (plotH - 1.0f));
                    SDL_SetRenderDrawColor(debugRen, 80, 180, 255, 255);
                    SDL_RenderDrawLine(debugRen, ppx - 3, ppy, ppx + 3, ppy);
                    SDL_RenderDrawLine(debugRen, ppx, ppy - 3, ppx, ppy + 3);

                    int typeY = mapPanelY + mapPanelH - 82;
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, "ENTITY SPAWN POS:");
                    typeY += 18;
                    std::string posLine;
                    const int maxShowPos = std::min((int)meta.entitySpawnPos.size(), 12);
                    for (int i = 0; i < maxShowPos; ++i) {
                        if (i > 0) posLine += ",";
                        posLine += std::to_string(meta.entitySpawnPos[i]);
                    }
                    if ((int)meta.entitySpawnPos.size() > maxShowPos) posLine += "...";
                    if (posLine.empty()) posLine = "(empty)";
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, posLine);

                    typeY += 18;
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, "ENTITY SPAWN TYPE:");
                    typeY += 18;
                    std::string typesLine;
                    const int maxShow = std::min((int)meta.entitySpawnType.size(), 12);
                    for (int i = 0; i < maxShow; ++i) {
                        if (i > 0) typesLine += ",";
                        typesLine += std::to_string(meta.entitySpawnType[i]);
                    }
                    if ((int)meta.entitySpawnType.size() > maxShow) typesLine += "...";
                    if (typesLine.empty()) typesLine = "(empty)";
                    DrawText(debugRen, mapPanelX + 8, typeY, 2, typesLine);

                    int start = std::max(0, detailedDebugObjectIndex - 10);
                    int end = std::min((int)objects.size(), start + 20);
                    for (int i = start; i < end; ++i) {
                        const ObjectInstance& o = objects[i];
                        std::string line = (i == detailedDebugObjectIndex ? "> " : "  ")
                                           + std::to_string(i) + " id=" + o.id
                                           + " x=" + std::to_string((int)o.x)
                                           + " y=" + std::to_string((int)o.y);
                        DrawText(debugRen, 12, y, 2, line);
                        y += 18;
                    }
                }
            } else if (detailedDebugSubmenu == 2) {
                long rssKB = -1, vmKB = -1;
                readProcessMemoryKB(rssKB, vmKB);
                const int panelX = 12;
                const int panelY = std::max(120, y + 8);
                const int panelW = 210;
                const int panelH = std::max(140, dbgH - panelY - 12);
                SDL_Rect guideRect{panelX, panelY, panelW, panelH};
                SDL_SetRenderDrawColor(debugRen, 36, 44, 58, 255);
                SDL_RenderDrawRect(debugRen, &guideRect);
                int gy = panelY + 10;
                DrawText(debugRen, panelX + 8, gy, 2, "GUIDELINES"); gy += 22;
                DrawText(debugRen, panelX + 8, gy, 2, "- target <16ms @60fps"); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, "- >33ms causes drops"); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, "- keep RSS stable"); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, "- rising RSS => leaks"); gy += 22;
                DrawText(debugRen, panelX + 8, gy, 2, std::string("RSS MB: ") + (rssKB >= 0 ? std::to_string((int)(rssKB / 1024)) : "N/A")); gy += 18;
                DrawText(debugRen, panelX + 8, gy, 2, std::string("VM MB: ") + (vmKB >= 0 ? std::to_string((int)(vmKB / 1024)) : "N/A"));

                const int graphX = panelX + panelW + 12;
                const int graphY = panelY;
                const int graphW = std::max(180, dbgW - graphX - 12);
                const int graphH = std::max(120, (dbgH - graphY - 24) / 2);
                SDL_Rect graphRect{graphX, graphY, graphW, graphH};
                SDL_SetRenderDrawColor(debugRen, 40, 50, 65, 255);
                SDL_RenderDrawRect(debugRen, &graphRect);
                SDL_SetRenderDrawColor(debugRen, 80, 160, 255, 255);
                for (int i = 0; i < graphW; ++i) {
                    int idx = (frameMsHistoryHead + i * (int)frameMsHistory.size() / std::max(1, graphW)) % (int)frameMsHistory.size();
                    float ms = frameMsHistory[idx];
                    float norm = std::clamp(ms / 50.0f, 0.0f, 1.0f);
                    int hPx = (int)std::lround(norm * (graphH - 2));
                    SDL_RenderDrawLine(debugRen, graphX + i, graphY + graphH - 1, graphX + i, graphY + graphH - 1 - hPx);
                }
                DrawText(debugRen, graphX + 8, graphY + 8, 2, "Frame Time History (0-50ms)");
                DrawText(debugRen, graphX + 8, graphY + 28, 2, std::string("Samples: ") + std::to_string((int)frameMsHistory.size()));
                DrawText(debugRen, graphX + 8, graphY + 48, 2, std::string("Current ms: ") + std::to_string((int)std::lround(dt * 1000.0f)));

                const int memGraphY = graphY + graphH + 12;
                const int memGraphH = std::max(90, dbgH - memGraphY - 12);
                SDL_Rect memGraphRect{graphX, memGraphY, graphW, memGraphH};
                SDL_SetRenderDrawColor(debugRen, 40, 50, 65, 255);
                SDL_RenderDrawRect(debugRen, &memGraphRect);
                float maxMem = 1.0f;
                for (float v : memRssHistory) maxMem = std::max(maxMem, v);
                SDL_SetRenderDrawColor(debugRen, 110, 220, 120, 255);
                for (int i = 0; i < graphW; ++i) {
                    int idx = (frameMsHistoryHead + i * (int)memRssHistory.size() / std::max(1, graphW)) % (int)memRssHistory.size();
                    float mem = memRssHistory[idx];
                    float norm = std::clamp(mem / maxMem, 0.0f, 1.0f);
                    int hPx = (int)std::lround(norm * (memGraphH - 2));
                    SDL_RenderDrawLine(debugRen, graphX + i, memGraphY + memGraphH - 1, graphX + i, memGraphY + memGraphH - 1 - hPx);
                }
                DrawText(debugRen, graphX + 8, memGraphY + 8, 2, "Memory RSS History");
                DrawText(debugRen, graphX + 8, memGraphY + 28, 2, std::string("Peak MB: ") + std::to_string((int)std::lround(maxMem)));
            } else {
                DrawText(debugRen, 12, y, 2, "PLAYER STATUS"); y += 24;
                DrawText(debugRen, 12, y, 2, std::string("Position: ") + std::to_string((int)player.x) + ", " + std::to_string((int)player.y)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Velocity: ") + std::to_string((int)player.vx) + ", " + std::to_string((int)player.vy)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Facing: ") + (player.facing < 0 ? "LEFT" : "RIGHT")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Anim: ") + std::to_string(player.anim) + " (" + debugAnimName + ")"); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("Hitbox W/H: ") + std::to_string(player.w) + "/" + std::to_string(player.h)); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("OnGround: ") + (player.onGround ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("InWater: ") + (player.inWater ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("FreeMove: ") + (player.freeMove ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("JumpHeld/WasDown: ") + (player.jumpHeld ? "1" : "0") + "/" + (player.jumpWasDown ? "1" : "0")); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("JumpHoldTime: ") + std::to_string((int)std::lround(player.jumpHoldTime * 1000.0f)) + "ms"); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("JumpBuffer: ") + std::to_string((int)std::lround(player.jumpBufferTime * 1000.0f)) + "ms"); y += 20;
                DrawText(debugRen, 12, y, 2, std::string("DrownTimer: ") + std::to_string((int)std::lround(player.drownTimer * 1000.0f)) + "ms"); y += 20;
            }

            SDL_RenderPresent(debugRen);
        }
        }
        if (!running) break;
        stopReplayRecording(returnToSelect ? "return_to_select" : "level_end");
        audio.unloadLevelMusic();
        if (returnToSelect) {
            if (selectedFromUserMenu) reopenUserLevelMenu = true;
            continue;
        }
    }

    if (blocksTex) SDL_DestroyTexture(blocksTex);
    if (entitiesTex) SDL_DestroyTexture(entitiesTex);
    if (bossesTex) SDL_DestroyTexture(bossesTex);
    if (endSignTex) SDL_DestroyTexture(endSignTex);
    if (pauseTex) SDL_DestroyTexture(pauseTex);
    if (introCardTex) SDL_DestroyTexture(introCardTex);
    if (bgTexWorld1) SDL_DestroyTexture(bgTexWorld1);
    if (bgTexWorld2) SDL_DestroyTexture(bgTexWorld2);
    if (bgTexWorld4) SDL_DestroyTexture(bgTexWorld4);
    if (bgTexWorld5) SDL_DestroyTexture(bgTexWorld5);
    if (bgTexWorld6) SDL_DestroyTexture(bgTexWorld6);
    if (worldTarget) SDL_DestroyTexture(worldTarget);
    if (gameTarget) SDL_DestroyTexture(gameTarget);
    if (debugRen && debugRen != ren) SDL_DestroyRenderer(debugRen);
    if (debugWin && debugWin != win) SDL_DestroyWindow(debugWin);
    ShutdownTextRenderer();
    audio.shutdown();
    sendDiscordTelemetry("shutdown", {
        {"build_uuid", buildUuid},
        {"version", appVersion},
        {"uptime_ms", (long long)SDL_GetTicks()}
    });
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Log("Shutting down");
    CrashReporter::stop();
    SDL_Quit();
    return 0;
