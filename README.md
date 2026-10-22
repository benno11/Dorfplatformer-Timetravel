Platformer Engine

Gameplay-only sprite sheets and frame tables prepare on a worker while the title
menu is open. Textures upload on the main thread when entering gameplay; if
preparation is still running, a loading screen keeps pumping window events.
Only the selected world's background is decoded on demand, also on a worker.
Native gameplay sound effects decode asynchronously; title music stays available
immediately. Pending loads join before shutdown, and low-power resume rebuilds
textures and animation pointers through the same loading path.

The **Character Creator [Experimental]** appears in Settings after enabling
**General > Experimental Features**. Choose skin, hair style and color, shirt,
pants, headwear, and accent colors, then turn **Use Custom Character** on. The
animated preview updates immediately and changes save automatically in client
settings. Turn the custom character off to use the original player; Reset also
clears the design. Disabling Experimental Features uses the original player
while keeping your design saved. Colors, textures, and visual build keep the
standard collision box; movement attributes change the enabled custom character.
**Customize Built-in Hair** opens controls for top thickness, fringe length,
back length, back width, and spike height. Select a value to cycle it; editing
selects Custom Parts and activates editable procedural parts. Hair Color and
Hair Texture apply to this hairstyle, including its spring motion. Use Custom
Hair Parts reapplies the saved shape; Reset Hair Shape restores its defaults.
The animated preview updates immediately, and the shape saves in client settings
and exported `.dfcharacter` files.
**Uncap Hair Length** removes the normal fringe and back length limits. These
values increase without wrapping and have Shorten buttons; their minimum remains
0 (hidden). Width stays at least 1. Longer hair can extend below the sprite;
rendering only processes visible rows. Turning the toggle off clamps lengths to
their normal limits (fringe 6, back 10). Reset Hair Shape also turns it off.
Off-screen culling includes the full uncapped hair length and vertical physics
motion, so visible hair stays drawn when the body moves above the viewport.
**Import Custom Hairstyle** accepts PNG, JPG, BMP, or WebP hair art and selects
the Imported Image hair style. Use a transparent 16x16 PNG: the head occupies columns
3–12 and rows 3–12, with room above and below for spikes and locks. Other sizes
are scaled to 16x16. Imported hair keeps its image colors and transparency,
mirrors with the character, and sways with hair physics. Importing activates
editable procedural parts. Cycle Hair Style to return to a built-in style, or
choose Remove Imported Hairstyle to delete the saved image. Custom hair is embedded
in settings and exported `.dfcharacter` files.
Custom parts have cosmetic spring physics: hair trails during acceleration,
headwear bounces on landing, and arms and legs swing from their joints. These
effects also appear in the creator preview and pause with gameplay.
Built-in hair uses its own head-anchored chain of physics joints. Inertia, air
resistance, gravity, and joint constraints let fringe and locks bend independently
along their length and settle after stops, jumps, and landings. Turning preserves
their world-space momentum. Short, Spiky, Long, and Custom Parts share this
simulation; imported image hair keeps its existing spring behavior. The simulation
uses fixed storage even for uncapped hair and follows Part Springiness.
Vertical elasticity lets hair stretch, compress, and bounce during jumps and
landings. Its joints collide with the face and torso, solid level tiles, platform
tops, and moving platforms. Contacts stop inward motion and add a small rebound;
hair can slide along surfaces. Extended tails stop at level surfaces instead of
passing through floors. Wrapped levels use the neighboring wrapped tiles. These
collisions are cosmetic and never push the player or activate gameplay objects.
**Placeholder Characters** offers six editable starters: Explorer, Runner,
Mechanic, Ranger, Royal, and Frost. Selecting one enables it and saves its look;
the creator preview shows its animated parts immediately.
**Character Attributes** controls visual build, part springiness, run speed,
acceleration, jump strength, and gravity. Movement stats range from 75% to 125%
of the configured defaults and return to normal when the custom character or
Experimental Features is disabled. Reset Attributes restores 100% values.
**Part Textures** applies Solid, Stripes, Checks, Dots, Denim, or Metal separately
to skin, hair, shirt, pants, headwear, and accents. Each part also has an image
import button (PNG, JPG, BMP, WebP). Images become 8x8 RGBA texture tiles stored
inside client settings, including transparency, so the source file can move.
Use Clear All Part Textures to remove patterns and imported tiles.

