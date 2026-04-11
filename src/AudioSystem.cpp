#include "AudioSystem.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#include <SDL3/SDL.h>

#ifndef PLATFORMER_HAS_SDL3_MIXER
#define PLATFORMER_HAS_SDL3_MIXER 0
#endif

#if PLATFORMER_HAS_SDL3_MIXER
#include <SDL3_mixer/SDL_mixer.h>
#define AUDIO_HAS_SDL3_MIXER 1
#else
#define AUDIO_HAS_SDL3_MIXER 0
#endif

#include "AssetPath.h"

#if AUDIO_HAS_SDL3_MIXER
namespace {
struct Mix_Chunk { MIX_Audio* audio = nullptr; };
using Mix_Music = Mix_Chunk;

static MIX_Mixer* g_mix_mixer = nullptr;
static std::vector<MIX_Track*> g_mix_channels;
static MIX_Track* g_mix_music_track = nullptr;
static bool g_mix_initialized = false;

#ifndef MIX_INIT_MP3
#define MIX_INIT_MP3 0x00000008
#endif
#ifndef MIX_DEFAULT_FORMAT
#define MIX_DEFAULT_FORMAT SDL_AUDIO_S16
#endif

static MIX_Track* mix_track_at(int idx) {
    if (idx < 0) return nullptr;
    if ((int)g_mix_channels.size() <= idx) g_mix_channels.resize(idx + 1, nullptr);
    if (!g_mix_channels[idx] && g_mix_mixer) g_mix_channels[idx] = MIX_CreateTrack(g_mix_mixer);
    return g_mix_channels[idx];
}

static int mix_find_channel() {
    for (int i = 0; i < (int)g_mix_channels.size(); ++i) {
        if (!g_mix_channels[i] || !MIX_TrackPlaying(g_mix_channels[i])) return i;
    }
    g_mix_channels.push_back(nullptr);
    return (int)g_mix_channels.size() - 1;
}

static int Mix_InitCompat(int flags) {
    const bool ok = MIX_Init();
    g_mix_initialized = ok;
    return ok ? flags : 0;
}

static int Mix_OpenAudioCompat(int frequency, SDL_AudioFormat format, int channels, int chunksize) {
    (void)chunksize;
    if (g_mix_mixer) return 0;
    SDL_AudioSpec want{};
    want.freq = frequency;
    want.format = format;
    want.channels = (Uint8)((channels <= 0) ? 2 : channels);
    g_mix_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
    return g_mix_mixer ? 0 : -1;
}

static const char* Mix_GetErrorCompat() { return SDL_GetError(); }

static std::string availableAudioDecoders() {
    std::ostringstream out;
    const int count = MIX_GetNumAudioDecoders();
    bool first = true;
    for (int i = 0; i < count; ++i) {
        const char* decoder = MIX_GetAudioDecoder(i);
        if (!decoder || !*decoder) continue;
        if (!first) out << ", ";
        out << decoder;
        first = false;
    }
    return out.str();
}

static MIX_Audio* mixLoadAudioWithDecoder(MIX_Mixer* mixer, const char* path, bool predecode, const char* decoder) {
    if (!mixer || !path || !*path) return nullptr;
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) return nullptr;

    const SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        SDL_CloseIO(io);
        return nullptr;
    }

    SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, io);
    SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, true);
    SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN, predecode);
    SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_SKIP_METADATA_TAGS_BOOLEAN, false);
    SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_PREFERRED_MIXER_POINTER, mixer);
    if (decoder && *decoder) SDL_SetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, decoder);

    MIX_Audio* audio = MIX_LoadAudioWithProperties(props);
    SDL_DestroyProperties(props);
    return audio;
}

