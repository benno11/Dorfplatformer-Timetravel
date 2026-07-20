#!/usr/bin/env bash
set -euo pipefail

# CMake/Xcode iOS build helper. Defaults to simulator so it can be used for
# quick smoke builds; set IOS_SDK=iphoneos for device/archive builds.
# The ios-package target emits dist/ios/platformer.ipa and the requested
# dist/ios/platformer.ipsw compatibility artifact.
MODE="${1:-build}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
IOS_SDK_WAS_SET=0
if [[ -n "${IOS_SDK+x}" ]]; then
  IOS_SDK_WAS_SET=1
fi
IOS_ARCHS_WAS_SET=0
if [[ -n "${IOS_ARCHS+x}" ]]; then
  IOS_ARCHS_WAS_SET=1
fi
IOS_CODE_SIGNING_REQUIRED_WAS_SET=0
if [[ -n "${IOS_CODE_SIGNING_REQUIRED+x}" ]]; then
  IOS_CODE_SIGNING_REQUIRED_WAS_SET=1
fi
DEPLOYMENT_TARGET_WAS_SET=0
if [[ -n "${PLATFORMER_IOS_DEPLOYMENT_TARGET+x}" ]]; then
  DEPLOYMENT_TARGET_WAS_SET=1
fi
if [[ "$MODE" == "device" || "$MODE" == "ipsw" ]]; then
  IOS_SDK="${IOS_SDK:-iphoneos}"
fi
IOS_SDK_SYSROOT="${IOS_SDK:-iphonesimulator}"
IOS_SDK_LOWER="$(printf '%s' "$IOS_SDK_SYSROOT" | tr '[:upper:]' '[:lower:]')"
case "$IOS_SDK_LOWER" in
  *iphonesimulator*)
    IOS_SDK="iphonesimulator"
    ;;
  *iphoneos*)
    IOS_SDK="iphoneos"
    ;;
  *)
    echo "[ERROR] IOS_SDK must be iphoneos, iphonesimulator, or a matching Xcode SDK path (got: $IOS_SDK_SYSROOT)" >&2
    exit 1
    ;;
esac
if [[ "$MODE" == "device" || "$MODE" == "ipsw" ]]; then
  if [[ "$IOS_SDK" != "iphoneos" ]]; then
    echo "[ERROR] $MODE mode must use IOS_SDK=iphoneos for a device IPSW artifact (got: $IOS_SDK_SYSROOT)" >&2
    exit 1
  fi
fi
CONFIG="${CONFIG:-Release}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/.build/ios-$IOS_SDK}"
DEPLOYMENT_TARGET="${PLATFORMER_IOS_DEPLOYMENT_TARGET:-15.0}"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT_DIR/vcpkg}"
VCPKG_TOOLCHAIN_FILE="${VCPKG_TOOLCHAIN_FILE:-$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake}"
VCPKG_OVERLAY_TRIPLETS_DIR="${VCPKG_OVERLAY_TRIPLETS_DIR:-$ROOT_DIR/cmake/vcpkg-triplets}"
VCPKG_INSTALL_DEPS="${VCPKG_INSTALL_DEPS:-ON}"
IOS_REQUIRE_SDL3_MIXER="${IOS_REQUIRE_SDL3_MIXER:-ON}"
IOS_REQUIRE_CURL="${IOS_REQUIRE_CURL:-ON}"
IOS_BUNDLE_ID="${IOS_BUNDLE_ID:-}"
IOS_CODE_SIGNING_ALLOWED="${IOS_CODE_SIGNING_ALLOWED:-NO}"
IOS_CODE_SIGNING_REQUIRED="${IOS_CODE_SIGNING_REQUIRED:-NO}"
IOS_CODE_SIGN_IDENTITY="${IOS_CODE_SIGN_IDENTITY:-}"
IOS_CODE_SIGN_STYLE="${IOS_CODE_SIGN_STYLE:-}"
IOS_DEVELOPMENT_TEAM="${IOS_DEVELOPMENT_TEAM:-}"
IOS_PROVISIONING_PROFILE_SPECIFIER="${IOS_PROVISIONING_PROFILE_SPECIFIER:-}"
IOS_PACKAGE_DIR="${IOS_PACKAGE_DIR:-$ROOT_DIR/dist/ios}"
IOS_SOURCE_ASSETS_DIR="${IOS_SOURCE_ASSETS_DIR:-$ROOT_DIR/assets}"
IOS_SOURCE_ROOT_DIR="${IOS_SOURCE_ROOT_DIR:-$ROOT_DIR}"
IOS_LIPO_TOOL="${IOS_LIPO_TOOL:-}"
IOS_VTOOL_TOOL="${IOS_VTOOL_TOOL:-}"
IOS_PLUTIL_TOOL="${IOS_PLUTIL_TOOL:-}"
IOS_CODESIGN_TOOL="${IOS_CODESIGN_TOOL:-}"

