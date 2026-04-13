#include "AndroidAudioBridge.h"

#if defined(__ANDROID__)

#include <jni.h>

#include <mutex>
#include <string>

namespace {

JavaVM* g_vm = nullptr;
jclass g_bridge_class = nullptr;
std::mutex g_jni_mutex;

struct MethodCache {
    jmethodID initialize = nullptr;
    jmethodID shutdown = nullptr;
    jmethodID loadGlobalAssets = nullptr;
    jmethodID unloadGlobalAssets = nullptr;
    jmethodID applyVolumes = nullptr;
    jmethodID applyMenuMusicToggle = nullptr;
    jmethodID setLoopingEnabled = nullptr;
    jmethodID loadLevelMusic = nullptr;
    jmethodID unloadLevelMusic = nullptr;
    jmethodID ensureLevelMusic = nullptr;
    jmethodID haltMusic = nullptr;
    jmethodID haltAllChannels = nullptr;
    jmethodID playCoinSfx = nullptr;
    jmethodID playLoseSfx = nullptr;
    jmethodID playBumperSfx = nullptr;
    jmethodID playMessageSfx = nullptr;
    jmethodID playVictorySfx = nullptr;
    jmethodID isChannelPlaying = nullptr;
} g_methods;

constexpr const char* kBridgeClassName = "com/Benno111/dorfplatformertimetravel/AndroidAudioBridge";

JNIEnv* getEnv(bool* didAttach) {
    if (didAttach) *didAttach = false;
    if (!g_vm) return nullptr;

    JNIEnv* env = nullptr;
    const jint getEnvResult = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (getEnvResult == JNI_OK) return env;
    if (getEnvResult != JNI_EDETACHED) return nullptr;

    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    if (didAttach) *didAttach = true;
    return env;
}

void releaseEnv(bool didAttach) {
    if (didAttach && g_vm) {
        g_vm->DetachCurrentThread();
    }
}

bool cacheMethods(JNIEnv* env) {
    if (!env) return false;
    std::lock_guard<std::mutex> lock(g_jni_mutex);
    if (g_bridge_class && g_methods.initialize) return true;

    jclass localClass = env->FindClass(kBridgeClassName);
    if (!localClass) {
        env->ExceptionClear();
        return false;
    }

    jclass globalClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
    if (!globalClass) return false;

    g_bridge_class = globalClass;
    g_methods.initialize = env->GetStaticMethodID(g_bridge_class, "initialize", "()Z");
    g_methods.shutdown = env->GetStaticMethodID(g_bridge_class, "shutdown", "()V");
    g_methods.loadGlobalAssets = env->GetStaticMethodID(g_bridge_class, "loadGlobalAssets", "()V");
    g_methods.unloadGlobalAssets = env->GetStaticMethodID(g_bridge_class, "unloadGlobalAssets", "()V");
    g_methods.applyVolumes = env->GetStaticMethodID(g_bridge_class, "applyVolumes", "(ZII)V");
    g_methods.applyMenuMusicToggle = env->GetStaticMethodID(g_bridge_class, "applyMenuMusicToggle", "(Z)V");
    g_methods.setLoopingEnabled = env->GetStaticMethodID(g_bridge_class, "setLoopingEnabled", "(Z)V");
    g_methods.loadLevelMusic = env->GetStaticMethodID(g_bridge_class, "loadLevelMusic", "(Ljava/lang/String;)V");
    g_methods.unloadLevelMusic = env->GetStaticMethodID(g_bridge_class, "unloadLevelMusic", "()V");
    g_methods.ensureLevelMusic = env->GetStaticMethodID(g_bridge_class, "ensureLevelMusic", "(ZZZ)V");
    g_methods.haltMusic = env->GetStaticMethodID(g_bridge_class, "haltMusic", "()V");
    g_methods.haltAllChannels = env->GetStaticMethodID(g_bridge_class, "haltAllChannels", "()V");
    g_methods.playCoinSfx = env->GetStaticMethodID(g_bridge_class, "playCoinSfx", "()V");
    g_methods.playLoseSfx = env->GetStaticMethodID(g_bridge_class, "playLoseSfx", "()V");
    g_methods.playBumperSfx = env->GetStaticMethodID(g_bridge_class, "playBumperSfx", "()V");
    g_methods.playMessageSfx = env->GetStaticMethodID(g_bridge_class, "playMessageSfx", "()V");
    g_methods.playVictorySfx = env->GetStaticMethodID(g_bridge_class, "playVictorySfx", "()I");
    g_methods.isChannelPlaying = env->GetStaticMethodID(g_bridge_class, "isChannelPlaying", "(I)Z");

    if (env->ExceptionCheck() ||
        !g_methods.initialize ||
        !g_methods.shutdown ||
        !g_methods.loadGlobalAssets ||
        !g_methods.unloadGlobalAssets ||
        !g_methods.applyVolumes ||
        !g_methods.applyMenuMusicToggle ||
        !g_methods.setLoopingEnabled ||
        !g_methods.loadLevelMusic ||
        !g_methods.unloadLevelMusic ||
        !g_methods.ensureLevelMusic ||
        !g_methods.haltMusic ||
        !g_methods.haltAllChannels ||
        !g_methods.playCoinSfx ||
        !g_methods.playLoseSfx ||
        !g_methods.playBumperSfx ||
        !g_methods.playMessageSfx ||
        !g_methods.playVictorySfx ||
        !g_methods.isChannelPlaying) {
        env->ExceptionClear();
        return false;
    }

    return true;
}

template <typename Fn>
auto withEnv(Fn&& fn) -> decltype(fn(static_cast<JNIEnv*>(nullptr))) {
    bool didAttach = false;
    JNIEnv* env = getEnv(&didAttach);
    if (!env || !cacheMethods(env)) {
        using ReturnT = decltype(fn(static_cast<JNIEnv*>(nullptr)));
        releaseEnv(didAttach);
        return ReturnT();
    }

    auto result = fn(env);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    releaseEnv(didAttach);
    return result;
}

void callVoid(jmethodID method) {
    withEnv([&](JNIEnv* env) {
        if (!env || !g_bridge_class || !method) return 0;
        env->CallStaticVoidMethod(g_bridge_class, method);
        return 0;
    });
}

std::string sanitizeAssetPath(const std::string& path) {
    constexpr const char* kPrefix = "assets/";
    if (path.rfind(kPrefix, 0) == 0) {
        return path.substr(7);
    }
    return path;
}

} // namespace

