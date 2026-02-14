    CrashReporter::start();
    // Keep existing terminal output (e.g. compile script logs) visible.
    const std::string buildUuid = makeBuildUuid();
    auto reportStartupError = [](const char* title, const std::string& msg, SDL_Window* parent) {
#if defined(__ANDROID__)
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s: %s", title, msg.c_str());
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg.c_str(), parent);
#endif
    };
    SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    auto sdlErr = []() -> std::string {
        const char* e = SDL_GetError();
        if (e && *e) return std::string(e);
        return "unknown error";
    };
    std::ofstream startupLog("build/startup.log", std::ios::app);
    auto logStartup = [&](const std::string& line) {
        SDL_Log("%s", line.c_str());
        if (startupLog.is_open()) startupLog << line << "\n";
    };
    logStartup("=== startup begin ===");
    auto envOrUnset = [](const char* name) -> std::string {
        const char* v = std::getenv(name);
        return (v && *v) ? std::string(v) : std::string("<unset>");
    };
    auto configuredAudioDriver = [&]() -> std::string {
        const char* hintDriver = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
        if (hintDriver && *hintDriver) return std::string(hintDriver);
        const std::string modernEnv = envOrUnset("SDL_AUDIO_DRIVER");
        if (modernEnv != "<unset>") return modernEnv;
        const std::string legacyEnv = envOrUnset("SDL_AUDIODRIVER");
        if (legacyEnv != "<unset>") return legacyEnv;
        return "<unset>";
    };
    auto applyAudioDriverSelection = [&](const char* driver) {
        if (driver && *driver) {
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
            setenv("SDL_AUDIO_DRIVER", driver, 1);
            setenv("SDL_AUDIODRIVER", driver, 1); // Legacy compatibility.
            return;
        }
        SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
        unsetenv("SDL_AUDIO_DRIVER");
        unsetenv("SDL_AUDIODRIVER");
    };
    struct InitAttempt {
        const char* label;
        const char* videoDriver; // nullptr means keep current env
        Uint32 flags;
    };
    auto hasVideoDriver = [](const char* name) -> bool {
        const int n = SDL_GetNumVideoDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetVideoDriver(i);
            if (d && std::strcmp(d, name) == 0) return true;
        }
        return false;
    };
    auto hasAudioDriver = [](const char* name) -> bool {
        const int n = SDL_GetNumAudioDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetAudioDriver(i);
            if (d && std::strcmp(d, name) == 0) return true;
        }
        return false;
    };
    auto listVideoDrivers = []() -> std::string {
        std::string out;
        const int n = SDL_GetNumVideoDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetVideoDriver(i);
            if (!d) continue;
            if (!out.empty()) out += ", ";
            out += d;
        }
        if (out.empty()) out = "<none>";
        return out;
    };
    auto listAudioDrivers = []() -> std::string {
        std::string out;
        const int n = SDL_GetNumAudioDrivers();
        for (int i = 0; i < n; ++i) {
            const char* d = SDL_GetAudioDriver(i);
            if (!d) continue;
            if (!out.empty()) out += ", ";
            out += d;
        }
        if (out.empty()) out = "<none>";
        return out;
    };
    auto listConnectedGamepads = []() -> std::string {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids || count <= 0) {
            if (ids) SDL_free(ids);
            return "<none>";
        }
        std::string out;
        for (int i = 0; i < count; ++i) {
            SDL_Gamepad* pad = SDL_OpenGamepad(ids[i]);
            const char* name = pad ? SDL_GetGamepadName(pad) : nullptr;
            if (!out.empty()) out += ", ";
            out += name ? name : "<unnamed>";
            if (pad) SDL_CloseGamepad(pad);
        }
        SDL_free(ids);
        return out.empty() ? "<none>" : out;
    };
    std::vector<InitAttempt> attempts;
    const bool hasExplicitAudioEnv = configuredAudioDriver() != "<unset>";
    if (!hasExplicitAudioEnv) {
        const char* preselectedAudio = nullptr;
#if defined(__ANDROID__)
        if (hasAudioDriver("aaudio")) preselectedAudio = "aaudio";
        else if (hasAudioDriver("openslES")) preselectedAudio = "openslES";
#else
        if (hasAudioDriver("pulseaudio")) preselectedAudio = "pulseaudio";
        else if (hasAudioDriver("dummy")) preselectedAudio = "dummy";
        else if (hasAudioDriver("alsa")) preselectedAudio = "alsa";
        else if (hasAudioDriver("pipewire")) preselectedAudio = "pipewire";
#endif
        if (preselectedAudio) {
            applyAudioDriverSelection(preselectedAudio);
            logStartup(std::string("audio preselect: ") + preselectedAudio);
        }
    }
    attempts.push_back({"video+gamepad (env defaults)", nullptr, SDL_INIT_VIDEO | SDL_INIT_GAMEPAD});
    attempts.push_back({"video only (env defaults)", nullptr, SDL_INIT_VIDEO});
    const bool hasX11 = !envOrUnset("DISPLAY").empty() && envOrUnset("DISPLAY") != "<unset>";
    const bool hasWayland = !envOrUnset("WAYLAND_DISPLAY").empty() && envOrUnset("WAYLAND_DISPLAY") != "<unset>";
    const bool canX11 = hasX11 && hasVideoDriver("x11");
    const bool canWayland = hasWayland && hasVideoDriver("wayland");
    const std::string sessionType = envOrUnset("XDG_SESSION_TYPE");
    const bool preferWayland = canWayland && (sessionType == "wayland" || !canX11);
    auto pushWaylandAttempts = [&attempts]() {
        attempts.push_back({"video+gamepad (wayland)", "wayland", SDL_INIT_VIDEO | SDL_INIT_GAMEPAD});
        attempts.push_back({"video only (wayland)", "wayland", SDL_INIT_VIDEO});
    };
    auto pushX11Attempts = [&attempts]() {
        attempts.push_back({"video+gamepad (x11)", "x11", SDL_INIT_VIDEO | SDL_INIT_GAMEPAD});
        attempts.push_back({"video only (x11)", "x11", SDL_INIT_VIDEO});
    };
    if (preferWayland) {
        if (canWayland) pushWaylandAttempts();
        if (canX11) pushX11Attempts();
    } else {
        if (canX11) pushX11Attempts();
        if (canWayland) pushWaylandAttempts();
    }
    if (hasVideoDriver("offscreen")) {
        attempts.push_back({"video only (offscreen)", "offscreen", SDL_INIT_VIDEO});
    }
    if (hasVideoDriver("dummy")) {
        attempts.push_back({"video only (dummy)", "dummy", SDL_INIT_VIDEO});
    }

    bool sdlOk = false;
    std::string initTrace;
    const std::string initialVideoEnv = envOrUnset("SDL_VIDEODRIVER");
    const std::string initialAudioEnv = configuredAudioDriver();
    for (const auto& a : attempts) {
        if (a.videoDriver) setenv("SDL_VIDEODRIVER", a.videoDriver, 1);
        else if (initialVideoEnv != "<unset>") setenv("SDL_VIDEODRIVER", initialVideoEnv.c_str(), 1);
        else unsetenv("SDL_VIDEODRIVER");
        if (initialAudioEnv != "<unset>") applyAudioDriverSelection(initialAudioEnv.c_str());
        else applyAudioDriverSelection(nullptr);
        SDL_Quit();
        if (SDL_Init(a.flags)) {
            initTrace += std::string(a.label) + ": ok\n";
            logStartup(std::string("init attempt: ") + a.label + " -> ok");
            sdlOk = true;
            break;
        }
        const std::string err = sdlErr();
        initTrace += std::string(a.label) + ": " + err + "\n";
        logStartup(std::string("init attempt: ") + a.label + " -> " + err);
    }
    if (!sdlOk) {
        std::string msg = "SDL_Init failed.\n";
        msg += initTrace;
        msg += "DISPLAY=" + envOrUnset("DISPLAY") + "\n";
        msg += "WAYLAND_DISPLAY=" + envOrUnset("WAYLAND_DISPLAY") + "\n";
        msg += "XDG_RUNTIME_DIR=" + envOrUnset("XDG_RUNTIME_DIR") + "\n";
        msg += "available_video_drivers=" + listVideoDrivers() + "\n";
        msg += "SDL_VIDEODRIVER=" + envOrUnset("SDL_VIDEODRIVER") + "\n";
        msg += "SDL_AUDIO_DRIVER=" + envOrUnset("SDL_AUDIO_DRIVER") + "\n";
        msg += "SDL_AUDIODRIVER=" + envOrUnset("SDL_AUDIODRIVER") + "\n";
        msg += "SDL_HINT_AUDIO_DRIVER=" + configuredAudioDriver();
        logStartup("init failed");
        logStartup(msg);
        reportStartupError("SDL Init Error", msg, nullptr);
        CrashReporter::stop();
        return 1;
    }
    logStartup("SDL_Init completed");
    auto tryAudioInit = [&](const char* label, const char* forcedDriver) -> bool {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        if (forcedDriver && *forcedDriver) applyAudioDriverSelection(forcedDriver);
        else if (initialAudioEnv != "<unset>") applyAudioDriverSelection(initialAudioEnv.c_str());
        else applyAudioDriverSelection(nullptr);
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const char* active = SDL_GetCurrentAudioDriver();
            if (forcedDriver && *forcedDriver && active && std::strcmp(active, forcedDriver) != 0) {
                logStartup(std::string("audio init attempt (") + label + "): mismatch active=" + active + " expected=" + forcedDriver);
                return false;
            }
            logStartup(std::string("audio init attempt (") + label + "): ok");
            return true;
        }
        const std::string err = sdlErr();
        logStartup(std::string("audio init attempt (") + label + ") failed: " + err);
        if ((forcedDriver && *forcedDriver && std::strcmp(forcedDriver, "dummy") != 0) &&
            err.find("Pipewire: Failed to connect hotplug detection context") != std::string::npos) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            applyAudioDriverSelection("dummy");
            if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                const char* active = SDL_GetCurrentAudioDriver();
                if (active && std::strcmp(active, "dummy") == 0) {
                    logStartup(std::string("audio init fallback (") + label + " -> dummy): ok");
                    return true;
                }
                logStartup(std::string("audio init fallback (") + label + " -> dummy): mismatch active=" +
                           (active ? active : "<none>") + " expected=dummy");
            } else {
                logStartup(std::string("audio init fallback (") + label + " -> dummy) failed: " + sdlErr());
            }
        }
        return false;
    };
    auto isGoodAudioDriver = [](const char* driver) -> bool {
        if (!driver) return false;
#if defined(__ANDROID__)
        return std::strcmp(driver, "dummy") != 0;
#else
        return std::strcmp(driver, "pipewire") == 0 ||
               std::strcmp(driver, "pulseaudio") == 0 ||
               std::strcmp(driver, "dummy") == 0;
#endif
    };
    bool audioReady = false;
    if (hasExplicitAudioEnv) {
        audioReady = tryAudioInit("env default", nullptr);
    } else {
#if defined(__ANDROID__)
        if (!audioReady) audioReady = tryAudioInit("env default", nullptr);
        if (!audioReady && hasAudioDriver("aaudio")) audioReady = tryAudioInit("aaudio", "aaudio");
        if (!audioReady && hasAudioDriver("openslES")) audioReady = tryAudioInit("openslES", "openslES");
        if (!audioReady && hasAudioDriver("dummy")) audioReady = tryAudioInit("dummy", "dummy");
#else
        if (hasAudioDriver("pipewire")) audioReady = tryAudioInit("pipewire", "pipewire");
        if (!audioReady && hasAudioDriver("pulseaudio")) audioReady = tryAudioInit("pulseaudio", "pulseaudio");
        if (!audioReady) audioReady = tryAudioInit("env default", nullptr);
        if (!audioReady && hasAudioDriver("alsa")) audioReady = tryAudioInit("alsa", "alsa");
        if (!audioReady && hasAudioDriver("sndio")) audioReady = tryAudioInit("sndio", "sndio");
        if (!audioReady && hasAudioDriver("dummy")) audioReady = tryAudioInit("dummy", "dummy");
#endif
    }
    if (!audioReady) {
        logStartup("audio init failed for all attempted drivers");
    } else {
        const char* active = SDL_GetCurrentAudioDriver();
        if (!isGoodAudioDriver(active)) {
            logStartup(std::string("audio override: active driver '") + (active ? active : "<none>") + "' is not preferred");
            bool overridden = false;
#if defined(__ANDROID__)
            if (hasAudioDriver("aaudio")) overridden = tryAudioInit("override aaudio", "aaudio");
            if (!overridden && hasAudioDriver("openslES")) overridden = tryAudioInit("override openslES", "openslES");
#else
            if (hasAudioDriver("pipewire")) overridden = tryAudioInit("override pipewire", "pipewire");
            if (!overridden && hasAudioDriver("pulseaudio")) overridden = tryAudioInit("override pulseaudio", "pulseaudio");
            if (!overridden && hasAudioDriver("dummy")) overridden = tryAudioInit("override dummy", "dummy");
#endif
            if (!overridden) {
                logStartup("audio override: no preferred driver override succeeded");
                (void)tryAudioInit("restore env default", nullptr);
            } else {
                logStartup(std::string("audio override: switched to ") +
                           (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "<none>"));
            }
        }
    }
    logStartup(std::string("SDL drivers: video=") +
               (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "<none>") +
               " audio=" +
               (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "<none>"));
    logStartup(std::string("audio drivers available: ") + listAudioDrivers());
    logStartup(std::string("active audio driver: ") +
               (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "<none>"));
    if ((SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) == 0) {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            logStartup(std::string("gamepad init attempt failed: ") + sdlErr());
        } else {
            logStartup("gamepad init attempt: ok");
        }
    }
    logStartup(std::string("connected gamepads: ") + listConnectedGamepads());
    InitTextRenderer(ResolveAssetPath("assets/Fonts/Main.ttf"));
    AudioSystem audio;
    audio.setLoopingEnabled(true);
    bool audioRecoveryEnabled = true;
    int audioRecoverFailures = 0;
    constexpr int kMaxAudioRecoverFailures = 3;
    const bool audioInitializedAtStartup = audio.initialize();
    if (!audioInitializedAtStartup) {
        const char* startupAudioDriver = SDL_GetCurrentAudioDriver();
        if (!isGoodAudioDriver(startupAudioDriver)) {
            SDL_Log("audio: disabling runtime recovery because no preferred audio backend is active");
            audioRecoveryEnabled = false;
        }
    }
    InputSystem input;
    input.scanConnected();
    {
        InputSystem::DetectionEvent ev;
        while (input.pollDetectionEvent(ev)) {
            const char* typeStr = (ev.type == InputSystem::DetectionEvent::Type::Connected) ? "connected" : "disconnected";
            SDL_Log("controller %s: id=%d name=\"%s\" connected=%d", typeStr, (int)ev.id, ev.name.c_str(), ev.connectedCount);
        }
        if (input.hasGamepad()) {
            const char* name = input.activeGamepadName();
            SDL_Log("active controller: %s", name ? name : "<unnamed>");
        } else {
            SDL_Log("active controller: <none>");
        }
    }

    int kBaseScreenW = 1920;
    int kBaseScreenH = 1080;
    {
        const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* displayMode = nullptr;
        if (primaryDisplay != 0) {
            displayMode = SDL_GetCurrentDisplayMode(primaryDisplay);
            if (!displayMode) displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
        }
        if (displayMode && displayMode->w > 0 && displayMode->h > 0) {
            float aspect = (float)displayMode->w / (float)displayMode->h;
            // Clamp to practical gameplay targets: 4:3 .. 32:9.
            aspect = std::clamp(aspect, 4.0f / 3.0f, 32.0f / 9.0f);
            kBaseScreenH = 1080;
            kBaseScreenW = std::max(1280, (int)std::lround((float)kBaseScreenH * aspect));
            if ((kBaseScreenW & 1) != 0) ++kBaseScreenW;
            SDL_Log("Adaptive aspect target: %dx%d (display %dx%d)", kBaseScreenW, kBaseScreenH, displayMode->w, displayMode->h);
        }
    }
    // Base resolution is 2x legacy (1920x1080 vs 960x540), so zoom must also be
    // doubled to preserve the original gameplay/background framing.
    constexpr float kGameplayZoom = 3.0f;
    int kGameplayViewW = std::max(1, (int)std::lround((float)kBaseScreenW / kGameplayZoom));
    int kGameplayViewH = std::max(1, (int)std::lround((float)kBaseScreenH / kGameplayZoom));

    SDL_Window* win = SDL_CreateWindow("Dorfplatformer Timetravel", kBaseScreenW, kBaseScreenH, SDL_WINDOW_RESIZABLE);
    if (!win) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow(resizable) failed: %s", SDL_GetError());
        win = SDL_CreateWindow("Dorfplatformer Timetravel", kBaseScreenW, kBaseScreenH, 0);
    }
    if (!win) {
        reportStartupError("Window Error", std::string("SDL_CreateWindow failed: ") + SDL_GetError(), nullptr);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (!ren) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer(default) failed: %s", SDL_GetError());
        ren = SDL_CreateRenderer(win, "software");
    }
    if (!ren) {
        reportStartupError("Renderer Error", std::string("SDL_CreateRenderer failed: ") + SDL_GetError(), win);
        SDL_DestroyWindow(win);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }
    SDL_Log("Window and renderer created");
    SDL_Texture* gameTarget = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        kBaseScreenW, kBaseScreenH
    );
    SDL_Texture* worldTarget = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        kGameplayViewW, kGameplayViewH
    );
    if (!gameTarget || !worldTarget) {
        reportStartupError("Render Target Error", std::string("SDL_CreateTexture failed: ") + SDL_GetError(), win);
        if (worldTarget) SDL_DestroyTexture(worldTarget);
        if (gameTarget) SDL_DestroyTexture(gameTarget);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        ShutdownTextRenderer();
        CrashReporter::stop();
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(worldTarget, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(gameTarget, SDL_SCALEMODE_NEAREST);
    SDL_Window* debugWin = nullptr;
    SDL_Renderer* debugRen = nullptr;
    bool showDetailedDebugger = false;
    std::array<float, 240> frameMsHistory{};
    std::array<float, 240> memRssHistory{};
    int frameMsHistoryHead = 0;

    nlohmann::json texJson;
    {
        const std::string text = ReadTextFile("assets/textures.json");
        if (!text.empty()) {
            try { texJson = nlohmann::json::parse(text); } catch (...) { texJson = nlohmann::json(); }
        }
    }
    auto texPath = [&](const std::string& section, const std::string& key, const std::string& fallback) -> std::string {
        if (texJson.contains(section) && texJson[section].is_object()) {
            const auto& t = texJson[section];
            if (t.contains(key) && t[key].is_string()) return t[key].get<std::string>();
        }
        return fallback;
    };

    std::string pausePlist = texPath("plists", "pause", "assets/Sheets/DF_Pause-uhd.plist");
    SDL_Texture* pauseTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "pause", "assets/Sheets/DF_Pause-uhd.png")).c_str());
    if (pauseTex) SDL_SetTextureScaleMode(pauseTex, SDL_SCALEMODE_NEAREST);
    auto pauseFrames = loadPlistFrames(pausePlist);

    std::string blocksPlist = texPath("plists", "blocks", "assets/Sheets/DF_Blocks-uhd.plist");
    SDL_Texture* blocksTex = loadTextureWithColorKey(ren, texPath("textures", "blocks", "assets/Sheets/DF_Blocks-uhd.png"), 0x9f, 0x61, 0xff);
    auto blocksFrameList = loadPlistFrameList(blocksPlist);
    std::unordered_map<std::string, Frame> blocksFrameByName;
    blocksFrameByName.reserve(blocksFrameList.size());
    for (const auto& e : blocksFrameList) blocksFrameByName[e.name] = e.frame;
    std::vector<const Frame*> blocksFrameById(65536, nullptr);
    for (const auto& e : blocksFrameList) {
        bool numeric = !e.name.empty();
        for (char ch : e.name) {
            if (!std::isdigit((unsigned char)ch)) {
                numeric = false;
                break;
            }
        }
        if (!numeric) continue;
        int id = 0;
        try { id = std::stoi(e.name); } catch (...) { continue; }
        if (id < 0 || id >= (int)blocksFrameById.size()) continue;
        blocksFrameById[id] = &e.frame;
    }
    const Frame* cycleFrames[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 8; ++i) {
        const std::string key = std::string("c") + std::to_string(i + 1);
        auto it = blocksFrameByName.find(key);
        if (it != blocksFrameByName.end()) cycleFrames[i] = &it->second;
    }
    const Frame* world3PatternFrames[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 10; ++i) {
        const std::string key = std::string("3.") + std::to_string(i + 1);
        auto it = blocksFrameByName.find(key);
        if (it != blocksFrameByName.end()) world3PatternFrames[i] = &it->second;
        else {
            auto itPng = blocksFrameByName.find(key + ".png");
            if (itPng != blocksFrameByName.end()) world3PatternFrames[i] = &itPng->second;
            else {
                const std::string fallbackKey = std::string("c") + std::to_string(i + 1);
                auto itFallback = blocksFrameByName.find(fallbackKey);
                if (itFallback != blocksFrameByName.end()) world3PatternFrames[i] = &itFallback->second;
                else {
                    auto itFallbackPng = blocksFrameByName.find(fallbackKey + ".png");
                    if (itFallbackPng != blocksFrameByName.end()) world3PatternFrames[i] = &itFallbackPng->second;
                }
            }
        }
    }

    const std::string bgTexPathFallback = "assets/Sheets/DF_Background-uhd.png";
    const std::string bgPlistFallback = "assets/Sheets/DF_Background-uhd.plist";
    SDL_Texture* bgTexWorld1 = nullptr;
    SDL_Texture* bgTexWorld2 = nullptr;
    SDL_Texture* bgTexWorld4 = nullptr;
    SDL_Texture* bgTexWorld5 = nullptr;
    SDL_Texture* bgTexWorld6 = nullptr;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld1;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld2;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld4;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld5;
    std::unordered_map<std::string, Frame> bgFrameByNameWorld6;
    std::vector<Frame> bgFrameListWorld1;
    std::vector<Frame> bgFrameListWorld2;
    std::vector<Frame> bgFrameListWorld4;
    std::vector<Frame> bgFrameListWorld5;
    std::vector<Frame> bgAnimFramesWorld6;
    auto loadBgSheet = [&](const std::string& texPathPrimary,
                           const std::string& plistPrimary,
                           SDL_Texture*& outTex,
                           std::unordered_map<std::string, Frame>& outFrames,
                           std::vector<Frame>* outAnimFrames = nullptr) {
        outTex = IMG_LoadTexture(ren, ResolveAssetPath(texPathPrimary).c_str());
        if (outTex) SDL_SetTextureScaleMode(outTex, SDL_SCALEMODE_NEAREST);
        auto bgFrameList = loadPlistFrameList(plistPrimary);
        if (!outTex || bgFrameList.empty()) {
            if (outTex) {
                SDL_DestroyTexture(outTex);
                outTex = nullptr;
            }
            outTex = IMG_LoadTexture(ren, ResolveAssetPath(bgTexPathFallback).c_str());
            if (outTex) SDL_SetTextureScaleMode(outTex, SDL_SCALEMODE_NEAREST);
            bgFrameList = loadPlistFrameList(bgPlistFallback);
        }
        outFrames.clear();
        outFrames.reserve(bgFrameList.size());
        for (const auto& e : bgFrameList) outFrames[e.name] = e.frame;
        if (outAnimFrames) {
            outAnimFrames->clear();
            outAnimFrames->reserve(bgFrameList.size());
            for (const auto& e : bgFrameList) outAnimFrames->push_back(e.frame);
        }
    };
    loadBgSheet(
        texPath("textures", "background_world1", "assets/Sheets/DF_Back_1-uhd.png"),
        texPath("plists", "background_world1", "assets/Sheets/DF_Back_1-uhd.plist"),
        bgTexWorld1,
        bgFrameByNameWorld1,
        &bgFrameListWorld1
    );
    loadBgSheet(
        texPath("textures", "background_world2", "assets/Sheets/DF_Back_2-uhd.png"),
        texPath("plists", "background_world2", "assets/Sheets/DF_Back_2-uhd.plist"),
        bgTexWorld2,
        bgFrameByNameWorld2,
        &bgFrameListWorld2
    );
    loadBgSheet(
        texPath("textures", "background_world4", "assets/Sheets/DF_Back_4-uhd.png"),
        texPath("plists", "background_world4", "assets/Sheets/DF_Back_4-uhd.plist"),
        bgTexWorld4,
        bgFrameByNameWorld4,
        &bgFrameListWorld4
    );
    loadBgSheet(
        texPath("textures", "background_world5", "assets/Sheets/DF_Back_5-uhd.png"),
        texPath("plists", "background_world5", "assets/Sheets/DF_Back_5-uhd.plist"),
        bgTexWorld5,
        bgFrameByNameWorld5,