**Import Character** and **Export Character** in the creator use self-contained
`.dfcharacter` JSON files. These include attributes, textures, and any sprite
layers. Invalid imports leave the current character intact; canceled dialogs
make no changes. Export captures the character when the button is selected.
**Load Example Characters** includes Original and Companion from the supplied
Timetravel character pack. To import that ZIP's assets, extract it and select
`sprites.json`, then choose a character. Keep its PNGs beside the manifest.
Each character uses eleven 16x28 frames with separate base and bangs layers;
both layers share the animation frame and mirror together. Export embeds the
pixels, so the exported file needs no sidecar images. Original sprite art keeps
its authored colors and hair; part textures and movement attributes still apply.
**Switch to Editable Procedural Parts** returns to the parts-based appearance.

Press Down (S, Down Arrow, or the controller/touch Down control) on the ground
to slide forward in the facing direction. Sliding grants 50 frames of
invulnerability at 60 FPS (0.83 seconds at any frame rate) and opens boxes in
the way for their usual coins or shield. Release Down before starting another
slide.

Bumpers have a 0.5-second cooldown after a successful activation before their
bounce, animation, particles, and sound can trigger again. Each bumper cools
down independently; the timer freezes while paused and resets on level restart.

On every level start (including retries, time changes, and Continue), button
tiles reset to the versions authored in that level: 66 stays inactive and 67
stays activated. The current area's completion flag follows those starting
buttons, so a previous activation cannot force a fresh button into its alternate
state. Progress in other areas is retained.
Pressing any inactive button immediately sets the current area's activation
flag; other buttons in that level are alternative triggers, so pressing every
button is not required.
If a level starts with any button already activated (tile 67), its activation
flag starts on even when other buttons remain inactive.

The editor's **Level Config** button (F2, or the controller's left face button)
opens per-level settings for world (1–7), area (1–3), time (1–4), and **Bouncy
tile 12**. Zero/Auto preserves path-derived settings when editing older levels.
Apply changes the editor settings; saving the level writes them to disk.

New saves use **DFLVL4**, with world/area/time and `bouncy_tile_12` in the META
header. DFLVL2/3 and legacy levels remain readable; their bounce toggle defaults
to off. The level API format identifier is now 3.

With Bouncy tile 12 enabled, landing on tile 12 starts a one-fifth-block hop.
Normal gravity controls the flight. Jump cancels an active automatic bounce,
and a jump buffered within 150 ms of landing takes priority over bouncing.

Custom object IDs: **1001 = pickup**, **1002 = goal**. The editor and gameplay
use these IDs; existing level object IDs are not remapped.
**95 = ShellChraker**, available in the editor's object palette. Within 2.5
blocks it extends its arm toward the player, retracts, then briefly recovers.
Only the exposed arm damages the player; touching the body destroys the enemy
and knocks the player upward without changing horizontal momentum. The arm's
rear half is hidden behind the shell.
**97 = shield**, using `Shied_icon.png`. Place it on a coin box (tile 20) to
replace that box's five coins with a one-hit shield and visible force field.
The object alone does not grant a shield. The shield takes damage before coins,
knockback, or death; actual deaths clear it, while restarts and level transitions
preserve it. Shield state is included in saves and replays.
**96 = solid moving platform**, using `moving_block.png`. Standing on any one
starts all platforms together on a diagonal trip of 11 blocks left and 11 blocks
down over 6.7 seconds. They block the player on every side and carry the player
and any objects resting on top,
then stay at their destinations until the level restarts. Run saves
preserve its position and remaining travel time.

Finishing either world 7 ending unlocks **World Select** in Play. The time-4
good ending also unlocks **Hardmode**. Unlocks and the furthest hardmode level
persist across save slots in `progression.json` in the app save folder.
Hardmode runs through `assets/Hardmode levels/level_01.txt` through `level_20.txt`
in order. Any death resets the run to level 1 while retaining the best level
reached; finishing level 20 records hardmode completion. Hardmode has its own
Continue Run save, separate from the three campaign slots.
The Hardmode menu also offers **Practice Levels** for all 20 stages. Practice
retries the selected level after death, returns to the selector on completion,
and does not change unlocks, best progress, completion flags, or the run save.