static MIX_Audio* mixLoadAudioBestEffort(const char* path, bool predecode) {
    if (!g_mix_mixer || !path || !*path) return nullptr;

    MIX_Audio* audio = MIX_LoadAudio(g_mix_mixer, path, predecode);
    if (audio) return audio;

    static const char* kDecoderCandidates[] = {"DRMP3", "MPG123", "MP3"};
    for (const char* decoder : kDecoderCandidates) {
        audio = mixLoadAudioWithDecoder(g_mix_mixer, path, predecode, decoder);
        if (audio) return audio;
    }

    return nullptr;
}

static Mix_Chunk* Mix_LoadWAVCompat(const char* path) {
    if (!g_mix_mixer || !path) return nullptr;
    MIX_Audio* a = mixLoadAudioBestEffort(path, true);
    if (!a) return nullptr;
    Mix_Chunk* c = new Mix_Chunk();
    c->audio = a;
    return c;
}

static Mix_Music* Mix_LoadMUSCompat(const char* path) {
    if (!g_mix_mixer || !path) return nullptr;
    MIX_Audio* a = mixLoadAudioBestEffort(path, false);
    if (!a) {
        // Some MP3 builds only behave when fully predecoded up front.
        a = mixLoadAudioBestEffort(path, true);
    }
    if (!a) return nullptr;
    Mix_Music* m = new Mix_Music();
    m->audio = a;
    return m;
}

static int Mix_PlayChannelCompat(int channel, Mix_Chunk* chunk, int loops) {
    if (!g_mix_mixer || !chunk || !chunk->audio) return -1;
    const int idx = (channel < 0) ? mix_find_channel() : channel;
    MIX_Track* t = mix_track_at(idx);
    if (!t) return -1;
    if (!MIX_SetTrackAudio(t, chunk->audio)) return -1;
    const SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) return -1;
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
    const bool ok = MIX_PlayTrack(t, props);
    SDL_DestroyProperties(props);
    if (!ok) return -1;
    return idx;
}

static int Mix_PlayMusicCompat(Mix_Music* music, int loops) {
    if (!g_mix_mixer || !music || !music->audio) return -1;
    if (!g_mix_music_track) g_mix_music_track = MIX_CreateTrack(g_mix_mixer);
    if (!g_mix_music_track) return -1;
    if (!MIX_SetTrackAudio(g_mix_music_track, music->audio)) return -1;
    const SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) return -1;
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
    const bool ok = MIX_PlayTrack(g_mix_music_track, props);
    SDL_DestroyProperties(props);
    return ok ? 0 : -1;
}

static int Mix_PlayingCompat(int channel) {
    if (channel < 0) {
        for (MIX_Track* t : g_mix_channels) if (t && MIX_TrackPlaying(t)) return 1;
        return 0;
    }
    MIX_Track* t = mix_track_at(channel);
    return (t && MIX_TrackPlaying(t)) ? 1 : 0;
}

static int Mix_PlayingMusicCompat() {
    return (g_mix_music_track && MIX_TrackPlaying(g_mix_music_track)) ? 1 : 0;
}

static void Mix_HaltMusicCompat() {
    if (g_mix_music_track) (void)MIX_StopTrack(g_mix_music_track, 0);
}

static int Mix_HaltChannelCompat(int channel) {
    if (!g_mix_mixer) return 0;
    if (channel < 0) return MIX_StopAllTracks(g_mix_mixer, 0) ? 0 : -1;
    MIX_Track* t = mix_track_at(channel);
    if (!t) return 0;
    return MIX_StopTrack(t, 0) ? 0 : -1;
}

static int Mix_VolumeMusicCompat(int volume) {
    if (!g_mix_mixer) return volume;
    const float gain = std::clamp(volume / 128.0f, 0.0f, 1.0f);
    (void)MIX_SetMixerGain(g_mix_mixer, gain);
    if (g_mix_music_track) (void)MIX_SetTrackGain(g_mix_music_track, gain);
    return volume;
}

