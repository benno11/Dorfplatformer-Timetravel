                    SDL_Rect dstBL = dst;
                    SDL_Rect dstBR = dst;
                    dstTL.x -= wrapW; dstTL.y -= wrapH;
                    dstTR.x += wrapW; dstTR.y -= wrapH;
                    dstBL.x -= wrapW; dstBL.y += wrapH;
                    dstBR.x += wrapW; dstBR.y += wrapH;
                    renderObjFrame(dstTL);
                    renderObjFrame(dstTR);
                    renderObjFrame(dstBL);
                    renderObjFrame(dstBR);
                }
            } else if (!isFastTravelChanger) {
                SDL_SetRenderDrawColor(ren, 120, 220, 120, 255);
                SDL_FRect orc{ entityBaseX - camX, entityBaseY - camY, 32.0f, 32.0f };
                SDL_RenderDrawRectF(ren, &orc);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect top{orc.x, orc.y - wrapH, orc.w, orc.h};
                    SDL_FRect bottom{orc.x, orc.y + wrapH, orc.w, orc.h};
                    SDL_RenderDrawRectF(ren, &top);
                    SDL_RenderDrawRectF(ren, &bottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect left{orc.x - wrapW, orc.y, orc.w, orc.h};
                    SDL_FRect right{orc.x + wrapW, orc.y, orc.w, orc.h};
                    SDL_RenderDrawRectF(ren, &left);
                    SDL_RenderDrawRectF(ren, &right);
                }
                if (renderWrapY && renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect tl{orc.x - wrapW, orc.y - wrapH, orc.w, orc.h};
                    SDL_FRect tr{orc.x + wrapW, orc.y - wrapH, orc.w, orc.h};
                    SDL_FRect bl{orc.x - wrapW, orc.y + wrapH, orc.w, orc.h};
                    SDL_FRect br{orc.x + wrapW, orc.y + wrapH, orc.w, orc.h};
                    SDL_RenderDrawRectF(ren, &tl);
                    SDL_RenderDrawRectF(ren, &tr);
                    SDL_RenderDrawRectF(ren, &bl);
                    SDL_RenderDrawRectF(ren, &br);
                }
            }

            if (showHitboxes) {
                if (isFastTravelChanger) SDL_SetRenderDrawColor(ren, 220, 60, 220, 255);
                else SDL_SetRenderDrawColor(ren, 255, 150, 60, 255);
                SDL_FRect ehb{
                    entityBaseX - camX,
                    entityBaseY - camY,
                    32.0f,
                    32.0f
                };
                SDL_RenderDrawRectF(ren, &ehb);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect top{ehb.x, ehb.y - wrapH, ehb.w, ehb.h};
                    SDL_FRect bottom{ehb.x, ehb.y + wrapH, ehb.w, ehb.h};
                    SDL_RenderDrawRectF(ren, &top);
                    SDL_RenderDrawRectF(ren, &bottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect left{ehb.x - wrapW, ehb.y, ehb.w, ehb.h};
                    SDL_FRect right{ehb.x + wrapW, ehb.y, ehb.w, ehb.h};
                    SDL_RenderDrawRectF(ren, &left);
                    SDL_RenderDrawRectF(ren, &right);
                }

                const int idScale = 1;
                DrawText(ren, (int)std::lround(ehb.x), (int)std::lround(ehb.y) - 10, idScale, obj.id);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    DrawText(ren, (int)std::lround(ehb.x), (int)std::lround(ehb.y - wrapH) - 10, idScale, obj.id);
                    DrawText(ren, (int)std::lround(ehb.x), (int)std::lround(ehb.y + wrapH) - 10, idScale, obj.id);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    DrawText(ren, (int)std::lround(ehb.x - wrapW), (int)std::lround(ehb.y) - 10, idScale, obj.id);
                    DrawText(ren, (int)std::lround(ehb.x + wrapW), (int)std::lround(ehb.y) - 10, idScale, obj.id);
                }
            }
        }

        if (bossState.active && bossesTex && bossNormalFrame) {
            const Frame* bf = (bossState.hurtFlash > 0.0f && bossHurtFrame) ? bossHurtFrame : bossNormalFrame;
            if (bossState.sourceWorld == 7 && bossFinalNormalFrame) {
                bf = bossFinalNormalFrame;
            }
            const bool bossUsesFinalAnimation = (bossState.sourceWorld == 7);
            int dstW = bossUsesFinalAnimation ? 56 : 28;
            int dstH = bossUsesFinalAnimation ? 56 : 28;
            if (bossUsesFinalAnimation && bf) {
                const int srcW = bf->rotated ? bf->rect.h : bf->rect.w;
                const int srcH = bf->rotated ? bf->rect.w : bf->rect.h;
                const double safeW = (double)std::max(1, srcW);
                const double safeH = (double)std::max(1, srcH);
                const double scale = std::max(56.0 / safeW, 56.0 / safeH); // cover 56x56 hitbox
                dstW = std::max(56, (int)std::lround(safeW * scale));
                dstH = std::max(56, (int)std::lround(safeH * scale));
            }
            SDL_Rect dst{
                (int)std::lround((bossState.x - dstW * 0.5f) - camX),
                (int)std::lround((bossState.y - dstH * 0.5f) - camY),
                dstW,
                dstH
            };
            if (!(dst.x + dst.w <= 0 || dst.y + dst.h <= 0 || dst.x >= worldViewW || dst.y >= worldViewH)) {
                if (bossState.sourceWorld == 4 && bossState.rainbowTimer > 0.0f) {
                    const float t = (float)((double)SDL_GetTicksNS() / 1000000000.0 * 6.5);
                    const int r = (int)std::lround(127.0 + 128.0 * std::sin(t));
                    const int g = (int)std::lround(127.0 + 128.0 * std::sin(t + 2.0943951f));
                    const int b = (int)std::lround(127.0 + 128.0 * std::sin(t + 4.1887902f));
                    SDL_SetTextureColorMod(bossesTex, (Uint8)std::clamp(r, 0, 255), (Uint8)std::clamp(g, 0, 255), (Uint8)std::clamp(b, 0, 255));
                } else {
                    SDL_SetTextureColorMod(bossesTex, 255, 255, 255);
                }
                renderFrame(ren, bossesTex, *bf, dst);
                SDL_SetTextureColorMod(bossesTex, 255, 255, 255);
                if (showHitboxes) {
                    SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
                    SDL_FRect hb{(float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h};
                    SDL_RenderDrawRectF(ren, &hb);
                    if (renderWrapY) {
                        const float wrapH = (float)(map.h * map.tileSize);
                        SDL_FRect top{hb.x, hb.y - wrapH, hb.w, hb.h};
                        SDL_FRect bottom{hb.x, hb.y + wrapH, hb.w, hb.h};
                        SDL_RenderDrawRectF(ren, &top);
                        SDL_RenderDrawRectF(ren, &bottom);
                    }
                    if (renderWrapX) {
                        const float wrapW = (float)(map.w * map.tileSize);
                        SDL_FRect left{hb.x - wrapW, hb.y, hb.w, hb.h};
                        SDL_FRect right{hb.x + wrapW, hb.y, hb.w, hb.h};
                        SDL_RenderDrawRectF(ren, &left);
                        SDL_RenderDrawRectF(ren, &right);
                    }
                    if (renderWrapY && renderWrapX) {
                        const float wrapW = (float)(map.w * map.tileSize);
                        const float wrapH = (float)(map.h * map.tileSize);
                        SDL_FRect tl{hb.x - wrapW, hb.y - wrapH, hb.w, hb.h};
                        SDL_FRect tr{hb.x + wrapW, hb.y - wrapH, hb.w, hb.h};
                        SDL_FRect bl{hb.x - wrapW, hb.y + wrapH, hb.w, hb.h};
                        SDL_FRect br{hb.x + wrapW, hb.y + wrapH, hb.w, hb.h};
                        SDL_RenderDrawRectF(ren, &tl);
                        SDL_RenderDrawRectF(ren, &tr);
                        SDL_RenderDrawRectF(ren, &bl);
                        SDL_RenderDrawRectF(ren, &br);
                    }
                }
                if (showDebugView) {
                    const std::string hpText = std::string("BOSS HP: ") + std::to_string(bossState.health) + "/" + std::to_string(bossState.maxHealth);
                    DrawText(ren, std::max(8, dst.x - 8), std::max(8, dst.y - 18), 1, hpText);
                }
            }
        }

        if (!droppedCoins.empty()) {
            const int cycleIndex = (int)((SDL_GetTicks() / 300) % 8);
            const Frame* coinFrame = cycleFrames[cycleIndex];
            for (const auto& c : droppedCoins) {
                SDL_Rect dst{
                    (int)std::lround(c.x - 8.0f - camX),
                    (int)std::lround(c.y - 8.0f - camY),
                    16, 16
                };
                if (dst.x + dst.w <= 0 || dst.y + dst.h <= 0 || dst.x >= worldViewW || dst.y >= worldViewH) continue;
                if (blocksTex && coinFrame) {
                    renderFrame(ren, blocksTex, *coinFrame, dst);
                } else {
                    SDL_SetRenderDrawColor(ren, 255, 220, 40, 255);
                    SDL_RenderFillRect(ren, &dst);
                }
            }
        }

        if (showDemoPath && !demoState.pathTiles.empty()) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            for (size_t i = 0; i < demoState.pathTiles.size(); ++i) {
                const SDL_Point p = demoState.pathTiles[i];
                const bool completed = i < demoState.waypointIndex;
                SDL_SetRenderDrawColor(ren, completed ? 70 : 80, completed ? 80 : 240, completed ? 95 : 120, completed ? 110 : 190);
                SDL_FRect tileRect{
                    p.x * (float)map.tileSize - camX + 4.0f,
                    p.y * (float)map.tileSize - camY + 4.0f,
                    (float)map.tileSize - 8.0f,
                    (float)map.tileSize - 8.0f
                };
                SDL_RenderDrawRectF(ren, &tileRect);
                if (i > 0) {
                    const SDL_Point prev = demoState.pathTiles[i - 1];
                    const bool segmentCompleted = (i - 1) < demoState.waypointIndex;
                    SDL_SetRenderDrawColor(ren, segmentCompleted ? 70 : 80, segmentCompleted ? 80 : 240, segmentCompleted ? 95 : 120, segmentCompleted ? 95 : 180);
                    const float x1 = prev.x * (float)map.tileSize + map.tileSize * 0.5f - camX;
                    const float y1 = prev.y * (float)map.tileSize + map.tileSize * 0.5f - camY;
                    const float x2 = p.x * (float)map.tileSize + map.tileSize * 0.5f - camX;
                    const float y2 = p.y * (float)map.tileSize + map.tileSize * 0.5f - camY;
                    SDL_RenderLine(ren, x1, y1, x2, y2);
                }
            }
            if (!demoState.pathTiles.empty()) {
                const SDL_Point start = demoState.pathTiles.front();
                const SDL_Point finish = demoState.pathTiles.back();
                SDL_SetRenderDrawColor(ren, 90, 180, 255, 220);
                SDL_FRect sRect{
                    start.x * (float)map.tileSize - camX + 1.0f,
                    start.y * (float)map.tileSize - camY + 1.0f,
                    (float)map.tileSize - 2.0f,
                    (float)map.tileSize - 2.0f
                };
                SDL_RenderDrawRectF(ren, &sRect);
                SDL_SetRenderDrawColor(ren, 255, 120, 90, 220);
                SDL_FRect fRect{
                    finish.x * (float)map.tileSize - camX + 1.0f,
                    finish.y * (float)map.tileSize - camY + 1.0f,
                    (float)map.tileSize - 2.0f,
                    (float)map.tileSize - 2.0f
                };
                SDL_RenderDrawRectF(ren, &fRect);
            }
            if (demoState.waypointIndex < demoState.pathTiles.size()) {
                const SDL_Point wp = demoState.pathTiles[demoState.waypointIndex];
                SDL_SetRenderDrawColor(ren, 255, 240, 80, 220);
                SDL_FRect wpRect{
                    wp.x * (float)map.tileSize - camX + 2.0f,
                    wp.y * (float)map.tileSize - camY + 2.0f,
                    (float)map.tileSize - 4.0f,
                    (float)map.tileSize - 4.0f
                };
                SDL_RenderDrawRectF(ren, &wpRect);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            DrawText(ren, 12, 82, 2, "PATH S->F");
        }

        SDL_FRect pr{ player.x - camX, player.y - camY, (float)player.w, (float)player.h };
        if (renderWrapX) {
            const float wrapW = (float)(map.w * map.tileSize);
            while (pr.x < -wrapW * 0.5f) pr.x += wrapW;
            while (pr.x >  wrapW * 0.5f) pr.x -= wrapW;
        }
        if (renderWrapY) {
            const float wrapH = (float)(map.h * map.tileSize);
            while (pr.y < -wrapH * 0.5f) pr.y += wrapH;
            while (pr.y >  wrapH * 0.5f) pr.y -= wrapH;
        }
        {
            const std::vector<const Frame*>* seq = &animIdleFrames;
            switch (player.anim) {
                case ANIM_WALK: seq = &animWalkFrames; break;
                case ANIM_JUMP: seq = &animJumpFrames; break;
                case ANIM_FALL: seq = &animFallFrames; break;
                case ANIM_CROUCH: seq = &animCrouchFrames; break;
                case ANIM_SKID: seq = &animSkidFrames; break;
                case ANIM_HURT: seq = &animHurtFrames; break;
                case ANIM_DEATH: seq = &animDeathFrames; break;
                default: seq = &animIdleFrames; break;
            }

            const Frame* renderFramePtr = nullptr;
            int frameCount = (int)seq->size();
            if (frameCount > 0) {
                float fps = animCfg.fps > 0.0f ? animCfg.fps : 20.0f;
                fps = std::max(20.0f, fps);
                int idx = (int)(player.animTime * fps) % frameCount;
                renderAnimFrameIndex = idx;
                renderFramePtr = (*seq)[idx];
                if (player.anim == ANIM_IDLE && idx < (int)animCfg.idle.size()) {
                    renderFrameName = animCfg.idle[idx];
                } else if (player.anim == ANIM_WALK && idx < (int)animCfg.walk.size()) {
                    renderFrameName = animCfg.walk[idx];
                } else if (player.anim == ANIM_JUMP && idx < (int)animCfg.jump.size()) {
                    renderFrameName = animCfg.jump[idx];
                } else if (player.anim == ANIM_FALL && idx < (int)animCfg.fall.size()) {
                    renderFrameName = animCfg.fall[idx];
                } else if (player.anim == ANIM_CROUCH && idx < (int)animCfg.crouch.size()) {
                    renderFrameName = animCfg.crouch[idx];
                } else if (player.anim == ANIM_SKID && idx < (int)animCfg.skid.size()) {
                    renderFrameName = animCfg.skid[idx];
                } else if (player.anim == ANIM_HURT && idx < (int)animCfg.hurt.size()) {
                    renderFrameName = animCfg.hurt[idx];
                } else if (player.anim == ANIM_DEATH && idx < (int)animCfg.death.size()) {
                    renderFrameName = animCfg.death[idx];
                }
            }

            if (playerTex && renderFramePtr) {
                if (playerInvincibleTimer > 0.0f) {
                    const float phase = (float)(SDL_GetTicksNS() / 1000000000.0) * 14.0f;
                    const float wave = 0.5f + 0.5f * std::sin(phase);
                    const Uint8 alpha = (Uint8)std::clamp((int)std::lround(95.0f + 160.0f * wave), 70, 255);
                    SDL_SetTextureAlphaMod(playerTex, alpha);
                } else {
                    SDL_SetTextureAlphaMod(playerTex, 255);
                }
                SDL_Rect dst{
                    (int)pr.x,
                    (int)(pr.y + pr.h - pr.h),
                    (int)pr.w,
                    (int)pr.h
                };
                SDL_RendererFlip flip = (player.facing < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                renderFrameEx(ren, playerTex, *renderFramePtr, dst, flip);
                if (renderWrapY) {
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTop = dst;
                    SDL_Rect dstBottom = dst;
                    dstTop.y -= wrapH;
                    dstBottom.y += wrapH;
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstTop, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstBottom, flip);
                }
                if (renderWrapX) {
                    const int wrapW = map.w * map.tileSize;
                    SDL_Rect dstLeft = dst;
                    SDL_Rect dstRight = dst;
                    dstLeft.x -= wrapW;
                    dstRight.x += wrapW;
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstLeft, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstRight, flip);
                }
                if (renderWrapY && renderWrapX) {
                    const int wrapW = map.w * map.tileSize;
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTL = dst;
                    SDL_Rect dstTR = dst;
                    SDL_Rect dstBL = dst;
                    SDL_Rect dstBR = dst;
                    dstTL.x -= wrapW; dstTL.y -= wrapH;
                    dstTR.x += wrapW; dstTR.y -= wrapH;
                    dstBL.x -= wrapW; dstBL.y += wrapH;
                    dstBR.x += wrapW; dstBR.y += wrapH;
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstTL, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstTR, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstBL, flip);
                    renderFrameEx(ren, playerTex, *renderFramePtr, dstBR, flip);
                }
                SDL_SetTextureAlphaMod(playerTex, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 200, 200, 230, 255);
                SDL_RenderFillRectF(ren, &pr);
                SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                SDL_RenderDrawRectF(ren, &pr);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect prTop{ pr.x, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prBottom{ pr.x, pr.y + wrapH, pr.w, pr.h };
                    SDL_RenderFillRectF(ren, &prTop);
                    SDL_RenderFillRectF(ren, &prBottom);
                    SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                    SDL_RenderDrawRectF(ren, &prTop);
                    SDL_RenderDrawRectF(ren, &prBottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect prLeft{ pr.x - wrapW, pr.y, pr.w, pr.h };
                    SDL_FRect prRight{ pr.x + wrapW, pr.y, pr.w, pr.h };
                    SDL_RenderFillRectF(ren, &prLeft);
                    SDL_RenderFillRectF(ren, &prRight);
                    SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                    SDL_RenderDrawRectF(ren, &prLeft);
                    SDL_RenderDrawRectF(ren, &prRight);
                }
                if (renderWrapY && renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect prTL{ pr.x - wrapW, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prTR{ pr.x + wrapW, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prBL{ pr.x - wrapW, pr.y + wrapH, pr.w, pr.h };
                    SDL_FRect prBR{ pr.x + wrapW, pr.y + wrapH, pr.w, pr.h };
                    SDL_RenderFillRectF(ren, &prTL);
                    SDL_RenderFillRectF(ren, &prTR);
                    SDL_RenderFillRectF(ren, &prBL);
                    SDL_RenderFillRectF(ren, &prBR);
                    SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
                    SDL_RenderDrawRectF(ren, &prTL);
                    SDL_RenderDrawRectF(ren, &prTR);
                    SDL_RenderDrawRectF(ren, &prBL);
                    SDL_RenderDrawRectF(ren, &prBR);
                }
            }
        }

        SDL_SetRenderTarget(ren, gameTarget);
        SDL_Rect worldDst{0, 0, screenW, screenH};
        SDL_RenderTexture(ren, worldTarget, nullptr, &worldDst);

        // HUD: coin/timer/score
        int hudSeconds = levelCompleteActive ? levelCompleteSnapshotSeconds : (int)levelTimerSeconds;
        int mins = hudSeconds / 60;
        int secs = hudSeconds % 60;
        std::ostringstream timerText;
        timerText << mins << "," << std::setw(2) << std::setfill('0') << secs;
        const int hudSlideOutX = -(int)std::lround(levelCompleteUiLerp * 260.0f);
        DrawText(ren, 16 + hudSlideOutX, 16, 2, "COINS");
        DrawText(ren, 94 + hudSlideOutX, 16, 2, std::to_string(levelManager.coinCount()));
        DrawText(ren, 16 + hudSlideOutX, 48, 2, "TIME");
        DrawText(ren, 94 + hudSlideOutX, 48, 2, timerText.str());
        DrawText(ren, 16 + hudSlideOutX, 80, 2, "SCORE");
        DrawText(ren, 94 + hudSlideOutX, 80, 2, std::to_string(scoreCount));

        // HUD: lives counter (bottom-left).
        DrawText(ren, 16 + hudSlideOutX, screenH - 32, 2, "LIVES");
        DrawText(ren, 94 + hudSlideOutX, screenH - 32, 2, std::to_string(livesCount));
        if (levelReloadTitleTimer > 0.0f) {
            const float showT = std::clamp(levelReloadTitleTimer / 2.0f, 0.0f, 1.0f);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 8, 10, 14, (Uint8)std::lround(140.0f * showT));
            SDL_FRect titleBackdrop{(float)(screenW / 2 - 300), 18.0f, 600.0f, 52.0f};
            SDL_RenderFillRect(ren, &titleBackdrop);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, (Uint8)std::lround(255.0f * std::min(1.0f, showT * 2.0f)));
            const int titleScale = 2;
            const int titleW = MeasureTextWidth(titleScale, currentFancyLevelName);
            DrawText(ren, screenW / 2 - titleW / 2, 32, titleScale, currentFancyLevelName);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        if (paused) {
            const float uiButtonScale = std::clamp((float)uiScalePercent / 100.0f, 0.5f, 2.0f);
            auto scaleRectCentered = [&](const SDL_Rect& in) -> SDL_Rect {
                const float cx = (float)in.x + (float)in.w * 0.5f;
                const float cy = (float)in.y + (float)in.h * 0.5f;
                const int nw = std::max(1, (int)std::lround((float)in.w * uiButtonScale));
                const int nh = std::max(1, (int)std::lround((float)in.h * uiButtonScale));
                return SDL_Rect{
                    (int)std::lround(cx - (float)nw * 0.5f),
                    (int)std::lround(cy - (float)nh * 0.5f),
                    nw,
                    nh
                };
            };
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 10, 10, 14, 180);
            SDL_Rect overlay{0, 0, screenW, screenH};
            SDL_RenderFillRect(ren, &overlay);

            bool hasPanel = pauseTex && pauseFrames.count("Panel") != 0;
            bool hasContinue = pauseTex && pauseFrames.count("Continuebtn") != 0;
            bool hasRestart = pauseTex && pauseFrames.count("Retartbtn") != 0;
            bool hasExit = pauseTex && pauseFrames.count("exitbtn") != 0;

            if (hasPanel && hasContinue && hasRestart && hasExit) {
                const Frame& panelFrame = pauseFrames["Panel"];
                SDL_Rect panelDst = scaleRectCentered(SDL_Rect{
                    screenW / 2 - panelFrame.rect.w / 2,
                    screenH / 2 - panelFrame.rect.h / 2,
                    panelFrame.rect.w,
                    panelFrame.rect.h
                });
                renderFrame(ren, pauseTex, panelFrame, panelDst);

                const Frame& continueFrame = pauseFrames["Continuebtn"];
                const Frame& restartFrame = pauseFrames["Retartbtn"];
                const Frame& exitFrame = pauseFrames["exitbtn"];

                int contW = std::max(1, (int)std::lround((continueFrame.rotated ? continueFrame.rect.h : continueFrame.rect.w) * uiButtonScale));
                int contH = std::max(1, (int)std::lround((continueFrame.rotated ? continueFrame.rect.w : continueFrame.rect.h) * uiButtonScale));
                int restartW = std::max(1, (int)std::lround((restartFrame.rotated ? restartFrame.rect.h : restartFrame.rect.w) * uiButtonScale));
                int restartH = std::max(1, (int)std::lround((restartFrame.rotated ? restartFrame.rect.w : restartFrame.rect.h) * uiButtonScale));
                int exitW = std::max(1, (int)std::lround((exitFrame.rotated ? exitFrame.rect.h : exitFrame.rect.w) * uiButtonScale));
                int exitH = std::max(1, (int)std::lround((exitFrame.rotated ? exitFrame.rect.w : exitFrame.rect.h) * uiButtonScale));

                int spacing = std::max(8, (int)std::lround(18.0f * uiButtonScale));
                int totalW = contW + restartW + exitW + spacing * 2;
                int startX = screenW / 2 - totalW / 2;
                int centerY = screenH / 2 + (int)std::lround(30.0f * uiButtonScale);

                SDL_Rect contDst{startX, centerY - contH / 2, contW, contH};
                SDL_Rect restartDst{startX + contW + spacing, centerY - restartH / 2, restartW, restartH};
                SDL_Rect exitDst{startX + contW + spacing + restartW + spacing, centerY - exitH / 2, exitW, exitH};
                pauseBtnContinue = contDst;
                pauseBtnRestart = restartDst;
                pauseBtnExit = exitDst;

                renderFrame(ren, pauseTex, continueFrame, contDst);
                renderFrame(ren, pauseTex, restartFrame, restartDst);
                renderFrame(ren, pauseTex, exitFrame, exitDst);

                SDL_SetRenderDrawColor(ren, 255, 255, 255, 200);
                SDL_Rect highlight = (pauseSelection == 0) ? contDst : (pauseSelection == 1 ? restartDst : exitDst);
                SDL_RenderDrawRect(ren, &highlight);
            } else {
                SDL_Rect panel = scaleRectCentered(SDL_Rect{screenW / 2 - 140, screenH / 2 - 90, 280, 180});
                SDL_SetRenderDrawColor(ren, 30, 30, 38, 230);
                SDL_RenderFillRect(ren, &panel);
                SDL_SetRenderDrawColor(ren, 80, 90, 110, 255);
                SDL_RenderDrawRect(ren, &panel);

                SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
                DrawText(ren, screenW / 2 - MeasureTextWidth(3, "PAUSED") / 2, panel.y + (int)std::lround(22.0f * uiButtonScale), 3, "PAUSED");

                SDL_Rect resumeBtn = scaleRectCentered(SDL_Rect{screenW / 2 - 140, screenH / 2 + 10, 100, 36});
                SDL_Rect restartBtn = scaleRectCentered(SDL_Rect{screenW / 2 - 50, screenH / 2 + 10, 100, 36});
                SDL_Rect quitBtn = scaleRectCentered(SDL_Rect{screenW / 2 + 40, screenH / 2 + 10, 100, 36});
                const int labelLineH = std::max(10, (int)std::lround(16.0f * uiButtonScale));
                pauseBtnContinue = resumeBtn;
                pauseBtnRestart = restartBtn;
                pauseBtnExit = quitBtn;

                SDL_SetRenderDrawColor(ren, pauseSelection == 0 ? 70 : 45, pauseSelection == 0 ? 120 : 70, pauseSelection == 0 ? 170 : 90, 255);
                SDL_RenderFillRect(ren, &resumeBtn);
