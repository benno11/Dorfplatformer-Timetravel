#pragma once

#if defined(__ANDROID__)

#include <string>

namespace AndroidAudioBridge {

bool initialize();
void shutdown();

void loadGlobalAssets();
void unloadGlobalAssets();

void applyVolumes(bool muteAllAudio, int musicVolume, int sfxVolume);
void applyMenuMusicToggle(bool menuMusicEnabled);
void setLoopingEnabled(bool enabled);

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
bool isChannelPlaying(int channel);

} // namespace AndroidAudioBridge

#endif
