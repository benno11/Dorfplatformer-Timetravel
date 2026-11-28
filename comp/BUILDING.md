# Production and test builds

Production builds exclude tests by default (`BUILD_TESTING=OFF`). Test target
registration lives in `cmake/Tests.cmake` and is included only when testing is
explicitly enabled. Desktop release CI also sets `BUILD_TESTING=OFF`.

Use CMake 3.21+ for these presets, with the usual SDL and other dependencies:

```sh
cmake --preset production
cmake --build --preset production --parallel
```

Production files go to `build/production`. Tests have an independent cache and
outputs in `build/tests`:

```sh
cmake --preset tests
cmake --build --preset tests --parallel
ctest --preset tests
```

`comp/build-local.sh` builds production; `comp/build-tests.sh` builds and runs
tests. For toolchains without preset support, configure separate directories
manually with `-DBUILD_TESTING=OFF` or `-DBUILD_TESTING=ON`. Pass any dependency
or toolchain settings to the configure command as usual.

When reusing an older build directory, explicitly set `-DBUILD_TESTING=OFF`:
CMake preserves existing cache values when defaults change. New production
preset directories start with testing disabled.

Linux release compatibility uses glibc **2.31** (Ubuntu 20.04 baseline), rather
than the moving `ubuntu-latest` runtime. The release workflow builds the game
and vcpkg dependencies inside `ubuntu:20.04`, with a separate dependency cache.
The GCC C++ runtimes are linked statically; glibc remains dynamically linked.
After optional BOLT optimization, `comp/check-linux-libc.py` rejects packaged
executables requiring newer glibc or a shared GCC C++ runtime. This supports
Ubuntu 20.04+, Debian 11+, and distributions with compatible glibc 2.31+ runtimes;
musl-based distributions such as Alpine need a glibc environment.

To reproduce the release compilation from a checkout with vcpkg in `.ci/vcpkg`:

```sh
docker run --rm -e PLATFORMER_LITE_EDITION=OFF -v "$PWD:$PWD" -w "$PWD" \
  ubuntu:20.04 bash comp/build-linux-compatible.sh
```

The compatibility container builds checksum-verified Autoconf 2.72 for modern
dependency configure scripts; Ubuntu 20.04’s system Autoconf 2.69 is too old.
Failed Linux CI jobs upload dependency configure/build logs as artifacts.

Python 3.12.12 is also built from checksum-verified source inside the same
container, because current Meson requires Python 3.10 or newer. Its SSL, SQLite,
and ctypes modules are checked before dependencies are built.
The container also uses checksum-verified Linux 6.12 UAPI headers for modern
syscall declarations. These are headers only and do not upgrade glibc.
D-Bus is built without its optional systemd at_console integration, which
requires newer glibc in current upstream systemd. D-Bus client support and
SDL audio/display/input backends remain enabled.
