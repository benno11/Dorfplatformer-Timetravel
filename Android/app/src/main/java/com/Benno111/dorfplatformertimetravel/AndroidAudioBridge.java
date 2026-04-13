package com.Benno111.dorfplatformertimetravel;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.MediaMetadataRetriever;
import android.media.MediaPlayer;
import android.media.SoundPool;
import android.os.SystemClock;
import android.util.Log;
import android.util.SparseArray;
import android.util.SparseBooleanArray;
import android.util.SparseIntArray;

import java.io.IOException;
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

    private static SoundPool soundPool;
    private static final SparseBooleanArray loadedSoundIds = new SparseBooleanArray();
    private static final SparseIntArray soundDurationsMs = new SparseIntArray();
    private static final SparseArray<Long> activeStreams = new SparseArray<>();

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
            if (soundPool != null) {
                return true;
            }

            final Context context = getContext();
            if (context == null) {
                Log.i(TAG, "initialize: no SDL activity/context yet");
                return false;
            }

            final AudioAttributes attrs = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build();

            soundPool = new SoundPool.Builder()
                    .setAudioAttributes(attrs)
                    .setMaxStreams(8)
                    .build();

            soundPool.setOnLoadCompleteListener((pool, sampleId, status) -> {
                synchronized (LOCK) {
                    loadedSoundIds.put(sampleId, status == 0);
                    LOCK.notifyAll();
                }
            });

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
            loadedSoundIds.clear();
            soundDurationsMs.clear();
            if (soundPool != null) {
                soundPool.release();
                soundPool = null;
            }
        }
    }

    public static void loadGlobalAssets() {
        synchronized (LOCK) {
            if (!initialize()) {
                return;
            }

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
            coinSoundId = 0;
            loseSoundId = 0;
            victorySoundId = 0;
            messageSoundId = 0;
            bumperSoundId = 0;
            loadedSoundIds.clear();
            soundDurationsMs.clear();
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
            final long now = SystemClock.elapsedRealtime();
            pruneFinishedStreamsLocked(now);
            final Long endsAt = activeStreams.get(channel);
            return endsAt != null && endsAt > now;
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
        for (int i = 0; i < activeStreams.size(); i++) {
            final int streamId = activeStreams.keyAt(i);
            if (soundPool != null) {
                soundPool.setVolume(streamId, sfx, sfx);
            }
        }
    }

    private static int loadSoundLocked(String assetPath) {
        if (soundPool == null) {
            return 0;
        }
        final Context context = getContext();
        if (context == null) {
            return 0;
        }

        try (AssetFileDescriptor afd = context.getAssets().openFd(assetPath)) {
            final int soundId = soundPool.load(afd, 1);
            if (soundId != 0) {
                soundDurationsMs.put(soundId, readDurationMsLocked(assetPath));
                waitForSoundLoadLocked(soundId);
            }
            return soundId;
        } catch (IOException e) {
            Log.i(TAG, "loadSound failed for " + assetPath, e);
            return 0;
        }
    }

    private static void waitForSoundLoadLocked(int soundId) {
        final long deadline = SystemClock.elapsedRealtime() + 2000L;
        while (SystemClock.elapsedRealtime() < deadline) {
            if (loadedSoundIds.get(soundId, false)) {
                return;
            }
            try {
                LOCK.wait(15L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return;
            }
        }
    }

    private static int readDurationMsLocked(String assetPath) {
        final Context context = getContext();
        if (context == null) {
            return 1000;
        }

        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        try (AssetFileDescriptor afd = context.getAssets().openFd(assetPath)) {
            retriever.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
            final String duration = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
            if (duration != null) {
                return Math.max(1, Integer.parseInt(duration));
            }
        } catch (Throwable t) {
            Log.i(TAG, "readDuration failed for " + assetPath, t);
        } finally {
            try {
                retriever.release();
            } catch (Throwable ignored) {
            }
        }
        return 1000;
    }

    private static MediaPlayer createPlayerLocked(String assetPath) {
        final Context context = getContext();
        if (context == null) {
            return null;
        }

        try (AssetFileDescriptor afd = context.getAssets().openFd(assetPath)) {
            final MediaPlayer player = new MediaPlayer();
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

    private static int playSoundLocked(int soundId) {
        if (soundPool == null || soundId == 0 || !loadedSoundIds.get(soundId, false)) {
            return -1;
        }

        pruneFinishedStreamsLocked(SystemClock.elapsedRealtime());
        final float volume = appliedSfxVolume();
        final int streamId = soundPool.play(soundId, volume, volume, 1, 0, 1.0f);
        if (streamId == 0) {
            return -1;
        }

        final long durationMs = Math.max(1, soundDurationsMs.get(soundId, 1000));
        activeStreams.put(streamId, SystemClock.elapsedRealtime() + durationMs);
        return streamId;
    }

    private static void haltAllChannelsLocked() {
        if (soundPool == null) {
            activeStreams.clear();
            return;
        }
        final ArrayList<Integer> streamIds = new ArrayList<>(activeStreams.size());
        for (int i = 0; i < activeStreams.size(); i++) {
            streamIds.add(activeStreams.keyAt(i));
        }
        for (Integer streamId : streamIds) {
            soundPool.stop(streamId);
        }
        activeStreams.clear();
    }

    private static void pruneFinishedStreamsLocked(long now) {
        for (int i = activeStreams.size() - 1; i >= 0; i--) {
            final Long endsAt = activeStreams.valueAt(i);
            if (endsAt == null || endsAt <= now) {
                activeStreams.removeAt(i);
            }
        }
    }
}
