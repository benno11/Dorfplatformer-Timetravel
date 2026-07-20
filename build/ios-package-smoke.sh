#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TMP_DIR="$(mktemp -d)"
SMOKE_SOURCE_ROOT="$TMP_DIR/source-root"
SMOKE_ASSETS_DIR="$SMOKE_SOURCE_ROOT/assets"

prepare_smoke_source() {
  python3 - "$ROOT_DIR" "$SMOKE_SOURCE_ROOT" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
source_root = Path(sys.argv[2])

def quoted_entries(path):
    text = path.read_text(encoding="utf-8")
    return re.findall(r'"([^"]+)"', text)

entries = quoted_entries(root / "cmake" / "IosRequiredBundleEntries.cmake")
project_files = quoted_entries(root / "cmake" / "IosBundledProjectFiles.cmake")

for rel in entries + project_files:
    path = source_root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(f"smoke fixture for {rel}\n".encode("utf-8"))

(source_root / "assets" / "levels" / "levels.json").write_text(
    '{\n  "levels": [\n    "level_001.bnnlvl"\n  ]\n}\n',
    encoding="utf-8",
)
(source_root / "assets" / "custom_levels" / "levels.json").write_text(
    '{\n  "levels": []\n}\n',
    encoding="utf-8",
)
(source_root / "assets" / "textures.json").write_text(
    '{\n'
    '  "textures": {\n'
    '    "blocks": "assets/Sheets/DF_Blocks-uhd.png",\n'
    '    "player": "assets/Sheets/DF_Player1-uhd.png"\n'
    '  },\n'
    '  "plists": {\n'
    '    "blocks": "assets/Sheets/DF_Blocks-uhd.plist",\n'
    '    "player": "assets/Sheets/DF_Player1-uhd.plist"\n'
    '  }\n'
    '}\n',
    encoding="utf-8",
)
PY
}

make_fake_macho_tools() {
  local tool_dir="$TMP_DIR/fake-tools"
  mkdir -p "$tool_dir"
  cat > "$tool_dir/lipo" <<'SH'
#!/usr/bin/env bash
if [[ "${1:-}" == "-archs" ]]; then
  echo "arm64"
  exit 0
fi
echo "fake lipo only supports -archs" >&2
exit 1
SH
  cat > "$tool_dir/vtool" <<'SH'
#!/usr/bin/env bash
if [[ "${1:-}" == "-show-build" ]]; then
  printf 'Load command 0\n      cmd LC_BUILD_VERSION\n platform IOS\n'
  exit 0
fi
echo "fake vtool only supports -show-build" >&2
exit 1
SH
  cat > "$tool_dir/codesign" <<'SH'
#!/usr/bin/env bash
if [[ "${1:-}" == "--verify" ]]; then
  echo "fake codesign verified $*" >&2
  exit 0
fi
echo "fake codesign only supports --verify" >&2
exit 1
SH
  chmod +x "$tool_dir/lipo" "$tool_dir/vtool" "$tool_dir/codesign"
  FAKE_LIPO_TOOL="$tool_dir/lipo"
  FAKE_VTOOL_TOOL="$tool_dir/vtool"
  FAKE_CODESIGN_TOOL="$tool_dir/codesign"
}

validate_ios_triplets() {
  for triplet in arm64-ios arm64-ios-simulator; do
    local triplet_file="$ROOT_DIR/cmake/vcpkg-triplets/${triplet}.cmake"
    if [[ ! -f "$triplet_file" ]]; then
      echo "[fail] missing iOS vcpkg triplet: $triplet_file" >&2
      exit 1
    fi
    if ! grep -q 'PLATFORMER_IOS_DEPLOYMENT_TARGET' "$triplet_file"; then
      echo "[fail] iOS triplet does not honor PLATFORMER_IOS_DEPLOYMENT_TARGET: $triplet_file" >&2
      exit 1
    fi
  done
  echo "[ok] iOS vcpkg triplets honor deployment target override"
}

