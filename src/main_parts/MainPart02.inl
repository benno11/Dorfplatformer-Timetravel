        &bgFrameListWorld5
    );
    loadBgSheet(
        texPath("textures", "background_world6", "assets/Sheets/DF_Back_6-uhd.png"),
        texPath("plists", "background_world6", "assets/Sheets/DF_Back_6-uhd.plist"),
        bgTexWorld6,
        bgFrameByNameWorld6,
        &bgAnimFramesWorld6
    );
    SDL_Texture* introCardTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/Introcard-uhd.png").c_str());
    if (introCardTex) SDL_SetTextureScaleMode(introCardTex, SDL_SCALEMODE_NEAREST);
    auto introCardFrames = loadPlistFrames("assets/Sheets/Introcard-uhd.plist");
    SDL_Texture* endSignTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/end_sign-uhd.png").c_str());
    if (endSignTex) SDL_SetTextureScaleMode(endSignTex, SDL_SCALEMODE_NEAREST);
    auto endSignFrames = loadPlistFrames("assets/Sheets/end_sign-uhd.plist");
    const Frame* defaultEndSignFrame = nullptr;
    {
        auto it = endSignFrames.find("SignPost9");
        if (it == endSignFrames.end()) it = endSignFrames.find("SignPost9.png");
        if (it != endSignFrames.end()) defaultEndSignFrame = &it->second;
    }
    SDL_Texture* bossesTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/DF_Bosses-uhd.png").c_str());
    if (bossesTex) SDL_SetTextureScaleMode(bossesTex, SDL_SCALEMODE_NEAREST);
    auto bossesFrames = loadPlistFrames("assets/Sheets/DF_Bosses-uhd.plist");
    const Frame* bossNormalFrame = nullptr;
    const Frame* bossHurtFrame = nullptr;
    const Frame* bossFinalNormalFrame = nullptr;
    {
        auto it = bossesFrames.find("Normal-1");
        if (it == bossesFrames.end()) it = bossesFrames.find("Normal-1.png");
        if (it != bossesFrames.end()) bossNormalFrame = &it->second;
        it = bossesFrames.find("Hurt-1");
        if (it == bossesFrames.end()) it = bossesFrames.find("Hurt-1.png");
        if (it != bossesFrames.end()) bossHurtFrame = &it->second;
        it = bossesFrames.find("Final-Normal");
        if (it == bossesFrames.end()) it = bossesFrames.find("Final-Normal.png");
        if (it != bossesFrames.end()) bossFinalNormalFrame = &it->second;
    }

    const std::string entitiesPlist = "assets/Sheets/DF_Enitys-uhd.plist";
    SDL_Texture* entitiesTex = IMG_LoadTexture(ren, ResolveAssetPath("assets/Sheets/DF_Enitys-uhd.png").c_str());
    if (entitiesTex) SDL_SetTextureScaleMode(entitiesTex, SDL_SCALEMODE_NEAREST);
    auto entitiesFrameList = loadPlistFrameList(entitiesPlist);
    std::unordered_map<std::string, Frame> entitiesFrameByName;
    entitiesFrameByName.reserve(entitiesFrameList.size());
    for (const auto& e : entitiesFrameList) entitiesFrameByName[e.name] = e.frame;
    std::unordered_map<std::string, std::string> entityFrameKeyByObjectId;
    entityFrameKeyByObjectId["31"] = "Spring";
    const Frame* defaultEntityFrame = !entitiesFrameList.empty() ? &entitiesFrameList[0].frame : nullptr;

    std::unordered_map<int, std::string> tileFrameById;
    {
        const std::string text = ReadTextFile("assets/tile_defs.json");
        if (!text.empty()) {
            nlohmann::json j;
            try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
            auto readSection = [&](const char* key) {
                if (!j.contains(key) || !j[key].is_object()) return;
                for (auto it = j[key].begin(); it != j[key].end(); ++it) {
                    if (!it.value().is_object()) continue;
                    if (!it.value().contains("frame") || !it.value()["frame"].is_string()) continue;
                    int id = 0;
                    try { id = std::stoi(it.key()); } catch (...) { continue; }
                    tileFrameById[id] = it.value()["frame"].get<std::string>();
                }
            };
            readSection("bg");
            readSection("fg");
        }
    }

    std::string playerPlist = texPath("plists", "player", "assets/Sheets/DF_Player1-uhd.plist");
    SDL_Texture* playerTex = IMG_LoadTexture(ren, ResolveAssetPath(texPath("textures", "player", "assets/Sheets/DF_Player1-uhd.png")).c_str());
    if (playerTex) SDL_SetTextureScaleMode(playerTex, SDL_SCALEMODE_NEAREST);
    auto playerFrameList = loadPlistFrameList(playerPlist);
    std::unordered_map<std::string, Frame> playerFramesByName;
    playerFramesByName.reserve(playerFrameList.size());
    for (const auto& e : playerFrameList) playerFramesByName[e.name] = e.frame;
    const Frame* fallbackPlayerFrame = !playerFrameList.empty() ? &playerFrameList[0].frame : nullptr;
    audio.loadGlobalAssets();
    std::unordered_map<int, std::string> worldNamesById;
    struct IntroTextStyle {
        int x = 150;
        int y = 332;
        int scale = 4;
    };
    IntroTextStyle introWorldNameStyle;
    IntroTextStyle introAreaIdStyle{545, 322, 8};
    {
        const std::string worldNameText = ReadTextFile("assets/world_names.json");
        if (!worldNameText.empty()) {
            try {
                const nlohmann::json j = nlohmann::json::parse(worldNameText);
                auto parseIntMap = [](const nlohmann::json& obj, std::unordered_map<int, std::string>& out) {
                    if (!obj.is_object()) return;
                    for (auto it = obj.begin(); it != obj.end(); ++it) {
                        if (!it.value().is_string()) continue;
                        try {
                            out[std::stoi(it.key())] = it.value().get<std::string>();
                        } catch (...) {}
                    }
                };
                auto parseStyle = [](const nlohmann::json& obj, IntroTextStyle& style) {
                    if (!obj.is_object()) return;
                    if (obj.contains("x") && obj["x"].is_number_integer()) style.x = obj["x"].get<int>();
                    if (obj.contains("y") && obj["y"].is_number_integer()) style.y = obj["y"].get<int>();
                    if (obj.contains("scale") && obj["scale"].is_number_integer()) style.scale = std::max(1, obj["scale"].get<int>());
                };
                if (j.is_object()) {
                    if (j.contains("world_names")) parseIntMap(j["world_names"], worldNamesById);
                    if (worldNamesById.empty()) parseIntMap(j, worldNamesById);
                    if (j.contains("intro_layout") && j["intro_layout"].is_object()) {
                        const auto& layout = j["intro_layout"];
                        if (layout.contains("world_name")) parseStyle(layout["world_name"], introWorldNameStyle);
                        if (layout.contains("area_id")) parseStyle(layout["area_id"], introAreaIdStyle);
                    }
                }
            } catch (...) {}
        }
    }

    auto getPlayerFrame = [&](const std::string& name) -> const Frame* {
        auto it = playerFramesByName.find(name);
        if (it == playerFramesByName.end()) return fallbackPlayerFrame;
        return &it->second;
    };

    struct AnimConfig {
        float fps = 20.0f;
        std::vector<std::string> idle;
        std::vector<std::string> walk;
        std::vector<std::string> jump;
        std::vector<std::string> fall;
        std::vector<std::string> crouch;
        std::vector<std::string> skid;
        std::vector<std::string> hurt;
        std::vector<std::string> death;
    };

    auto loadAnimConfig = [&](const std::string& path) -> AnimConfig {
        AnimConfig cfg;
        const std::string text = ReadTextFile(path);
        if (text.empty()) return cfg;
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(text);
        } catch (...) {
            return cfg;
        }
        if (j.contains("fps") && j["fps"].is_number()) cfg.fps = (float)j["fps"].get<double>();
        auto readList = [&](const char* key, std::vector<std::string>& out) {
            if (!j.contains(key) || !j[key].is_array()) return;
            out.clear();
            for (const auto& v : j[key]) {
                if (v.is_string()) out.push_back(v.get<std::string>());
            }
        };
        readList("idle", cfg.idle);
        readList("walk", cfg.walk);
        readList("jump", cfg.jump);
        readList("fall", cfg.fall);
        readList("crouch", cfg.crouch);
        readList("skid", cfg.skid);
        readList("hurt", cfg.hurt);
        readList("death", cfg.death);
        return cfg;
    };

    AnimConfig animCfg = loadAnimConfig("assets/player_anim.json");
    auto framesFromNames = [&](const std::vector<std::string>& names) {
        std::vector<const Frame*> out;
        out.reserve(names.size());
        for (const auto& n : names) out.push_back(getPlayerFrame(n));
        return out;
    };
    const std::vector<const Frame*> animIdleFrames = framesFromNames(animCfg.idle);
    const std::vector<const Frame*> animWalkFrames = framesFromNames(animCfg.walk);
    const std::vector<const Frame*> animJumpFrames = framesFromNames(animCfg.jump);
    const std::vector<const Frame*> animFallFrames = framesFromNames(animCfg.fall);
    const std::vector<const Frame*> animCrouchFrames = framesFromNames(animCfg.crouch);
    const std::vector<const Frame*> animSkidFrames = framesFromNames(animCfg.skid);
    const std::vector<const Frame*> animHurtFrames = framesFromNames(animCfg.hurt);
    const std::vector<const Frame*> animDeathFrames = framesFromNames(animCfg.death);
    float jumpBufferMax = 0.12f;
    std::string levelServerUrl;
    std::string levelServerAuthToken;
    std::string appVersion = "dev";
    MovementConfig movementCfg{};
    float bossGravity = 0.0f;
    std::array<float, 3> parallaxLayerScales{{0.80f, 0.80f, 0.80f}};
    {
        const std::string text = ReadTextFile("assets/config.json");
        if (!text.empty()) {
            nlohmann::json cfg;
            try { cfg = nlohmann::json::parse(text); } catch (...) { cfg = nlohmann::json(); }
            if (cfg.contains("version") && cfg["version"].is_string()) {
                appVersion = cfg["version"].get<std::string>();
            }
            if (cfg.contains("level_server_url") && cfg["level_server_url"].is_string()) {
                levelServerUrl = cfg["level_server_url"].get<std::string>();
            }
            if (cfg.contains("level_server_auth_token") && cfg["level_server_auth_token"].is_string()) {
                levelServerAuthToken = cfg["level_server_auth_token"].get<std::string>();
            }
            if (cfg.contains("jump_buffer_seconds") && cfg["jump_buffer_seconds"].is_number()) {
                jumpBufferMax = (float)cfg["jump_buffer_seconds"].get<double>();
                if (jumpBufferMax < 0.0f) jumpBufferMax = 0.0f;
            }
            if (cfg.contains("movement") && cfg["movement"].is_object()) {
                const auto& m = cfg["movement"];
                auto readMove = [&](const char* key, float& out) {
                    if (m.contains(key) && m[key].is_number()) out = (float)m[key].get<double>();
                };
                readMove("accel_in_water", movementCfg.accelInWater);
                readMove("accel_ground", movementCfg.accelGround);
                readMove("max_speed_in_water", movementCfg.maxSpeedInWater);
                readMove("max_speed_ground", movementCfg.maxSpeedGround);
                readMove("friction_in_water", movementCfg.frictionInWater);
                readMove("friction_ground", movementCfg.frictionGround);
                readMove("gravity_in_water", movementCfg.gravityInWater);
                readMove("gravity_ground", movementCfg.gravityGround);
                readMove("jump_speed", movementCfg.jumpSpeed);
                readMove("jump_hold_gravity", movementCfg.jumpHoldGravity);
                readMove("jump_hold_max", movementCfg.jumpHoldMax);
                readMove("jump_cut_speed", movementCfg.jumpCutSpeed);
                readMove("swim_up_speed", movementCfg.swimUpSpeed);
                readMove("swim_rise", movementCfg.swimRise);
                readMove("boss_gravity", bossGravity);
            }
            if (cfg.contains("background") && cfg["background"].is_object()) {
                const auto& bg = cfg["background"];
                if (bg.contains("parallax_layer_scales") && bg["parallax_layer_scales"].is_array()) {
                    const auto& a = bg["parallax_layer_scales"];
                    for (int i = 0; i < 3 && i < (int)a.size(); ++i) {
                        if (a[i].is_number()) {
                            parallaxLayerScales[i] = std::clamp((float)a[i].get<double>(), 0.1f, 4.0f);
                        }
                    }
                }
            }
        }
    }
    SetLevelServerUrl(levelServerUrl);
    SetLevelServerAuthToken(levelServerAuthToken);
    if (!levelServerUrl.empty()) {
        SDL_Log("Level server: %s", levelServerUrl.c_str());
    }
    if (!levelServerAuthToken.empty()) {
        SDL_Log("Level server auth token configured");
    }
    {
        const std::string text = ReadTextFile("assets/log_settings.json");
        if (!text.empty()) {
            nlohmann::json logCfg;
            try { logCfg = nlohmann::json::parse(text); } catch (...) { logCfg = nlohmann::json(); }
            auto parsePriority = [](const nlohmann::json& v, SDL_LogPriority fallback) -> SDL_LogPriority {
                if (v.is_number_integer()) {
                    int p = v.get<int>();
                    if (p >= SDL_LOG_PRIORITY_VERBOSE && p <= SDL_LOG_PRIORITY_CRITICAL) {
                        return (SDL_LogPriority)p;
                    }
                    return fallback;
                }
                if (!v.is_string()) return fallback;
                std::string s = v.get<std::string>();
                for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
                if (s == "verbose") return SDL_LOG_PRIORITY_VERBOSE;
                if (s == "debug") return SDL_LOG_PRIORITY_DEBUG;
                if (s == "info") return SDL_LOG_PRIORITY_INFO;
                if (s == "warn" || s == "warning") return SDL_LOG_PRIORITY_WARN;
                if (s == "error") return SDL_LOG_PRIORITY_ERROR;
                if (s == "critical" || s == "fatal") return SDL_LOG_PRIORITY_CRITICAL;
                return fallback;
            };
            auto categoryFromName = [](const std::string& name) -> int {
                if (name == "application" || name == "app") return SDL_LOG_CATEGORY_APPLICATION;
                if (name == "error") return SDL_LOG_CATEGORY_ERROR;
                if (name == "assert") return SDL_LOG_CATEGORY_ASSERT;
                if (name == "system") return SDL_LOG_CATEGORY_SYSTEM;
                if (name == "audio") return SDL_LOG_CATEGORY_AUDIO;
                if (name == "video") return SDL_LOG_CATEGORY_VIDEO;
                if (name == "render") return SDL_LOG_CATEGORY_RENDER;
                if (name == "input") return SDL_LOG_CATEGORY_INPUT;
                if (name == "test") return SDL_LOG_CATEGORY_TEST;
                return SDL_LOG_CATEGORY_CUSTOM;
            };

            SDL_LogPriority defaultPriority = SDL_LOG_PRIORITY_INFO;
            if (logCfg.contains("global_priority")) {
                defaultPriority = parsePriority(logCfg["global_priority"], defaultPriority);
            }
            SDL_LogSetAllPriority(defaultPriority);

            if (logCfg.contains("categories") && logCfg["categories"].is_object()) {
                for (auto it = logCfg["categories"].begin(); it != logCfg["categories"].end(); ++it) {
                    std::string key = it.key();
                    for (char& ch : key) ch = (char)std::tolower((unsigned char)ch);
                    int category = categoryFromName(key);
                    SDL_LogSetPriority(category, parsePriority(it.value(), defaultPriority));
                }
            }
        }
    }

    if (!pauseTex || !blocksTex || !playerTex || pauseFrames.empty() || blocksFrameList.empty() || playerFrameList.empty()) {
        std::string msg = "Failed to load assets:";
        if (!pauseTex) msg += "\n- pause texture";
        if (pauseFrames.empty()) msg += "\n- pause plist";
        if (!blocksTex) msg += "\n- blocks texture";
        if (blocksFrameList.empty()) msg += "\n- blocks plist";
        if (!playerTex) msg += "\n- player texture";
        if (playerFrameList.empty()) msg += "\n- player plist";
        reportStartupError("Asset Load Error", msg, win);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        audio.shutdown();
        ShutdownTextRenderer();
        SDL_Quit();
        return 1;
    }
    if (!gameTarget) {
        reportStartupError("Render Target Error", "Failed to create game render target.", win);
        if (playerTex) SDL_DestroyTexture(playerTex);
        if (bgTexWorld1) SDL_DestroyTexture(bgTexWorld1);
        if (bgTexWorld2) SDL_DestroyTexture(bgTexWorld2);
        if (bgTexWorld4) SDL_DestroyTexture(bgTexWorld4);
        if (bgTexWorld5) SDL_DestroyTexture(bgTexWorld5);
        if (bgTexWorld6) SDL_DestroyTexture(bgTexWorld6);
        if (introCardTex) SDL_DestroyTexture(introCardTex);
        if (blocksTex) SDL_DestroyTexture(blocksTex);
        if (entitiesTex) SDL_DestroyTexture(entitiesTex);
        if (bossesTex) SDL_DestroyTexture(bossesTex);
        if (endSignTex) SDL_DestroyTexture(endSignTex);
        if (pauseTex) SDL_DestroyTexture(pauseTex);
        if (worldTarget) SDL_DestroyTexture(worldTarget);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        audio.shutdown();
        ShutdownTextRenderer();
        SDL_Quit();
        return 1;
    }

    bool fullscreen = false;
    bool vsyncEnabled = false;
    bool clampCamX = true;
    bool defaultShowFpsCounter = false;
    bool defaultShowDetailedDebugger = false;
    bool defaultShowHitboxes = false;
    bool defaultShowPlayerHitbox = false;
    bool defaultShowDebugView = false;
    bool defaultHideUnknownObjectTypes = false;
    bool powerManagementEnabled = true;
    bool menuMusicEnabled = true;
    bool muteAllAudio = false;
    KeyboardBindings keybinds{};
    constexpr int kUiScaleMinPercent = 50;
    constexpr int kUiScaleMaxPercent = 200;
    int uiScalePercent = kUiScaleMaxPercent;
    std::array<bool, 55> extraSettings{};
    extraSettings[44] = true; // PRIVACY+ -> SEND ANONYMOUS METRICS
    const std::string defaultTelemetryWebhook = "https://discord.com/api/webhooks/1471610164829356085/at2iXFzt7euIGzvIaN8iQEgNS6m1RfKUShwq6RPyIUUefIO7Id-uWxdB9Mo4wP1WKVWj";
    std::string telemetryWebhookUrl = defaultTelemetryWebhook;
    if (const char* envHook = std::getenv("DF_DISCORD_WEBHOOK"); envHook && *envHook) {
        telemetryWebhookUrl = envHook;
    }
    float fastTravelChangeDelay = 0.0f;
    int musicVolume = 96; // 0..128
    int sfxVolume = 96;   // 0..128
    const std::string localClientSettingsPath = "client_settings.json";
    const std::string appSaveRootPath = GetAppSaveRootPath();
    const std::filesystem::path replayDirPath = std::filesystem::path(appSaveRootPath) / "replays";
    std::string clientSettingsPath = localClientSettingsPath;
    if (!appSaveRootPath.empty()) {
        clientSettingsPath = (std::filesystem::path(appSaveRootPath) / "client_settings.json").string();
    }
    auto saveClientSettings = [&]() {
        nlohmann::json j;
        j["build_uuid"] = buildUuid;
        nlohmann::json settings;
        settings["display"] = {
            {"fullscreen", fullscreen},
            {"vsync", vsyncEnabled},
            {"ui_scale_percent", uiScalePercent}
        };
        settings["camera"] = {
            {"clamp_cam_x", clampCamX}
        };
        settings["debug"] = {
            {"show_fps_counter", defaultShowFpsCounter},
            {"show_detailed_debugger", defaultShowDetailedDebugger},
            {"show_hitboxes", defaultShowHitboxes},
            {"show_player_hitbox", defaultShowPlayerHitbox},
            {"show_debug_view", defaultShowDebugView},
            {"hide_unknown_object_types", defaultHideUnknownObjectTypes}
        };
        settings["audio"] = {
            {"menu_music_enabled", menuMusicEnabled},
            {"mute_all_audio", muteAllAudio},
            {"music_volume", musicVolume},
            {"sfx_volume", sfxVolume}
        };
        settings["controls"] = {
            {"move_left", (int)keybinds.moveLeft},
            {"move_right", (int)keybinds.moveRight},
            {"move_down", (int)keybinds.moveDown},
            {"jump", (int)keybinds.jump},
            {"pause", (int)keybinds.pause}
        };
        settings["gameplay"] = {
            {"power_management", powerManagementEnabled},
            {"fast_travel_delay", fastTravelChangeDelay}
        };
        settings["telemetry"] = {
            {"telemetry_webhook_url", telemetryWebhookUrl}
        };
        {
            nlohmann::json extra = nlohmann::json::array();
            for (bool v : extraSettings) extra.push_back(v);
            settings["extra_settings"] = std::move(extra);
        }
        j["settings"] = std::move(settings);

        // Legacy flat keys kept for backward compatibility.
        j["fullscreen"] = fullscreen;
        j["vsync"] = vsyncEnabled;
        j["clamp_cam_x"] = clampCamX;
        j["show_fps_counter"] = defaultShowFpsCounter;
        j["show_detailed_debugger"] = defaultShowDetailedDebugger;
        j["show_hitboxes"] = defaultShowHitboxes;
        j["show_player_hitbox"] = defaultShowPlayerHitbox;
        j["show_debug_view"] = defaultShowDebugView;
        j["hide_unknown_object_types"] = defaultHideUnknownObjectTypes;
        j["power_management"] = powerManagementEnabled;
        j["menu_music_enabled"] = menuMusicEnabled;
        j["mute_all_audio"] = muteAllAudio;
        j["key_move_left"] = (int)keybinds.moveLeft;
        j["key_move_right"] = (int)keybinds.moveRight;
        j["key_move_down"] = (int)keybinds.moveDown;
        j["key_jump"] = (int)keybinds.jump;
        j["key_pause"] = (int)keybinds.pause;
        j["ui_scale_percent"] = uiScalePercent;
        j["extra_settings"] = j["settings"]["extra_settings"];
        j["telemetry_webhook_url"] = telemetryWebhookUrl;
        j["fast_travel_delay"] = fastTravelChangeDelay;
        j["music_volume"] = musicVolume;
        j["sfx_volume"] = sfxVolume;
        auto tryWriteSettings = [&](const std::string& path) -> bool {
            try {
                std::filesystem::path p(path);
                if (p.has_parent_path()) {
                    std::error_code ec;
                    std::filesystem::create_directories(p.parent_path(), ec);
                }
                const std::filesystem::path tmp = p.string() + ".tmp";
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) return false;
                out << j.dump(2);
                out.flush();
                if (!out.good()) return false;
                out.close();
                std::error_code ec;
                std::filesystem::rename(tmp, p, ec);
                if (ec) {
                    // Replace existing target if rename can't overwrite.
                    std::filesystem::remove(p, ec);
                    ec.clear();
                    std::filesystem::rename(tmp, p, ec);
                    if (ec) return false;
                }
                return true;
            } catch (...) {
                return false;
            }
        };
        if (!tryWriteSettings(clientSettingsPath)) {
            (void)tryWriteSettings(localClientSettingsPath);
        }
    };
    {
        auto applyClientSettingsJson = [&](const nlohmann::json& j) {
            auto parseScancode = [](const nlohmann::json& v, SDL_Scancode fallback) -> SDL_Scancode {
                if (!v.is_number_integer()) return fallback;
                const int raw = v.get<int>();
                if (raw <= (int)SDL_SCANCODE_UNKNOWN || raw >= (int)SDL_SCANCODE_COUNT) return fallback;
                return (SDL_Scancode)raw;
            };
            const bool hasStructured = j.contains("settings") && j["settings"].is_object();
            if (hasStructured) {
                const auto& s = j["settings"];
                if (s.contains("display") && s["display"].is_object()) {
                    const auto& d = s["display"];
                    if (d.contains("fullscreen") && d["fullscreen"].is_boolean()) fullscreen = d["fullscreen"].get<bool>();
                    if (d.contains("vsync") && d["vsync"].is_boolean()) vsyncEnabled = d["vsync"].get<bool>();
                    if (d.contains("ui_scale_percent") && d["ui_scale_percent"].is_number_integer()) {
                        uiScalePercent = std::clamp(d["ui_scale_percent"].get<int>(), kUiScaleMinPercent, kUiScaleMaxPercent);
                    }
                }
                if (s.contains("camera") && s["camera"].is_object()) {
                    const auto& c = s["camera"];
                    if (c.contains("clamp_cam_x") && c["clamp_cam_x"].is_boolean()) clampCamX = c["clamp_cam_x"].get<bool>();
                }
                if (s.contains("debug") && s["debug"].is_object()) {
                    const auto& d = s["debug"];
                    if (d.contains("show_fps_counter") && d["show_fps_counter"].is_boolean()) defaultShowFpsCounter = d["show_fps_counter"].get<bool>();
                    if (d.contains("show_detailed_debugger") && d["show_detailed_debugger"].is_boolean()) defaultShowDetailedDebugger = d["show_detailed_debugger"].get<bool>();
                    if (d.contains("show_hitboxes") && d["show_hitboxes"].is_boolean()) defaultShowHitboxes = d["show_hitboxes"].get<bool>();
                    if (d.contains("show_player_hitbox") && d["show_player_hitbox"].is_boolean()) defaultShowPlayerHitbox = d["show_player_hitbox"].get<bool>();
                    if (d.contains("show_debug_view") && d["show_debug_view"].is_boolean()) defaultShowDebugView = d["show_debug_view"].get<bool>();
                    if (d.contains("hide_unknown_object_types") && d["hide_unknown_object_types"].is_boolean()) defaultHideUnknownObjectTypes = d["hide_unknown_object_types"].get<bool>();
                }
                if (s.contains("audio") && s["audio"].is_object()) {
                    const auto& a = s["audio"];
                    if (a.contains("menu_music_enabled") && a["menu_music_enabled"].is_boolean()) menuMusicEnabled = a["menu_music_enabled"].get<bool>();
                    if (a.contains("mute_all_audio") && a["mute_all_audio"].is_boolean()) muteAllAudio = a["mute_all_audio"].get<bool>();
                    if (a.contains("music_volume") && a["music_volume"].is_number_integer()) musicVolume = std::clamp(a["music_volume"].get<int>(), 0, 128);
                    if (a.contains("sfx_volume") && a["sfx_volume"].is_number_integer()) sfxVolume = std::clamp(a["sfx_volume"].get<int>(), 0, 128);
                }
                if (s.contains("controls") && s["controls"].is_object()) {
                    const auto& c = s["controls"];
                    if (c.contains("move_left")) keybinds.moveLeft = parseScancode(c["move_left"], keybinds.moveLeft);
                    if (c.contains("move_right")) keybinds.moveRight = parseScancode(c["move_right"], keybinds.moveRight);
                    if (c.contains("move_down")) keybinds.moveDown = parseScancode(c["move_down"], keybinds.moveDown);
                    if (c.contains("jump")) keybinds.jump = parseScancode(c["jump"], keybinds.jump);
                    if (c.contains("pause")) keybinds.pause = parseScancode(c["pause"], keybinds.pause);
                }
                if (s.contains("gameplay") && s["gameplay"].is_object()) {
                    const auto& g = s["gameplay"];
                    if (g.contains("power_management") && g["power_management"].is_boolean()) powerManagementEnabled = g["power_management"].get<bool>();
                    if (g.contains("fast_travel_delay") && g["fast_travel_delay"].is_number()) {
                        // Deprecated: delay removed in favor of immediate smooth transitions.
                        fastTravelChangeDelay = 0.0f;
                    }
                }
                if (s.contains("telemetry") && s["telemetry"].is_object()) {
                    const auto& t = s["telemetry"];
                    if (t.contains("telemetry_webhook_url") && t["telemetry_webhook_url"].is_string()) {
                        telemetryWebhookUrl = t["telemetry_webhook_url"].get<std::string>();
                    }
                }
                if (s.contains("extra_settings") && s["extra_settings"].is_array()) {
                    const auto& a = s["extra_settings"];
                    for (size_t i = 0; i < extraSettings.size() && i < a.size(); ++i) {
                        if (a[i].is_boolean()) extraSettings[i] = a[i].get<bool>();
                    }
                }
                return;
            }

            // Legacy flat format fallback.
            if (j.contains("fullscreen") && j["fullscreen"].is_boolean()) fullscreen = j["fullscreen"].get<bool>();
            if (j.contains("vsync") && j["vsync"].is_boolean()) vsyncEnabled = j["vsync"].get<bool>();
            if (j.contains("clamp_cam_x") && j["clamp_cam_x"].is_boolean()) clampCamX = j["clamp_cam_x"].get<bool>();
            if (j.contains("show_fps_counter") && j["show_fps_counter"].is_boolean()) defaultShowFpsCounter = j["show_fps_counter"].get<bool>();
            if (j.contains("show_detailed_debugger") && j["show_detailed_debugger"].is_boolean()) defaultShowDetailedDebugger = j["show_detailed_debugger"].get<bool>();
            if (j.contains("show_hitboxes") && j["show_hitboxes"].is_boolean()) defaultShowHitboxes = j["show_hitboxes"].get<bool>();
            if (j.contains("show_player_hitbox") && j["show_player_hitbox"].is_boolean()) defaultShowPlayerHitbox = j["show_player_hitbox"].get<bool>();
            if (j.contains("show_debug_view") && j["show_debug_view"].is_boolean()) defaultShowDebugView = j["show_debug_view"].get<bool>();
            if (j.contains("hide_unknown_object_types") && j["hide_unknown_object_types"].is_boolean()) defaultHideUnknownObjectTypes = j["hide_unknown_object_types"].get<bool>();
            if (j.contains("power_management") && j["power_management"].is_boolean()) powerManagementEnabled = j["power_management"].get<bool>();
            if (j.contains("menu_music_enabled") && j["menu_music_enabled"].is_boolean()) menuMusicEnabled = j["menu_music_enabled"].get<bool>();
            if (j.contains("mute_all_audio") && j["mute_all_audio"].is_boolean()) muteAllAudio = j["mute_all_audio"].get<bool>();
            if (j.contains("key_move_left")) keybinds.moveLeft = parseScancode(j["key_move_left"], keybinds.moveLeft);
            if (j.contains("key_move_right")) keybinds.moveRight = parseScancode(j["key_move_right"], keybinds.moveRight);
            if (j.contains("key_move_down")) keybinds.moveDown = parseScancode(j["key_move_down"], keybinds.moveDown);
            if (j.contains("key_jump")) keybinds.jump = parseScancode(j["key_jump"], keybinds.jump);
            if (j.contains("key_pause")) keybinds.pause = parseScancode(j["key_pause"], keybinds.pause);
            if (j.contains("ui_scale_percent") && j["ui_scale_percent"].is_number_integer()) {
                uiScalePercent = std::clamp(j["ui_scale_percent"].get<int>(), kUiScaleMinPercent, kUiScaleMaxPercent);
            }
            if (j.contains("extra_settings") && j["extra_settings"].is_array()) {
                const auto& a = j["extra_settings"];
                for (size_t i = 0; i < extraSettings.size() && i < a.size(); ++i) {
                    if (a[i].is_boolean()) extraSettings[i] = a[i].get<bool>();
                }
            }
            if (j.contains("telemetry_webhook_url") && j["telemetry_webhook_url"].is_string()) {
                telemetryWebhookUrl = j["telemetry_webhook_url"].get<std::string>();
            }
            if (j.contains("fast_travel_delay") && j["fast_travel_delay"].is_number()) {
                // Deprecated: delay removed in favor of immediate smooth transitions.
                fastTravelChangeDelay = 0.0f;
            }
            if (j.contains("music_volume") && j["music_volume"].is_number_integer()) musicVolume = std::clamp(j["music_volume"].get<int>(), 0, 128);
            if (j.contains("sfx_volume") && j["sfx_volume"].is_number_integer()) sfxVolume = std::clamp(j["sfx_volume"].get<int>(), 0, 128);
        };

        std::string text = ReadTextFile(clientSettingsPath);
        if (text.empty() && clientSettingsPath != localClientSettingsPath) {
            text = ReadTextFile(localClientSettingsPath);
        }
        if (!text.empty()) {
            nlohmann::json j;
            try { j = nlohmann::json::parse(text); } catch (...) { j = nlohmann::json(); }
            applyClientSettingsJson(j);
        } else {
            const std::string placeholderPath = "assets/client_settings.json";
            const std::string placeholderText = ReadTextFile(placeholderPath);
            bool usedPlaceholder = false;
            if (!placeholderText.empty()) {
