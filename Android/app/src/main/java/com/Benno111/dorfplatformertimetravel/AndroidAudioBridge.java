package com.Benno111.dorfplatformertimetravel;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.util.Log;
import android.util.SparseArray;

import java.util.ArrayList;

public final class AndroidAudioBridge {
    private static final String TAG = "AndroidAudioBridge";
    private static final Object LOCK = new Object();

    private static final String SFX_COIN = "Audio/sfx/Coin.mp3";
    private static final String SFX_LOSE = "Audio/sfx/Lose.mp3";
    private static final String SFX_VICTORY = "Audio/sfx/Victory.mp3";
    private static final String SFX_MESSAGE = "Audio/sfx/Message.mp3";
    private static final String SFX_BUMPER = "Audio/sfx/Bumper.mp3";
    private static final String MENU_MUSIC = "Audio/Music/Menu.mp3";

    private static boolean initialized;
    private static final SparseArray<String> soundAssets = new SparseArray<>();
    private static final SparseArray<MediaPlayer> activeStreams = new SparseArray<>();
    private static int nextSoundId = 1;
    private static int nextStreamId = 1;

    private static final String[] EVENT_SFX = {"Audio/sfx/signflip.mp3", "Audio/sfx/max.wav",
        "Audio/sfx/SuperBounce.mp3", "Audio/sfx/Hint.mp3", "Audio/sfx/Click5.mp3", "Audio/sfx/Error3.mp3"};
    private static final int[] eventSoundIds = new int[EVENT_SFX.length];
    private static int coinSoundId;
    private static int loseSoundId;
    private static int victorySoundId;
    private static int messageSoundId;
    private static int bumperSoundId;

    private static MediaPlayer menuPlayer;
    private static MediaPlayer levelPlayer;
    private static String levelMusicAssetPath;
    private static boolean menuMusicEnabled;
    private static boolean loopingEnabled = true;
    private static float musicVolume = 1.0f;
    private static float sfxVolume = 1.0f;
    private static boolean muteAllAudio;

    private AndroidAudioBridge() {}

    public static boolean initialize() {
        synchronized (LOCK) {
            if (initialized) return true;
            if (getContext() == null) return false;
            initialized = true;
            return true;
        }
    }

    public static void shutdown() {
        synchronized (LOCK) {
            haltAllChannelsLocked();
            releasePlayerLocked(menuPlayer);
            menuPlayer = null;
            releasePlayerLocked(levelPlayer);
            levelPlayer = null;
            levelMusicAssetPath = null;
            soundAssets.clear();
            initialized = false;
        }
    }

    public static void loadGlobalAssets() {
        synchronized (LOCK) {
            if (!initialize()) {
                return;
            }

            for (int i=0; i<EVENT_SFX.length; ++i) eventSoundIds[i] = loadSoundLocked(EVENT_SFX[i]);
            coinSoundId = loadSoundLocked(SFX_COIN);
            loseSoundId = loadSoundLocked(SFX_LOSE);
            victorySoundId = loadSoundLocked(SFX_VICTORY);
            messageSoundId = loadSoundLocked(SFX_MESSAGE);
            bumperSoundId = loadSoundLocked(SFX_BUMPER);

            releasePlayerLocked(menuPlayer);
            menuPlayer = createPlayerLocked(MENU_MUSIC);
            applyVolumesLocked();
            syncMenuMusicLocked();
        }
    }

    public static void unloadGlobalAssets() {
        synchronized (LOCK) {
            haltAllChannelsLocked();
            releasePlayerLocked(menuPlayer);
            menuPlayer = null;
            java.util.Arrays.fill(eventSoundIds, 0);
            coinSoundId = 0;
            loseSoundId = 0;
            victorySoundId = 0;
            messageSoundId = 0;
            bumperSoundId = 0;
            soundAssets.clear();
        }
    }

    public static void applyVolumes(boolean muteAllAudioValue, int musicVolumeValue, int sfxVolumeValue) {
        synchronized (LOCK) {
            muteAllAudio = muteAllAudioValue;
            musicVolume = clampVolume(musicVolumeValue);
            sfxVolume = clampVolume(sfxVolumeValue);
            applyVolumesLocked();
        }
    }

    public static void applyMenuMusicToggle(boolean enabled) {
        synchronized (LOCK) {
            menuMusicEnabled = enabled;
            syncMenuMusicLocked();
        }
    }

