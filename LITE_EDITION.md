# Lite edition

Use a separate build directory for each edition. Full edition is the default.

```sh
cmake -S . -B .build-lite -DPLATFORMER_LITE_EDITION=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .build-lite --parallel 4
```

Linux produces `platformer-lite`. The game window, Windows installer/product,
iOS bundle, and Android application name include **Lite**. Android uses the
`com.Benno111.dorfplatformertimetravel_Lite` application ID while keeping the Java namespace stable for JNI.
Windows Lite uses its own installer identity. Settings and saves are isolated
from the full edition; Lite never migrates full-edition progression.

For the Windows-hosted Android build script:

```powershell
./build-android.ps1 -LiteEdition -Configuration Release
```

This passes the flag to both native compilation and Gradle. Do not combine Lite
with `-SkipNativeBuild`, since existing native libraries may be full-edition.
For other Android hosts, build the native library using
`PLATFORMER_LITE_EDITION=ON bash comp/android-prod.sh`, sync it using
`bash comp/update-android-app.sh`, then run Gradle from `Android` with
`./gradlew -PliteEdition=true assembleRelease`. Supply the normal NDK and SDL
prerequisites described by the build scripts. Native and APK edition flags must
match. APKs and Apple/Windows installer outputs require their platform tools.

Lite restrictions:

- Lite mode is forced, including after loading settings. The editor, local and
  online user-level browsers, account actions, and HTTP asset/level requests are
  unavailable. Account credentials are not loaded into the active session.
- World Select offers worlds **1, 3, and 5**. World 7 is unavailable in World Select in every edition. Campaign progression keeps its
  existing level sequence; worlds are filtered from selection, not deleted from
  assets.
- The `hardmode_unlocked` progression flag stays true, while Hardmode menus,
  start/continue/practice commands, and Hardmode snapshot loading/saving are
  blocked. Campaign snapshots cannot bypass this restriction.
- Campaign progress saves on every successful level/time-period transition,
  including transitions within a world, and at the initial level start.
- Scoring is completely disabled: no score HUD, completion bonuses, score tally,
  debug score display, or persisted score. Full edition pays coin and time bonuses
  from a shared **1,200 points per second** budget, retaining unpaid bonuses.
  The full-edition tally advances at a constant rate and plays paced coin sounds while awarding points.
- Experiments and custom character features remain off regardless of saved
  preferences. Lite menu, command, and setting allowlists require explicit
  opt-in for future features. Shared gameplay mechanics remain available.
- The updater page does not install full-edition packages over Lite. Install
  Lite updates using Lite packages until a dedicated update channel exists.

The edition tests compile the policy and progression code twice, once per flag,
so full-edition behavior and Lite restrictions are both checked:

```sh
ctest --test-dir .build --output-on-failure -R '^edition_policy_'
```

## GitHub Actions

The Build Game workflow builds Full and Lite variants for Linux x64, Windows
x64/x86, and Android debug/release APKs. It passes the edition flag to native
compilation and Android Gradle together. Workflow artifacts include the edition
in their names; Lite release assets have separate names while existing full
release filenames are preserved. The iOS jobs retain their existing full-edition
builds.

Lite uses the supplied artwork in `assets/icons/lite.png` for its desktop window
icon, a multi-size `assets/icons/lite.ico` for Windows executable resources, and
`Android/app/src/main/res/drawable-nodpi/lite_icon.png` for Android and iOS
packaging. Square app-icon variants pad the original artwork rather than crop it.
The full edition retains its existing icons.
