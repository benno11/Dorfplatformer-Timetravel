                levelCompleteAudioChannel = audio.playVictorySfx();
                if (levelCompleteAudioChannel < 0) {
                    levelCompleteCounting = true;
                }
            } else {
                levelCompleteCounting = true;
            }
        };

        reloadLevel();
        if (!running) break;

        bool levelRunning = true;
        bool paused = false;
        bool deathSequenceActive = false;
        bool deathLifeDeducted = false;
        float deathTimer = 0.0f;
        struct DroppedCoin {
            float x = 0.0f;
            float y = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            float life = 12.0f;
            float noPickupTimer = 0.0f;
            int value = 1;
        };
        std::vector<DroppedCoin> droppedCoins;
        float fastTravelCooldown = 0.0f;
        bool showHitboxes = defaultShowHitboxes;
        bool showPlayerHitbox = defaultShowPlayerHitbox;
        bool showDebugView = defaultShowDebugView;
        bool showDemoPath = false;
        bool hideUnknownObjectTypes = defaultHideUnknownObjectTypes;
        bool showFpsCounter = defaultShowFpsCounter;
        showDetailedDebugger = defaultShowDetailedDebugger;
        if (showDetailedDebugger && !debugWin) {
#if defined(__ANDROID__)
            debugWin = win;
            debugRen = ren;
#else
            debugWin = SDL_CreateWindow("Detailed Debugger",
                                        560, 760,
                                        SDL_WINDOW_RESIZABLE);
            if (debugWin) {
                debugRen = SDL_CreateRenderer(debugWin, nullptr);
                if (!debugRen) {
                    SDL_DestroyWindow(debugWin);
                    debugWin = nullptr;
                    showDetailedDebugger = false;
                }
            } else {
                showDetailedDebugger = false;
            }
#endif
        } else if (debugWin) {
#if !defined(__ANDROID__)
            if (showDetailedDebugger) SDL_ShowWindow(debugWin);
            else SDL_HideWindow(debugWin);
#endif
        }
        bool verticalWrapActive = false;
        float camXClampBlend = 1.0f; // 1 = fully clamped, 0 = free-follow.
        float camYClampBlend = 1.0f; // 1 = fully clamped, 0 = free-follow.
        int detailedDebugSubmenu = 0; // 0 Overview, 1 Objects, 2 Performance, 3 Player Status
        int detailedDebugObjectIndex = 0;
        std::vector<SDL_FRect> solidHitboxes;
        std::vector<SDL_FRect> semiHitboxes;
        std::vector<SDL_FRect> waterHitboxes;
        std::vector<SDL_FRect> airDebugHitboxes;
        std::unordered_set<int> activeBumperIndices;
        int pauseSelection = 0; // 0 = Resume, 1 = Restart, 2 = Quit
        bool returnToSelect = false;
        std::unordered_map<SDL_FingerID, SDL_FPoint> activeTouches;
        struct ReplayInputSnapshot {
            float touchMove = 0.0f;
            bool touchDown = false;
            bool touchJump = false;
            float gamepadMove = 0.0f;
            bool gamepadDown = false;
            bool gamepadJump = false;
            bool gamepadFreeMove = false;
            float inputMove = 0.0f;
            bool inputDown = false;
            bool forceRightMovement = false;
            bool fastTravelEnabled = false;
            bool demoEnabled = false;
        };
        ReplayInputSnapshot replayInput{};
        struct ReplayRecorder {
            bool enabled = false;
            uint64_t frameIndex = 0;
            Uint64 startTicksNs = 0;
            std::string path;
            std::ofstream out;
        };
        struct ReplayFrameSample {
            float x = 0.0f;
            float y = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            int w = 0;
            int h = 0;
            bool onGround = false;
            bool inWater = false;
            int facing = 1;
            bool freeMove = false;
            int anim = 0;
            float animTime = 0.0f;
            bool jumpHeld = false;
            float jumpHoldTime = 0.0f;
            bool jumpWasDown = false;
            float jumpBufferTime = 0.0f;
            float inputMove = 0.0f;
            bool inputDown = false;
        };
        struct ReplayPlaybackState {
            bool active = false;
            std::string sourcePath;
            std::vector<ReplayFrameSample> frames;
            size_t nextFrame = 0;
        };
        ReplayRecorder replayRecorder{};
        ReplayPlaybackState replayPlayback{};
        auto stopReplayRecording = [&](const char* reason) {
            if (!replayRecorder.enabled || !replayRecorder.out.is_open()) return;
            nlohmann::json end;
            end["type"] = "end";
            end["reason"] = reason ? reason : "stop";
            end["frames"] = replayRecorder.frameIndex;
            end["level_timer"] = levelTimerSeconds;
            replayRecorder.out << end.dump() << "\n";
            replayRecorder.out.flush();
            replayRecorder.out.close();
            replayRecorder.enabled = false;
        };
        auto startReplayRecording = [&]() {
            stopReplayRecording("restart");
            replayRecorder = ReplayRecorder{};
            try {
                std::error_code ec;
                std::filesystem::create_directories(replayDirPath, ec);
                const Uint64 stamp = SDL_GetTicksNS();
                replayRecorder.path = (replayDirPath / ("replay-" + std::to_string((unsigned long long)stamp) + ".jsonl")).string();
                replayRecorder.out.open(replayRecorder.path, std::ios::binary | std::ios::trunc);
                if (replayRecorder.out.is_open()) {
                    replayRecorder.enabled = true;
                    replayRecorder.startTicksNs = stamp;
                    nlohmann::json meta;
                    meta["type"] = "meta";
                    meta["build_uuid"] = buildUuid;
                    meta["version"] = appVersion;
                    meta["level_path"] = levelManager.levelPath();
                    meta["world"] = levelManager.worldId();
                    meta["area"] = levelManager.levelPartId();
                    meta["time_id"] = levelManager.timeId();
                    meta["tile_size"] = map.tileSize;
                    meta["map_w"] = map.w;
                    meta["map_h"] = map.h;
                    meta["start_ticks_ns"] = (uint64_t)replayRecorder.startTicksNs;
                    replayRecorder.out << meta.dump() << "\n";
                    replayRecorder.out.flush();
                    std::ofstream latest((replayDirPath / "latest_replay.txt").string(), std::ios::binary | std::ios::trunc);
                    if (latest.is_open()) latest << replayRecorder.path << "\n";
                    SDL_Log("Replay recording: %s", replayRecorder.path.c_str());
                }
            } catch (...) {
                replayRecorder.enabled = false;
            }
        };
        auto stopReplayPlayback = [&]() {
            replayPlayback.active = false;
            replayPlayback.nextFrame = 0;
            replayPlayback.frames.clear();
            replayPlayback.sourcePath.clear();
        };
        auto loadReplayForPlayback = [&](const std::string& path, std::string& outLevelPath) -> bool {
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open()) return false;
            std::vector<ReplayFrameSample> loaded;
            loaded.reserve(8192);
            outLevelPath.clear();
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                nlohmann::json j;
                try { j = nlohmann::json::parse(line); } catch (...) { continue; }
                if (!j.is_object()) continue;
                const std::string type = j.value("type", std::string());
                if (type == "meta") {
                    if (j.contains("level_path") && j["level_path"].is_string()) {
                        outLevelPath = j["level_path"].get<std::string>();
                    }
                    continue;
                }
                if (type != "frame") continue;
                if (!j.contains("player") || !j["player"].is_object()) continue;
                const auto& p = j["player"];
                ReplayFrameSample s{};
                s.x = p.value("x", 0.0f);
                s.y = p.value("y", 0.0f);
                s.vx = p.value("vx", 0.0f);
                s.vy = p.value("vy", 0.0f);
                s.w = p.value("w", 32);
                s.h = p.value("h", 32);
                s.onGround = p.value("on_ground", false);
                s.inWater = p.value("in_water", false);
                s.facing = p.value("facing", 1);
                s.freeMove = p.value("free_move", false);
                s.anim = p.value("anim", 0);
                s.animTime = p.value("anim_time", 0.0f);
                s.jumpHeld = p.value("jump_held", false);
                s.jumpHoldTime = p.value("jump_hold_time", 0.0f);
                s.jumpWasDown = p.value("jump_was_down", false);
                s.jumpBufferTime = p.value("jump_buffer_time", 0.0f);
                if (j.contains("input_map") && j["input_map"].is_object()) {
                    const auto& im = j["input_map"];
                    s.inputMove = im.value("input_move", 0.0f);
                    s.inputDown = im.value("input_down", false);
                }
                loaded.push_back(s);
            }
            if (loaded.empty()) return false;
            replayPlayback.frames = std::move(loaded);
            replayPlayback.nextFrame = 0;
            replayPlayback.sourcePath = path;
            replayPlayback.active = true;
            return true;
        };
        // Replay recording is opt-in; keep disabled until user toggles it.
        SDL_Event e;
        Uint32 lastTicks = SDL_GetTicks();
        Uint32 lastPresentTicks = lastTicks;
        Uint32 nextPresentTicks = lastTicks;
        bool mainWindowFocused = true;
        bool mainWindowMinimized = false;
        constexpr int kFpsDisplayMax = 99999999;
        float updateFpsSmoothed = 0.0f;
        float renderFpsSmoothed = 0.0f;
        int updateFpsDisplay = 0;
        int renderFpsDisplay = 0;

        SDL_Rect pauseBtnContinue{0,0,0,0};
        SDL_Rect pauseBtnRestart{0,0,0,0};
        SDL_Rect pauseBtnExit{0,0,0,0};

        auto handlePauseSelect = [&](int sel) {
            pauseSelection = sel;
            if (pauseSelection == 0) {
                paused = false;
            } else if (pauseSelection == 1) {
                droppedCoins.clear();
                reloadLevel();
                paused = false;
            } else {
                returnToSelect = true;
                levelRunning = false;
            }
        };
        auto toggleDetailedDebugger = [&]() {
            showDetailedDebugger = !showDetailedDebugger;
            if (showDetailedDebugger && !debugWin) {
#if defined(__ANDROID__)
                debugWin = win;
                debugRen = ren;
#else
                debugWin = SDL_CreateWindow("Detailed Debugger",
                                            560, 760,
                                            SDL_WINDOW_RESIZABLE);
                if (debugWin) {
                    debugRen = SDL_CreateRenderer(debugWin, nullptr);
                    if (!debugRen) {
                        SDL_DestroyWindow(debugWin);
                        debugWin = nullptr;
                        showDetailedDebugger = false;
                    }
                } else {
                    showDetailedDebugger = false;
                }
#endif
            } else if (!showDetailedDebugger) {
#if defined(__ANDROID__)
                debugWin = nullptr;
                debugRen = nullptr;
#endif
            }
            if (debugWin) {
#if !defined(__ANDROID__)
                if (showDetailedDebugger) SDL_ShowWindow(debugWin);
                else SDL_HideWindow(debugWin);
#endif
            }
        };
        auto handleDetailedDebuggerTap = [&](int mx, int my) -> bool {
            if (!showDetailedDebugger) return false;
            SDL_Rect tab0{12, 38, 130, 36};
            SDL_Rect tab1{152, 38, 130, 36};
            SDL_Rect tab2{292, 38, 130, 36};
            SDL_Rect tab3{432, 38, 130, 36};
            SDL_Rect closeBtn{500, 8, 52, 24};
            SDL_Point pt{mx, my};
            bool handled = false;
            if (SDL_PointInRect(&pt, &tab0)) { detailedDebugSubmenu = 0; handled = true; }
            else if (SDL_PointInRect(&pt, &tab1)) { detailedDebugSubmenu = 1; handled = true; }
            else if (SDL_PointInRect(&pt, &tab2)) { detailedDebugSubmenu = 2; handled = true; }
            else if (SDL_PointInRect(&pt, &tab3)) { detailedDebugSubmenu = 3; handled = true; }
            else if (SDL_PointInRect(&pt, &closeBtn)) { toggleDetailedDebugger(); handled = true; }
            if (detailedDebugSubmenu == 1) {
                SDL_Rect prevBtn{12, 92, 120, 34};
                SDL_Rect nextBtn{142, 92, 120, 34};
                if (SDL_PointInRect(&pt, &prevBtn)) { detailedDebugObjectIndex--; handled = true; }
                if (SDL_PointInRect(&pt, &nextBtn)) { detailedDebugObjectIndex++; handled = true; }
            }
            return handled;
        };
        auto dropPlayerCoins = [&](float originX, float originY) {
            const int owned = levelManager.coinCount();
            if (owned <= 0) return;
            levelManager.resetCoinCount();
            const int spawnCount = std::min(owned, 120);
            droppedCoins.reserve(droppedCoins.size() + spawnCount);
            int remaining = owned;
            for (int i = 0; i < spawnCount; ++i) {
                DroppedCoin c{};
                const float t = (spawnCount <= 1) ? 0.0f : (i / (float)(spawnCount - 1)) * 2.0f - 1.0f;
                c.x = originX;
                c.y = originY;
                c.vx = t * 280.0f;
                c.vy = -430.0f - 90.0f * std::fabs(t);
                c.life = 12.0f;
                // Short grace period so freshly dropped coins don't instantly re-collect.
                c.noPickupTimer = 0.45f;
                droppedCoins.push_back(c);
                remaining--;
            }
            int idx = 0;
            while (remaining > 0 && !droppedCoins.empty()) {
                droppedCoins[idx % droppedCoins.size()].value++;
                remaining--;
                idx++;
            }
        };
        auto removeTimewarpObjectsAndExit = [&]() {
            objects.erase(std::remove_if(objects.begin(), objects.end(), [](const ObjectInstance& obj) {
                int objId = 0;
                try { objId = std::stoi(obj.id); } catch (...) { return false; }
                return objId >= 57 && objId <= 61;
            }), objects.end());
            setFastTravelActiveDir(-1, "boss_start_disable_timewarp");
            fastTravelOverlapWasActive = false;
            fastTravelBlendVx = 0.0f;
            fastTravelBlendVy = 0.0f;
            fastTravelCooldown = std::max(fastTravelCooldown, 0.30f);
            timeTravelTriggerCooldown = std::max(timeTravelTriggerCooldown, 0.50f);
        };

        while (levelRunning) {
            recoverAudioIfNeeded(true);
            applyDynamicResolutionFromWindow(false);
            Uint32 now = SDL_GetTicks();
            float dt = (now - lastTicks) / 1000.0f;
            lastTicks = now;
            SetTextScaleMultiplier(std::clamp((float)uiScalePercent / 100.0f, 0.5f, 2.0f));
            auto isVerticalWrapEnabledAtX = [&](float x) -> bool {
                if (((currentLevelId == 29 && x > 1250.0f) ||
                     (currentLevelId == 30 && x > 1250.0f) ||
                     currentLevelId == 39 ||
                     currentLevelId == 40 ||
                     currentLevelId == 53 ||
                     currentLevelId == 54)) {
                    return true;
                }
                if ((currentLevelId == 21 || currentLevelId == 22 || currentLevelId == 23 || currentLevelId == 24) &&
                    x > 3211.0f && x < 4559.0f) {
                    return true;
                }
                return false;
            };
            if (levelCompleteActive) {
                paused = false;
            }
            const int updateFpsInstant = std::clamp((dt > 0.0f) ? (int)(1.0f / dt) : 0, 0, kFpsDisplayMax);
            if (updateFpsSmoothed <= 0.0f) updateFpsSmoothed = (float)updateFpsInstant;
            else updateFpsSmoothed += ((float)updateFpsInstant - updateFpsSmoothed) * 0.16f;
            updateFpsDisplay = std::clamp((int)std::lround(updateFpsSmoothed), 0, kFpsDisplayMax);
            bool temp1TouchedThisFrame = false;
            const bool gameplayWrapX = (currentLevelId == 39 || currentLevelId == 40);
            const bool gameplayWrapY = isVerticalWrapEnabledAtX(player.x);
            verticalWrapActive = gameplayWrapY;
            activeBumperIndices.clear();
            SetHorizontalWrapCollision(gameplayWrapX);
            SetVerticalWrapCollision(gameplayWrapY);
            frameMsHistory[frameMsHistoryHead] = dt * 1000.0f;
            {
                long rssKB = -1, vmKB = -1;
                if (readProcessMemoryKB(rssKB, vmKB) && rssKB >= 0) {
                    memRssHistory[frameMsHistoryHead] = (float)rssKB / 1024.0f;
                } else {
                    memRssHistory[frameMsHistoryHead] = 0.0f;
                }
            }
