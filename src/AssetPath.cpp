#include "AssetPath.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif
#if defined(__ANDROID__)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif
#if defined(HAVE_CURL) && HAVE_CURL
#include <curl/curl.h>
#endif

namespace {
std::string g_levelServerUrl;
std::string g_levelServerAuthToken;
std::string g_levelServerAccountUsername;

bool isHttpUrl(const std::string& path) {
    if (path.size() < 8) return false;
    std::string prefix;
    prefix.resize(std::min<size_t>(8, path.size()));
    std::transform(path.begin(), path.begin() + prefix.size(), prefix.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return prefix.rfind("http://", 0) == 0 || prefix.rfind("https://", 0) == 0;
}

bool shouldLogAssetPath(const std::string& path) {
    if (path.empty()) return false;
    if (path.rfind("assets/", 0) == 0) return true;
    if (path.find("/assets/") != std::string::npos) return true;
    if (path.find("\\assets\\") != std::string::npos) return true;
    return false;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::filesystem::path p(a);
    p /= b;
    return p.lexically_normal().string();
}

std::string resolveFromBasePath(const std::string& path) {
    const char* basePath = SDL_GetBasePath();
    if (!basePath || !*basePath) {
        return {};
    }
    const std::string base(basePath);
    const std::string candidate = joinPath(base, path);
    if (std::filesystem::exists(std::filesystem::path(candidate))) {
        return candidate;
    }
    return {};
}

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), needed);
    return out;
}

std::string win32ErrorString(DWORD err) {
    return "WinHTTP error " + std::to_string((unsigned long)err) + ".";
}

