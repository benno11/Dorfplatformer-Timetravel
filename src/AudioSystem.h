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
    void setSleeping(bool sleeping);
    void setLoading(bool loading);
    bool isLoading() const { return loading_; }
    void updateGameplayPause(bool paused, float elapsedSeconds);
    void resetGameplayPause();

    void loadGlobalAssets();
    bool gameplayAssetsReady() const;
    void finishGameplayAssetLoading();
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

    static void pumpEventSfx();

    void playCoinSfx();
    void playLoseSfx();
    void playBumperSfx();
    void playMessageSfx();
    int playVictorySfx();
    bool isChannelPlaying(int channel) const;

private:
    void applyOutputGain();
    bool loading_ = false, sleeping_ = false;
    float pauseGain_ = 1.0f;
    bool muted_ = false;
    int musicVolume_ = 128, sfxVolume_ = 128;
    struct Impl;
    Impl* impl_ = nullptr;
};