    public static void setLoopingEnabled(boolean enabled) {
        synchronized (LOCK) {
            loopingEnabled = enabled;
            if (menuPlayer != null) {
                menuPlayer.setLooping(loopingEnabled);
            }
            if (levelPlayer != null) {
                levelPlayer.setLooping(loopingEnabled);
            }
        }
    }

    public static void loadLevelMusic(String musicPath) {
        synchronized (LOCK) {
            releasePlayerLocked(levelPlayer);
            levelPlayer = null;
            levelMusicAssetPath = null;

            final String assetPath = normalizeAssetPath(musicPath);
            if (assetPath.isEmpty()) {
                syncMenuMusicLocked();
                return;
            }

            levelPlayer = createPlayerLocked(assetPath);
            if (levelPlayer != null) {
                levelMusicAssetPath = assetPath;
                if (menuPlayer != null && menuPlayer.isPlaying()) {
                    stopMusicLocked(menuPlayer);
                }
                startMusicLocked(levelPlayer);
            } else {
                syncMenuMusicLocked();
            }
        }
    }

    public static void unloadLevelMusic() {
        synchronized (LOCK) {
            releasePlayerLocked(levelPlayer);
            levelPlayer = null;
            levelMusicAssetPath = null;
            syncMenuMusicLocked();
        }
    }

    public static void ensureLevelMusic(boolean paused, boolean deathSequenceActive, boolean levelCompleteActive) {
        synchronized (LOCK) {
            if (levelPlayer == null) {
                return;
            }
            if (paused || deathSequenceActive || levelCompleteActive) {
                return;
            }
            if (!levelPlayer.isPlaying()) {
                startMusicLocked(levelPlayer);
            }
        }
    }

    public static void haltMusic() {
        synchronized (LOCK) {
            if (levelPlayer != null) {
                stopMusicLocked(levelPlayer);
            }
            if (menuPlayer != null) {
                stopMusicLocked(menuPlayer);
            }
        }
    }

    public static void haltAllChannels() {
        synchronized (LOCK) {
            haltAllChannelsLocked();
        }
    }

    public static void playEventSfx(int event) {
        synchronized (LOCK) {
            if (event >= 0 && event < eventSoundIds.length) playSoundLocked(eventSoundIds[event]);
        }
    }

    public static void playCoinSfx() {
        synchronized (LOCK) {
            playSoundLocked(coinSoundId);
        }
    }

    public static void playLoseSfx() {
        synchronized (LOCK) {
            playSoundLocked(loseSoundId);
        }
    }

    public static void playBumperSfx() {
        synchronized (LOCK) {
            playSoundLocked(bumperSoundId);
        }
    }

    public static void playMessageSfx() {
        synchronized (LOCK) {
            playSoundLocked(messageSoundId);
        }
    }

    public static int playVictorySfx() {
        synchronized (LOCK) {
            return playSoundLocked(victorySoundId);
        }
    }

    public static boolean isChannelPlaying(int channel) {
        synchronized (LOCK) {
            // Preparing asynchronous streams count as active until completion.
            return activeStreams.get(channel) != null;
        }
    }

    private static Context getContext() {
        return MainActivity.getAppContext();
    }

    private static String normalizeAssetPath(String path) {
        if (path == null) {
            return "";
        }
        String normalized = path.trim().replace('\\', '/');
        while (normalized.startsWith("./")) {
            normalized = normalized.substring(2);
        }
        if (normalized.startsWith("assets/")) {
            normalized = normalized.substring("assets/".length());
        }
        return normalized;
    }

    private static float clampVolume(int value) {
        final int clamped = Math.max(0, Math.min(128, value));
        return clamped / 128.0f;
    }

    private static float appliedMusicVolume() {
        return muteAllAudio ? 0.0f : musicVolume;
    }

    private static float appliedSfxVolume() {
        return muteAllAudio ? 0.0f : sfxVolume;
    }

    private static void applyVolumesLocked() {
        final float music = appliedMusicVolume();
        if (menuPlayer != null) {
            menuPlayer.setVolume(music, music);
        }
        if (levelPlayer != null) {
            levelPlayer.setVolume(music, music);
        }

        final float sfx = appliedSfxVolume();
        for (int i=0; i<activeStreams.size(); ++i) {
            try { activeStreams.valueAt(i).setVolume(sfx,sfx); }
            catch (IllegalStateException ignored) {}
        }
    }