bool winHttpRequestText(const std::string& method,
                        const std::string& url,
                        const std::vector<std::string>& headers,
                        const std::string& body,
                        long* statusCodeOut,
                        std::string* responseBodyOut,
                        std::string* errorOut,
                        int timeoutMs) {
    if (statusCodeOut) *statusCodeOut = 0;
    if (responseBodyOut) responseBodyOut->clear();
    if (errorOut) errorOut->clear();

    const std::wstring wurl = utf8ToWide(url);
    const std::wstring wmethod = utf8ToWide(method);
    if (wurl.empty() || wmethod.empty()) {
        if (errorOut) *errorOut = "Invalid request URL or method.";
        return false;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = (DWORD)-1;
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        if (errorOut) *errorOut = win32ErrorString(GetLastError());
        return false;
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && parts.nScheme != INTERNET_SCHEME_HTTP) {
        if (errorOut) *errorOut = "Unsupported URL scheme.";
        return false;
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath ? parts.lpszUrlPath : L"", parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";

    HINTERNET session = WinHttpOpen(L"DF-New/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        if (errorOut) *errorOut = win32ErrorString(GetLastError());
        return false;
    }
    const int clampedTimeoutMs = std::max(1000, timeoutMs);
    WinHttpSetTimeouts(session, clampedTimeoutMs, clampedTimeoutMs, clampedTimeoutMs, clampedTimeoutMs);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connect) {
        if (errorOut) *errorOut = win32ErrorString(GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connect,
                                           wmethod.c_str(),
                                           path.c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        if (errorOut) *errorOut = win32ErrorString(GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    std::wstring headerBlock;
    for (const std::string& h : headers) {
        if (h.empty()) continue;
        if (!headerBlock.empty()) headerBlock += L"\r\n";
        headerBlock += utf8ToWide(h);
    }

    LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data();
    const DWORD bodySize = (DWORD)body.size();
    BOOL ok = WinHttpSendRequest(request,
                                 headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                                 headerBlock.empty() ? 0 : (DWORD)-1,
                                 bodyPtr,
                                 bodySize,
                                 bodySize,
                                 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    if (!ok) {
        if (errorOut) *errorOut = win32ErrorString(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status,
                            &statusSize,
                            WINHTTP_NO_HEADER_INDEX) &&
        statusCodeOut) {
        *statusCodeOut = (long)status;
    }

    std::string response;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            if (errorOut) *errorOut = win32ErrorString(GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        if (available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
            if (errorOut) *errorOut = win32ErrorString(GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        chunk.resize(read);
        response += chunk;
    }

    if (responseBodyOut) *responseBodyOut = std::move(response);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}
#endif

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
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        SDL_Log("NET: Java fallback missing MainActivity.httpGet(String,int)");
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
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        SDL_Log("NET: Java fallback exception in MainActivity.httpGet");
        env->DeleteLocalRef(jurl);
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
        return {};
    }
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

bool HttpRequestText(const std::string& method,
                     const std::string& url,
                     const std::vector<std::string>& headers,
                     const std::string& body,
                     long* statusCodeOut,
                     std::string* responseBodyOut,
                     std::string* errorOut,
                     int timeoutMs) {
#if defined(_WIN32)
    return winHttpRequestText(method, url, headers, body, statusCodeOut, responseBodyOut, errorOut, timeoutMs);
#else
    (void)method;
    (void)url;
    (void)headers;
    (void)body;
    (void)statusCodeOut;
    (void)responseBodyOut;
    if (errorOut) *errorOut = "HTTP fallback unavailable on this platform.";
    (void)timeoutMs;
    return false;
#endif
}

std::string ResolveAssetPath(const std::string& path) {
    if (isHttpUrl(path)) return path;
#if defined(__ANDROID__)
    constexpr const char* kPrefix = "assets/";
    if (path.rfind(kPrefix, 0) == 0) {
        return path.substr(7);
    }
#endif
    if (path.empty()) return path;
    const std::filesystem::path p(path);
    if (p.is_absolute()) {
        if (shouldLogAssetPath(path)) {
            SDL_Log("ASSET PATH: absolute input='%s' exists=%d", path.c_str(),
                    std::filesystem::exists(p) ? 1 : 0);
        }
        return path;
    }
    if (std::filesystem::exists(p)) {
        if (shouldLogAssetPath(path)) {
            SDL_Log("ASSET PATH: cwd input='%s' resolved='%s' exists=1",
                    path.c_str(), path.c_str());
        }
        return path;
    }
    const std::string fromBase = resolveFromBasePath(path);
    if (!fromBase.empty()) {
        if (shouldLogAssetPath(path)) {
            SDL_Log("ASSET PATH: base input='%s' resolved='%s' exists=1",
                    path.c_str(), fromBase.c_str());
        }
        return fromBase;
    }
    if (shouldLogAssetPath(path)) {
        SDL_Log("ASSET PATH: unresolved input='%s' (cwd/base not found)", path.c_str());
    }
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
        std::string body;
        long code = 0;
        std::string httpErr;
        if (HttpRequestText("GET", resolved, {}, {}, &code, &body, &httpErr, 10000) &&
            code >= 200 && code < 300) {
            SDL_Log("NET: ok GET (fallback) %s code=%ld bytes=%d", resolved.c_str(), code, (int)body.size());
            return body;
        }
        SDL_Log("NET: HTTP GET failed and curl support is disabled (HAVE_CURL=0): %s code=%ld reason=%s",
                resolved.c_str(), code, httpErr.c_str());
        return {};
    }
#endif
    SDL_IOStream* io = SDL_IOFromFile(resolved.c_str(), "rb");
    if (!io) {
        if (shouldLogAssetPath(path)) {
            SDL_Log("ASSET READ: failed path='%s' resolved='%s' err='%s'",
                    path.c_str(), resolved.c_str(), SDL_GetError());
        }
        return {};
    }

    const Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) {
        SDL_CloseIO(io);
        if (shouldLogAssetPath(path)) {
            SDL_Log("ASSET READ: empty/unreadable path='%s' resolved='%s' size=%lld",
                    path.c_str(), resolved.c_str(), (long long)sz);
        }
        return {};
    }

    std::vector<char> data(static_cast<size_t>(sz));
    const size_t got = static_cast<size_t>(SDL_ReadIO(io, data.data(), data.size()));
    SDL_CloseIO(io);
    if (got != data.size()) {
        if (shouldLogAssetPath(path)) {
            SDL_Log("ASSET READ: short read path='%s' resolved='%s' got=%d expected=%d",
                    path.c_str(), resolved.c_str(), (int)got, (int)data.size());
        }
        return {};
    }

    if (shouldLogAssetPath(path)) {
        SDL_Log("ASSET READ: ok path='%s' resolved='%s' bytes=%d",
                path.c_str(), resolved.c_str(), (int)data.size());
    }

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

std::string GetAppSaveRootPath() {
#if defined(__ANDROID__)
    const char* ext = SDL_GetAndroidExternalStoragePath();
    if (ext && *ext) {
        // SDL external path is usually:
        // /storage/emulated/0/Android/data/<package>/files
        // For user-visible saves, migrate to:
        // /storage/emulated/0/Android/media/<package>/DorfplatformerTimetravel
        std::string base(ext);
        const std::string marker = "/Android/data/";
        const std::size_t dataPos = base.find(marker);
        if (dataPos != std::string::npos) {
            const std::size_t pkgStart = dataPos + marker.size();
            const std::size_t pkgEnd = base.find('/', pkgStart);
            const std::string pkg = (pkgEnd == std::string::npos)
                ? base.substr(pkgStart)
                : base.substr(pkgStart, pkgEnd - pkgStart);
            if (!pkg.empty()) {
                std::filesystem::path mediaRoot = base.substr(0, dataPos);
                mediaRoot /= "Android";
                mediaRoot /= "media";
                mediaRoot /= pkg;
                mediaRoot /= "DorfplatformerTimetravel";
                std::error_code ec;
                std::filesystem::create_directories(mediaRoot, ec);
                if (!ec) return mediaRoot.string();
            }
        }
        std::filesystem::path fallback(base);
        fallback /= "DorfplatformerTimetravel";
        std::error_code ec;
        std::filesystem::create_directories(fallback, ec);
        return fallback.string();
    }
#endif
    std::string base = ".";
    char* prefPath = SDL_GetPrefPath("Benno111", "DorfplatformerTimetravel");
    if (prefPath) {
        base = prefPath;
        SDL_free(prefPath);
    }
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base;
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

void SetLevelServerAccountUsername(const std::string& username) {
    g_levelServerAccountUsername = username;
}

std::string GetLevelServerAccountUsername() {
    return g_levelServerAccountUsername;
}

