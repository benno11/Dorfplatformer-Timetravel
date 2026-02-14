        }
        const bool forceBossCameraActive = forceBossCamera && !lockCameraToEndSign;
        const float cameraTargetX = forceBossCameraActive
            ? forcedBossCameraX
            : (lockCameraToEndSign ? endSignState.objectX : (player.x + player.w * 0.5f));
        const float cameraTargetY = forceBossCameraActive
            ? forcedBossCameraY
            : (player.y + player.h * 0.5f);
        const float freeCamX = cameraTargetX - worldViewW * 0.5f;
        float camX = freeCamX;
        const float freeCamY = cameraTargetY - worldViewH * 0.5f;
        float camY = freeCamY;
        float maxCamX = map.w * map.tileSize - worldViewW - map.tileSize;
        float maxCamY = map.h * map.tileSize - worldViewH;
        const float clampedCamX = std::clamp(freeCamX, (float)map.tileSize, std::max((float)map.tileSize, maxCamX));
        const float clampXTarget = cameraWrapX ? 0.0f : 1.0f;
        const float clampLerpSpeed = 7.5f;
        const float clampBlendStep = 1.0f - std::exp(-clampLerpSpeed * std::max(0.0f, dt));
        if (cameraSmoothingSuppressTimer > 0.0f) {
            camXClampBlend = clampXTarget;
        } else {
            camXClampBlend += (clampXTarget - camXClampBlend) * clampBlendStep;
        }
        camX = freeCamX * (1.0f - camXClampBlend) + clampedCamX * camXClampBlend;
        const float clampedCamY = std::clamp(freeCamY, (float)map.tileSize, std::max((float)map.tileSize, maxCamY));
        const float clampTarget = cameraWrapY ? 0.0f : 1.0f;
        if (cameraSmoothingSuppressTimer > 0.0f) {
            camYClampBlend = clampTarget;
        } else {
            camYClampBlend += (clampTarget - camYClampBlend) * clampBlendStep;
        }
        camY = freeCamY * (1.0f - camYClampBlend) + clampedCamY * camYClampBlend;
        if (lockCameraToEndSign) {
            if (!endSignCameraLocked) {
                endSignCameraLocked = true;
                endSignCameraX = camX;
            }
            camX = endSignCameraX;
        } else {
            endSignCameraLocked = false;
        }
        if (levelCompleteActive) {
            if (!levelCompleteCameraLocked) {
                levelCompleteCameraLocked = true;
                levelCompleteCameraX = camX;
                levelCompleteCameraY = camY;
            }
            camX = levelCompleteCameraX;
            camY = levelCompleteCameraY;
        } else {
            levelCompleteCameraLocked = false;
        }
        // Snap render camera to pixel grid to avoid subpixel shimmer across all worlds.
        camX = std::round(camX);
        camY = std::round(camY);

        SDL_SetRenderTarget(ren, worldTarget);
        const int currentWorldId = levelManager.worldId();
        if (currentWorldId == 5) {
            const SDL_Color top{0x66, 0xea, 0xff, 0xff};    // #66eaff
            const SDL_Color bottom{0xc0, 0x68, 0x72, 0xff}; // #c06872
            for (int y = 0; y < worldViewH; ++y) {
                const float t = (worldViewH > 1) ? ((float)y / (float)(worldViewH - 1)) : 0.0f;
                const Uint8 r = (Uint8)std::lround((float)top.r + ((float)bottom.r - (float)top.r) * t);
                const Uint8 g = (Uint8)std::lround((float)top.g + ((float)bottom.g - (float)top.g) * t);
                const Uint8 b = (Uint8)std::lround((float)top.b + ((float)bottom.b - (float)top.b) * t);
                SDL_SetRenderDrawColor(ren, r, g, b, 255);
                SDL_RenderLine(ren, 0.0f, (float)y, (float)worldViewW, (float)y);
            }
        } else {
            SDL_SetRenderDrawColor(ren, 221, 248, 255, 255); // #ddf8ff
            SDL_RenderClear(ren);
        }

        // World 3 uses a full-screen block pattern from DF_Blocks (3.1..3.10).
        bool renderedWorld3PatternBg = false;
        if (currentWorldId == 3 && blocksTex) {
            const Frame* fallbackFrame = nullptr;
            for (const Frame* f : world3PatternFrames) {
                if (f) {
                    fallbackFrame = f;
                    break;
                }
            }
            if (fallbackFrame) {
                const int blockW = std::max(16, map.tileSize * 2);
                const int blockH = std::max(16, map.tileSize * 2);
                const float world3BgParallaxX = 0.35f;
                const float world3BgParallaxY = 0.25f;
                const float bgCamX = camX * world3BgParallaxX;
                const float bgCamY = camY * world3BgParallaxY;
                const int bgOffsetX = (int)std::floor(bgCamX) % blockW;
                const int bgOffsetY = (int)std::floor(bgCamY) % blockH;
                const int worldBaseGX = (int)std::floor(bgCamX / (float)blockW);
                const int worldBaseGY = (int)std::floor(bgCamY / (float)blockH);
                SDL_SetTextureColorMod(blocksTex, 150, 150, 150);
                for (int y = -bgOffsetY - blockH, gy = worldBaseGY - 1; y < worldViewH + blockH; y += blockH, ++gy) {
                    for (int x = -bgOffsetX - blockW, gx = worldBaseGX - 1; x < worldViewW + blockW; x += blockW, ++gx) {
                        unsigned int hash = (unsigned int)(gx * 73856093u) ^ (unsigned int)(gy * 19349663u) ^ 0x9e3779b9u;
                        hash ^= (unsigned int)(currentLevelId * 83492791u);
                        const int frameIndex = (int)(hash % 10u);
                        const Frame* frame = world3PatternFrames[frameIndex];
                        if (!frame) frame = fallbackFrame;
                        const int dstLeft = std::max(0, x);
                        const int dstTop = std::max(0, y);
                        const int dstRight = std::min(worldViewW, x + blockW);
                        const int dstBottom = std::min(worldViewH, y + blockH);
                        if (dstLeft >= dstRight || dstTop >= dstBottom) continue;
                        SDL_Rect dst{dstLeft, dstTop, dstRight - dstLeft, dstBottom - dstTop};
                        renderFrame(ren, blocksTex, *frame, dst);
                    }
                }
                SDL_SetTextureColorMod(blocksTex, 255, 255, 255);
                renderedWorld3PatternBg = true;
            }
        }

        // World 6 uses an animated full-screen background instead of parallax.
        bool renderedWorld6AnimatedBg = false;
        if (currentWorldId == 6 && bgTexWorld6 && !bgAnimFramesWorld6.empty()) {
            const int animIndex = (int)((SDL_GetTicks() / 120) % (Uint32)bgAnimFramesWorld6.size());
            const Frame& animFrame = bgAnimFramesWorld6[animIndex];
            const SDL_Rect fullDst{0, 0, worldViewW, worldViewH};
            renderFrame(ren, bgTexWorld6, animFrame, fullDst);
            renderedWorld6AnimatedBg = true;
        }
        if (!renderedWorld3PatternBg && !renderedWorld6AnimatedBg) {
            RenderParallaxBackground(
                ren,
                currentWorldId,
                camX,
                camY,
                map.w,
                map.h,
                map.tileSize,
                worldViewW,
                worldViewH,
                parallaxLayerScales,
                ParallaxWorldAssets{bgTexWorld1, &bgFrameByNameWorld1, &bgFrameListWorld1},
                ParallaxWorldAssets{bgTexWorld2, &bgFrameByNameWorld2, &bgFrameListWorld2},
                ParallaxWorldAssets{bgTexWorld4, &bgFrameByNameWorld4, &bgFrameListWorld4},
                ParallaxWorldAssets{bgTexWorld5, &bgFrameByNameWorld5, &bgFrameListWorld5}
            );
        }
        if (currentWorldId == 5) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            const SDL_Color top{0x66, 0xea, 0xff, 0x58};    // #66eaff
            const SDL_Color bottom{0xc0, 0x68, 0x72, 0x72}; // #c06872
            for (int y = 0; y < worldViewH; ++y) {
                const float t = (worldViewH > 1) ? ((float)y / (float)(worldViewH - 1)) : 0.0f;
                const Uint8 r = (Uint8)std::lround((float)top.r + ((float)bottom.r - (float)top.r) * t);
                const Uint8 g = (Uint8)std::lround((float)top.g + ((float)bottom.g - (float)top.g) * t);
                const Uint8 b = (Uint8)std::lround((float)top.b + ((float)bottom.b - (float)top.b) * t);
                const Uint8 a = (Uint8)std::lround((float)top.a + ((float)bottom.a - (float)top.a) * t);
                SDL_SetRenderDrawColor(ren, r, g, b, a);
                SDL_RenderLine(ren, 0.0f, (float)y, (float)worldViewW, (float)y);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        map.renderBgDebug(ren, camX, camY);

        const int tileMinX = renderWrapX
            ? ((int)std::floor(camX / map.tileSize) - 1)
            : std::max(0, (int)std::floor(camX / map.tileSize) - 1);
        const int tileMaxX = renderWrapX
            ? ((int)std::floor((camX + worldViewW) / map.tileSize) + 1)
            : std::min(map.w - 1, (int)std::floor((camX + worldViewW) / map.tileSize) + 1);
        const int tileMinY = renderWrapY
            ? ((int)std::floor(camY / map.tileSize) - 1)
            : std::max(0, (int)std::floor(camY / map.tileSize) - 1);
        const int tileMaxY = renderWrapY
            ? ((int)std::floor((camY + worldViewH) / map.tileSize) + 1)
            : std::min(map.h - 1, (int)std::floor((camY + worldViewH) / map.tileSize) + 1);

        // Tile textures (DF_Blocks)
        if (blocksTex && !blocksFrameList.empty()) {
            auto frameByName = [&](const std::string& key) -> const Frame* {
                auto it = blocksFrameByName.find(key);
                if (it != blocksFrameByName.end()) return &it->second;
                auto itPng = blocksFrameByName.find(key + ".png");
                if (itPng != blocksFrameByName.end()) return &itPng->second;
                return nullptr;
            };
            const float tileSizeF = (float)map.tileSize;
            const int cycleIndex = (int)((SDL_GetTicks() / 300) % 8);
            const int tileStartY = renderWrapY ? tileMinY : std::max(1, tileMinY);
            for (int y = tileStartY; y <= tileMaxY; y++) {
                int mapY = y;
                if (renderWrapY) {
                    mapY %= map.h;
                    if (mapY < 0) mapY += map.h;
                }
                const int row = mapY * map.w;
                const int screenY = (int)(y * tileSizeF - camY);
                for (int x = tileMinX; x <= tileMaxX; x++) {
                    int mapX = x;
                    if (renderWrapX) {
                        mapX %= map.w;
                        if (mapX < 0) mapX += map.w;
                    }
                    const unsigned short id = map.tileIds[row + mapX];
                    if (id == 0) continue;

                    const Frame* frame = nullptr;
                    if (id == 24) {
                        frame = cycleFrames[cycleIndex];
                    } else {
                        auto tileIt = tileFrameById.find((int)id);
                        if (tileIt != tileFrameById.end()) {
                            frame = frameByName(tileIt->second);
                        }
                    }
                    if (!frame && levelManager.worldId() == 3 && id >= 3 && id <= 11) {
                        const unsigned int h = (unsigned int)mapX * 73856093u ^
                                               (unsigned int)mapY * 19349663u ^
                                               (unsigned int)id * 83492791u;
                        const int v = (int)(h % 10u) + 1; // 1..10
                        const std::string key = std::string("3.") + std::to_string(v);
                        frame = frameByName(key);
                    }
                    if (!frame) {
                        frame = blocksFrameById[id];
                    }
                    if (!frame) continue;

                    SDL_Rect dst{(int)(x * tileSizeF - camX), screenY, map.tileSize, map.tileSize};
                    renderFrame(ren, blocksTex, *frame, dst);
                }
            }
        } else {
            map.renderWater(ren, camX, camY);
            map.renderSemiSolid(ren, camX, camY);
            map.renderSolid(ren, camX, camY);
        }

        std::string debugAnimName = "IDLE";
        std::string renderFrameName;
        int renderAnimFrameIndex = -1;
        switch (player.anim) {
            case 1: debugAnimName = "WALK"; break;
            case 2: debugAnimName = "JUMP"; break;
            case 3: debugAnimName = "FALL"; break;
            case 4: debugAnimName = "CROUCH"; break;
            case 5: debugAnimName = "SKID"; break;
            case 6: debugAnimName = "HURT"; break;
            case 7: debugAnimName = "DEATH"; break;
            default: debugAnimName = "IDLE"; break;
        }

        if (showHitboxes || showPlayerHitbox) {
            if (showHitboxes) {
                // Tile hitboxes
                solidHitboxes.clear();
                semiHitboxes.clear();
                waterHitboxes.clear();
                airDebugHitboxes.clear();
                int visibleTiles = (tileMaxX - tileMinX + 1) * (tileMaxY - tileMinY + 1);
                if ((int)solidHitboxes.capacity() < visibleTiles) solidHitboxes.reserve(visibleTiles);
                if ((int)semiHitboxes.capacity() < visibleTiles) semiHitboxes.reserve(visibleTiles);
                if ((int)waterHitboxes.capacity() < visibleTiles) waterHitboxes.reserve(visibleTiles);
                if ((int)airDebugHitboxes.capacity() < visibleTiles) airDebugHitboxes.reserve(visibleTiles);
                const float tileSizeF = (float)map.tileSize;
                constexpr int kUpdateThreads = 3;
                struct HitboxBuckets {
                    std::vector<SDL_FRect> solid;
                    std::vector<SDL_FRect> semi;
                    std::vector<SDL_FRect> water;
                    std::vector<SDL_FRect> air;
                };
                std::array<HitboxBuckets, kUpdateThreads> buckets;
                std::array<std::thread, kUpdateThreads> workers;
                const int totalRows = tileMaxY - tileMinY + 1;
                for (int t = 0; t < kUpdateThreads; ++t) {
                    workers[t] = std::thread([&, t]() {
                        int yStart = tileMinY + (totalRows * t) / kUpdateThreads;
                        int yEnd = tileMinY + (totalRows * (t + 1)) / kUpdateThreads - 1;
                        if (yStart > yEnd) return;
                        int rows = yEnd - yStart + 1;
                        int cap = rows * (tileMaxX - tileMinX + 1);
                        buckets[t].solid.reserve(cap / 2);
                        buckets[t].semi.reserve(cap / 4);
                        buckets[t].water.reserve(cap / 4);
                        buckets[t].air.reserve(cap / 4);
                        for (int y = yStart; y <= yEnd; y++) {
                            const float screenY = y * tileSizeF - camY;
                            int mapY = y;
                            if (renderWrapY) {
                                mapY %= map.h;
                                if (mapY < 0) mapY += map.h;
                            }
                            const int row = mapY * map.w;
                            for (int x = tileMinX; x <= tileMaxX; x++) {
                                int mapX = x;
                                if (renderWrapX) {
                                    mapX %= map.w;
                                    if (mapX < 0) mapX += map.w;
                                }
                                int idx = row + mapX;
                                const bool isSolid = map.solid[idx] != 0;
                                const bool isSemi = map.semisolid[idx] != 0;
                                const bool isWater = map.water[idx] != 0;
                                if (!isSolid && !isSemi && !isWater && map.tileIds[idx] == 2) continue;

                                SDL_FRect rc{ x * tileSizeF - camX, screenY, tileSizeF, tileSizeF };
                                if (isSolid) {
                                    buckets[t].solid.push_back(rc);
                                    continue;
                                }
                                if (isSemi) {
                                    buckets[t].semi.push_back(rc);
                                    continue;
                                }
                                if (isWater) {
                                    buckets[t].water.push_back(rc);
                                    continue;
                                }
                                int id = (int)map.tileIds[idx];
                                if (id != 2) buckets[t].air.push_back(rc);
                            }
                        }
                    });
                }
                for (auto& th : workers) th.join();
                for (int t = 0; t < kUpdateThreads; ++t) {
                    solidHitboxes.insert(solidHitboxes.end(), buckets[t].solid.begin(), buckets[t].solid.end());
                    semiHitboxes.insert(semiHitboxes.end(), buckets[t].semi.begin(), buckets[t].semi.end());
                    waterHitboxes.insert(waterHitboxes.end(), buckets[t].water.begin(), buckets[t].water.end());
                    airDebugHitboxes.insert(airDebugHitboxes.end(), buckets[t].air.begin(), buckets[t].air.end());
                }
                if (!solidHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
                    SDL_RenderDrawRectsF(ren, solidHitboxes.data(), (int)solidHitboxes.size());
                }
                if (!semiHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 120, 220, 255, 255);
                    SDL_RenderDrawRectsF(ren, semiHitboxes.data(), (int)semiHitboxes.size());
                }
                if (!waterHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 60, 120, 220, 255);
                    SDL_RenderDrawRectsF(ren, waterHitboxes.data(), (int)waterHitboxes.size());
                }
                if (!airDebugHitboxes.empty()) {
                    SDL_SetRenderDrawColor(ren, 180, 180, 180, 255);
                    SDL_RenderDrawRectsF(ren, airDebugHitboxes.data(), (int)airDebugHitboxes.size());
                }
            }

            // Player hitbox
            if (showPlayerHitbox) {
                SDL_SetRenderDrawColor(ren, 255, 200, 80, 255);
                float playerHitboxScreenY = player.y - camY;
                if (playerHitboxScreenY < 0.0f) playerHitboxScreenY = 0.0f;
                SDL_FRect pr{ player.x - camX, playerHitboxScreenY, (float)player.w, (float)player.h };
                SDL_RenderDrawRectF(ren, &pr);
                if (renderWrapY) {
                    const float wrapH = (float)(map.h * map.tileSize);
                    SDL_FRect prTop{ pr.x, pr.y - wrapH, pr.w, pr.h };
                    SDL_FRect prBottom{ pr.x, pr.y + wrapH, pr.w, pr.h };
                    SDL_RenderDrawRectF(ren, &prTop);
                    SDL_RenderDrawRectF(ren, &prBottom);
                }
                if (renderWrapX) {
                    const float wrapW = (float)(map.w * map.tileSize);
                    SDL_FRect prLeft{ pr.x - wrapW, pr.y, pr.w, pr.h };
                    SDL_FRect prRight{ pr.x + wrapW, pr.y, pr.w, pr.h };
                    SDL_RenderDrawRectF(ren, &prLeft);
                    SDL_RenderDrawRectF(ren, &prRight);
                }
            }
        }


        for (int objIdx = 0; objIdx < (int)objects.size(); ++objIdx) {
            const auto& obj = objects[objIdx];
            int objId = 0;
            try { objId = std::stoi(obj.id); } catch (...) { objId = 0; }
            const bool isFastTravelChanger = (objId >= 57 && objId <= 61);
            const bool isBumper = (obj.id == "46");
            const bool isEndSign = (obj.id == "67");
            float entityBaseX = obj.x - 16.0f;
            float entityBaseY = obj.y - 16.0f;
            if (renderWrapX) {
                const float wrapW = (float)(map.w * map.tileSize);
                // Keep the primary entity render closest to camera X for stable wrapping.
                while ((entityBaseX - camX) < -wrapW * 0.5f) entityBaseX += wrapW;
                while ((entityBaseX - camX) >  wrapW * 0.5f) entityBaseX -= wrapW;
            }
            if (renderWrapY) {
                const float wrapH = (float)(map.h * map.tileSize);
                // Keep the primary entity render closest to camera Y for stable wrapping.
                while ((entityBaseY - camY) < -wrapH * 0.5f) entityBaseY += wrapH;
                while ((entityBaseY - camY) >  wrapH * 0.5f) entityBaseY -= wrapH;
            }
            const Frame* of = nullptr;
            SDL_Texture* objectTex = entitiesTex;
            bool objectTypeKnown = false;
            if (isEndSign) {
                objectTex = endSignTex;
                std::string key = "SignPost9";
                if (endSignState.present && endSignState.triggered) {
                    if (endSignState.phase == EndSignPhase::PlayerForward ||
                        endSignState.phase == EndSignPhase::PlayerBackward ||
                        endSignState.phase == EndSignPhase::TriggerComplete ||
                        endSignState.phase == EndSignPhase::Done) {
                        key = std::string("SignPostPlayer1.") + std::to_string(std::clamp(endSignState.playerFrame, 1, 7));
                    } else {
                        key = std::string("SignPost") + std::to_string(std::clamp(endSignState.signFrame, 9, 15));
                    }
                }
                auto sit = endSignFrames.find(key);
                if (sit == endSignFrames.end()) sit = endSignFrames.find(key + ".png");
                if (sit != endSignFrames.end()) {
                    of = &sit->second;
                    objectTypeKnown = true;
                }
            } else {
                std::string frameKey = obj.id;
                if (obj.id == "46" && activeBumperIndices.find(objIdx) != activeBumperIndices.end()) {
                    frameKey = "Bumper";
                }
                auto mapIt = entityFrameKeyByObjectId.find(obj.id);
                if (mapIt != entityFrameKeyByObjectId.end()) frameKey = mapIt->second;

                auto it = entitiesFrameByName.find(frameKey);
                if (it != entitiesFrameByName.end()) {
                    of = &it->second;
                    objectTypeKnown = true;
                } else {
                    std::string pngKey = frameKey + ".png";
                    it = entitiesFrameByName.find(pngKey);
                    if (it != entitiesFrameByName.end()) {
                        of = &it->second;
                        objectTypeKnown = true;
                    }
                }
            }
            if (!of && isEndSign) of = defaultEndSignFrame;
            if (!of) of = defaultEntityFrame;
            if (!isFastTravelChanger && !isBumper && !isEndSign && hideUnknownObjectTypes && !objectTypeKnown) {
                continue;
            }

            if (!isFastTravelChanger && objectTex && of) {
                int fw = 32;
                int fh = 32;
                int dstX = (int)std::lround(entityBaseX - camX);
                int dstY = (int)std::lround(entityBaseY - camY);
                if (isEndSign) {
                    const int srcW = of->rotated ? of->rect.h : of->rect.w;
                    const int srcH = of->rotated ? of->rect.w : of->rect.h;
                    // Use a stable world-scale size for signpost frames.
                    const int targetH = 70;
                    fh = targetH;
                    fw = std::max(18, (int)std::lround((srcW / (double)std::max(1, srcH)) * targetH));
                    // Anchor end_sign to object center X and tile-bottom Y for consistent visibility.
                    const float objectCenterScreenX = obj.x - camX;
                    const float objectBottomScreenY = (obj.y - 16.0f) - camY + 32.0f;
                    dstX = (int)std::lround(objectCenterScreenX - fw * 0.5f);
                    dstY = (int)std::lround(objectBottomScreenY - fh);
                }
                SDL_Rect dst{
                    dstX,
                    dstY,
                    fw,
                    fh
                };
                auto isOnscreen = [&](const SDL_Rect& r) -> bool {
                    if (r.x + r.w <= 0 || r.y + r.h <= 0) return false;
                    if (r.x >= worldViewW || r.y >= worldViewH) return false;
                    return true;
                };
                auto renderObjFrame = [&](const SDL_Rect& outDst) {
                    if (isEndSign && !isOnscreen(outDst)) return;
                    renderFrame(ren, objectTex, *of, outDst);
                };
                renderObjFrame(dst);
                if (renderWrapY && !isEndSign) {
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTop = dst;
                    SDL_Rect dstBottom = dst;
                    dstTop.y -= wrapH;
                    dstBottom.y += wrapH;
                    renderObjFrame(dstTop);
                    renderObjFrame(dstBottom);
                }
                if (renderWrapX && !isEndSign) {
                    const int wrapW = map.w * map.tileSize;
                    SDL_Rect dstLeft = dst;
                    SDL_Rect dstRight = dst;
                    dstLeft.x -= wrapW;
                    dstRight.x += wrapW;
                    renderObjFrame(dstLeft);
                    renderObjFrame(dstRight);
                }
                if (renderWrapY && renderWrapX && !isEndSign) {
                    const int wrapW = map.w * map.tileSize;
                    const int wrapH = map.h * map.tileSize;
                    SDL_Rect dstTL = dst;
                    SDL_Rect dstTR = dst;
