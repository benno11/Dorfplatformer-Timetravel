#include "AssetPath.h"

#include <sdl3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>
#if defined(__ANDROID__)
#include <jni.h>
#include <sdl3/SDL_system.h>
#endif
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif

namespace {
std::string g_levelServerUrl;
std::string g_levelServerAuthToken;

bool isHttpUrl(const std::string& path) {
    if (path.size() < 8) return false;
    std::string prefix;
    prefix.resize(std::min<size_t>(8, path.size()));
    std::transform(path.begin(), path.begin() + prefix.size(), prefix.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return prefix.rfind("http://", 0) == 0 || prefix.rfind("https://", 0) == 0;
}

#if defined(__ANDROID__)
std::string androidHttpGetViaJava(const std::string& url, int timeoutMs) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return {};
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return {};
    jclass cls = env->GetObjectClass(activity);
    if (!cls) {
        env->DeleteLocalRef(activity);
        return {};
    }
    jmethodID mid = env->GetStaticMethodID(cls, "httpGet", "(Ljava/lang/String;I)Ljava/lang/String;");
    if (!mid) {
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
        return {};
    }
    jstring jurl = env->NewStringUTF(url.c_str());
    if (!jurl) {
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
        return {};
    }
    jstring jout = static_cast<jstring>(env->CallStaticObjectMethod(cls, mid, jurl, (jint)timeoutMs));
    std::string out;
    if (jout) {
        const char* chars = env->GetStringUTFChars(jout, nullptr);
        if (chars) {
            out = chars;
            env->ReleaseStringUTFChars(jout, chars);
        }
        env->DeleteLocalRef(jout);
    }
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return out;
}
#endif
} // namespace

std::string ResolveAssetPath(const std::string& path) {
    if (isHttpUrl(path)) return path;
#if defined(__ANDROID__)
    constexpr const char* kPrefix = "assets/";
    if (path.rfind(kPrefix, 0) == 0) {
        return path.substr(7);
    }
#endif
    return path;
}

std::string ReadTextFile(const std::string& path) {
    const std::string resolved = ResolveAssetPath(path);
#if defined(HAVE_CURL) && HAVE_CURL
    if (isHttpUrl(resolved)) {
        const auto t0 = std::chrono::steady_clock::now();
        SDL_Log("NET: begin GET %s", resolved.c_str());
        CURL* curl = curl_easy_init();
        if (!curl) {
            SDL_Log("NET: curl_easy_init failed for %s", resolved.c_str());
            return {};
        }
        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, resolved.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "DF-New/1.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                         +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                             std::string* out = static_cast<std::string*>(userdata);
                             out->append(ptr, size * nmemb);
                             return size * nmemb;
                         });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        const CURLcode rc = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        double totalSecs = 0.0;
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalSecs);
        curl_easy_cleanup(curl);
        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        if (rc != CURLE_OK || code < 200 || code >= 300) {
            SDL_Log("NET: fail GET %s code=%ld curl=%d elapsed_ms=%lld curl_total=%.3fs",
                    resolved.c_str(), code, (int)rc, (long long)ms, totalSecs);
#if defined(__ANDROID__)
            const std::string fallback = androidHttpGetViaJava(resolved, 10000);
            if (!fallback.empty()) {
                SDL_Log("NET: Java fallback GET ok %s bytes=%d", resolved.c_str(), (int)fallback.size());
                return fallback;
            }
            SDL_Log("NET: Java fallback GET failed %s", resolved.c_str());
#endif
            return {};
        }
        SDL_Log("NET: ok GET %s code=%ld bytes=%d elapsed_ms=%lld curl_total=%.3fs",
                resolved.c_str(), code, (int)body.size(), (long long)ms, totalSecs);
        return body;
    }
#else
    if (isHttpUrl(resolved)) {
        SDL_Log("NET: HTTP requested but curl support is disabled at build time (HAVE_CURL=0): %s",
                resolved.c_str());
        return {};
    }
#endif
    SDL_IOStream* io = SDL_IOFromFile(resolved.c_str(), "rb");
    if (!io) return {};

    const Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) {
        SDL_CloseIO(io);
        return {};
    }

    std::vector<char> data(static_cast<size_t>(sz));
    const size_t got = static_cast<size_t>(SDL_ReadIO(io, data.data(), data.size()));
    SDL_CloseIO(io);
    if (got != data.size()) return {};

    return std::string(data.begin(), data.end());
}

bool FileExists(const std::string& path) {
    const std::string resolved = ResolveAssetPath(path);
    if (isHttpUrl(resolved)) return false;
    SDL_IOStream* io = SDL_IOFromFile(resolved.c_str(), "rb");
    if (!io) return false;
    SDL_CloseIO(io);
    return true;
}

void SetLevelServerUrl(const std::string& url) {
    g_levelServerUrl = url;
}

std::string GetLevelServerUrl() {
    return g_levelServerUrl;
}

void SetLevelServerAuthToken(const std::string& token) {
    g_levelServerAuthToken = token;
}

std::string GetLevelServerAuthToken() {
    return g_levelServerAuthToken;
}
