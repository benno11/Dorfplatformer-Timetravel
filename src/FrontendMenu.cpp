#include "EditionPolicy.h"
#include "BuildInfo.h"
#include "FrontendMenu.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif

#include "AssetPath.h"
#include "NetworkTransport.h"
#include "GameSupport.h"
#include "InputSystem.h"
#include "Platform.h"
#include "SlimMenuSystem.h"
#include "TextRenderer.h"
#include "UiScale.h"
#include "ButtonAnimation.h"

FrontendAction runFrontendMenu(FrontendMenuContext& ctx, bool settingsOnly) {
    bool& running = *ctx.running;
    bool& fullscreen = *ctx.fullscreen;
    bool& vsyncEnabled = *ctx.vsyncEnabled;
    int& uiScalePercent = *ctx.uiScalePercent;
    bool& defaultShowFpsCounter = *ctx.defaultShowFpsCounter;
    int uiEdgePaddingLocal = 0;
    int& uiEdgePadding = ctx.uiEdgePadding ? *ctx.uiEdgePadding : uiEdgePaddingLocal;
    bool levelSelectEnabledLocal = true;
    bool& levelSelectEnabled = ctx.levelSelectEnabled ? *ctx.levelSelectEnabled : levelSelectEnabledLocal;
    if (Edition::lite) levelSelectEnabled = false;
    std::string networkServerUrlLocal, networkAuthTokenLocal, networkUsernameLocal;
    std::string& levelServerUrl = ctx.levelServerUrl ? *ctx.levelServerUrl : networkServerUrlLocal;
    std::string& levelServerAuthToken = ctx.levelServerAuthToken ? *ctx.levelServerAuthToken : networkAuthTokenLocal;
    std::string& levelServerAccountUsername = ctx.levelServerAccountUsername ? *ctx.levelServerAccountUsername : networkUsernameLocal;
    auto uiButtonScale = [&]() { return UiScale::multiplier(uiScalePercent); };
    auto fpsBadgeScale = [&]() { return UiScale::settingsMultiplier(uiScalePercent); };
    constexpr float kMenuAuthorW = 960.0f;
    constexpr float kMenuAuthorH = 540.0f;
    auto menuCanvasScale = [&]() -> float {
        const float sx = (float)ctx.baseScreenW / kMenuAuthorW;
        const float sy = (float)ctx.baseScreenH / kMenuAuthorH;
        return std::max(0.5f, std::min(sx, sy));
    };
    auto menuCanvasOriginX = [&]() -> float {
        const float s = menuCanvasScale();
        return ((float)ctx.baseScreenW - kMenuAuthorW * s) * 0.5f;
    };
    auto menuCanvasOriginY = [&]() -> float {
        const float s = menuCanvasScale();
        return ((float)ctx.baseScreenH - kMenuAuthorH * s) * 0.5f;
    };
    auto mainMenuScale = [&]() -> float {
        const float canvas = menuCanvasScale();
        const float desired = uiButtonScale();
        const float desiredBtnW = 112.0f * canvas * desired;
        const float desiredGap = 44.0f * canvas * desired;
        const float desiredTotalW = desiredBtnW * 3.0f + desiredGap * 2.0f;
        const float maxTotalW = std::max(260.0f, (float)ctx.baseScreenW - 120.0f);
        const float fit = (desiredTotalW > 0.0f) ? std::min(1.0f, maxTotalW / desiredTotalW) : 1.0f;
        return std::clamp(desired * fit, 0.55f, 5.0f);
    };
    auto scaleRectCentered = [&](const SDL_Rect& in, float scale) -> SDL_Rect {
        return UiScale::scaleRectCentered(in, scale);
    };

    auto applyRenderVsync = [&]() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (SDL_RenderSetVSync(ctx.ren, vsyncEnabled ? 1 : 0) != 0) {
            SDL_Log("Could not set renderer VSync=%d: %s", vsyncEnabled ? 1 : 0, SDL_GetError());
        }
#endif
    };
    auto applyFullscreen = [&](bool enabled) -> bool {
#if PLATFORMER_MOBILE
        (void)enabled;
        return false;
#else
        static int restoreX = SDL_WINDOWPOS_CENTERED;
        static int restoreY = SDL_WINDOWPOS_CENTERED;
        static int restoreW = 1280;
        static int restoreH = 720;
        const bool currentlyFullscreen = (SDL_GetWindowFlags(ctx.win) & SDL_WINDOW_FULLSCREEN) != 0;
        if (enabled && !currentlyFullscreen) {
            SDL_GetWindowPosition(ctx.win, &restoreX, &restoreY);
            SDL_GetWindowSize(ctx.win, &restoreW, &restoreH);
        }
        if (!SDL_SetWindowFullscreen(ctx.win, enabled)) {
            SDL_Log("Could not set menu fullscreen=%d: %s", enabled ? 1 : 0, SDL_GetError());
            return false;
        }
        if (!enabled) {
            if (restoreW <= 0 || restoreH <= 0) {
                restoreW = std::max(960, ctx.baseScreenW / 2);
                restoreH = std::max(540, ctx.baseScreenH / 2);
            }
            SDL_SetWindowSize(ctx.win, restoreW, restoreH);
            SDL_SetWindowPosition(ctx.win, restoreX, restoreY);
        }
        fullscreen = enabled;
        return true;
#endif
    };

    bool closeMenuOpen = false;
    int closeMenuSel = 0;
    int menuSel = -1;
    ButtonAnimations buttonAnimations;
    Uint64 inputBlockUntilTicks = 0;
    auto blockMenuInput = [&]() { inputBlockUntilTicks = SDL_GetTicks() + 250; };
    auto sanitizeAccountUsername = [](const std::string& in) -> std::string {
        std::string out;
        out.reserve(in.size());
        for (char ch : in) {
            const bool alphaNum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
            if (alphaNum || ch == '_' || ch == '-') out.push_back(ch);
        }
        if (out.size() > 48) out.resize(48);
        return out;
    };    auto loginWithGameServer = [&](const std::string& email, const std::string& password, std::string& errOut) -> bool {
        errOut.clear();
        if (Edition::lite) { errOut = "Accounts are unavailable in Lite."; return false; }
        SDL_Log("ACCOUNT: sign-in requested email=%s", email.c_str());
        if (email.empty() || password.empty()) {
            errOut = "Email/password required.";
            SDL_Log("ACCOUNT: sign-in rejected (missing email/password)");
            return false;
        }
        std::string serverBase = levelServerUrl;
        while (!serverBase.empty() && serverBase.back() == '/') serverBase.pop_back();
        if (serverBase.empty()) {
            errOut = "Game server URL is missing.";
            return false;
        }
#if defined(__ANDROID__)
        {
            JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            if (env) {
                jclass cls = env->FindClass("com/Benno111/dorfplatformertimetravel/MainActivity");
                if (!cls) {
                    if (env->ExceptionCheck()) env->ExceptionClear();
                } else {
                    jmethodID mid = env->GetStaticMethodID(
                        cls, "gameServerSignIn",
                        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
                    if (mid) {
                        jstring jApi = env->NewStringUTF(serverBase.c_str());
                        jstring jEmail = env->NewStringUTF(email.c_str());
                        jstring jPass = env->NewStringUTF(password.c_str());
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                        }
                        jobject jRespObj = PerformJavaNetworkObject(env, cls, mid, {jApi, jEmail, jPass}, (jint)12000);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                        }
                        if (jApi) env->DeleteLocalRef(jApi);
                        if (jEmail) env->DeleteLocalRef(jEmail);
                        if (jPass) env->DeleteLocalRef(jPass);
                        std::string respBody;
                        if (jRespObj) {
                            jstring jResp = static_cast<jstring>(jRespObj);
                            const char* cResp = env->GetStringUTFChars(jResp, nullptr);
                            if (cResp) {
                                respBody = cResp;
                                env->ReleaseStringUTFChars(jResp, cResp);
                            }
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                            }
                            env->DeleteLocalRef(jRespObj);
                        }
                        env->DeleteLocalRef(cls);
                        if (!respBody.empty()) {
                            nlohmann::json resp;
                            try { resp = nlohmann::json::parse(respBody); } catch (...) { resp = nlohmann::json(); }
                            if (resp.is_object() && resp.contains("idToken") && resp["idToken"].is_string()) {
                                levelServerAuthToken = resp["idToken"].get<std::string>();
                                if (resp.contains("displayName") && resp["displayName"].is_string()) {
                                    levelServerAccountUsername = sanitizeAccountUsername(resp["displayName"].get<std::string>());
                                }
                                if (levelServerAccountUsername.empty()) {
                                    const std::size_t atPos = email.find('@');
                                    const std::string localName = (atPos == std::string::npos) ? email : email.substr(0, atPos);
                                    levelServerAccountUsername = sanitizeAccountUsername(localName);
                                }
                                SDL_Log("ACCOUNT: sign-in ok (Java) email=%s username=%s",
                                        email.c_str(), levelServerAccountUsername.c_str());
                                return true;
                            }
                            if (resp.is_object() && resp.contains("error") && resp["error"].is_object() &&
                                resp["error"].contains("message") && resp["error"]["message"].is_string()) {
                                errOut = resp["error"]["message"].get<std::string>();
                                SDL_Log("ACCOUNT: sign-in failed (Java) email=%s reason=%s", email.c_str(), errOut.c_str());
                                return false;
                            }
                        }
                    } else {
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        env->DeleteLocalRef(cls);
                    }
                }
            }
        }