    private static int loadSoundLocked(String assetPath) {
        if (!initialized) return 0;
        final int id=nextSoundId++;
        soundAssets.put(id,assetPath);
        return id;
    }

    private static MediaPlayer createPlayerLocked(String assetPath) {
        final Context context = getContext();
        if (context == null) {
            return null;
        }

        final MediaPlayer player = new MediaPlayer();
        try (AssetFileDescriptor afd = context.getAssets().openFd(assetPath)) {
            player.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
            player.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build());
            player.setLooping(loopingEnabled);
            final float volume = appliedMusicVolume();
            player.setVolume(volume, volume);
            player.prepare();
            player.setOnCompletionListener(mp -> {
                synchronized (LOCK) {
                    if (!loopingEnabled) {
                        try {
                            mp.seekTo(0);
                        } catch (IllegalStateException ignored) {
                        }
                    }
                }
            });
            return player;
        } catch (Throwable t) {
            releasePlayerLocked(player);
            Log.i(TAG, "createPlayer failed for " + assetPath, t);
            return null;
        }
    }

    private static void releasePlayerLocked(MediaPlayer player) {
        if (player == null) {
            return;
        }
        try {
            player.stop();
        } catch (IllegalStateException ignored) {
        }
        try {
            player.release();
        } catch (Throwable ignored) {
        }
    }

    private static void startMusicLocked(MediaPlayer player) {
        if (player == null) {
            return;
        }
        try {
            player.setLooping(loopingEnabled);
            final float volume = appliedMusicVolume();
            player.setVolume(volume, volume);
            if (!player.isPlaying()) {
                player.seekTo(0);
                player.start();
            }
        } catch (IllegalStateException e) {
            Log.i(TAG, "startMusic failed", e);
        }
    }

    private static void stopMusicLocked(MediaPlayer player) {
        if (player == null) {
            return;
        }
        try {
            if (player.isPlaying()) {
                player.pause();
            }
            player.seekTo(0);
        } catch (IllegalStateException e) {
            Log.i(TAG, "stopMusic failed", e);
        }
    }

    private static void syncMenuMusicLocked() {
        if (levelPlayer != null) {
            return;
        }
        if (menuPlayer == null) {
            return;
        }
        if (menuMusicEnabled) {
            startMusicLocked(menuPlayer);
        } else {
            stopMusicLocked(menuPlayer);
        }
    }

    private static void finishStreamLocked(int id, MediaPlayer player) {
        if (activeStreams.get(id) != player) return;
        activeStreams.remove(id);
        releasePlayerLocked(player);
    }

    private static int playSoundLocked(int soundId) {
        final String assetPath=soundAssets.get(soundId);
        final Context context=getContext();
        if (!initialized || assetPath==null || context==null) return -1;
        // Bound concurrent decoders/file descriptors without stealing victory
        // playback or destroying an effect which is still preparing.
        if (activeStreams.size()>=8) return -1;
        final int id=nextStreamId++;
        final MediaPlayer player=new MediaPlayer();
        try (AssetFileDescriptor afd=context.getAssets().openFd(assetPath)) {
            player.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build());
            player.setDataSource(afd.getFileDescriptor(),afd.getStartOffset(),afd.getLength());
            player.setLooping(false);
            final float volume=appliedSfxVolume();
            player.setVolume(volume,volume);
            activeStreams.put(id,player);
            player.setOnPreparedListener(mp -> {
                synchronized (LOCK) {
                    if(activeStreams.get(id)!=mp)return;
                    try { final float gain=appliedSfxVolume();mp.setVolume(gain,gain);mp.start(); }
                    catch(IllegalStateException error){finishStreamLocked(id,mp);}
                }
            });
            player.setOnCompletionListener(mp -> { synchronized(LOCK){finishStreamLocked(id,mp);} });
            player.setOnErrorListener((mp,what,extra) -> {
                synchronized(LOCK){finishStreamLocked(id,mp);}return true;
            });
            player.prepareAsync();
            return id;
        } catch (Throwable error) {
            activeStreams.remove(id);
            releasePlayerLocked(player);
            Log.i(TAG,"Could not stream SFX " + assetPath,error);
            return -1;
        }
    }

    private static void haltAllChannelsLocked() {
        final ArrayList<MediaPlayer> players=new ArrayList<>();
        for(int i=0;i<activeStreams.size();++i)players.add(activeStreams.valueAt(i));
        activeStreams.clear();
        for(MediaPlayer player:players)releasePlayerLocked(player);
    }
}