validate_platform_macros() {
  local probe="$TMP_DIR/platform_probe.cpp"
  cat > "$probe" <<'CPP'
#include "src/Platform.h"
#ifndef EXPECT_MOBILE
#error EXPECT_MOBILE missing
#endif
#ifndef EXPECT_IOS
#error EXPECT_IOS missing
#endif
#ifndef EXPECT_TVOS
#error EXPECT_TVOS missing
#endif
static_assert(PLATFORMER_MOBILE == EXPECT_MOBILE, "mobile mismatch");
static_assert(PLATFORMER_IOS == EXPECT_IOS, "ios mismatch");
static_assert(PLATFORMER_TVOS == EXPECT_TVOS, "tvos mismatch");
int main() { return 0; }
CPP

  if ! command -v "${CXX:-c++}" >/dev/null 2>&1; then
    echo "[warn] C++ compiler not found; skipping platform macro probe" >&2
    return
  fi

  local common_args=(-std=c++17 -fsyntax-only -I "$ROOT_DIR" "$probe")
  "${CXX:-c++}" "${common_args[@]}" -DEXPECT_MOBILE=0 -DEXPECT_IOS=0 -DEXPECT_TVOS=0
  "${CXX:-c++}" "${common_args[@]}" -D__ANDROID__=1 -DEXPECT_MOBILE=1 -DEXPECT_IOS=0 -DEXPECT_TVOS=0
  "${CXX:-c++}" "${common_args[@]}" -DTARGET_OS_IPHONE=1 -DEXPECT_MOBILE=1 -DEXPECT_IOS=1 -DEXPECT_TVOS=0
  "${CXX:-c++}" "${common_args[@]}" -DTARGET_OS_IOS=1 -DEXPECT_MOBILE=1 -DEXPECT_IOS=1 -DEXPECT_TVOS=0
  "${CXX:-c++}" "${common_args[@]}" -DTARGET_OS_IPHONE=1 -DTARGET_OS_TV=1 -DEXPECT_MOBILE=0 -DEXPECT_IOS=0 -DEXPECT_TVOS=1
  "${CXX:-c++}" "${common_args[@]}" -DTARGET_OS_IPHONE=1 -DTARGET_OS_MACCATALYST=1 -DEXPECT_MOBILE=0 -DEXPECT_IOS=0 -DEXPECT_TVOS=0
  echo "[ok] platform macros classify iOS/mobile targets"
}

negative_ipsw_mode_rejects_simulator_sdk() {
  expect_failure "ipsw-mode-simulator-sdk" "ipsw mode must use IOS_SDK=iphoneos" \
    env \
      IOS_SDK=iphonesimulator \
      bash "$ROOT_DIR/build/ios.sh" ipsw
}

