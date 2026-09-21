$ErrorActionPreference = 'Stop'
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
if ($args -contains '--run') {
  if (Test-Path './build/platformer.exe') {
    & './build/platformer.exe'
  } elseif (Test-Path './build/platformer') {
    & './build/platformer'
  }
}