normalize_on_off() {
  local name="$1"
  local value="$2"
  local lower
  lower="$(printf '%s' "$value" | tr '[:upper:]' '[:lower:]')"
  case "$lower" in
    1|true|yes|on)
      printf -v "$name" '%s' "ON"
      ;;
    0|false|no|off|"")
      printf -v "$name" '%s' "OFF"
      ;;
    *)
      echo "[ERROR] $name must be a boolean value (ON/OFF, YES/NO, true/false, 1/0); got: $value" >&2
      exit 1
      ;;
  esac
}

normalize_yes_no() {
  local name="$1"
  local value="$2"
  local lower
  lower="$(printf '%s' "$value" | tr '[:upper:]' '[:lower:]')"
  case "$lower" in
    1|true|yes|on)
      printf -v "$name" '%s' "YES"
      ;;
    0|false|no|off|"")
      printf -v "$name" '%s' "NO"
      ;;
    *)
      echo "[ERROR] $name must be a boolean value (YES/NO, ON/OFF, true/false, 1/0); got: $value" >&2
      exit 1
      ;;
  esac
}

normalize_on_off VCPKG_INSTALL_DEPS "$VCPKG_INSTALL_DEPS"
normalize_on_off IOS_REQUIRE_SDL3_MIXER "$IOS_REQUIRE_SDL3_MIXER"
normalize_on_off IOS_REQUIRE_CURL "$IOS_REQUIRE_CURL"
normalize_yes_no IOS_CODE_SIGNING_ALLOWED "$IOS_CODE_SIGNING_ALLOWED"
normalize_yes_no IOS_CODE_SIGNING_REQUIRED "$IOS_CODE_SIGNING_REQUIRED"

case "$IOS_SDK" in
  iphoneos)
    IOS_ARCHS="${IOS_ARCHS:-arm64}"
    VCPKG_TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-ios}"
    ;;
  iphonesimulator)
    IOS_ARCHS="${IOS_ARCHS:-arm64}"
    VCPKG_TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-ios-simulator}"
    ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
  echo "[ERROR] cmake is required for iOS builds." >&2
  exit 1
fi

require_xcode() {
  if ! command -v xcodebuild >/dev/null 2>&1; then
    echo "[ERROR] Xcode command line tools are required for iOS builds." >&2
    exit 1
  fi
}

resolve_xcode_sdk_path() {
  local sdk_token="$1"
  local configured_sysroot="$2"
  if [[ "$configured_sysroot" != "$sdk_token" ]]; then
    printf '%s\n' "$configured_sysroot"
    return 0
  fi
  if ! command -v xcrun >/dev/null 2>&1; then
    printf '%s\n' "$configured_sysroot"
    return 0
  fi
  local resolved
  if resolved="$(xcrun --sdk "$sdk_token" --show-sdk-path 2>/dev/null)" && [[ -n "$resolved" ]]; then
    printf '%s\n' "$resolved"
    return 0
  fi
  printf '%s\n' "$configured_sysroot"
}

tool_status() {
  local label="$1"
  local configured="$2"
  local fallback="$3"
  if [[ -n "$configured" ]]; then
    if [[ -x "$configured" ]]; then
      echo "[OK] $label: $configured"
      return 0
    fi
    echo "[WARN] $label override is not executable: $configured" >&2
    return 1
  fi
  if command -v "$fallback" >/dev/null 2>&1; then
    echo "[OK] $label: $(command -v "$fallback")"
    return 0
  fi
  echo "[WARN] $label not found on PATH" >&2
  return 1
}

