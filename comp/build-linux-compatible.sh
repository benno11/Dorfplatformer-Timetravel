#!/usr/bin/env bash
# Run inside Ubuntu 20.04: build every linked dependency against glibc 2.31.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential ninja-build git curl ca-certificates \
  python3 python3-pip autoconf autoconf-archive automake libtool libltdl-dev pkg-config zip unzip \
  tar bzip2 xz-utils linux-libc-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
  libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libgl1-mesa-dev libegl1-mesa-dev
python3 -m pip install --no-cache-dir 'cmake==3.31.10'
export VCPKG_ROOT="$PWD/.ci/vcpkg"
# A separate cache namespace prevents reuse of packages built on a newer host.
export VCPKG_DEFAULT_BINARY_CACHE="$PWD/.ci/linux-glibc231-cache"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
"$VCPKG_ROOT/vcpkg" install sdl3 'sdl3-image[png,jpeg]' sdl3-ttf 'sdl3-mixer[mpg123]' curl nlohmann-json \
  --triplet x64-linux --vcpkg-root "$VCPKG_ROOT"
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
  -DPLATFORMER_LITE_EDITION="${PLATFORMER_LITE_EDITION:-OFF}" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux -DPLATFORMER_REQUIRE_SDL3_MIXER=ON \
  '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc'
cmake --build build-ci --parallel