extern "C" jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    bool didAttach = false;
    JNIEnv* env = getEnv(&didAttach);
    if (env) {
        (void)cacheMethods(env);
    }
    releaseEnv(didAttach);
    return JNI_VERSION_1_6;
}

namespace AndroidAudioBridge {

bool initialize() {
    return withEnv([&](JNIEnv* env) -> bool {
        if (!env || !g_bridge_class || !g_methods.initialize) return false;
        return env->CallStaticBooleanMethod(g_bridge_class, g_methods.initialize) == JNI_TRUE;
    });
}

void shutdown() { callVoid(g_methods.shutdown); }

void loadGlobalAssets() { callVoid(g_methods.loadGlobalAssets); }

void unloadGlobalAssets() { callVoid(g_methods.unloadGlobalAssets); }

void applyVolumes(bool muteAllAudio, int musicVolume, int sfxVolume) {
    withEnv([&](JNIEnv* env) {
        if (!env || !g_bridge_class || !g_methods.applyVolumes) return 0;
        env->CallStaticVoidMethod(g_bridge_class,
                                  g_methods.applyVolumes,
                                  muteAllAudio ? JNI_TRUE : JNI_FALSE,
                                  static_cast<jint>(musicVolume),
                                  static_cast<jint>(sfxVolume));
        return 0;
    });
}

void applyMenuMusicToggle(bool menuMusicEnabled) {
    withEnv([&](JNIEnv* env) {
        if (!env || !g_bridge_class || !g_methods.applyMenuMusicToggle) return 0;
        env->CallStaticVoidMethod(g_bridge_class,
                                  g_methods.applyMenuMusicToggle,
                                  menuMusicEnabled ? JNI_TRUE : JNI_FALSE);
        return 0;
    });
}

void setLoopingEnabled(bool enabled) {
    withEnv([&](JNIEnv* env) {
        if (!env || !g_bridge_class || !g_methods.setLoopingEnabled) return 0;
        env->CallStaticVoidMethod(g_bridge_class,
                                  g_methods.setLoopingEnabled,
                                  enabled ? JNI_TRUE : JNI_FALSE);
        return 0;
    });
}

void loadLevelMusic(const std::string& musicPath) {
    withEnv([&](JNIEnv* env) {
        if (!env || !g_bridge_class || !g_methods.loadLevelMusic) return 0;
        const std::string assetPath = sanitizeAssetPath(musicPath);
        jstring jPath = env->NewStringUTF(assetPath.c_str());
        if (!jPath) return 0;
        env->CallStaticVoidMethod(g_bridge_class, g_methods.loadLevelMusic, jPath);
        env->DeleteLocalRef(jPath);
        return 0;
    });
}

void unloadLevelMusic() { callVoid(g_methods.unloadLevelMusic); }

void ensureLevelMusic(bool paused, bool deathSequenceActive, bool levelCompleteActive) {
    withEnv([&](JNIEnv* env) {
        if (!env || !g_bridge_class || !g_methods.ensureLevelMusic) return 0;
        env->CallStaticVoidMethod(g_bridge_class,
                                  g_methods.ensureLevelMusic,
                                  paused ? JNI_TRUE : JNI_FALSE,
                                  deathSequenceActive ? JNI_TRUE : JNI_FALSE,
                                  levelCompleteActive ? JNI_TRUE : JNI_FALSE);
        return 0;
    });
}

void haltMusic() { callVoid(g_methods.haltMusic); }

void haltAllChannels() { callVoid(g_methods.haltAllChannels); }

void playCoinSfx() { callVoid(g_methods.playCoinSfx); }

void playLoseSfx() { callVoid(g_methods.playLoseSfx); }

void playBumperSfx() { callVoid(g_methods.playBumperSfx); }

void playMessageSfx() { callVoid(g_methods.playMessageSfx); }

int playVictorySfx() {
    return withEnv([&](JNIEnv* env) -> int {
        if (!env || !g_bridge_class || !g_methods.playVictorySfx) return -1;
        return static_cast<int>(env->CallStaticIntMethod(g_bridge_class, g_methods.playVictorySfx));
    });
}

bool isChannelPlaying(int channel) {
    return withEnv([&](JNIEnv* env) -> bool {
        if (!env || !g_bridge_class || !g_methods.isChannelPlaying) return false;
        return env->CallStaticBooleanMethod(g_bridge_class,
                                            g_methods.isChannelPlaying,
                                            static_cast<jint>(channel)) == JNI_TRUE;
    });
}

} // namespace AndroidAudioBridge

#endif
