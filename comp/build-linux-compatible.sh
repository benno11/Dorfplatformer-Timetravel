#!/usr/bin/env bash
# Run inside Ubuntu 20.04: build every linked dependency against glibc 2.31.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 install -y --no-install-recommends build-essential ninja-build git curl ca-certificates \
  python3 python3-pip autoconf autoconf-archive automake libtool libltdl-dev pkg-config zip unzip \
  bison flex gettext autopoint gperf texinfo nasm \
  tar bzip2 xz-utils linux-libc-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
  libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libgl1-mesa-dev libegl1-mesa-dev \
  libssl-dev libffi-dev zlib1g-dev libbz2-dev liblzma-dev libreadline-dev libsqlite3-dev
# Modern dependencies use newer syscall declarations (for example mount_setattr).
# UAPI headers contain no libc binaries, so this keeps glibc at 2.31.
uapi_archive=/tmp/df-linux-uapi.deb
curl --fail --location --retry 3 \
  https://snapshot.debian.org/file/70a24f2faafa6d008249156b7ec085e6a5740c49 -o "$uapi_archive"
echo "8b4848725a2705e5b1612f2135c44300716690ba762368ab87df127e9a5ed416  $uapi_archive" | sha256sum --check
# Debian linux-libc-dev 6.12.117-1: extract declarations only; no package hooks.
dpkg-deb --extract "$uapi_archive" /
# Recent dependency configure scripts need Autoconf >= 2.70. Building the
# tool from source upgrades tooling without upgrading the glibc baseline.
autoconf_version=2.72
autoconf_archive="/tmp/autoconf-${autoconf_version}.tar.xz"
curl --fail --location --retry 3 "https://ftp.gnu.org/gnu/autoconf/autoconf-${autoconf_version}.tar.xz" -o "$autoconf_archive"
echo "ba885c1319578d6c94d46e9b0dceb4014caafe2490e437a0dbca3f270a223f5a  $autoconf_archive" | sha256sum --check
tar -xJf "$autoconf_archive" -C /tmp
(
  cd "/tmp/autoconf-${autoconf_version}"
  ./configure --prefix=/opt/df-build-tools
  make -j"$(nproc)"
  make install
)
export PATH="/opt/df-build-tools/bin:$PATH"
autoconf --version
# Meson and other build tools require a newer Python than Focal's 3.8.
# Compile it against this container's libc rather than importing a newer distro.
python_version=3.12.12
python_archive="/tmp/Python-${python_version}.tar.xz"
curl --fail --location --retry 3 "https://www.python.org/ftp/python/${python_version}/Python-${python_version}.tar.xz" -o "$python_archive"
echo "fb85a13414b028c49ba18bbd523c2d055a30b56b18b92ce454ea2c51edc656c4  $python_archive" | sha256sum --check
tar -xJf "$python_archive" -C /tmp
(
  cd "/tmp/Python-${python_version}"
  ./configure --prefix=/opt/df-build-tools --with-ensurepip=install
  make -j"$(nproc)"
  make install
)
ln -sf python3.12 /opt/df-build-tools/bin/python3
python3 --version
python3 -c 'import ssl, sqlite3, ctypes; assert ssl.OPENSSL_VERSION'
python3 -m pip install --no-cache-dir 'cmake==3.31.10'
export VCPKG_ROOT="$PWD/.ci/vcpkg"
# A separate cache namespace prevents reuse of packages built on a newer host.
export VCPKG_DEFAULT_BINARY_CACHE="$PWD/.ci/linux-glibc231-cache"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
# SDL needs the D-Bus client, not a newly built systemd service manager.
# Disable only the port's optional at_console systemd integration; preserve
# D-Bus itself and all game audio/display/input features.
overlay_ports="$PWD/.ci/linux-compatible-ports"
mkdir -p "$overlay_ports"
cp -R "$VCPKG_ROOT/ports/dbus" "$overlay_ports/"
python3 - "$overlay_ports/dbus/vcpkg.json" <<'PYTHON'
import json
import sys
from pathlib import Path
path = Path(sys.argv[1])
port = json.loads(path.read_text())
port['default-features'] = [feature for feature in port.get('default-features', [])
    if (feature if isinstance(feature, str) else feature['name']) != 'systemd']
path.write_text(json.dumps(port, indent=2) + '\n')
PYTHON
"$VCPKG_ROOT/vcpkg" install sdl3 'sdl3-image[png,jpeg]' sdl3-ttf 'sdl3-mixer[mpg123]' curl nlohmann-json \
  --triplet x64-linux --vcpkg-root "$VCPKG_ROOT" --clean-after-build \
  --overlay-ports "$overlay_ports"
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
  -DPLATFORMER_LITE_EDITION="${PLATFORMER_LITE_EDITION:-OFF}" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux -DPLATFORMER_REQUIRE_SDL3_MIXER=ON \
  '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc'
cmake --build build-ci --parallel
