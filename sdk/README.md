# Native mod SDK — API 1.1

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

The API exposes gameplay state instead of private C++ pointers.
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

## API 1.1: game features and menus

The host now appends `get_run_state`, `set_run_state`, `get_tile`, and `set_tile`
to `DFHostAPI`. Check `api_minor >= 1` and `struct_size` before accessing them.
Use `DFRunState` to read/write coins, lives, and score during gameplay callbacks.
Values must be nonnegative, lives at most 999 and coins at most 1,000,000.
Tiles use zero-based coordinates and IDs 0–65535. Tile edits update gameplay
collision/attributes through the normal game tile setter. Invalid calls return
0 without changing the game. These functions return 0 outside gameplay callbacks.

Optional `on_menu_load(userdata, menu)` runs after a Slim menu is loaded/generated,
before its first frame. Mods run in dependency/load order. The `DFMenuAPI` can
read, replace, insert, and remove rows, and replace the title. `DFMenuItem` carries
fixed-size, null-terminated UTF-8 buffers. New insertions are limited to 512 rows.
Original alignment/icons are retained for existing rows. Use `DF_MENU_TEXT`,
`DF_MENU_BUTTON`, or `DF_MENU_SLIDER` and existing game command/argument strings.

For a mod-owned button, set `command` to `mod_action` and `argument` to a local
action name. The loader namespaces it with your mod ID and calls
`on_menu_action(userdata, menu_id, action)` only for that loaded mod. Update your
mod's own state in that callback; gameplay host access is unavailable in menus.
The menu reloads after the action, so its labels can reflect the new state.
Do not retain the menu API pointer or call its functions after `on_menu_load`.
See `example/auto_hop.c` for an optional Mods-menu toggle: it checks callback
capacity and continues to work on API 1.0 hosts without its optional menu hooks.
The previously exported API 1.0 ZIP remains compatible with the new loader.

## Icons, restart, and diagnostics

Add optional `"icon": "icon.png"` to `mod.json`. The path must stay inside the
mod folder, the file must be at most 2 MiB, and decoded dimensions at most
1024×1024. The menu preserves aspect ratio. Missing/invalid artwork uses the
standard mod icon and does not stop the native mod from loading.

**Settings → Mods → Restart Game** saves settings/current campaign area and
reinitializes the game, renderer, audio, networking and native mod loader. Mod
unload callbacks run before libraries unload; pending enable/disable choices
apply on restart. This works within the existing application on desktop and
Android, without spawning a second game process. Mods must clean up threads and
resources in `on_unload`.

**Settings → Logging** enables the game log live and persists the choice. The
in-game viewer shows recent messages; full output is in `logs/game.log` under
the game's user-data directory. The viewer can copy the file path. Logging is
off by default; `--enable-game-log` still enables it at startup. Logs rotate at
8 MiB to a single `.previous` file, and the viewer retains 200 messages (showing
the most recent 50).

Android also offers **Save Logcat to File**. It captures only this app's PID to
`logs/logcat.txt`, including Java and SDL/native output allowed by the device.
The choice is mirrored in Activity preferences to begin capture before native
startup on subsequent launches. Disable/destroy stops the child process; logs
rotate at 8 MiB. No `READ_LOGS` permission or access to other apps' logs is used.
Game logging and Android logcat capture are independently selectable, including
in Lite (native mods remain Full-only).
