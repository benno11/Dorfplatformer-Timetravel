#pragma once

#include <string>

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool initialize();
    void shutdown();

    bool isReady() const;

    void loadGlobalAssets();
    void unloadGlobalAssets();

    void applyVolumes(bool muteAllAudio, int musicVolume, int sfxVolume);
    void applyMenuMusicToggle(bool menuMusicEnabled);
    void setLoopingEnabled(bool enabled);
    bool isLoopingEnabled() const;

    void loadLevelMusic(const std::string& musicPath);
    void unloadLevelMusic();
    void ensureLevelMusic(bool paused, bool deathSequenceActive, bool levelCompleteActive);

    void haltMusic();
    void haltAllChannels();

    void playCoinSfx();
    void playLoseSfx();
    void playBumperSfx();
    void playMessageSfx();
    int playVictorySfx();
    bool isChannelPlaying(int channel) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