doctor() {
  local rc=0
  echo "iOS build doctor"
  echo "  sdk: $IOS_SDK"
  echo "  sysroot: $IOS_SDK_SYSROOT"
  if command -v xcrun >/dev/null 2>&1; then
    local xcode_sdk_path
    xcode_sdk_path="$(resolve_xcode_sdk_path "$IOS_SDK" "$IOS_SDK_SYSROOT")"
    echo "  xcode sdk path: $xcode_sdk_path"
  else
    echo "  xcode sdk path: <xcrun not found>"
  fi
  echo "  archs: $IOS_ARCHS"
  echo "  triplet: $VCPKG_TRIPLET"
  echo "  build dir: $BUILD_DIR"
  echo "  package dir: $IOS_PACKAGE_DIR"
  echo "  deployment target: $DEPLOYMENT_TARGET"
  echo "  require mixer: $IOS_REQUIRE_SDL3_MIXER"
  echo "  require curl: $IOS_REQUIRE_CURL"
  echo "  code signing required: $IOS_CODE_SIGNING_REQUIRED"

  command -v cmake >/dev/null 2>&1 || { echo "[ERROR] cmake is required." >&2; rc=1; }
  command -v xcodebuild >/dev/null 2>&1 || { echo "[ERROR] xcodebuild is required for configure/build modes." >&2; rc=1; }
  [[ -d "$IOS_SOURCE_ASSETS_DIR" ]] || { echo "[ERROR] IOS_SOURCE_ASSETS_DIR does not exist: $IOS_SOURCE_ASSETS_DIR" >&2; rc=1; }
  [[ -d "$IOS_SOURCE_ROOT_DIR" ]] || { echo "[ERROR] IOS_SOURCE_ROOT_DIR does not exist: $IOS_SOURCE_ROOT_DIR" >&2; rc=1; }
  [[ -f "$ROOT_DIR/cmake/vcpkg-triplets/$VCPKG_TRIPLET.cmake" ]] || {
    echo "[ERROR] iOS vcpkg triplet is missing: $ROOT_DIR/cmake/vcpkg-triplets/$VCPKG_TRIPLET.cmake" >&2
    rc=1
  }
  if [[ "$VCPKG_INSTALL_DEPS" == "ON" && ! -f "$VCPKG_TOOLCHAIN_FILE" ]]; then
    echo "[WARN] vcpkg install is enabled but toolchain is missing: $VCPKG_TOOLCHAIN_FILE" >&2
  fi

  tool_status lipo "$IOS_LIPO_TOOL" lipo || true
  tool_status vtool "$IOS_VTOOL_TOOL" vtool || true
  tool_status plutil "$IOS_PLUTIL_TOOL" plutil || true
  if [[ "$IOS_CODE_SIGNING_REQUIRED" == "YES" ]]; then
    tool_status codesign "$IOS_CODESIGN_TOOL" codesign || rc=1
  else
    tool_status codesign "$IOS_CODESIGN_TOOL" codesign || true
  fi

  return "$rc"
}

install_deps() {
  if [[ "$VCPKG_INSTALL_DEPS" != "ON" ]]; then
    return
  fi
  if [[ ! -f "$VCPKG_TOOLCHAIN_FILE" ]]; then
    echo "[WARN] vcpkg toolchain not found at $VCPKG_TOOLCHAIN_FILE; skipping dependency install." >&2
    return
  fi

  local vcpkg_exe="$VCPKG_ROOT/vcpkg"
  if [[ ! -x "$vcpkg_exe" ]]; then
    if [[ -x "$VCPKG_ROOT/bootstrap-vcpkg.sh" ]]; then
      "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
    else
      echo "[WARN] vcpkg executable not found and bootstrap-vcpkg.sh is missing; skipping dependency install." >&2
      return
    fi
  fi
  if [[ ! -x "$vcpkg_exe" ]]; then
    echo "[WARN] vcpkg executable still unavailable at $vcpkg_exe; skipping dependency install." >&2
    return
  fi

  local install_args=(
    install
    sdl3
    sdl3-image
    sdl3-ttf
    sdl3-mixer
    curl
    nlohmann-json
    --triplet "$VCPKG_TRIPLET"
    --vcpkg-root "$VCPKG_ROOT"
  )
  if [[ -d "$VCPKG_OVERLAY_TRIPLETS_DIR" ]]; then
    install_args+=(--overlay-triplets "$VCPKG_OVERLAY_TRIPLETS_DIR")
  fi
  PLATFORMER_IOS_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" "$vcpkg_exe" "${install_args[@]}"
}