#endif
#if defined(HAVE_CURL) && HAVE_CURL
        CURL* curl = curl_easy_init();
        if (!curl) {
            errOut = "curl init failed.";
            return false;
        }
        const std::string url = serverBase + "/api/auth/login";
        nlohmann::json req;
        req["email"] = email;
        req["password"] = password;
        req["returnSecureToken"] = true;
        const std::string body = req.dump();
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string respBody;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, PLATFORMER_CLIENT_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
            +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                std::string* out = static_cast<std::string*>(userdata);
                out->append(ptr, size * nmemb);
                return size * nmemb;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respBody);
        const CURLcode rc = PerformNetworkRequest(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK || code < 200 || code >= 300) {
            if (!respBody.empty()) {
                try {
                    const nlohmann::json errJson = nlohmann::json::parse(respBody);
                    if (errJson.is_object() && errJson.contains("error") && errJson["error"].is_object() &&
                        errJson["error"].contains("message") && errJson["error"]["message"].is_string()) {
                        errOut = errJson["error"]["message"].get<std::string>();
                    }
                } catch (...) {}
            }
            if (errOut.empty()) errOut = "Login failed.";
            SDL_Log("ACCOUNT: sign-in failed email=%s http=%ld curl=%d reason=%s",
                    email.c_str(), code, (int)rc, errOut.c_str());
            return false;
        }
        nlohmann::json resp;
        try { resp = nlohmann::json::parse(respBody); } catch (...) { resp = nlohmann::json(); }
        if (!resp.is_object() || !resp.contains("idToken") || !resp["idToken"].is_string()) {
            errOut = "Invalid login response.";
            SDL_Log("ACCOUNT: sign-in failed email=%s reason=%s", email.c_str(), errOut.c_str());
            return false;
        }
        levelServerAuthToken = resp["idToken"].get<std::string>();
        if (resp.contains("displayName") && resp["displayName"].is_string()) {
            levelServerAccountUsername = sanitizeAccountUsername(resp["displayName"].get<std::string>());
        }
        if (levelServerAccountUsername.empty()) {
            const std::size_t atPos = email.find('@');
            const std::string localName = (atPos == std::string::npos) ? email : email.substr(0, atPos);
            levelServerAccountUsername = sanitizeAccountUsername(localName);
        }
        SDL_Log("ACCOUNT: sign-in ok email=%s username=%s", email.c_str(), levelServerAccountUsername.c_str());
        return true;
#else
        errOut = "curl disabled in this build.";
        SDL_Log("ACCOUNT: sign-in unavailable (curl disabled)");
        return false;