Build locally:
- Windows PowerShell: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1`
- Windows PowerShell + run after build: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1 -Run`
- 32-bit Windows build: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1 -WindowsArch x86`
- Linux/macOS: `./build-local.sh`
- Linux/macOS + run after build: `./build-local.sh --run`

Custom boot loader:
- Startup now enters `CustomBoot::Run` before the SDL game runtime.
- The loader reads `assets/boot_config.json`, checks battery state, applies boot partition/options, resolves the startup partition, and verifies the startup executable plus runtime/init assets before launching the game.
- `--boot-select` or `--ctrl-b` selects the recovery partition. `--boot-partition=<id>` selects a configured partition directly. `--boot-option=safe`, `--boot-option=offline`, `--boot-option=diagnostic`, or `--boot-option=recovery` apply startup options.
- `trusted_components` may contain FNV-1a 64-bit hex digests for `startup_executable`, `kernel`, or `init_script`; when a digest is absent, the loader performs an existence check and logs the missing trust pin.
- iOS simulator (macOS + Xcode): `IOS_SDK=iphonesimulator ./build/ios.sh`
- iOS device/package (macOS + Xcode): `./build/ios.sh ipsw` or `IOS_SDK=iphoneos ./build/ios.sh`
  - Output: `dist/ios/platformer.ipa`, `dist/ios/platformer.ipsw`, `dist/ios/platformer-ios-package-manifest.json`, and `dist/ios/platformer-ios-package-sha256.txt`. The `.ipa` is the normal iOS app archive; the `.ipsw` is emitted for requested tooling compatibility, and the manifest records `ipsw_format: app_archive_compatibility_copy`.
  - Check a Mac before building with `IOS_SDK=iphoneos ./build/ios.sh doctor`.
  - `IOS_SDK` also accepts full Xcode SDK paths that contain `iPhoneOS` or `iPhoneSimulator`; the helper normalizes them for triplets, build folders, manifests, and verification. On macOS, token values like `iphoneos` are resolved through `xcrun --show-sdk-path` before CMake configures Xcode.
  - Set `IOS_PACKAGE_DIR=/path/to/output` to choose a different package output directory.
  - The manifest records the normalized SDK (`iphoneos` or `iphonesimulator`), architecture, bundle identifier, app version, minimum OS, whether it is a device artifact, whether code signing was required, and the validated counts for required bundle entries, source assets, and top-level project files; `./build/ios.sh verify` checks those values and infers SDK, architecture, and signing requirement from the manifest when the matching env vars are not set. On macOS, package validation also inspects the executable architecture/platform, lints `Info.plist`, and verifies code signatures when signing is required; pass `IOS_LIPO_TOOL`, `IOS_VTOOL_TOOL`, `IOS_PLUTIL_TOOL`, or `IOS_CODESIGN_TOOL` to the CMake validator to override tool discovery.
  - Those tool override environment variables are also forwarded into the `ios-package` target during `./build/ios.sh` builds.
  - Package validator smoke test without Xcode: `bash build/ios-package-smoke.sh`.
  - The package step validates the app bundle metadata plus every bundled source asset under `assets/`, so missing or truncated runtime data fails the iOS artifact build.
  - The package step also validates the top-level runtime files copied into the app bundle, such as `object_type_map.json` and `level store.txt`.
  - Xcode builds generate the iOS app icon asset catalog from the existing Android icon and package validation requires the compiled `Assets.car`.
  - iOS is packaged landscape-only to match the Android gameplay orientation.
  - iOS saves, custom levels, and replays use the app Documents folder and the package enables Files-app document sharing.
  - The helper installs iOS vcpkg dependencies by default when `vcpkg/` or `VCPKG_ROOT` is available, and the iOS triplets honor `PLATFORMER_IOS_DEPLOYMENT_TARGET` so dependencies and the app use the same minimum OS. Set `VCPKG_INSTALL_DEPS=OFF` to skip that step.
  - Audio parity uses SDL3_mixer by default. Set `IOS_REQUIRE_SDL3_MIXER=OFF` only for dependency smoke builds without music/SFX.
  - Online/account parity requires libcurl by default. Set `IOS_REQUIRE_CURL=OFF` only for dependency smoke builds without online features.
  - Unsigned builds are the default. For a signed device archive, set `IOS_CODE_SIGNING_ALLOWED=YES`, `IOS_CODE_SIGNING_REQUIRED=YES`, `IOS_BUNDLE_ID`, `IOS_DEVELOPMENT_TEAM`, `IOS_CODE_SIGN_STYLE`, `IOS_CODE_SIGN_IDENTITY`, and optionally `IOS_PROVISIONING_PROFILE_SPECIFIER`. Boolean iOS helper env vars accept `ON/OFF`, `YES/NO`, `true/false`, or `1/0`.
  - GitHub Actions uploads validated `platformer-ios-simulator` and `platformer-ios-device` artifacts on every macOS iOS build. Release downloads are renamed to `platformer-ios-device.ipsw`/`.ipa` and get matching release-name checksum files.
  - GitHub Actions enables signed `iphoneos` artifacts only when both `IOS_DEVELOPMENT_TEAM` and `IOS_CERTIFICATE_P12_BASE64` are configured as repository secrets; optional companion secrets are `IOS_BUNDLE_ID`, `IOS_CODE_SIGN_IDENTITY`, `IOS_CODE_SIGN_STYLE`, `IOS_PROVISIONING_PROFILE_SPECIFIER`, `IOS_CERTIFICATE_PASSWORD`, and `IOS_PROVISIONING_PROFILE_BASE64`.
- iOS device (macOS + Xcode): `cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_TOOLCHAIN_FILE="$PWD/.ci/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=arm64-ios-device-release -DVCPKG_OVERLAY_TRIPLETS="$PWD/cmake/vcpkg-triplets" -DPLATFORMER_REQUIRE_SDL3_MIXER=OFF`