configure() {
  require_xcode
  install_deps
  local cmake_sysroot
  cmake_sysroot="$(resolve_xcode_sdk_path "$IOS_SDK" "$IOS_SDK_SYSROOT")"
  local cmake_args=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    -G Xcode
    -DCMAKE_SYSTEM_NAME=iOS
    -DCMAKE_OSX_SYSROOT="$cmake_sysroot"
    -DCMAKE_OSX_ARCHITECTURES="$IOS_ARCHS"
    -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO
    -DPLATFORMER_IOS_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET"
    -DPLATFORMER_IOS_PACKAGE_DIR="$IOS_PACKAGE_DIR"
    -DPLATFORMER_REQUIRE_SDL3_MIXER="$IOS_REQUIRE_SDL3_MIXER"
    -DPLATFORMER_REQUIRE_CURL="$IOS_REQUIRE_CURL"
    -DPLATFORMER_IOS_LIPO_TOOL="$IOS_LIPO_TOOL"
    -DPLATFORMER_IOS_VTOOL_TOOL="$IOS_VTOOL_TOOL"
    -DPLATFORMER_IOS_PLUTIL_TOOL="$IOS_PLUTIL_TOOL"
    -DPLATFORMER_IOS_CODESIGN_TOOL="$IOS_CODESIGN_TOOL"
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET"
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED="$IOS_CODE_SIGNING_ALLOWED"
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED="$IOS_CODE_SIGNING_REQUIRED"
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="$IOS_CODE_SIGN_IDENTITY"
  )
  if [[ -n "$IOS_BUNDLE_ID" ]]; then
    cmake_args+=(-DPLATFORMER_IOS_BUNDLE_ID="$IOS_BUNDLE_ID")
  fi
  if [[ -n "$IOS_CODE_SIGN_STYLE" ]]; then
    cmake_args+=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE="$IOS_CODE_SIGN_STYLE")
  fi
  if [[ -n "$IOS_DEVELOPMENT_TEAM" ]]; then
    cmake_args+=(-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$IOS_DEVELOPMENT_TEAM")
  fi
  if [[ -n "$IOS_PROVISIONING_PROFILE_SPECIFIER" ]]; then
    cmake_args+=(-DCMAKE_XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER="$IOS_PROVISIONING_PROFILE_SPECIFIER")
  fi
  if [[ -f "$VCPKG_TOOLCHAIN_FILE" ]]; then
    cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN_FILE")
  fi
  if [[ -d "$VCPKG_OVERLAY_TRIPLETS_DIR" ]]; then
    cmake_args+=(-DVCPKG_OVERLAY_TRIPLETS="$VCPKG_OVERLAY_TRIPLETS_DIR")
  fi
  cmake "${cmake_args[@]}"
}

manifest_string_value() {
  local key="$1"
  local manifest="$IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"
  if [[ ! -f "$manifest" ]]; then
    return
  fi
  sed -n -E "s/.*\"${key}\"[[:space:]]*:[[:space:]]*\"([^\"]+)\".*/\\1/p" "$manifest" | head -n 1
}

manifest_number_value() {
  local key="$1"
  local manifest="$IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"
  if [[ ! -f "$manifest" ]]; then
    return
  fi
  sed -n -E "s/.*\"${key}\"[[:space:]]*:[[:space:]]*([0-9]+).*/\\1/p" "$manifest" | head -n 1
}