make_app() {
  local app="$1"
  local include_assets_car="$2"
  local include_portrait="$3"
  local minimum_os="${4:-15.0}"

  mkdir -p "$app/Base.lproj/LaunchScreen.storyboardc"
  printf '#!/bin/sh\n' > "$app/platformer"
  chmod +x "$app/platformer"
  if [[ "$include_assets_car" == "yes" ]]; then
    printf 'fake compiled asset catalog\n' > "$app/Assets.car"
  fi

  local extra_orientation=""
  if [[ "$include_portrait" == "yes" ]]; then
    extra_orientation='<string>UIInterfaceOrientationPortrait</string>'
  fi

  cat > "$app/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>platformer</string>
<key>CFBundleIdentifier</key><string>com.example.platformer</string>
<key>CFBundleIcons</key><dict><key>CFBundlePrimaryIcon</key><dict><key>CFBundleIconFiles</key><array><string>AppIcon</string></array><key>CFBundleIconName</key><string>AppIcon</string></dict></dict>
<key>CFBundleIcons~ipad</key><dict><key>CFBundlePrimaryIcon</key><dict><key>CFBundleIconFiles</key><array><string>AppIcon</string></array><key>CFBundleIconName</key><string>AppIcon</string></dict></dict>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.0.0</string>
<key>CFBundleVersion</key><string>1</string>
<key>LSRequiresIPhoneOS</key><true/>
<key>LSSupportsOpeningDocumentsInPlace</key><true/>
<key>MinimumOSVersion</key><string>${minimum_os}</string>
<key>NSAppTransportSecurity</key><dict><key>NSAllowsArbitraryLoads</key><true/></dict>
<key>UILaunchStoryboardName</key><string>LaunchScreen</string>
<key>UIFileSharingEnabled</key><true/>
<key>UIRequiresFullScreen</key><true/>
<key>UIApplicationSupportsIndirectInputEvents</key><true/>
<key>UIRequiredDeviceCapabilities</key><array><string>arm64</string></array>
<key>UISupportedInterfaceOrientations</key><array><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string>${extra_orientation}</array>
<key>UISupportedInterfaceOrientations~ipad</key><array><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
</dict></plist>
PLIST

  cp -R "$SMOKE_ASSETS_DIR" "$app/assets"
  cp "$SMOKE_SOURCE_ROOT/LICENSE" "$app/LICENSE"
  cp "$SMOKE_SOURCE_ROOT/README.md" "$app/README.md"
  cp "$SMOKE_SOURCE_ROOT/object_type_map.json" "$app/object_type_map.json"
  cp "$SMOKE_SOURCE_ROOT/level store.txt" "$app/level store.txt"
}

positive_smoke() {
  local app="$TMP_DIR/platformer.app"
  local out="$TMP_DIR/out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  cmake \
    -DPACKAGE_FILE="$out/platformer.ipsw" \
    -DAPP_NAME=platformer \
    -DEXPECTED_IOS_SDK=iphoneos \
    -DEXPECTED_IOS_ARCHS=arm64 \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/ValidateIosPackage.cmake"

  cmake \
    -DMANIFEST_FILE="$out/platformer-ios-package-manifest.json" \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DEXPECTED_IOS_SDK=iphoneos \
    -DEXPECTED_IOS_ARCHS=arm64 \
    -DEXPECTED_IOS_CODE_SIGNING_REQUIRED=OFF \
    -P "$ROOT_DIR/cmake/ValidateIosPackageManifest.cmake"

  find "$out" -maxdepth 1 -type f -printf '%f %s bytes\n' | sort
}

expect_failure() {
  local label="$1"
  local pattern="$2"
  shift 2
  local log="$TMP_DIR/${label}.log"

  set +e
  "$@" >"$log" 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -eq 0 ]]; then
    echo "[error] $label unexpectedly passed" >&2
    return 1
  fi
  if ! grep -q "$pattern" "$log"; then
    echo "[error] $label failed, but did not report: $pattern" >&2
    cat "$log" >&2
    return 1
  fi
  echo "[ok] $label rejected as expected"
}

negative_missing_assets_car() {
  local app="$TMP_DIR/missing-assets-car.app"
  local out="$TMP_DIR/missing-assets-car-out"
  mkdir -p "$out"
  make_app "$app" no no
  expect_failure "missing-assets-car" "missing compiled asset catalog Assets.car" \
    cmake \
      -DAPP_BUNDLE="$app" \
      -DOUTPUT_DIR="$out" \
      -DAPP_NAME=platformer \
      -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      -DIOS_SDK=iphoneos \
      -DIOS_ARCHS=arm64 \
      -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
      -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      -P "$ROOT_DIR/cmake/PackageIosApp.cmake"
}

negative_empty_assets_car() {
  local app="$TMP_DIR/empty-assets-car.app"
  local out="$TMP_DIR/empty-assets-car-out"
  mkdir -p "$out"
  make_app "$app" yes no
  : > "$app/Assets.car"
  expect_failure "empty-assets-car" "compiled asset catalog is empty" \
    cmake \
      -DAPP_BUNDLE="$app" \
      -DOUTPUT_DIR="$out" \
      -DAPP_NAME=platformer \
      -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      -DIOS_SDK=iphoneos \
      -DIOS_ARCHS=arm64 \
      -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
      -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      -P "$ROOT_DIR/cmake/PackageIosApp.cmake"
}

