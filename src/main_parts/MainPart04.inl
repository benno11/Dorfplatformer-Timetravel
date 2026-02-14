
        if (!force &&
            newBaseW == kBaseScreenW && newBaseH == kBaseScreenH &&
            newGameplayW == kGameplayViewW && newGameplayH == kGameplayViewH) {
            return true;
        }

        SDL_Texture* newGameTarget = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, newBaseW, newBaseH);
        SDL_Texture* newWorldTarget = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, newGameplayW, newGameplayH);
        if (!newGameTarget || !newWorldTarget) {
            if (newWorldTarget) SDL_DestroyTexture(newWorldTarget);
            if (newGameTarget) SDL_DestroyTexture(newGameTarget);
            SDL_Log("Dynamic resolution update failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureScaleMode(newWorldTarget, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureScaleMode(newGameTarget, SDL_SCALEMODE_NEAREST);
        SDL_DestroyTexture(worldTarget);
        SDL_DestroyTexture(gameTarget);
        worldTarget = newWorldTarget;
        gameTarget = newGameTarget;
        kBaseScreenW = newBaseW;
        kBaseScreenH = newBaseH;
        kGameplayViewW = newGameplayW;
        kGameplayViewH = newGameplayH;
        frontendCtx.gameTarget = gameTarget;
        frontendCtx.baseScreenW = kBaseScreenW;
        frontendCtx.baseScreenH = kBaseScreenH;
        SDL_Log("Dynamic resolution updated: base=%dx%d gameplay=%dx%d window=%dx%d",
            kBaseScreenW, kBaseScreenH, kGameplayViewW, kGameplayViewH, winW, winH);
        return true;
    };
    frontendCtx.updateDynamicResolution = [&]() { (void)applyDynamicResolutionFromWindow(false); };
    applyDynamicResolutionFromWindow(true);
    // Enforce persisted startup audio state immediately.
    applyMenuMusicToggle();
    while (running) {
        recoverAudioIfNeeded(false);
        std::string selectedLevelPath;
        bool selectedFromUserMenu = false;
        if (reopenUserLevelMenu) {
            selectedLevelPath = RunLevelSelect(win, ren);
            selectedFromUserMenu = true;
            reopenUserLevelMenu = false;
        } else {
            frontendSelectedLevelPath.clear();
            FrontendAction action = runFrontendMenu(frontendCtx);
            saveClientSettings();
            if (!running || action == FrontendAction::Quit) break;

            selectedLevelPath = frontendSelectedLevelPath;
            if (!selectedLevelPath.empty()) {
                selectedFromUserMenu = true;
            } else {
                selectedLevelPath = RunLevelSelect(win, ren);
            }
        }
        if (selectedLevelPath.empty()) {
            continue;
        }
        const bool allowNextLevelProgression = !selectedFromUserMenu;
        audio.haltMusic();
        levelManager.setLevelPath(selectedLevelPath);

        TileMap map;
        std::vector<ObjectInstance> objects;
        LevelMeta meta;
        Player player;
        int livesCount = 5;
        int scoreCount = 0;
        bool levelCompleteActive = false;
        bool levelCompleteCounting = false;
        std::string levelCompleteNextPath;
        int levelCompleteAudioChannel = -1;
        int levelCompleteAreaId = 0;
        int levelCompleteCoinBonus = 0;
        int levelCompleteTimeScore = 0;
        int levelCompleteAccountedScore = 0;
        int levelCompleteSnapshotSeconds = 0;
        float levelCompleteUiLerp = 0.0f;
        enum class EndSignPhase {
            Idle,
            SignForward,
            SignBackward,
            PlayerForward,
            PlayerBackward,
            TriggerComplete,
            Done
        };
        struct EndSignRuntimeState {
            bool present = false;
            bool triggered = false;
            int objectIndex = -1;
            float objectX = 0.0f;
            float objectY = 0.0f;
            EndSignPhase phase = EndSignPhase::Idle;
            float frameTimer = 0.0f;
            int signFrame = 9;
            int signLoopCount = 0;
            int playerFrame = 1;
            int playerLoopCount = 0;
            bool lockPlayerRight = false;
        };
        EndSignRuntimeState endSignState;
        struct BossRuntimeState {
            bool active = false;
            int world = 0;
            int sourceWorld = 0;
            float activationCooldown = 0.0f;
            int health = 0;
            int maxHealth = 0;
            float x = 0.0f;
            float y = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            float hurtFlash = 0.0f;
            int phase = 0; // world3: 0 wait_near_start, 1 replay_playback, 2 wait_near_fight, 3 fight
            std::vector<SDL_FPoint> replayPath;
            size_t replayIndex = 0;
            float replayFrameAcc = 0.0f;
            float rainbowTimer = 0.0f; // world4: teleport/invuln flash window
        };
        BossRuntimeState bossState;
        auto resetBossStateForLoadedLevel = [&]() {
            bossState = BossRuntimeState{};
            const int world = levelManager.worldId();
            const int bossProfileWorld = (world == 2 || world == 6) ? 1 : std::max(1, world);
            bossState.active = false;
            bossState.world = bossProfileWorld;
            bossState.sourceWorld = std::max(1, world);
            bossState.activationCooldown = 3.0f;
            bossState.maxHealth = 4;
            bossState.health = 4;

            auto loadBossReplayPathPositions = [&](const std::string& replayFile) -> std::vector<SDL_FPoint> {
                std::vector<SDL_FPoint> out;
                std::vector<std::string> candidates;
                candidates.push_back((replayDirPath / replayFile).string());
                candidates.push_back((replayDirPath / std::filesystem::path(replayFile).filename()).string());
                candidates.push_back(replayFile);
                for (const auto& p : candidates) {
                    const std::string text = ReadTextFile(p);
                    if (text.empty()) continue;
                    std::istringstream in(text);
                    std::string line;
                    while (std::getline(in, line)) {
                        if (line.empty()) continue;
                        nlohmann::json j;
                        try { j = nlohmann::json::parse(line); } catch (...) { continue; }
                        if (!j.is_object()) continue;
                        if (j.value("type", std::string()) != "frame") continue;
                        if (!j.contains("player") || !j["player"].is_object()) continue;
                        const auto& pstate = j["player"];
                        SDL_FPoint pt{};
                        pt.x = pstate.value("x", 0.0f) + pstate.value("w", 32) * 0.5f;
                        pt.y = pstate.value("y", 0.0f) + pstate.value("h", 32) * 0.5f;
                        out.push_back(pt);
                    }
                    if (!out.empty()) {
                        SDL_Log("boss.w3: loaded replay positions=%d from %s", (int)out.size(), p.c_str());
                        break;
                    }
                }
                if (out.empty()) {
                    SDL_Log("boss.w3: replay load failed for %s", replayFile.c_str());
                }
                return out;
            };

            const float mapW = (float)(map.w * map.tileSize);
            const float mapH = (float)(map.h * map.tileSize);
            if (bossProfileWorld == 1) {
                bossState.x = std::clamp(mapW * 0.72f, 96.0f, std::max(96.0f, mapW - 96.0f));
                bossState.y = std::clamp(mapH * 0.38f, 96.0f, std::max(96.0f, mapH - 96.0f));
                bossState.vx = 280.0f;
                bossState.vy = 220.0f;
            } else if (bossProfileWorld == 2) {
                bossState.x = std::clamp(mapW * 0.60f, 96.0f, std::max(96.0f, mapW - 96.0f));
                bossState.y = std::clamp(mapH * 0.42f, 96.0f, std::max(96.0f, mapH - 96.0f));
                bossState.vx = -260.0f;
                bossState.vy = 240.0f;
            } else {
                const float phase = (float)(bossProfileWorld % 6) / 6.0f;
                bossState.x = std::clamp(mapW * (0.32f + phase * 0.36f), 96.0f, std::max(96.0f, mapW - 96.0f));
                bossState.y = std::clamp(mapH * (0.30f + (1.0f - phase) * 0.28f), 96.0f, std::max(96.0f, mapH - 96.0f));
                bossState.vx = ((bossProfileWorld % 2) == 0) ? -250.0f : 250.0f;
                bossState.vy = 220.0f + (float)((bossProfileWorld % 3) * 20);
            }
            if (bossState.sourceWorld == 7) {
                const float world7MinX = 96.0f + 28.0f;
                const float world7MaxX = 96.0f + (float)kGameplayViewW - 28.0f;
                const float minX = std::min(world7MinX, world7MaxX);
                const float maxX = std::max(world7MinX, world7MaxX);
                const float rx = (float)std::rand() / (float)RAND_MAX;
                bossState.x = minX + (maxX - minX) * rx;
                bossState.y = 32.0f + (float)kGameplayViewH + 28.0f;
                bossState.vx = 0.0f;
                bossState.vy = -320.0f;
            }
            if (bossState.sourceWorld == 3) {
                bossState.maxHealth = 8;
                bossState.health = 8;
                bossState.phase = 0;
                bossState.replayPath = loadBossReplayPathPositions("assets/boss3.replay");
                bossState.replayIndex = 0;
                bossState.replayFrameAcc = 0.0f;
                if (!bossState.replayPath.empty()) {
                    bossState.x = bossState.replayPath[0].x;
                    bossState.y = bossState.replayPath[0].y;
                }
                bossState.vx = 280.0f;
                bossState.vy = 220.0f;
                bossState.active = true;
            }
            if (bossState.sourceWorld == 4) {
                bossState.rainbowTimer = 0.0f;
            }
        };
        struct DemoRuntimeState {
            bool enabled = false;
            float dir = 1.0f;
            float jumpCooldown = 0.0f;
            float jumpHoldTimer = 0.0f;
            float stuckTime = 0.0f;
            float lastX = 0.0f;
            float repathTimer = 0.0f;
            size_t waypointIndex = 0;
            std::vector<SDL_Point> pathTiles;
            bool startTileSet = false;
            SDL_Point startTile{0, 0};
        };
        DemoRuntimeState demoState;
        float levelReloadTitleTimer = 0.0f;
        float levelTimerSeconds = 0.0f;
        float playerInvincibleTimer = 0.0f;
        constexpr float kPlayerInvincibleDuration = 5.00f;
        constexpr float kPlayerSpawnInvincibleDuration = 1.25f;
        float levelLoadDeathGraceTimer = 0.0f;
        constexpr float kLevelLoadDeathGraceDuration = 1.0f;
        constexpr int kPlayerSpawnLockFrames = 6;
        int playerSpawnLockFrames = 0;
        float playerSpawnLockX = 0.0f;
        float playerSpawnLockY = 0.0f;
        float cameraSmoothingSuppressTimer = 0.0f;
        bool levelCompleteCameraLocked = false;
        float levelCompleteCameraX = 0.0f;
        float levelCompleteCameraY = 0.0f;
        bool endSignCameraLocked = false;
        float endSignCameraX = 0.0f;
        enum FastTravelDir {
            FT_UP = 0,
            FT_DOWN = 1,
            FT_LEFT = 2,
            FT_RIGHT = 3,
            FT_EXIT = 4
        };
        int fastTravelActiveDir = -1;
        bool fastTravelOverlapWasActive = false;
        float fastTravelBlendVx = 0.0f;
        float fastTravelBlendVy = 0.0f;
        auto logFastTravelFlags = [&](const char* reason) {
            SDL_Log("fastTravel (%s): activeDir=%d", reason, fastTravelActiveDir);
        };
        auto fastTravelDirForObjectId = [&](int objId) -> int {
            if (objId == 57) return FT_UP;
            if (objId == 58) return FT_DOWN;
            if (objId == 59) return FT_LEFT;
            if (objId == 60) return FT_RIGHT;
            if (objId == 61) return FT_EXIT;
            return -1;
        };
        auto setFastTravelActiveDir = [&](int dir, const char* reason) {
            if (dir == fastTravelActiveDir) return;
            fastTravelActiveDir = dir;
            if (!(reason && std::strcmp(reason, "noop") == 0)) {
                logFastTravelFlags(reason);
            }
        };
        float timeTravelTriggerCooldown = 0.0f;
        int currentLevelId = parseLevelIdFromLevelPath(levelManager.levelPath());
        enum PlayerAnim {
            ANIM_IDLE,
            ANIM_WALK,
            ANIM_JUMP,
            ANIM_FALL,
            ANIM_CROUCH,
            ANIM_SKID,
            ANIM_HURT,
            ANIM_DEATH
        };
        auto updatePlayerAnimState = [&](float moveInput, bool downHeld, float stepDt) {
            int newAnim = ANIM_IDLE;
            const float vxAbs = std::abs(player.vx);
            if (player.freeMove) {
                newAnim = ANIM_IDLE;
            } else if (!player.onGround) {
                newAnim = (player.vy < 0.0f) ? ANIM_JUMP : ANIM_FALL;
            } else if (downHeld) {
                newAnim = ANIM_CROUCH;
            } else if (vxAbs > 8.0f) {
                const bool opposing = (player.vx > 20.0f && moveInput < -0.1f) || (player.vx < -20.0f && moveInput > 0.1f);
                newAnim = opposing ? ANIM_SKID : ANIM_WALK;
            }
            if (moveInput < -0.1f) player.facing = -1;
            if (moveInput > 0.1f) player.facing = 1;
            if (newAnim != player.anim) {
                player.anim = newAnim;
                player.animTime = 0.0f;
            } else {
                const float animStepDt = std::clamp(stepDt, 0.0f, 0.05f);
                player.animTime += animStepDt;
            }
        };

        auto reloadLevel = [&]() {
            levelManager.reloadLevel(map, objects, meta, player);
            levelTimerSeconds = 0.0f;
            levelCompleteActive = false;
            levelCompleteCounting = false;
            levelCompleteNextPath.clear();
            levelCompleteAudioChannel = -1;
            levelCompleteAreaId = 0;
            levelCompleteCoinBonus = 0;
            levelCompleteTimeScore = 0;
            levelCompleteAccountedScore = 0;
            levelCompleteSnapshotSeconds = 0;
            levelCompleteUiLerp = 0.0f;
            levelCompleteCameraLocked = false;
            levelCompleteCameraX = 0.0f;
            levelCompleteCameraY = 0.0f;
            endSignCameraLocked = false;
            endSignCameraX = 0.0f;
            endSignState = EndSignRuntimeState{};
            float nearestAheadDist = 1e30f;
            int fallbackSignIndex = -1;
            for (int i = 0; i < (int)objects.size(); ++i) {
                if (objects[i].id != "67") continue;
                if (fallbackSignIndex < 0) fallbackSignIndex = i;
                const float dx = objects[i].x - player.x;
                if (dx >= 0.0f && dx < nearestAheadDist) {
                    nearestAheadDist = dx;
                    endSignState.objectIndex = i;
                }
            }
            if (endSignState.objectIndex < 0) endSignState.objectIndex = fallbackSignIndex;
            if (endSignState.objectIndex >= 0 && endSignState.objectIndex < (int)objects.size()) {
                endSignState.present = true;
                endSignState.objectX = objects[endSignState.objectIndex].x;
                endSignState.objectY = objects[endSignState.objectIndex].y;
            }
            resetBossStateForLoadedLevel();
            demoState.jumpCooldown = 0.0f;
            demoState.jumpHoldTimer = 0.0f;
            demoState.stuckTime = 0.0f;
            demoState.lastX = player.x;
            demoState.repathTimer = 0.0f;
            demoState.waypointIndex = 0;
            demoState.pathTiles.clear();
            demoState.startTileSet = false;
            demoState.startTile = SDL_Point{0, 0};
            cameraSmoothingSuppressTimer = 0.20f;
            setFastTravelActiveDir(-1, "reload");
            fastTravelOverlapWasActive = false;
            fastTravelBlendVx = 0.0f;
            fastTravelBlendVy = 0.0f;
            timeTravelTriggerCooldown = 0.35f;

            audio.loadLevelMusic(levelManager.musicPath());
            currentFancyLevelName = buildFancyLevelName();
            currentAreaIdText = buildAreaIdText();
            levelReloadTitleTimer = 2.0f;
            currentLevelId = parseLevelIdFromLevelPath(levelManager.levelPath());
            playerInvincibleTimer = std::max(playerInvincibleTimer, kPlayerSpawnInvincibleDuration);
            levelLoadDeathGraceTimer = kLevelLoadDeathGraceDuration;
            playerSpawnLockX = player.x;
            playerSpawnLockY = player.y;
            playerSpawnLockFrames = kPlayerSpawnLockFrames;
            playSceneIntroCard();
        };
        auto startLevelCompleteSequence = [&]() {
            if (levelCompleteActive) return;
            levelCompleteActive = true;
            levelCompleteCounting = false;
            levelCompleteNextPath = allowNextLevelProgression ? levelManager.nextLevelPath() : "";
            levelCompleteAreaId = levelManager.levelPartId();
            levelCompleteSnapshotSeconds = (int)levelTimerSeconds;
            levelCompleteCoinBonus = levelManager.coinCount() * 10;
            const int elapsedMinutes = (int)std::floor(levelTimerSeconds / 60.0f);
            if (elapsedMinutes > 3) {
                levelCompleteTimeScore = 0;
            } else {
                int denom = 1;
                for (int i = 0; i < elapsedMinutes; ++i) denom *= 5;
                levelCompleteTimeScore = 10000 / std::max(1, denom);
            }
            levelCompleteAccountedScore = 0;
            if (audio.isReady()) {
                audio.haltMusic();
