Platformer Engine

Build locally:
- Windows PowerShell: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1`
- Windows PowerShell + run after build: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1 -Run`
- 32-bit Windows build: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1 -WindowsArch x86`
- Linux/macOS: `./build-local.sh`
- Linux/macOS + run after build: `./build-local.sh --run`
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

Level upload API (GitHub Pages):
- Static client lives in `pages/level-api/` and uploads to Firebase RTDB REST.
- Deployment workflow: `.github/workflows/level-api-pages.yml`.
- After Pages is enabled for the repo (GitHub Actions source), the app URL is:
  - `https://<your-user>.github.io/<your-repo>/`
- API descriptor JSON:
  - `https://<your-user>.github.io/<your-repo>/api.json`
- Upload format written by the app:
  - `PUT <level_server_url>/levels/<id>.json`
  - Body includes `data` (raw level text) plus metadata fields (`name`, `owner`, `level_id`, timestamps).

Custom levels:
- Main menu `Play` opens campaign levels
- Main menu `Editor` opens custom levels
- Desktop: put `.txt` or `.bnnlvl` files in `custom_levels/` or `assets/custom_levels/`
- Android/iOS assets-based: provide `assets/custom_levels/levels.json` with a `levels` array
- Realtime DB: set `level_server_url` in `assets/config.json` (and Android asset config) to your Firebase RTDB base URL.
  - For authenticated writes: `level_server_auth_token`
  - Account username for uploads: `level_server_account_username`
  - Supported remote list endpoints:
    - Firebase Realtime Database REST API is used (`.json` endpoints, `shallow=true` for ID listing).
    - `<level_server_url>/levels.json` (object keys are IDs; each level read from `/levels/<id>/data.json`)
    - `<level_server_url>/custom_levels/levels.json` with `{ "levels": ["level_a.txt", ...] }`
    - `<level_server_url>/custom_levels.json` with `{ "levels": ["https://.../file.txt", ...] }`
  - Remote user levels are downloaded and cached into the app save folder under `user_levels/`.
  - Upload IDs are now written as `username-levelname`.

Firebase Realtime Database rules:
- Use `firebase-realtime-database.rules.json` for RTDB rules.
- These rules require authenticated writes and enforce upload payload shape for `/levels/<username-levelname>`.