negative_extra_payload_app() {
  local app="$TMP_DIR/extra-payload/platformer.app"
  local extra_app="$TMP_DIR/extra-payload/Unexpected.app"
  local staging="$TMP_DIR/extra-payload-staging"
  local out="$TMP_DIR/extra-payload-out"
  mkdir -p "$out" "$staging/Payload"
  make_app "$app" yes no
  make_app "$extra_app" yes no
  cp -R "$app" "$staging/Payload/platformer.app"
  cp -R "$extra_app" "$staging/Payload/Unexpected.app"
  (cd "$staging" && cmake -E tar "cf" "$out/platformer.ipa" --format=zip "Payload")
  expect_failure "extra-payload-app" "Payload app bundle" \
    cmake \
      -DPACKAGE_FILE="$out/platformer.ipa" \
      -DAPP_NAME=platformer \
      -DEXPECTED_IOS_SDK=iphoneos \
      -DEXPECTED_IOS_ARCHS=arm64 \
      -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      -P "$ROOT_DIR/cmake/ValidateIosPackage.cmake"
}

negative_required_entry_decoy() {
  local app="$TMP_DIR/required-decoy/platformer.app"
  local staging="$TMP_DIR/required-decoy-staging"
  local out="$TMP_DIR/required-decoy-out"
  mkdir -p "$out" "$staging/Payload"
  make_app "$app" yes no
  rm -f "$app/assets/config.json"
  printf '{}\n' > "$app/assets/config.json.bak"
  cp -R "$app" "$staging/Payload/platformer.app"
  (cd "$staging" && cmake -E tar "cf" "$out/platformer.ipa" --format=zip "Payload")
  expect_failure "required-entry-decoy" "missing required entry: Payload/platformer.app/assets/config.json" \
    cmake \
      -DPACKAGE_FILE="$out/platformer.ipa" \
      -DAPP_NAME=platformer \
      -DEXPECTED_IOS_SDK=iphoneos \
      -DEXPECTED_IOS_ARCHS=arm64 \
      -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      -P "$ROOT_DIR/cmake/ValidateIosPackage.cmake"
}

negative_level_manifest_missing_entry() {
  local app="$TMP_DIR/missing-manifest-level/platformer.app"
  local staging="$TMP_DIR/missing-manifest-level-staging"
  local out="$TMP_DIR/missing-manifest-level-out"
  mkdir -p "$out" "$staging/Payload"
  make_app "$app" yes no
  cat > "$app/assets/levels/levels.json" <<'JSON'
{
  "levels": [
    "missing_level.bnnlvl"
  ]
}
JSON
  cp -R "$app" "$staging/Payload/platformer.app"
  (cd "$staging" && cmake -E tar "cf" "$out/platformer.ipa" --format=zip "Payload")
  expect_failure "missing-manifest-level" "references missing bundled level" \
    cmake \
      -DPACKAGE_FILE="$out/platformer.ipa" \
      -DAPP_NAME=platformer \
      -DEXPECTED_IOS_SDK=iphoneos \
      -DEXPECTED_IOS_ARCHS=arm64 \
      -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      -P "$ROOT_DIR/cmake/ValidateIosPackage.cmake"
}

