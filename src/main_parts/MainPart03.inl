                nlohmann::json j;
                try { j = nlohmann::json::parse(placeholderText); } catch (...) { j = nlohmann::json(); }
                if (!j.is_null()) {
                    applyClientSettingsJson(j);
                    usedPlaceholder = true;
                }
            }
            saveClientSettings();
            if (usedPlaceholder) {
                SDL_Log("Initialized client settings from placeholder: %s", placeholderPath.c_str());
            }
        }
    }
    auto applyRenderVsync = [&]() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (SDL_RenderSetVSync(ren, vsyncEnabled ? 1 : 0) != 0) {
            SDL_Log("Could not set renderer VSync=%d: %s", vsyncEnabled ? 1 : 0, SDL_GetError());
        }
#else
        SDL_Log("Renderer VSync toggle unsupported on this SDL version.");
#endif
    };
#if !defined(__ANDROID__)
    if (fullscreen) {
        if (!SDL_SetWindowFullscreen(win, true)) {
            SDL_Log("Could not apply startup fullscreen setting: %s", SDL_GetError());
            fullscreen = false;
        }
    }
#endif
    applyRenderVsync();
    SDL_Log("Build UUID: %s", buildUuid.c_str());
    auto applyAudioVolumes = [&]() {
        audio.applyVolumes(muteAllAudio, musicVolume, sfxVolume);
    };
    applyAudioVolumes();
    auto applyMenuMusicToggle = [&]() {
        audio.applyMenuMusicToggle(menuMusicEnabled);
    };
    auto telemetryEnabled = [&]() -> bool {
        return extraSettings.size() > 44 ? extraSettings[44] : false;
    };
    auto sendDiscordTelemetry = [&](const std::string& event, const nlohmann::json& details) {
        if (!telemetryEnabled()) return;
        if (telemetryWebhookUrl.empty()) return;
#if defined(HAVE_CURL) && HAVE_CURL
        CURL* curl = curl_easy_init();
        if (!curl) return;
        nlohmann::json embed;
        embed["title"] = "DF-New Telemetry";
        embed["description"] = event;
        embed["fields"] = nlohmann::json::array();
        for (auto it = details.begin(); it != details.end(); ++it) {
            nlohmann::json field;
            field["name"] = it.key();
            field["value"] = it->is_string() ? it->get<std::string>() : it->dump();
            field["inline"] = true;
            embed["fields"].push_back(std::move(field));
        }
        nlohmann::json payload;
        payload["username"] = "DF-New Telemetry";
        payload["embeds"] = nlohmann::json::array({embed});
        const std::string body = payload.dump();
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, telemetryWebhookUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        const CURLcode rc = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (rc != CURLE_OK || code < 200 || code >= 300) {
            SDL_Log("telemetry: webhook failed code=%ld curl=%d", code, (int)rc);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
#else
        (void)event;
        (void)details;
#endif
    };
    sendDiscordTelemetry("startup", {
        {"build_uuid", buildUuid},
        {"version", appVersion},
        {"platform", SDL_GetPlatform() ? SDL_GetPlatform() : "unknown"},
        {"video_driver", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown"},
        {"audio_driver", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown"}
    });
    LevelManager levelManager;
    Uint64 nextAudioRecoverTick = 0;
    auto recoverAudioIfNeeded = [&](bool inLevel) {
        if (!audioRecoveryEnabled) return;
        if (audio.isReady()) return;
        const Uint64 nowTick = SDL_GetTicks();
        if (nowTick < nextAudioRecoverTick) return;
        nextAudioRecoverTick = nowTick + 1500;
        SDL_Log("audio.recover: restarting audio backend (inLevel=%d)", inLevel ? 1 : 0);
        audio.shutdown();
        if (!audio.initialize()) {
            ++audioRecoverFailures;
            SDL_Log("audio.recover: initialize failed");
            if (audioRecoverFailures >= kMaxAudioRecoverFailures) {
                audioRecoveryEnabled = false;
                SDL_Log("audio.recover: disabled after %d consecutive failures", audioRecoverFailures);
            }
            return;
        }
        audioRecoverFailures = 0;
        audio.loadGlobalAssets();
        applyAudioVolumes();
        if (inLevel) {
            audio.loadLevelMusic(levelManager.musicPath());
        } else {
            applyMenuMusicToggle();
        }
        SDL_Log("audio.recover: restart ok");
    };
    bool running = true;
    std::string currentFancyLevelName = "LEVEL";
    std::string currentAreaIdText = "1";
    auto buildFancyLevelName = [&]() -> std::string {
        const int worldId = levelManager.worldId();
        if (worldId <= 0) {
            return "LEVEL";
        }
        auto worldIt = worldNamesById.find(worldId);
        std::string worldName = (worldIt != worldNamesById.end()) ? worldIt->second : (std::string("WORLD ") + std::to_string(worldId));
        return worldName;
    };
    auto buildAreaIdText = [&]() -> std::string {
        const int areaId = levelManager.levelPartId();
        return std::to_string(std::max(1, areaId));
    };
    auto playSceneIntroCard = [&]() {
        if (!introCardTex) return;
        auto bgIt = introCardFrames.find("background");
        auto leftIt = introCardFrames.find("leftbar");
        auto topIt = introCardFrames.find("topsecion");
        if (bgIt == introCardFrames.end() || leftIt == introCardFrames.end() || topIt == introCardFrames.end()) return;

        const Frame& bgFrame = bgIt->second;
        const Frame& leftFrame = leftIt->second;
        const Frame& topFrame = topIt->second;
        const Uint64 introStartNs = SDL_GetTicksNS();
        const double introDurationSec = 0.78;
        auto lerp = [](double a, double b, double t) {
            return a + (b - a) * t;
        };
        auto stage = [&](double t, double start, double span) { return std::clamp((t - start) / span, 0.0, 1.0); };

        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_EVENT_QUIT) {
                    running = false;
                    return;
                }
                if (ev.type == SDL_EVENT_GAMEPAD_ADDED || ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    input.handleEvent(ev);
                    InputSystem::DetectionEvent det;
                    while (input.pollDetectionEvent(det)) {
                        const char* typeStr = (det.type == InputSystem::DetectionEvent::Type::Connected) ? "connected" : "disconnected";
                        SDL_Log("controller %s: id=%d name=\"%s\" connected=%d", typeStr, (int)det.id, det.name.c_str(), det.connectedCount);
                    }
                }
            }

            const double elapsedSec = (double)(SDL_GetTicksNS() - introStartNs) / 1000000000.0;
            const double t = std::clamp(elapsedSec / introDurationSec, 0.0, 1.0);
            const double leftIn = stage(t, 0.00, 0.60);
            const double topIn = stage(t, 0.08, 0.60);
            const double bgIn = stage(t, 0.16, 0.68);
            const int fadeAlpha = (int)std::lround(255.0 * (1.0 - stage(t, 0.0, 0.42)));

            SDL_SetRenderTarget(ren, gameTarget);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);

            auto frameW = [](const Frame& f) { return f.rotated ? f.rect.h : f.rect.w; };
            auto frameH = [](const Frame& f) { return f.rotated ? f.rect.w : f.rect.h; };
            const int bgSrcW = std::max(1, frameW(bgFrame));
            const int bgSrcH = std::max(1, frameH(bgFrame));
            const int leftSrcW = std::max(1, frameW(leftFrame));
            const int leftSrcH = std::max(1, frameH(leftFrame));
            const int topSrcW = std::max(1, frameW(topFrame));
            const int topSrcH = std::max(1, frameH(topFrame));

            const int bgH = kBaseScreenH;
            const int bgW = (int)std::lround((double)bgSrcW * ((double)bgH / (double)bgSrcH));
            const int leftH = kBaseScreenH;
            const int leftW = (int)std::lround((double)leftSrcW * ((double)leftH / (double)leftSrcH));
            const int topW = kBaseScreenW;
            const int topH = (int)std::lround((double)topSrcH * ((double)topW / (double)topSrcW));
            const int bgTargetX = std::max(0, kBaseScreenW - bgW);

            SDL_Rect bgDst{
                (int)std::lround(lerp((double)kBaseScreenW, (double)bgTargetX, bgIn)),
                0,
                bgW,
                bgH
            };
            SDL_Rect leftDst{
                (int)std::lround(lerp(-(double)leftW, 0.0, leftIn)),
                0,
                leftW,
                leftH
            };
            SDL_Rect topDst{
                0,
                (int)std::lround(lerp(-(double)topH, 0.0, topIn)),
                topW,
                topH
            };

            renderFrame(ren, introCardTex, bgFrame, bgDst);
            renderFrame(ren, introCardTex, leftFrame, leftDst);
            renderFrame(ren, introCardTex, topFrame, topDst);
            if (t > 0.30) {
                const int worldW = MeasureTextWidth(introWorldNameStyle.scale, currentFancyLevelName);
                const int worldX = introWorldNameStyle.x - worldW / 2;
                const int areaW = MeasureTextWidth(introAreaIdStyle.scale, currentAreaIdText);
                const int areaX = introAreaIdStyle.x - areaW / 2;
                DrawText(ren, worldX, introWorldNameStyle.y, introWorldNameStyle.scale, currentFancyLevelName);
                DrawTextColored(ren, areaX, introAreaIdStyle.y, introAreaIdStyle.scale, currentAreaIdText, SDL_Color{50, 210, 70, 255});
            }
            if (fadeAlpha > 0) {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)std::clamp(fadeAlpha, 0, 255));
                SDL_FRect fadeRect{0.0f, 0.0f, (float)kBaseScreenW, (float)kBaseScreenH};
                SDL_RenderFillRect(ren, &fadeRect);
            }

            SDL_SetRenderTarget(ren, nullptr);
            int ww = 0, wh = 0;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_Rect presentRect = computePresentRect(ww, wh, kBaseScreenW, kBaseScreenH, 1.0f);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, gameTarget, nullptr, &presentRect);
            SDL_RenderPresent(ren);

            if (t >= 1.0) break;
            SDL_Delay(1);
        }
    };
    bool reopenUserLevelMenu = false;
    FrontendMenuContext frontendCtx{};
    frontendCtx.win = win;
    frontendCtx.ren = ren;
    frontendCtx.gameTarget = gameTarget;
    frontendCtx.gameTargetRef = &gameTarget;
    frontendCtx.baseScreenW = kBaseScreenW;
    frontendCtx.baseScreenH = kBaseScreenH;
    frontendCtx.buildUuid = buildUuid;
    frontendCtx.versionString = appVersion;
    frontendCtx.running = &running;
    frontendCtx.fullscreen = &fullscreen;
    frontendCtx.vsyncEnabled = &vsyncEnabled;
    frontendCtx.clampCamX = &clampCamX;
    frontendCtx.defaultShowFpsCounter = &defaultShowFpsCounter;
    frontendCtx.defaultShowDetailedDebugger = &defaultShowDetailedDebugger;
    frontendCtx.defaultShowHitboxes = &defaultShowHitboxes;
    frontendCtx.defaultShowPlayerHitbox = &defaultShowPlayerHitbox;
    frontendCtx.defaultShowDebugView = &defaultShowDebugView;
    frontendCtx.defaultHideUnknownObjectTypes = &defaultHideUnknownObjectTypes;
    frontendCtx.powerManagementEnabled = &powerManagementEnabled;
    frontendCtx.menuMusicEnabled = &menuMusicEnabled;
    frontendCtx.muteAllAudio = &muteAllAudio;
    frontendCtx.keyMoveLeft = &keybinds.moveLeft;
    frontendCtx.keyMoveRight = &keybinds.moveRight;
    frontendCtx.keyMoveDown = &keybinds.moveDown;
    frontendCtx.keyJump = &keybinds.jump;
    frontendCtx.keyPause = &keybinds.pause;
    frontendCtx.musicVolume = &musicVolume;
    frontendCtx.sfxVolume = &sfxVolume;
    frontendCtx.uiScalePercent = &uiScalePercent;
    frontendCtx.extraSettings = extraSettings.data();
    frontendCtx.extraSettingsCount = (int)extraSettings.size();
    std::string frontendSelectedLevelPath;
    frontendCtx.selectedLevelPath = &frontendSelectedLevelPath;
    frontendCtx.applyAudioVolumes = applyAudioVolumes;
    frontendCtx.applyMenuMusicToggle = applyMenuMusicToggle;
    frontendCtx.saveClientSettings = saveClientSettings;
    auto applyDynamicResolutionFromWindow = [&](bool force) -> bool {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        if (winW <= 0 || winH <= 0) return true;

        float aspect = (float)winW / (float)winH;
        aspect = std::clamp(aspect, 4.0f / 3.0f, 32.0f / 9.0f);
        const int newBaseH = 1080;
        int newBaseW = std::max(1280, (int)std::lround((float)newBaseH * aspect));
        if ((newBaseW & 1) != 0) ++newBaseW;
        const int newGameplayW = std::max(1, (int)std::lround((float)newBaseW / kGameplayZoom));
        const int newGameplayH = std::max(1, (int)std::lround((float)newBaseH / kGameplayZoom));