#endif
    };
    auto mainMenuBtnRect = [&](int idx) -> SDL_Rect {
        const int count = levelSelectEnabled ? 3 : 2;
        const float canvas = menuCanvasScale();
        const float s = mainMenuScale();
        const int btnW = std::max(56, (int)std::lround(112.0f * canvas * s));
        const int btnH = std::max(56, (int)std::lround(112.0f * canvas * s));
        const int step = std::max(btnW + 8, (int)std::lround((112.0f + 44.0f) * canvas * s));
        const int centerY = (int)std::lround(menuCanvasOriginY() + 266.0f * canvas);
        const float groupOffset = (float)idx - (float)(count - 1) * 0.5f;
        const int centerX = (int)std::lround(menuCanvasOriginX() + 480.0f * canvas + groupOffset * (float)step);
        return SDL_Rect{centerX - btnW / 2, centerY - btnH / 2, btnW, btnH};
    };
    auto mainMenuButtonCount = [&]() -> int {
        return levelSelectEnabled ? 3 : 2;
    };
    auto quitModalRect = [&]() -> SDL_Rect {
        const float s = uiButtonScale();
        SDL_Rect base{ctx.baseScreenW / 2 - 180, ctx.baseScreenH / 2 - 90, 360, 180};
        return scaleRectCentered(base, s);
    };
    auto quitResumeBtnRect = [&](const SDL_Rect& modal) -> SDL_Rect {
        const float s = uiButtonScale();
        return SDL_Rect{
            modal.x + (int)std::lround(26.0f * s),
            modal.y + (int)std::lround(94.0f * s),
            std::max(40, (int)std::lround(140.0f * s)),
            std::max(20, (int)std::lround(56.0f * s))
        };
    };
    auto quitCloseBtnRect = [&](const SDL_Rect& modal) -> SDL_Rect {
        const float s = uiButtonScale();
        return SDL_Rect{
            modal.x + (int)std::lround(194.0f * s),
            modal.y + (int)std::lround(94.0f * s),
            std::max(40, (int)std::lround(140.0f * s)),
            std::max(20, (int)std::lround(56.0f * s))
        };
    };
    SDL_Texture* mainMenuTex = loadTextureSafe(ctx.ren, "assets/Sheets/DF_Main_menu-uhd.png", nullptr);
    if (mainMenuTex) SDL_SetTextureScaleMode(mainMenuTex, SDL_SCALEMODE_NEAREST);
    auto mainMenuFrames = loadPlistFrames("assets/Sheets/DF_Main_menu-uhd.plist");
    SDL_Texture* menuFallbackTex = loadTextureSafe(ctx.ren, "assets/Sheets/DF_Menus-uhd.png", nullptr);
    if (menuFallbackTex) SDL_SetTextureScaleMode(menuFallbackTex, SDL_SCALEMODE_NEAREST);
    auto menuFallbackFrames = loadPlistFrames("assets/Sheets/DF_Menus-uhd.plist");
    std::string menuBgErr;
    SDL_Texture* menuBgTex = loadTextureSafe(ctx.ren, "assets/Sheets/DF_Back_1-uhd.png", &menuBgErr);
    auto menuBgFrames = loadPlistFrames("assets/Sheets/DF_Back_1-uhd.plist");
    if (!menuBgTex || menuBgFrames.empty()) {
        SDL_Log("Menu background load incomplete: texture=assets/Sheets/DF_Back_1-uhd.png loaded=%d plist frames=%d sdl_error='%s'",
                menuBgTex ? 1 : 0, (int)menuBgFrames.size(), menuBgErr.c_str());
    }
    auto getMenuFrame = [&](const char* name, SDL_Texture*& outTex) -> const Frame* {
        auto it = mainMenuFrames.find(name);
        if (it != mainMenuFrames.end()) {
            outTex = mainMenuTex;
            return &it->second;
        }
        auto itFallback = menuFallbackFrames.find(name);
        if (itFallback != menuFallbackFrames.end()) {
            outTex = menuFallbackTex;
            return &itFallback->second;
        }
        outTex = nullptr;
        return nullptr;
    };
    auto renderCenterLoopedFrame = [&](SDL_Texture* tex, const Frame& f, const SDL_Rect& dst) {
        if (!tex) return;
        if (f.rotated || f.rect.w < 3 || f.rect.h < 3 || dst.w <= 0 || dst.h <= 0) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }
        // Large UI atlas frames (e.g. window/button panels) are authored as 3x3 regions.
        // Decide per-axis so tall/narrow or wide/short frames still avoid stretch on the long axis.
        const bool useThirdSplitX = (f.rect.w >= 48);
        const bool useThirdSplitY = (f.rect.h >= 48);
        const int srcLeft = useThirdSplitX ? std::max(1, f.rect.w / 3) : (f.rect.w - 1) / 2;
        const int srcRight = useThirdSplitX ? std::max(1, f.rect.w / 3) : (f.rect.w - srcLeft - 1);
        const int srcTop = useThirdSplitY ? std::max(1, f.rect.h / 3) : (f.rect.h - 1) / 2;
        const int srcBottom = useThirdSplitY ? std::max(1, f.rect.h / 3) : (f.rect.h - srcTop - 1);
        if (srcLeft < 1 || srcRight < 1 || srcTop < 1 || srcBottom < 1) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }

        int dstLeft = std::min(srcLeft, std::max(0, dst.w / 2));
        int dstRight = std::min(srcRight, std::max(0, dst.w - dstLeft - 1));
        int dstTop = std::min(srcTop, std::max(0, dst.h / 2));
        int dstBottom = std::min(srcBottom, std::max(0, dst.h - dstTop - 1));
        int dstMidW = dst.w - dstLeft - dstRight;
        int dstMidH = dst.h - dstTop - dstBottom;
        if (dstMidW <= 0 || dstMidH <= 0) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }

        const int srcMidW = f.rect.w - srcLeft - srcRight;
        const int srcMidH = f.rect.h - srcTop - srcBottom;
        if (srcMidW <= 0 || srcMidH <= 0) {
            renderFrame(ctx.ren, tex, f, dst);
            return;
        }

        auto blit = [&](const SDL_Rect& src, const SDL_Rect& out) {
            if (src.w <= 0 || src.h <= 0 || out.w <= 0 || out.h <= 0) return;
            SDL_RenderTexture(ctx.ren, tex, &src, &out);
        };
        auto blitTiled = [&](const SDL_Rect& src, const SDL_Rect& out, bool tileX, bool tileY) {
            if (src.w <= 0 || src.h <= 0 || out.w <= 0 || out.h <= 0) return;
            const int stepX = tileX ? src.w : out.w;
            const int stepY = tileY ? src.h : out.h;
            for (int y = out.y; y < out.y + out.h; y += std::max(1, stepY)) {
                const int remH = out.y + out.h - y;
                const int h = tileY ? std::min(src.h, remH) : out.h;
                for (int x = out.x; x < out.x + out.w; x += std::max(1, stepX)) {
                    const int remW = out.x + out.w - x;
                    const int w = tileX ? std::min(src.w, remW) : out.w;
                    SDL_Rect s{src.x, src.y, w, h};
                    SDL_Rect d{x, y, w, h};
                    SDL_RenderTexture(ctx.ren, tex, &s, &d);
                    if (!tileX) break;
                }
                if (!tileY) break;
            }
        };

        const int cx = f.rect.x + srcLeft;
        const int cy = f.rect.y + srcTop;
        SDL_Rect sTL{f.rect.x, f.rect.y, srcLeft, srcTop};
        SDL_Rect sT{cx, f.rect.y, srcMidW, srcTop};
        SDL_Rect sTR{cx + srcMidW, f.rect.y, srcRight, srcTop};
        SDL_Rect sL{f.rect.x, cy, srcLeft, srcMidH};
        SDL_Rect sC{cx, cy, srcMidW, srcMidH};
        SDL_Rect sR{cx + srcMidW, cy, srcRight, srcMidH};
        SDL_Rect sBL{f.rect.x, cy + srcMidH, srcLeft, srcBottom};
        SDL_Rect sB{cx, cy + srcMidH, srcMidW, srcBottom};
        SDL_Rect sBR{cx + srcMidW, cy + srcMidH, srcRight, srcBottom};

        SDL_Rect dTL{dst.x, dst.y, dstLeft, dstTop};
        SDL_Rect dT{dst.x + dstLeft, dst.y, dstMidW, dstTop};
        SDL_Rect dTR{dst.x + dst.w - dstRight, dst.y, dstRight, dstTop};
        SDL_Rect dL{dst.x, dst.y + dstTop, dstLeft, dstMidH};
        SDL_Rect dC{dst.x + dstLeft, dst.y + dstTop, dstMidW, dstMidH};
        SDL_Rect dR{dst.x + dst.w - dstRight, dst.y + dstTop, dstRight, dstMidH};
        SDL_Rect dBL{dst.x, dst.y + dst.h - dstBottom, dstLeft, dstBottom};
        SDL_Rect dB{dst.x + dstLeft, dst.y + dst.h - dstBottom, dstMidW, dstBottom};
        SDL_Rect dBR{dst.x + dst.w - dstRight, dst.y + dst.h - dstBottom, dstRight, dstBottom};

        blit(sTL, dTL);
        blitTiled(sT, dT, true, false);
        blit(sTR, dTR);
        blitTiled(sL, dL, false, true);
        blitTiled(sC, dC, true, true);
        blitTiled(sR, dR, false, true);
        blit(sBL, dBL);
        blitTiled(sB, dB, true, false);
        blit(sBR, dBR);
    };
    auto renderOpaqueCenterLoopedFrame = [&](SDL_Texture* tex, const Frame& f, const SDL_Rect& dst) {
        if (!tex) return;
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        renderCenterLoopedFrame(tex, f, dst);
    };
    auto cleanupMenuAssets = [&]() {
        if (mainMenuTex) {
            SDL_DestroyTexture(mainMenuTex);
            mainMenuTex = nullptr;
        }
        if (menuFallbackTex) {
            SDL_DestroyTexture(menuFallbackTex);
            menuFallbackTex = nullptr;
        }
        if (menuBgTex) {
            SDL_DestroyTexture(menuBgTex);
            menuBgTex = nullptr;
        }
    };
    auto runScriptMenu = [&](const std::string& menuName) -> bool {
        if (!ctx.menuSession) ctx.menuSession = CreateSlimMenuSession();
        SlimMenuContext slim{};
        slim.mods = ctx.mods;
        slim.win = ctx.win;
        slim.ren = ctx.ren;
        slim.menuTexture = menuFallbackTex;
        slim.menuFrames = &menuFallbackFrames;
        slim.gameTarget = (ctx.gameTargetRef && *ctx.gameTargetRef) ? *ctx.gameTargetRef : ctx.gameTarget;
        slim.baseScreenW = ctx.baseScreenW;
        slim.baseScreenH = ctx.baseScreenH;
        slim.buildUuid = ctx.buildUuid;
        slim.buildTimestamp = ctx.buildTimestamp;
        slim.buildTimezone = ctx.buildTimezone;
        slim.versionString = ctx.versionString;
        slim.versionIdString = ctx.versionIdString;
        slim.running = ctx.running;
        slim.fullscreen = ctx.fullscreen;
        slim.vsyncEnabled = ctx.vsyncEnabled;
        slim.clampCamX = ctx.clampCamX;
        slim.defaultShowFpsCounter = ctx.defaultShowFpsCounter;
        slim.defaultShowDetailedDebugger = ctx.defaultShowDetailedDebugger;
        slim.defaultShowHitboxes = ctx.defaultShowHitboxes;
        slim.defaultShowPlayerHitbox = ctx.defaultShowPlayerHitbox;
        slim.defaultShowDebugView = ctx.defaultShowDebugView;
        slim.defaultHideUnknownObjectTypes = ctx.defaultHideUnknownObjectTypes;
        slim.powerManagementEnabled = ctx.powerManagementEnabled;
        slim.lowPowerModeEnabled = ctx.lowPowerModeEnabled;
        slim.showExperimentalFeatures = ctx.showExperimentalFeatures;
        slim.menuMusicEnabled = ctx.menuMusicEnabled;
        slim.muteAllAudio = ctx.muteAllAudio;
        slim.levelSelectEnabled = ctx.levelSelectEnabled;
        slim.nativeTextResolutionEnabled = ctx.nativeTextResolutionEnabled;
        slim.waterEffectsEnabled = ctx.waterEffectsEnabled;
        slim.musicVolume = ctx.musicVolume;
        slim.sfxVolume = ctx.sfxVolume;
        slim.uiScalePercent = ctx.uiScalePercent;
        slim.uiEdgePadding = ctx.uiEdgePadding;
        slim.activeSaveSlotIndex = ctx.activeSaveSlotIndex;
        slim.extraSettings = ctx.extraSettings;
        slim.extraSettingsCount = ctx.extraSettingsCount;
        slim.keyMoveLeft = ctx.keyMoveLeft;
        slim.keyMoveRight = ctx.keyMoveRight;
        slim.keyMoveDown = ctx.keyMoveDown;
        slim.keyJump = ctx.keyJump;
        slim.keyPause = ctx.keyPause;
        slim.levelServerUrl = ctx.levelServerUrl;
        slim.accountManagerUrl = ctx.accountManagerUrl;
        slim.levelServerAuthToken = ctx.levelServerAuthToken;
        slim.levelServerAccountUsername = ctx.levelServerAccountUsername;
        slim.selectedLevelPath = ctx.selectedLevelPath;
        slim.progression = ctx.progression;
        slim.characterAppearance = ctx.characterAppearance;
        slim.selectedLevelName = ctx.selectedLevelName;
        slim.session = ctx.menuSession;
        slim.debuggerAttached = ctx.debuggerAttached;
        slim.applyAudioVolumes = ctx.applyAudioVolumes;
        slim.applyMenuMusicToggle = ctx.applyMenuMusicToggle;
        slim.applyFullscreen = [&](bool enabled) { return applyFullscreen(enabled); };
        slim.applyRenderVsync = [&]() { applyRenderVsync(); };
        slim.updateDynamicResolution = ctx.updateDynamicResolution;
        slim.saveClientSettings = ctx.saveClientSettings;
        slim.signIn = [&](const std::string& email, const std::string& password, std::string& error) {
            bool success = false;
            try { success = loginWithGameServer(email, password, error); }
            catch (...) { error = "Sign-in was interrupted. Please try again."; }
            if (success) {
                SetLevelServerAuthToken(levelServerAuthToken);
                SetLevelServerAccountUsername(levelServerAccountUsername);
                if (ctx.saveClientSettings) ctx.saveClientSettings();
            }
            return success;
        };
        slim.getUpdaterStatusText = ctx.getUpdaterStatusText;
        slim.launchUpdater = ctx.launchUpdater;
        slim.getUpdaterStatusDetail = ctx.getUpdaterStatusDetail;
        slim.getUpdaterLatestVersionText = ctx.getUpdaterLatestVersionText;
        slim.getUpdaterNotesText = ctx.getUpdaterNotesText;
        slim.getUpdaterProgress01 = ctx.getUpdaterProgress01;
        slim.pollUpdaterAutoRelaunch = ctx.pollUpdaterAutoRelaunch;
        const SlimMenuExit result = RunSlimMenu(slim, menuName);
        if (result == SlimMenuExit::Quit) {
            cleanupMenuAssets();
            return false;
        }
        if (result == SlimMenuExit::StartGame) {
            cleanupMenuAssets();
            return true;
        }
        blockMenuInput();
        return false;
    };
    auto mainMenuSelectionAction = [&](int selection) {
        return runScriptMenu(selection == 0 ? "mainsetting" : selection == 1 ? "mainplay" : "mainedit");
    };
    auto mainMenuLabelForIndex = [](int index) -> std::string {
        return index == 0 ? "SETTINGS" : index == 1 ? "PLAY" : "EDITOR";
    };
    if (settingsOnly) {
        ctx.menuSession = CreateSlimMenuSession();
        runScriptMenu("mainsetting");
        cleanupMenuAssets();
        return running ? FrontendAction::StartGame : FrontendAction::Quit;
    }
    if (ctx.applyAudioVolumes) ctx.applyAudioVolumes();
    if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
    const std::string resumeMenu = PendingSlimMenu(ctx.menuSession);
    if (!resumeMenu.empty()) {
        if (runScriptMenu(resumeMenu)) return FrontendAction::StartGame;
        if (!running) return FrontendAction::Quit;
    }
    InputSystem menuInput;
    menuInput.scanConnected();
    Uint64 fpsLastSampleTicks = SDL_GetTicks();
    int fpsFramesSinceSample = 0;
    int menuFps = 0;
    while (running) {
        const Uint64 frameTicks = SDL_GetTicks();
        ++fpsFramesSinceSample;
        const Uint64 sampleElapsed = frameTicks - fpsLastSampleTicks;
        if (sampleElapsed >= 250) {
            menuFps = (int)std::lround((double)fpsFramesSinceSample * 1000.0 / std::max<Uint64>(1, sampleElapsed));
            fpsFramesSinceSample = 0;
            fpsLastSampleTicks = frameTicks;
        }
        if (ctx.pollUpdaterAutoRelaunch) ctx.pollUpdaterAutoRelaunch();
        if (!running) break;
        SDL_Texture* gameTarget = ctx.gameTargetRef && *ctx.gameTargetRef ? *ctx.gameTargetRef : ctx.gameTarget;
        if (!gameTarget) { SDL_Delay(1); continue; }
        SetTextScaleMultiplier(UiScale::multiplier(uiScalePercent));
        if (ctx.applyMenuMusicToggle) ctx.applyMenuMusicToggle();
        SDL_Event e;
        int eventsProcessed = 0;
        while (eventsProcessed++ < 256 && SDL_PollEvent(&e)) {
            menuInput.handleEvent(e);
            if (e.type == SDL_QUIT) { running = false; break; }
            if ((e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
                e.window.windowID == SDL_GetWindowID(ctx.win)) {
                if (ctx.updateDynamicResolution) ctx.updateDynamicResolution();
            }
            if (SDL_GetTicks() < inputBlockUntilTicks) continue;
            int direction = 0;
            bool accept = false, back = false;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.key == SDLK_F11) {
                    (void)applyFullscreen(!fullscreen);
                    continue;
                }
                const bool left = e.key.key == SDLK_LEFT || (ctx.keyMoveLeft && e.key.scancode == *ctx.keyMoveLeft);
                const bool right = e.key.key == SDLK_RIGHT || (ctx.keyMoveRight && e.key.scancode == *ctx.keyMoveRight);
                const bool up = e.key.key == SDLK_UP || (ctx.keyJump && e.key.scancode == *ctx.keyJump);
                const bool down = e.key.key == SDLK_DOWN || (ctx.keyMoveDown && e.key.scancode == *ctx.keyMoveDown);
                direction = left || up ? -1 : right || down ? 1 : 0;
                accept = e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER;
                back = e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK;
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                direction = e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP ? -1 :
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT || e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? 1 : 0;
                accept = e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_START;
                back = e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK;
            }
            SDL_Point point{};
            bool clicked = false;
            if ((e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) ||
                e.type == SDL_EVENT_FINGER_UP) {
                int width = 0, height = 0;
                getWindowSizeInPixelsCompat(ctx.win, width, height);
                const bool touch = e.type == SDL_EVENT_FINGER_UP;
                const int x = touch ? (int)(e.tfinger.x * width) : e.button.x;
                const int y = touch ? (int)(e.tfinger.y * height) : e.button.y;
                clicked = windowToGamePoint(x, y, width, height, ctx.baseScreenW, ctx.baseScreenH, point.x, point.y, 1.0f);
            }
            if (closeMenuOpen) {
                if (direction) closeMenuSel = 1 - closeMenuSel;
                if (back) { closeMenuOpen = false; blockMenuInput(); }
                if (clicked) {
                    const auto modal = quitModalRect();
                    const auto resume = quitResumeBtnRect(modal), close = quitCloseBtnRect(modal);
                    if (SDL_PointInRect(&point, &resume)) { closeMenuSel = 0; accept = true; }
                    if (SDL_PointInRect(&point, &close)) { closeMenuSel = 1; accept = true; }
                }
                if (accept) {
                    if (closeMenuSel == 1) running = false;
                    closeMenuOpen = false;
                    blockMenuInput();
                }
                continue;
            }
            const int count = mainMenuButtonCount();
            if (direction) menuSel = (std::clamp(menuSel, 0, count - 1) + direction + count) % count;
            if (back) { closeMenuOpen = true; closeMenuSel = 0; blockMenuInput(); continue; }
            if (clicked) {
                for (int i = 0; i < count; ++i) {
                    const auto rect = mainMenuBtnRect(i);
                    if (SDL_PointInRect(&point, &rect)) { menuSel = i; accept = true; break; }
                }
            }
            if (accept) {
                buttonAnimations.activate(std::clamp(menuSel, 0, count - 1));
                if (mainMenuSelectionAction(std::clamp(menuSel, 0, count - 1))) return FrontendAction::StartGame;
            }
            if (!running) break;
        }
        if (!running) break;
        gameTarget = ctx.gameTargetRef && *ctx.gameTargetRef ? *ctx.gameTargetRef : ctx.gameTarget;
        if (!gameTarget) continue;
        int winW = 0, winH = 0;
        getWindowSizeInPixelsCompat(ctx.win, winW, winH);
        SDL_Rect presentDst = computePresentRect(winW, winH, ctx.baseScreenW, ctx.baseScreenH, 1.0f);
        // Background labels must remain below the quit dialog's dimming layer.
        if (closeMenuOpen) CancelNativeTextOverlay(ctx.ren);
        else BeginNativeTextOverlay(ctx.ren, ctx.baseScreenW, ctx.baseScreenH, presentDst);
        SDL_SetRenderTarget(ctx.ren, gameTarget);
        SDL_SetRenderDrawColor(ctx.ren, 118, 225, 255, 255); // #76e1ff
        SDL_RenderClear(ctx.ren);
        if (menuBgTex && !menuBgFrames.empty()) {
            auto getBgFrame = [&](const char* name) -> const Frame* {
                auto it = menuBgFrames.find(name);
                if (it != menuBgFrames.end()) return &it->second;
                std::string withPng = std::string(name) + ".png";
                auto itPng = menuBgFrames.find(withPng);
                if (itPng != menuBgFrames.end()) return &itPng->second;
                return nullptr;
            };
            const Frame* bgBack = getBgFrame("far");
            const Frame* bgMiddle = getBgFrame("middle");
            if (!bgMiddle) bgMiddle = getBgFrame("mid");

            struct MenuParallaxLayer {
                const Frame* frame;
                float speed;
                float yBias;
                float alpha;
                float scaleMul;
            };
            MenuParallaxLayer layers[] = {
                {bgBack, 0.015f, 0.0f, 255.0f, 1.0f},
                {bgMiddle, 0.045f, 10.0f, 255.0f, 1.0f},
                {bgMiddle, 0.075f, 20.0f, 255.0f, 1.0f},
            };
            const Uint64 ticks = SDL_GetTicks();
            const float bgScale = std::clamp(menuCanvasScale() * mainMenuScale() * 0.80f, 0.5f, 3.0f);
            const float canvasScale = menuCanvasScale();
            const float canvasBottomY = menuCanvasOriginY() + kMenuAuthorH * canvasScale;
            for (const auto& layer : layers) {
                if (!layer.frame) continue;
                const int srcW = layer.frame->rotated ? layer.frame->rect.h : layer.frame->rect.w;
                const int srcH = layer.frame->rotated ? layer.frame->rect.w : layer.frame->rect.h;
                const float layerScale = std::clamp(bgScale * layer.scaleMul, 0.5f, 3.0f);
                int fw = std::max(1, (int)std::lround((float)srcW * layerScale));
                int fh = std::max(1, (int)std::lround((float)srcH * layerScale));
                float ox = std::fmod((float)ticks * layer.speed * layerScale, (float)fw);
                if (ox < 0.0f) ox += (float)fw;
                int y = (int)std::lround(canvasBottomY - (float)fh + layer.yBias * canvasScale);
                SDL_SetTextureAlphaMod(menuBgTex, (Uint8)std::clamp((int)std::lround(layer.alpha), 0, 255));
                for (int x = -1; x <= ctx.baseScreenW / fw + 1; ++x) {
                    SDL_Rect dst{(int)std::lround((float)(x * fw) - ox), y, fw, fh};
                    renderFrame(ctx.ren, menuBgTex, *layer.frame, dst);
                }
            }
            SDL_SetTextureAlphaMod(menuBgTex, 255);
        }
        {
            SDL_Texture* mainLogoTex = nullptr;
            SDL_Texture* btnSpriteTex = nullptr;
            SDL_Texture* playLogoTex = nullptr;
            SDL_Texture* settingsLogoTex = nullptr;
            SDL_Texture* editorLogoTex = nullptr;
            const char* mainLogoFrameName = levelSelectEnabled ? "Main_logo" : "lite_logo";
            const Frame* mainLogo = getMenuFrame(mainLogoFrameName, mainLogoTex);
            const Frame* btnSprite = getMenuFrame("btn_sprite", btnSpriteTex);
            bool btnSpriteIsGeneric = false;
            if (!btnSprite) {
                btnSprite = getMenuFrame("button_generic", btnSpriteTex);
                btnSpriteIsGeneric = (btnSprite != nullptr);
            }
            const Frame* playLogo = getMenuFrame("play_btn_logo", playLogoTex);
            const Frame* settingsLogo = getMenuFrame("settings_btn_logo", settingsLogoTex);
            const Frame* editorLogo = getMenuFrame("Editor_logo", editorLogoTex);
            if (mainLogoTex && mainLogo) {
                const float canvas = menuCanvasScale();
                const float s = mainMenuScale();
                const int logoW = std::max(120, (int)std::lround(640.0f * canvas * s));
                const int logoH = std::max(24, (int)std::lround(66.0f * canvas * s));
                if (logoW > mainLogo->rect.w || logoH > mainLogo->rect.h) {
                    // Don't upscale if it would cause significant blurring. Instead, just center the original texture.
                    SDL_Rect dst{(ctx.baseScreenW - mainLogo->rect.w) / 2, (int)std::lround(menuCanvasOriginY() + 75.0f * canvas) - mainLogo->rect.h / 2, mainLogo->rect.w, mainLogo->rect.h};
                    renderFrame(ctx.ren, mainLogoTex, *mainLogo, dst);
                } else {
                const int logoCX = (int)std::lround(menuCanvasOriginX() + 480.0f * canvas);
                const int logoCY = (int)std::lround(menuCanvasOriginY() + 75.0f * canvas);
                SDL_Rect dst{logoCX - logoW / 2, logoCY - logoH / 2, logoW, logoH};
                renderFrame(ctx.ren, mainLogoTex, *mainLogo, dst);
                }
            }
            int menubuttoncount = 0;
            SDL_Rect menuBtns[4];
            if (levelSelectEnabled) {
                menuBtns[0] = mainMenuBtnRect(0);
                menuBtns[1] = mainMenuBtnRect(1);
                menuBtns[2] = mainMenuBtnRect(2);
                menubuttoncount = 3;
            } else {
                menuBtns[0] = mainMenuBtnRect(0);
                menuBtns[1] = mainMenuBtnRect(1);
                menubuttoncount = 2;
            }
            for (int i = 0; i < menubuttoncount; ++i)
                menuBtns[i] = buttonAnimations.drawRect(i, menuBtns[i], i == menuSel, ctx.win,
                    ctx.baseScreenW, ctx.baseScreenH);
            if (btnSpriteTex && btnSprite) {
                for (int i = 0; i < menubuttoncount; ++i) {
                    if (btnSpriteIsGeneric) renderOpaqueCenterLoopedFrame(btnSpriteTex, *btnSprite, menuBtns[i]);
                    else renderFrame(ctx.ren, btnSpriteTex, *btnSprite, menuBtns[i]);
                }
            }
            for (int i = 0; i < menubuttoncount; ++i) {
                if (i == menuSel) {
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_ADD);
                    SDL_SetRenderDrawColor(ctx.ren, 45, 45, 45, 255);
                    SDL_RenderFillRect(ctx.ren, &menuBtns[i]);
                    SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(ctx.ren, 180, 200, 230, 255);
                    SDL_RenderRect(ctx.ren, &menuBtns[i]);
                    }
            }
            if (settingsLogoTex && settingsLogo) {
                SDL_Rect b = menuBtns[0];
                SDL_Rect dst{
                    b.x + (int)std::lround((float)b.w * 0.18f),
                    b.y + (int)std::lround((float)b.h * 0.14f),
                    (int)std::lround((float)b.w * 0.64f),
                    (int)std::lround((float)b.h * 0.64f)
                };
                renderFrame(ctx.ren, settingsLogoTex, *settingsLogo, dst);
            }
            const int playButtonIndex = 1;
            const int editorButtonIndex = 2;
            if (playLogoTex && playLogo) {
                SDL_Rect b = menuBtns[playButtonIndex];
                SDL_Rect dst{
                    b.x + (int)std::lround((float)b.w * 0.27f),
                    b.y + (int)std::lround((float)b.h * 0.11f),
                    (int)std::lround((float)b.w * 0.46f),
                    (int)std::lround((float)b.h * 0.79f)
                };
                renderFrame(ctx.ren, playLogoTex, *playLogo, dst);
            }
            if (editorLogoTex && editorLogo && levelSelectEnabled) {
                SDL_Rect b = menuBtns[editorButtonIndex];
                SDL_Rect dst{
                    b.x + (int)std::lround((float)b.w * 0.30f),
                    b.y + (int)std::lround((float)b.h * 0.14f),
                    (int)std::lround((float)b.w * 0.39f),
                    (int)std::lround((float)b.h * 0.71f)
                };
                if (levelSelectEnabled){
                    renderFrame(ctx.ren, editorLogoTex, *editorLogo, dst);
                }
            }
            if ((!settingsLogoTex || !settingsLogo) || (!playLogoTex || !playLogo) || (!editorLogoTex || !editorLogo)) {
                for (int i = 0; i < menubuttoncount; ++i) {
                    const std::string label = mainMenuLabelForIndex(i);
                    const int tw = MeasureTextWidth(2, label);
                    const int ty = menuBtns[i].y + (int)std::lround((float)menuBtns[i].h * 0.73f);
                    DrawText(ctx.ren, menuBtns[i].x + (menuBtns[i].w - tw) / 2, ty, 2, label);
                }
            }
            if (!mainLogoTex || !mainLogo) {
                const std::string title = levelSelectEnabled ? "Dorfplatformer Timetravel" : "Dorfplatformer Timetravel Lite";
                const int titleY = (int)std::lround(menuCanvasOriginY() + 84.0f * menuCanvasScale());
                DrawText(ctx.ren, ctx.baseScreenW / 2 - MeasureTextWidth(3, title) / 2, titleY, 3, title);
            }
            std::string versionText = std::string("v") + (ctx.versionString.empty() ? "dev" : ctx.versionString);
            const std::string copyrightText = "Copyright (c) Benno111 2024 - 2026";
            const int footerScale = std::max(2, (int)std::lround(2.0f * menuCanvasScale() * mainMenuScale()));
            const int footerY = (int)std::lround(menuCanvasOriginY() + 496.0f * menuCanvasScale());
            const int edgePad = std::max(8, (int)std::lround(12.0f * menuCanvasScale()));
            DrawText(ctx.ren, edgePad, footerY, footerScale, versionText);
            DrawText(ctx.ren, ctx.baseScreenW - edgePad - MeasureTextWidth(footerScale, copyrightText), footerY, footerScale, copyrightText);
            if (closeMenuOpen) {
                constexpr Uint8 kQuitDimAlpha = 150;
                constexpr Uint8 kQuitWindowAlpha = 236;
                constexpr Uint8 kQuitButtonAlpha = 228;
                constexpr Uint8 kQuitSelectAlpha = 56;
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ctx.ren, 0, 0, 0, kQuitDimAlpha);
                SDL_Rect dim{0, 0, ctx.baseScreenW, ctx.baseScreenH};
                SDL_RenderFillRect(ctx.ren, &dim);

                SDL_Rect modal = quitModalRect();
                SDL_Texture* popupWindowTex = nullptr;
                const Frame* popupWindow = getMenuFrame("window", popupWindowTex);
                if (!popupWindow) popupWindow = getMenuFrame("window.png", popupWindowTex);
                if (popupWindow && popupWindowTex) {
                    SDL_SetTextureBlendMode(popupWindowTex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(popupWindowTex, kQuitWindowAlpha);
                    renderFrame(ctx.ren, popupWindowTex, *popupWindow, modal);
                    SDL_SetTextureAlphaMod(popupWindowTex, 255);
                } else {
                    SDL_SetRenderDrawColor(ctx.ren, 26, 32, 42, kQuitWindowAlpha);
                    SDL_RenderFillRect(ctx.ren, &modal);
                    SDL_SetRenderDrawColor(ctx.ren, 170, 190, 220, 255);
                    SDL_RenderDrawRect(ctx.ren, &modal);
                }
                BeginNativeTextOverlay(ctx.ren, ctx.baseScreenW, ctx.baseScreenH, presentDst);
                DrawText(ctx.ren, modal.x + (modal.w - MeasureTextWidth(2, "CLOSE GAME?")) / 2, modal.y + (int)std::lround(18.0f * uiButtonScale()), 2, "CLOSE GAME?");

                SDL_Rect resumeBtn = buttonAnimations.drawRect(10, quitResumeBtnRect(modal), closeMenuSel == 0,
                    ctx.win, ctx.baseScreenW, ctx.baseScreenH);
                SDL_Rect closeBtn = buttonAnimations.drawRect(11, quitCloseBtnRect(modal), closeMenuSel == 1,
                    ctx.win, ctx.baseScreenW, ctx.baseScreenH);
                SDL_Texture* popupBtnTex = nullptr;
                const Frame* popupBtnFrame = getMenuFrame("button_genreaic", popupBtnTex);
                if (!popupBtnFrame) popupBtnFrame = getMenuFrame("button_genreaic.png", popupBtnTex);
                if (popupBtnFrame && popupBtnTex) {
                    SDL_SetTextureBlendMode(popupBtnTex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(popupBtnTex, kQuitButtonAlpha);
                    renderFrame(ctx.ren, popupBtnTex, *popupBtnFrame, resumeBtn);
                    renderFrame(ctx.ren, popupBtnTex, *popupBtnFrame, closeBtn);
                    SDL_SetTextureAlphaMod(popupBtnTex, 255);
                } else {
                    SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 0 ? 120 : 70, 95, 85, kQuitButtonAlpha);
                    SDL_RenderFillRect(ctx.ren, &resumeBtn);
                    SDL_SetRenderDrawColor(ctx.ren, closeMenuSel == 1 ? 120 : 70, 70, 75, kQuitButtonAlpha);
                    SDL_RenderFillRect(ctx.ren, &closeBtn);
                }
                // Keep selection opacity consistent regardless of textured/fallback button path.
                SDL_SetRenderDrawColor(ctx.ren, 255, 255, 255, kQuitSelectAlpha);
                SDL_Rect selectedBtn = (closeMenuSel == 0) ? resumeBtn : closeBtn;
                SDL_RenderFillRect(ctx.ren, &selectedBtn);
                SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ctx.ren, 190, 200, 220, 255);
                SDL_RenderRect(ctx.ren, &resumeBtn);
                SDL_RenderRect(ctx.ren, &closeBtn);
                const int labelLineH = std::max(10, (int)std::lround(16.0f * uiButtonScale()));
                DrawText(ctx.ren, resumeBtn.x + (resumeBtn.w - MeasureTextWidth(2, "RESUME")) / 2, resumeBtn.y + (resumeBtn.h - labelLineH) / 2, 2, "RESUME");
                DrawText(ctx.ren, closeBtn.x + (closeBtn.w - MeasureTextWidth(2, "CLOSE")) / 2, closeBtn.y + (closeBtn.h - labelLineH) / 2, 2, "CLOSE");
            }
        }
        if (defaultShowFpsCounter) {
            const std::string fpsText = std::string("FPS: ") + std::to_string(menuFps);
            const int fpsScale = std::max(2, (int)std::lround(2.0f * fpsBadgeScale()));
            const int fpsPadX = std::max(8, (int)std::lround(10.0f * fpsBadgeScale()));
            const int fpsPadY = std::max(5, (int)std::lround(6.0f * fpsBadgeScale()));
            const int fpsTextW = MeasureTextWidth(fpsScale, fpsText);
            const int fpsTextH = 10 * fpsScale;
            SDL_Rect fpsBadge{
                ctx.baseScreenW - fpsTextW - fpsPadX * 2 - std::max(8, uiEdgePadding),
                std::max(8, uiEdgePadding),
                fpsTextW + fpsPadX * 2,
                fpsTextH + fpsPadY * 2
            };
            SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ctx.ren, 18, 25, 36, 205);
            SDL_RenderFillRect(ctx.ren, &fpsBadge);
            SDL_SetRenderDrawBlendMode(ctx.ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ctx.ren, 176, 198, 230, 255);
            SDL_RenderRect(ctx.ren, &fpsBadge);
            DrawText(ctx.ren, fpsBadge.x + fpsPadX, fpsBadge.y + fpsPadY, fpsScale, fpsText);
        }

        SDL_SetRenderTarget(ctx.ren, nullptr);
        SDL_SetRenderDrawColor(ctx.ren, 118, 225, 255, 255); // #76e1ff
        SDL_RenderClear(ctx.ren);
        SDL_RenderCopy(ctx.ren, gameTarget, nullptr, &presentDst);
        FlushNativeTextOverlay(ctx.ren);
        SDL_RenderPresent(ctx.ren);
    }
    cleanupMenuAssets();
    return FrontendAction::Quit;
}