static int Mix_VolumeCompat(int channel, int volume) {
    const float gain = std::clamp(volume / 128.0f, 0.0f, 1.0f);
    if (channel < 0) {
        for (MIX_Track* t : g_mix_channels) if (t) (void)MIX_SetTrackGain(t, gain);
        return volume;
    }
    MIX_Track* t = mix_track_at(channel);
    if (t) (void)MIX_SetTrackGain(t, gain);
    return volume;
}

static int Mix_VolumeChunkCompat(Mix_Chunk* chunk, int volume) {
    (void)chunk;
    return volume;
}

static void Mix_FreeChunkCompat(Mix_Chunk* chunk) {
    if (!chunk) return;
    // Audio buffers are owned by the active mixer; only destroy while mixer is alive.
    if (chunk->audio && g_mix_mixer) MIX_DestroyAudio(chunk->audio);
    chunk->audio = nullptr;
    delete chunk;
}

static void Mix_FreeMusicCompat(Mix_Music* music) { Mix_FreeChunkCompat(music); }

static void Mix_CloseAudioCompat() {
    if (g_mix_mixer) (void)MIX_StopAllTracks(g_mix_mixer, 0);
    if (g_mix_music_track) {
        MIX_DestroyTrack(g_mix_music_track);
        g_mix_music_track = nullptr;
    }
    for (MIX_Track*& t : g_mix_channels) {
        if (t) {
            MIX_DestroyTrack(t);
            t = nullptr;
        }
    }
    g_mix_channels.clear();
    if (g_mix_mixer) {
        MIX_DestroyMixer(g_mix_mixer);
        g_mix_mixer = nullptr;
    }
}

static void Mix_QuitCompat() {
    MIX_Quit();
    g_mix_initialized = false;
}

static int musicLoopCount(bool loopingEnabled) {
    return loopingEnabled ? -1 : 0;
}

static bool startMusicWithRetry(Mix_Music* music, int loops) {
    if (!music) return false;
    if (Mix_PlayMusicCompat(music, loops) == 0) return true;
    // Recover from stale music track state by halting and retrying once.
    Mix_HaltMusicCompat();
    return Mix_PlayMusicCompat(music, loops) == 0;
}

static bool audioDriverAvailable(const char* driver) {
    if (!driver || !*driver) return false;
    const int n = SDL_GetNumAudioDrivers();
    for (int i = 0; i < n; ++i) {
        const char* d = SDL_GetAudioDriver(i);
        if (d && std::strcmp(d, driver) == 0) return true;
    }
    return false;
}

static bool setAudioDriverEnv(const char* driver) {
    if (!driver || !*driver) return false;
#if defined(_WIN32)
    const errno_t a = _putenv_s("SDL_AUDIO_DRIVER", driver);
    const errno_t b = _putenv_s("SDL_AUDIODRIVER", driver);
    return a == 0 && b == 0;
#else
    return setenv("SDL_AUDIO_DRIVER", driver, 1) == 0 &&
           setenv("SDL_AUDIODRIVER", driver, 1) == 0;
#endif
}

static bool reinitAudioSubsystemWithDriver(const char* driver) {
    if (!driver || !*driver) return false;
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
    if (!setAudioDriverEnv(driver)) {
        SDL_Log("Audio mixer retry: could not set audio driver env to %s", driver);
        return false;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Audio mixer retry: SDL_InitSubSystem failed for driver %s: %s", driver, SDL_GetError());
        return false;
    }
    return true;
}
} // namespace
#endif

struct AudioSystem::Impl {
    bool ready = false;
    bool loopingEnabled = true;
    bool shuttingDown = false;

#if AUDIO_HAS_SDL3_MIXER
    Mix_Chunk* coinSfx = nullptr;
    Mix_Chunk* loseSfx = nullptr;
    Mix_Chunk* victorySfx = nullptr;
    Mix_Chunk* messageSfx = nullptr;
    Mix_Chunk* bumperSfx = nullptr;
    Mix_Music* menuMusic = nullptr;
    Mix_Music* levelMusic = nullptr;
    bool menuMusicPlaying = false;
#endif
};

