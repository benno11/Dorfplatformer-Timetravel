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