Windows releases now ship both x64 and x86 installers, and the embedded app manifest advertises compatibility from Windows Vista through Windows 11.
The 32-bit Windows build also uses the static MSVC runtime to reduce dependency on installed VC++ redistributables.
The iOS release workflow now always packages a SideStore-friendly `.ipa` with the bundled `assets/` directory included. SideStore installs `.ipa` app archives, not `.ipsw` firmware images.

Manual CMake (separate folder):
- Configure: `cmake -S . -B .build`
- Build: `cmake --build .build --config Release`

Microsoft Store update helper (Windows):
- Run: `powershell -ExecutionPolicy Bypass -File build/force-msstore-update.ps1`
- Note: this script uses `winget` to refresh sources and apply available Microsoft Store package updates.

The in-game Updater tab supports Windows, Linux, macOS, Android, and iOS.
Set `update_manifest_url` in `assets/config.json` to your hosted JSON manifest;
the existing `windows_update_manifest_url` remains a fallback. See
`installer/update-manifest.example.json` for platform and architecture entries.
Checks run in the background and compare `version_id` (or `version` when absent).
Windows uses the bundled installer helper. Other platforms open their matching
download or installation page; finish installation there and restart the game.
For iOS, point the URL at your App Store or sideloading instructions. Missing
platform packages produce an error instead of downloading another OS's installer.
Desktop and iOS update checks require libcurl; Android also supports its Java
network fallback. Legacy Windows manifests with `installer_url` or flat
`installer_urls.x64` / `installer_urls.x86` entries remain supported.

Features included:
- sdl3 C++ 2D platformer
- sdl3 version required: 3.4.0 or newer
- Background + solid tile grids
- Tilesheet-based BG rendering
- Object system (ID-based, JSON-defined)
- Level format: .bnnlvl
- Built-in levels in assets/levels
- Level select menu (campaign/custom)
- Mobile touch controls + editor UI

Custom game server:
- Run `python server/game_server.py`, then open http://127.0.0.1:8080.
- Accounts, sessions and shared levels are stored locally in SQLite.
- Desktop and Android use this server for login, upload and download.
- See [server/README.md](server/README.md) for deployment, configuration and tests.

In-game networking:
- HTTP requests run on background workers on desktop, iOS, and Android, including the Android Java fallback.
- Actions that need a response show a loading indicator and keep processing window events. Gameplay waits for required data; independent telemetry requests run without a loading screen.
- Esc cancels pending curl transfers. Android Java calls finish within their existing timeout before a cancelled result is discarded.
- Workers finish accepted requests before SDL and curl shut down.
- `ReadTextFileAsync` returns a future for callers that can poll results in their normal update loop. Worker tasks must own their inputs; update game state and render on the main thread.

Custom levels:
- Main menu `Play` opens campaign levels; `Editor` opens custom levels.
- Desktop local files: `custom_levels/` or `assets/custom_levels/`.
- Mobile assets: `assets/custom_levels/levels.json`.
- Remote levels use `/levels.json?shallow=true` and `/levels/<id>/data.json`.
- Uploads use bearer authentication and IDs in the form `username-levelname`.
- The server enforces level ownership independently of client-supplied metadata.