manifest_package_number_value() {
  local package_key="$1"
  local field_key="$2"
  local manifest="$IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"
  if [[ ! -f "$manifest" ]]; then
    return
  fi
  tr '\n' ' ' < "$manifest" \
    | sed -n -E "s/.*\"${package_key}\"[[:space:]]*:[[:space:]]*\\{[^}]*\"${field_key}\"[[:space:]]*:[[:space:]]*([0-9]+).*/\\1/p" \
    | head -n 1
}

infer_verify_manifest_metadata() {
  local manifest="$IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"
  if [[ ! -f "$manifest" ]]; then
    return
  fi

  if [[ "$IOS_SDK_WAS_SET" -eq 0 ]]; then
    local manifest_sdk
    manifest_sdk="$(manifest_string_value "sdk")"
    if [[ -n "$manifest_sdk" ]]; then
      IOS_SDK="$manifest_sdk"
    fi
  fi
  if [[ "$IOS_ARCHS_WAS_SET" -eq 0 ]]; then
    local manifest_archs
    manifest_archs="$(manifest_string_value "archs")"
    if [[ -n "$manifest_archs" ]]; then
      IOS_ARCHS="$manifest_archs"
    fi
  fi
  if [[ "$DEPLOYMENT_TARGET_WAS_SET" -eq 0 ]]; then
    local manifest_minimum_os
    manifest_minimum_os="$(manifest_string_value "minimum_os_version")"
    if [[ -n "$manifest_minimum_os" ]]; then
      DEPLOYMENT_TARGET="$manifest_minimum_os"
    fi
  fi
  if [[ "$IOS_CODE_SIGNING_REQUIRED_WAS_SET" -eq 1 ]]; then
    return
  fi
  if grep -q '"code_signing_required"[[:space:]]*:[[:space:]]*true' "$manifest"; then
    IOS_CODE_SIGNING_REQUIRED="YES"
  elif grep -q '"code_signing_required"[[:space:]]*:[[:space:]]*false' "$manifest"; then
    IOS_CODE_SIGNING_REQUIRED="NO"
  fi
}