negative_texture_manifest_missing_entry() {
  local app="$TMP_DIR/missing-manifest-texture/platformer.app"
  local staging="$TMP_DIR/missing-manifest-texture-staging"
  local out="$TMP_DIR/missing-manifest-texture-out"
  mkdir -p "$out" "$staging/Payload"
  make_app "$app" yes no
  cat > "$app/assets/textures.json" <<'JSON'
{
  "textures": {
    "blocks": "assets/Sheets/Missing_Blocks.png"
  },
  "plists": {
    "blocks": "assets/Sheets/DF_Blocks-uhd.plist"
  }
}
JSON
  cp -R "$app" "$staging/Payload/platformer.app"
  (cd "$staging" && cmake -E tar "cf" "$out/platformer.ipa" --format=zip "Payload")
  expect_failure "missing-manifest-texture" "references missing bundled asset" \
    cmake \
      -DPACKAGE_FILE="$out/platformer.ipa" \
      -DAPP_NAME=platformer \
      -DEXPECTED_IOS_SDK=iphoneos \
      -DEXPECTED_IOS_ARCHS=arm64 \
      -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      -P "$ROOT_DIR/cmake/ValidateIosPackage.cmake"
}

negative_portrait_orientation() {
  local app="$TMP_DIR/portrait.app"
  local out="$TMP_DIR/portrait-out"
  mkdir -p "$out"
  make_app "$app" yes yes
  expect_failure "portrait-orientation" "disallowed portrait orientation" \
    cmake \
      -DAPP_BUNDLE="$app" \
      -DOUTPUT_DIR="$out" \
      -DAPP_NAME=platformer \
      -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      -DIOS_SDK=iphoneos \
      -DIOS_ARCHS=arm64 \
      -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
      -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      -P "$ROOT_DIR/cmake/PackageIosApp.cmake"
}

