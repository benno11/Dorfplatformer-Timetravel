# Native mod SDK — API 1.0

Full-edition Windows, Linux, macOS, and Android builds can load native mods.
Settings → Mods lists installed mods, API/platform errors, and loaded state.
Enable/disable choices are saved and take effect after a **game restart**.
Lite does not load mods. External native mods are not supported on iOS.

## Install and manage

Each mod is a folder under the game's user-data `mods` directory. The folder name
must exactly match `mod.json`'s `id`. Settings → Mods can open/copy the folder path
and scan for newly installed mods. Installed mods default to enabled; API/platform
validation runs before loading. Remove a mod by closing the game and deleting its
folder. Its settings remain in `mods/.config/<id>` for a later reinstall.

On Android, **Import Mod ZIP** opens the system file picker. The ZIP can contain
`mod.json` at its root or one enclosing mod folder. The importer validates paths,
limits extraction to 128 MB / 512 entries, and installs the folder atomically.
Updating an existing mod keeps a hidden `.backup-…` directory. Scan and restart
after importing. The native loader copies `.so` files to private application
storage before loading them; external/media storage is not executable. Include
any native dependencies next to the main library, with an appropriate `$ORIGIN`
runpath. Native loading errors are shown in the Mods menu. Android device policy
and ABI compatibility still apply. Android uninstall removes the private copies.

Mods are native code with the game's process permissions. Only install code you
trust; a crashing native mod can crash the game. This loader is not a sandbox.

## Exported example

[Download the ready-to-import Auto Hop ZIP](packages/example.auto_hop.zip?raw=1).
It includes Android, Windows, Linux, and macOS libraries, source, and checksums.
Linux runtime behavior was verified; the other platform binaries are cross-built.
On Android, import it through Settings → Mods → Import Mod ZIP and restart.

## Build the example

The Auto Hop example makes the player jump automatically when landing:

```sh
cmake -S sdk/example -B mod-build
cmake --build mod-build --config Release
```

Copy `sdk/example/mod.json` to `mods/example.auto_hop/mod.json`. Copy the compiled
`auto_hop` library into that folder's matching platform subdirectory, as named in
the manifest. Windows multi-config generators put the DLL in `Release/`.

For Android, use the Android NDK toolchain:

```sh
cmake -S sdk/example -B mod-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
cmake --build mod-android
```

Copy `auto_hop.so` into `example.auto_hop/android-arm64/` alongside `mod.json`,
ZIP the `example.auto_hop` folder, then import it in the game. Compile separate
binaries for each supported architecture; Android also supports `android-armv7`,
`android-x64`, and `android-x86`. Desktop keys are `windows-x64`, `windows-x86`,
`linux-x64`, `linux-arm64`, `macos-x64`, and `macos-arm64`.

## Manifest

See `example/mod.json`. `id`, `version`, `api.major`, and a native library for the
current platform are required. `name` defaults to the ID. Optional
`"dependencies": ["author.other_mod"]` loads those enabled mods first. Missing,
disabled, incompatible, or cyclic dependencies prevent the dependent mod loading.
Dependency versions are informational; dependency authors must coordinate their
own interfaces. Library paths must stay inside the mod folder.

## Stable interface and compatibility

Include `include/df_mod.h` and export `df_mod_load` using `DF_MOD_ENTRY` and
`DF_MOD_CALL`. The example includes a Windows `.def` file for an undecorated export
on both x86 and x64. C++ implementation code is allowed, but no STL objects,
exceptions, engine classes, or SDL structures may cross the C ABI.

The loader checks the **mod API version**, not the game version or build number.
A mod requesting API 1.0 continues to load on game minor/patch updates retaining
API major 1. A newer required API minor is rejected by an older loader; an API
major mismatch is always rejected. Different CPUs/operating systems require their
own native binaries. An unrelated change to game version metadata does not
invalidate a mod. Only rely on this API; hooking private engine symbols has no
compatibility guarantee.

Existing API 1.0 field offsets/signatures are permanent for API major 1. Additive
host functions go at the end, with `struct_size` checks for availability. Future
callbacks are optional; older callback prefixes remain accepted. New data
formats should get new API functions/structures rather than changing existing
state structures. Breaking changes require a new API major.

`df_mod_load` receives a host API pointer plus stable UTF-8 mod/config directory
paths. Keep those pointers only until `on_unload`. Return 1 on successful
initialization, 0 on failure (clean up allocations before failure). The loader initializes the callbacks
`struct_size` to its buffer capacity; never write beyond that capacity. All callbacks
and host calls run on the game thread. Do not retain pointers to per-tick player
or level structures. Stop/join any worker threads in `on_unload`.

- `on_level_start` runs on the first active gameplay tick after a load/restart.
- `on_update` runs after simulation updates, with seconds capped at 0.25. It does
  not run while paused, dying, tallying score, or suspended.
- `get_player`, `set_player`, and `get_level` work only in gameplay callbacks.
  Initialize each state structure's `struct_size`. Player position, velocity,
  facing, grounded state, and shield can be changed. Non-finite or excessive
  position/velocity values are rejected atomically.
- `on_unload` runs in reverse load order at shutdown, followed by library unload.
  Toggle changes are deferred until restart; loaded native code is not hot-swapped.

API 1.0 intentionally exposes gameplay state instead of private C++ pointers.
Additional supported hooks can be added under the compatibility rules above.

## Verification

`native_mods` in CTest builds and loads a real native fixture. It checks API
compatibility, callback prefixes, player access, dependencies/cycles, restart
preferences, path containment, and unload order. `edition_policy_0/1` verifies
that Lite cannot open the Mods menu or execute mod commands.

The Android archive extractor has a standalone Java regression test:

```sh
javac -d archive-tests Android/app/src/main/java/com/Benno111/dorfplatformertimetravel/NativeModArchive.java tests/NativeModArchiveTest.java
java -cp archive-tests NativeModArchiveTest
```