verify_package() {
  infer_verify_manifest_metadata
  cmake \
    -DPACKAGE_FILE="$IOS_PACKAGE_DIR/platformer.ipa" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$IOS_SOURCE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$IOS_SOURCE_ROOT_DIR" \
    -DEXPECTED_IOS_SDK="$IOS_SDK" \
    -DEXPECTED_IOS_ARCHS="$IOS_ARCHS" \
    -DIOS_LIPO_TOOL="$IOS_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$IOS_VTOOL_TOOL" \
    -DIOS_PLUTIL_TOOL="$IOS_PLUTIL_TOOL" \
    -DIOS_REQUIRE_CODE_SIGNATURE="$IOS_CODE_SIGNING_REQUIRED" \
    -DIOS_CODESIGN_TOOL="$IOS_CODESIGN_TOOL" \
    -P "$ROOT_DIR/cmake/ValidateIosPackage.cmake"
  cmake \
    -DPACKAGE_FILE="$IOS_PACKAGE_DIR/platformer.ipsw" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$IOS_SOURCE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$IOS_SOURCE_ROOT_DIR" \
    -DEXPECTED_IOS_SDK="$IOS_SDK" \
    -DEXPECTED_IOS_ARCHS="$IOS_ARCHS" \
    -DIOS_LIPO_TOOL="$IOS_LIPO_TOOL" \
    -DIOS_VTOOL_TOOL="$IOS_VTOOL_TOOL" \
    -DIOS_PLUTIL_TOOL="$IOS_PLUTIL_TOOL" \
    -DIOS_REQUIRE_CODE_SIGNATURE="$IOS_CODE_SIGNING_REQUIRED" \
    -DIOS_CODESIGN_TOOL="$IOS_CODESIGN_TOOL" \
    -P "$ROOT_DIR/cmake/ValidateIosPackage.cmake"
  cmake \
    -DMANIFEST_FILE="$IOS_PACKAGE_DIR/platformer-ios-package-manifest.json" \
    -DAPP_NAME=platformer \
    -DSOURCE_ASSETS_DIR="$IOS_SOURCE_ASSETS_DIR" \
    -DSOURCE_ROOT_DIR="$IOS_SOURCE_ROOT_DIR" \
    -DEXPECTED_IOS_SDK="$IOS_SDK" \
    -DEXPECTED_IOS_ARCHS="$IOS_ARCHS" \
    -DEXPECTED_IOS_CODE_SIGNING_REQUIRED="$IOS_CODE_SIGNING_REQUIRED" \
    -DEXPECTED_IOS_BUNDLE_IDENTIFIER="$IOS_BUNDLE_ID" \
    -DEXPECTED_IOS_MINIMUM_OS_VERSION="$DEPLOYMENT_TARGET" \
    -P "$ROOT_DIR/cmake/ValidateIosPackageManifest.cmake"
  local manifest_sdk manifest_archs manifest_device manifest_assets manifest_required manifest_project_files ipa_bytes ipsw_bytes manifest_bundle_id manifest_bundle_version
  manifest_sdk="$(manifest_string_value "sdk")"
  manifest_archs="$(manifest_string_value "archs")"
  manifest_bundle_id="$(manifest_string_value "bundle_identifier")"
  manifest_bundle_version="$(manifest_string_value "bundle_short_version")"
  manifest_device="$(manifest_number_value "device_artifact")"
  if [[ -z "$manifest_device" ]]; then
    if grep -q '"device_artifact"[[:space:]]*:[[:space:]]*true' "$IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"; then
      manifest_device="true"
    elif grep -q '"device_artifact"[[:space:]]*:[[:space:]]*false' "$IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"; then
      manifest_device="false"
    fi
  fi
  manifest_assets="$(manifest_number_value "validated_source_assets")"
  manifest_required="$(manifest_number_value "validated_required_bundle_entries")"
  manifest_project_files="$(manifest_number_value "validated_project_files")"
  ipa_bytes="$(manifest_package_number_value "ipa" "size_bytes")"
  ipsw_bytes="$(manifest_package_number_value "ipsw" "size_bytes")"
  echo "[OK] iOS artifact audit: sdk=${manifest_sdk:-unknown} archs=${manifest_archs:-unknown} device_artifact=${manifest_device:-unknown} bundle=${manifest_bundle_id:-unknown} version=${manifest_bundle_version:-unknown} required_entries=${manifest_required:-unknown} assets=${manifest_assets:-unknown} project_files=${manifest_project_files:-unknown} ipa_bytes=${ipa_bytes:-unknown} ipsw_bytes=${ipsw_bytes:-unknown}"
}

case "$MODE" in
  configure)
    configure
    ;;
  build)
    configure
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target ios-package
    verify_package
    echo "[OK] iOS app archive: $IOS_PACKAGE_DIR/platformer.ipa"
    echo "[OK] Requested IPSW artifact: $IOS_PACKAGE_DIR/platformer.ipsw"
    echo "[OK] iOS package manifest: $IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"
    echo "[OK] iOS package checksums: $IOS_PACKAGE_DIR/platformer-ios-package-sha256.txt"
    ;;
  device|ipsw)
    configure
    cmake --build "$BUILD_DIR" --config "$CONFIG" --target ios-package
    verify_package
    echo "[OK] iOS device app archive: $IOS_PACKAGE_DIR/platformer.ipa"
    echo "[OK] Requested iOS device IPSW artifact: $IOS_PACKAGE_DIR/platformer.ipsw"
    echo "[OK] iOS package manifest: $IOS_PACKAGE_DIR/platformer-ios-package-manifest.json"
    echo "[OK] iOS package checksums: $IOS_PACKAGE_DIR/platformer-ios-package-sha256.txt"
    ;;
  clean)
    rm -rf "$BUILD_DIR"
    echo "[OK] Removed $BUILD_DIR"
    ;;
  deps)
    install_deps
    ;;
  doctor)
    doctor
    ;;
  verify)
    verify_package
    ;;
  *)
    echo "Usage: $0 [configure|build|device|ipsw|clean|deps|doctor|verify]" >&2
    exit 1
    ;;
esac