AudioSystem::~AudioSystem() { shutdown(); }

bool AudioSystem::initialize() {
    if (!impl_) impl_ = new Impl();
    if (impl_->shuttingDown) return false;
    if (impl_->ready) return true;

#if AUDIO_HAS_SDL3_MIXER
    constexpr int mixerFlags = MIX_INIT_MP3;
    impl_->ready = (Mix_InitCompat(mixerFlags) & mixerFlags) == mixerFlags;
    if (impl_->ready) {
        impl_->ready = (Mix_OpenAudioCompat(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0);
    }
    if (!impl_->ready) {
        const char* activeDriver = SDL_GetCurrentAudioDriver();
        const std::string firstError = Mix_GetErrorCompat() ? Mix_GetErrorCompat() : "";
        const bool pipewireHotplugFailed =
            firstError.find("Pipewire: Failed to connect hotplug detection context") != std::string::npos;
        SDL_Log("Audio mixer init failed (driver=%s): %s",
                activeDriver ? activeDriver : "<none>",
                firstError.c_str());

        const char* retryDrivers[] = {
#if defined(__ANDROID__)
            "aaudio", "openslES", "dummy"
#elif defined(_WIN32)
            "wasapi", "directsound", "xaudio2", "dummy"
#else
            "pipewire", "pulseaudio", "alsa", "sndio", "dummy"
#endif
        };
        for (const char* driver : retryDrivers) {
            if (pipewireHotplugFailed && std::strcmp(driver, "pipewire") == 0) continue;
            if (!audioDriverAvailable(driver)) continue;
            if (activeDriver && std::strcmp(activeDriver, driver) == 0) continue;
            if (!reinitAudioSubsystemWithDriver(driver)) continue;
            const char* switchedDriver = SDL_GetCurrentAudioDriver();
            if (!switchedDriver || std::strcmp(switchedDriver, driver) != 0) {
                SDL_Log("Audio mixer retry skipped (requested=%s active=%s)",
                        driver,
                        switchedDriver ? switchedDriver : "<none>");
                continue;
            }
            if (Mix_OpenAudioCompat(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0) {
                impl_->ready = true;
                SDL_Log("Audio mixer init recovered with SDL_AUDIODRIVER=%s", driver);
                break;
            }
            SDL_Log("Audio mixer retry failed (driver=%s): %s", driver, Mix_GetErrorCompat());
        }
    }
#else
    SDL_Log("SDL_mixer not found at compile time: music playback disabled.");
    impl_->ready = false;
#endif

    return impl_->ready;
}

void AudioSystem::shutdown() {
    if (!impl_) return;
    if (impl_->shuttingDown) return;
    impl_->shuttingDown = true;

    if (impl_->ready) {
        haltMusic();
        haltAllChannels();
    }
    unloadLevelMusic();
    unloadGlobalAssets();

#if AUDIO_HAS_SDL3_MIXER
    // Handle partial init safely (e.g. Mix_Init succeeded but open device failed).
    if (g_mix_mixer) {
        Mix_CloseAudioCompat();
    }
    if (g_mix_initialized) {
        Mix_QuitCompat();
    }
#endif

    delete impl_;
    impl_ = nullptr;
}

bool AudioSystem::isReady() const { return impl_ && impl_->ready; }

void AudioSystem::setLoopingEnabled(bool enabled) {
    if (!impl_) impl_ = new Impl();
    impl_->loopingEnabled = enabled;
}

bool AudioSystem::isLoopingEnabled() const {
    return impl_ ? impl_->loopingEnabled : true;
}

void AudioSystem::loadGlobalAssets() {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    const std::string decoders = availableAudioDecoders();
    auto loadSfx = [](const char* path, const char* label) -> Mix_Chunk* {
        Mix_Chunk* chunk = Mix_LoadWAVCompat(ResolveAssetPath(path).c_str());
        if (!chunk) SDL_Log("Could not load %s: %s", label, Mix_GetErrorCompat());
        return chunk;
    };
    impl_->coinSfx = loadSfx("assets/Audio/sfx/Coin.mp3", "coin sfx");
    impl_->loseSfx = loadSfx("assets/Audio/sfx/Lose.mp3", "lose sfx");
    impl_->victorySfx = loadSfx("assets/Audio/sfx/Victory.mp3", "victory sfx");
    impl_->messageSfx = loadSfx("assets/Audio/sfx/Message.mp3", "message sfx");
    impl_->bumperSfx = loadSfx("assets/Audio/sfx/Bumper.mp3", "bumper sfx");

    impl_->menuMusic = Mix_LoadMUSCompat(ResolveAssetPath("assets/Audio/Music/menu.mp3").c_str());
    if (!impl_->menuMusic) {
        impl_->menuMusic = Mix_LoadMUSCompat(ResolveAssetPath("assets/Audio/Music/Menu.mp3").c_str());
    }
    if (!impl_->menuMusic) {
        SDL_Log("Could not load menu music: %s", Mix_GetErrorCompat());
        if (!decoders.empty()) SDL_Log("Available audio decoders: %s", decoders.c_str());
    }
#endif
}

void AudioSystem::unloadGlobalAssets() {
    if (!impl_) return;
#if AUDIO_HAS_SDL3_MIXER
    if (!g_mix_mixer) {
        impl_->coinSfx = nullptr;
        impl_->loseSfx = nullptr;
        impl_->victorySfx = nullptr;
        impl_->messageSfx = nullptr;
        impl_->bumperSfx = nullptr;
        impl_->menuMusic = nullptr;
        impl_->menuMusicPlaying = false;
        return;
    }
    // Stop any active playback before freeing underlying audio buffers.
    Mix_HaltMusicCompat();
    Mix_HaltChannelCompat(-1);
    if (impl_->coinSfx) { Mix_FreeChunkCompat(impl_->coinSfx); impl_->coinSfx = nullptr; }
    if (impl_->loseSfx) { Mix_FreeChunkCompat(impl_->loseSfx); impl_->loseSfx = nullptr; }
    if (impl_->victorySfx) { Mix_FreeChunkCompat(impl_->victorySfx); impl_->victorySfx = nullptr; }
    if (impl_->messageSfx) { Mix_FreeChunkCompat(impl_->messageSfx); impl_->messageSfx = nullptr; }
    if (impl_->bumperSfx) { Mix_FreeChunkCompat(impl_->bumperSfx); impl_->bumperSfx = nullptr; }
    if (impl_->menuMusic) { Mix_FreeMusicCompat(impl_->menuMusic); impl_->menuMusic = nullptr; }
    impl_->menuMusicPlaying = false;
#endif
}

void AudioSystem::applyVolumes(bool muteAllAudio, int musicVolume, int sfxVolume) {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    const int appliedMusic = muteAllAudio ? 0 : musicVolume;
    const int appliedSfx = muteAllAudio ? 0 : sfxVolume;
    Mix_VolumeMusicCompat(appliedMusic);
    Mix_VolumeCompat(-1, appliedSfx);
    if (impl_->coinSfx) Mix_VolumeChunkCompat(impl_->coinSfx, appliedSfx);
    if (impl_->loseSfx) Mix_VolumeChunkCompat(impl_->loseSfx, appliedSfx);
    if (impl_->victorySfx) Mix_VolumeChunkCompat(impl_->victorySfx, appliedSfx);
    if (impl_->messageSfx) Mix_VolumeChunkCompat(impl_->messageSfx, appliedSfx);
    if (impl_->bumperSfx) Mix_VolumeChunkCompat(impl_->bumperSfx, appliedSfx);
#endif
}

void AudioSystem::applyMenuMusicToggle(bool menuMusicEnabled) {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    if (menuMusicEnabled && impl_->menuMusic) {
        if (!impl_->menuMusicPlaying || !Mix_PlayingMusicCompat()) {
            impl_->menuMusicPlaying = startMusicWithRetry(impl_->menuMusic, musicLoopCount(impl_->loopingEnabled));
        }
    } else if (impl_->menuMusicPlaying) {
        Mix_HaltMusicCompat();
        impl_->menuMusicPlaying = false;
    }
#endif
}

void AudioSystem::loadLevelMusic(const std::string& musicPath) {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    unloadLevelMusic();
    if (musicPath.empty()) return;
    impl_->levelMusic = Mix_LoadMUSCompat(ResolveAssetPath(musicPath).c_str());
    if (impl_->levelMusic) {
        impl_->menuMusicPlaying = false;
        (void)startMusicWithRetry(impl_->levelMusic, musicLoopCount(impl_->loopingEnabled));
    } else {
        SDL_Log("Could not load music: %s (%s)", musicPath.c_str(), Mix_GetErrorCompat());
        const std::string decoders = availableAudioDecoders();
        if (!decoders.empty()) SDL_Log("Available audio decoders: %s", decoders.c_str());
    }
#else
    (void)musicPath;
#endif
}

void AudioSystem::unloadLevelMusic() {
    if (!impl_) return;
#if AUDIO_HAS_SDL3_MIXER
    if (!g_mix_mixer) {
        impl_->levelMusic = nullptr;
        return;
    }
    if (impl_->levelMusic) {
        Mix_HaltMusicCompat();
        Mix_FreeMusicCompat(impl_->levelMusic);
        impl_->levelMusic = nullptr;
    }
#endif
}

void AudioSystem::ensureLevelMusic(bool paused, bool deathSequenceActive, bool levelCompleteActive) {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    if (impl_->levelMusic &&
        !paused &&
        !deathSequenceActive &&
        !levelCompleteActive &&
        !Mix_PlayingMusicCompat()) {
        (void)startMusicWithRetry(impl_->levelMusic, musicLoopCount(impl_->loopingEnabled));
    }
#else
    (void)paused;
    (void)deathSequenceActive;
    (void)levelCompleteActive;
#endif
}

void AudioSystem::haltMusic() {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    Mix_HaltMusicCompat();
#endif
}

void AudioSystem::haltAllChannels() {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    Mix_HaltChannelCompat(-1);
#endif
}

void AudioSystem::playCoinSfx() {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    if (impl_->coinSfx) Mix_PlayChannelCompat(-1, impl_->coinSfx, 0);
#endif
}

void AudioSystem::playLoseSfx() {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    if (impl_->loseSfx) Mix_PlayChannelCompat(-1, impl_->loseSfx, 0);
#endif
}

void AudioSystem::playBumperSfx() {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    if (impl_->bumperSfx) Mix_PlayChannelCompat(-1, impl_->bumperSfx, 0);
#endif
}

void AudioSystem::playMessageSfx() {
    if (!isReady() || impl_->shuttingDown) return;
#if AUDIO_HAS_SDL3_MIXER
    if (impl_->messageSfx) Mix_PlayChannelCompat(-1, impl_->messageSfx, 0);
#endif
}

int AudioSystem::playVictorySfx() {
    if (!isReady() || impl_->shuttingDown) return -1;
#if AUDIO_HAS_SDL3_MIXER
    if (impl_->victorySfx) return Mix_PlayChannelCompat(-1, impl_->victorySfx, 0);
#endif
    return -1;
}

bool AudioSystem::isChannelPlaying(int channel) const {
    if (!isReady() || impl_->shuttingDown) return false;
#if AUDIO_HAS_SDL3_MIXER
    return Mix_PlayingCompat(channel) != 0;
#else
    (void)channel;
    return false;
#endif
}

