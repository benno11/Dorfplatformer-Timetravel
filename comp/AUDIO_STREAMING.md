Audio streaming
===============

All audio uses file-backed streaming rather than fully decoded sound caches.

- SDL_mixer (Windows, Linux and iOS): sound registrations retain filenames only.
  Each playing effect or music track opens its own seekable SDL_IOStream and
  uses MIX_SetTrackIOStream for on-demand decoding. Concurrent plays never share
  a cursor. Stopping, replacing, finishing or destroying tracks releases their
  streams. Effects are bounded to 32 simultaneous tracks.
- Android: menu/level music uses MediaPlayer file-descriptor streaming. Effects
  now also use MediaPlayer with asynchronous preparation, one player per active
  effect, capped at eight. Completion/error callbacks release each player;
  pending preparation counts as active for victory/transition timing. Halting or
  unloading removes pending players so late callbacks cannot restart them.
- APK audio assets stay uncompressed so AssetManager can supply seekable file
  descriptors. Music looping, separate music/effect gain, and global mute remain
  controlled through AudioSystem.

The SDL audio_streaming CTest exercises real WAV/MP3 playback with SDL's dummy
output device, overlapping effects, looped music, independent volumes, EOF
cleanup and shutdown. Android playback still requires device validation.