negative_verify_wrong_sdk_override() {
  local app="$TMP_DIR/wrong-sdk/platformer.app"
  local out="$TMP_DIR/wrong-sdk-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  expect_failure "wrong-sdk-override" "does not match expected SDK iphonesimulator" \
    env \
      IOS_SDK=iphonesimulator \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

negative_verify_manifest_asset_count() {
  local app="$TMP_DIR/bad-manifest-count/platformer.app"
  local out="$TMP_DIR/bad-manifest-count-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  python3 - "$out/platformer-ios-package-manifest.json" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
match = re.search(r'("validated_source_assets":\s*)(\d+)', text)
if not match:
    raise SystemExit("validated_source_assets not found")
bad_count = max(0, int(match.group(2)) - 1)
path.write_text(text[:match.start(2)] + str(bad_count) + text[match.end(2):], encoding="utf-8")
PY

  expect_failure "bad-manifest-asset-count" "does not match expected validated_source_assets" \
    env \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

negative_verify_manifest_device_flag() {
  local app="$TMP_DIR/bad-device-flag/platformer.app"
  local out="$TMP_DIR/bad-device-flag-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  python3 - "$out/platformer-ios-package-manifest.json" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
text, count = re.subn(r'("device_artifact":\s*)true', r'\1false', text, count=1)
if count != 1:
    raise SystemExit("device_artifact true not found")
path.write_text(text, encoding="utf-8")
PY

  expect_failure "bad-device-artifact-flag" "does not match expected device artifact flag" \
    env \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

negative_verify_manifest_bundle_metadata() {
  local app="$TMP_DIR/bad-bundle-metadata/platformer.app"
  local out="$TMP_DIR/bad-bundle-metadata-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  python3 - "$out/platformer-ios-package-manifest.json" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
text, count = re.subn(r'("bundle_identifier":\s*")[^"]+(")', r'\1com.example.wrong\2', text, count=1)
if count != 1:
    raise SystemExit("bundle_identifier not found")
path.write_text(text, encoding="utf-8")
PY

  expect_failure "bad-bundle-metadata" "bundle_identifier does not match packaged Info.plist" \
    env \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

negative_verify_manifest_package_hash() {
  local app="$TMP_DIR/bad-package-hash/platformer.app"
  local out="$TMP_DIR/bad-package-hash-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  printf 'corrupt ipsw after manifest\n' >> "$out/platformer.ipsw"

  expect_failure "bad-package-hash" "wrong size for ipsw" \
    env \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

negative_verify_package_checksum_file() {
  local app="$TMP_DIR/bad-checksum/platformer.app"
  local out="$TMP_DIR/bad-checksum-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  printf '0000000000000000000000000000000000000000000000000000000000000000  platformer.ipsw\n' > "$out/platformer-ios-package-sha256.txt"

  expect_failure "bad-checksum-file" "checksum file" \
    env \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

negative_verify_expected_bundle_id() {
  local app="$TMP_DIR/wrong-expected-bundle/platformer.app"
  local out="$TMP_DIR/wrong-expected-bundle-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  expect_failure "wrong-expected-bundle-id" "does not match expected bundle identifier" \
    env \
      IOS_BUNDLE_ID="com.example.wrong" \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

negative_verify_expected_minimum_os() {
  local app="$TMP_DIR/wrong-expected-min-os/platformer.app"
  local out="$TMP_DIR/wrong-expected-min-os-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  expect_failure "wrong-expected-minimum-os" "does not match expected minimum OS version" \
    env \
      PLATFORMER_IOS_DEPLOYMENT_TARGET="16.0" \
      IOS_PACKAGE_DIR="$out" \
      IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
      IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
      IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
      IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
      bash "$ROOT_DIR/build/ios.sh" verify
}

verify_infers_minimum_os_manifest() {
  local app="$TMP_DIR/minimum-os-manifest/platformer.app"
  local out="$TMP_DIR/minimum-os-manifest-out"
  mkdir -p "$out"
  make_app "$app" yes no 16.0

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=OFF \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  IOS_PACKAGE_DIR="$out" \
  IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
  IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
  IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
  IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    bash "$ROOT_DIR/build/ios.sh" verify
  echo "[ok] verify inferred manifest minimum OS"
}

verify_infers_signed_manifest() {
  local app="$TMP_DIR/signed-manifest/platformer.app"
  local out="$TMP_DIR/signed-manifest-out"
  mkdir -p "$out"
  make_app "$app" yes no

  cmake \
    -DAPP_BUNDLE="$app" \
    -DOUTPUT_DIR="$out" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
    -DIOS_SDK=iphoneos \
    -DIOS_ARCHS=arm64 \
    -DIOS_REQUIRE_CODE_SIGNATURE=ON \
    -DIOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
    -DIOS_CODESIGN_TOOL="$FAKE_CODESIGN_TOOL" \
    -P "$ROOT_DIR/cmake/PackageIosApp.cmake"

  IOS_PACKAGE_DIR="$out" \
  IOS_SOURCE_ASSETS_DIR="$SMOKE_ASSETS_DIR" \
  IOS_SOURCE_ROOT_DIR="$SMOKE_SOURCE_ROOT" \
  IOS_LIPO_TOOL="$FAKE_LIPO_TOOL" \
  IOS_VTOOL_TOOL="$FAKE_VTOOL_TOOL" \
  IOS_CODESIGN_TOOL="$FAKE_CODESIGN_TOOL" \
    bash "$ROOT_DIR/build/ios.sh" verify
  echo "[ok] verify inferred manifest sdk, archs, and signing"
}

prepare_smoke_source
make_fake_macho_tools
validate_ios_triplets
validate_platform_macros
negative_ipsw_mode_rejects_simulator_sdk
positive_smoke
negative_missing_assets_car
negative_empty_assets_car
negative_extra_payload_app
negative_required_entry_decoy
negative_level_manifest_missing_entry
negative_texture_manifest_missing_entry
negative_portrait_orientation
negative_verify_wrong_sdk_override
negative_verify_manifest_asset_count
negative_verify_manifest_device_flag
negative_verify_manifest_bundle_metadata
negative_verify_manifest_package_hash
negative_verify_package_checksum_file
negative_verify_expected_bundle_id
negative_verify_expected_minimum_os
verify_infers_minimum_os_manifest
verify_infers_signed_manifest
echo "[ok] iOS package smoke passed"
