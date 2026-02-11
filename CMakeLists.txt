cmake_minimum_required(VERSION 3.16)
project(platformer LANGUAGES CXX)
set(PROJECT_SDL_MIN_VERSION "3.4.0")

message(STATUS "Configuring ${PROJECT_NAME}")
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g -DNDEBUG")

option(PLATFORMER_ENABLE_UNITY_BUILD "Enable CMake unity build for faster compile times" ON)
if(PLATFORMER_ENABLE_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
  message(STATUS "Unity build enabled")
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "Using compiler cache: ${CCACHE_PROGRAM}")
endif()

include(CheckIPOSupported)
option(PLATFORMER_ENABLE_IPO "Enable IPO/LTO (slower builds, smaller/faster binaries)" OFF)
if(PLATFORMER_ENABLE_IPO)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    message(STATUS "IPO/LTO enabled")
  else()
    message(STATUS "IPO/LTO not supported: ${ipo_error}")
  endif()
else()
  message(STATUS "IPO/LTO disabled for faster builds")
endif()

add_executable(platformer
  src/main.cpp
  src/TileMap.cpp
  src/LevelLoader.cpp
  src/TextRenderer.cpp
  src/LevelSelect.cpp
  src/PlayerController.cpp
  src/AssetPath.cpp
  src/LevelManager.cpp
  src/GameSupport.cpp
  src/CrashReporter.cpp
  src/FrontendMenu.cpp
  src/AudioSystem.cpp
)
message(STATUS "Added target: platformer")

find_package(sdl3 ${PROJECT_SDL_MIN_VERSION} QUIET)
if (NOT sdl3_FOUND)
  message(WARNING "sdl3 ${PROJECT_SDL_MIN_VERSION}+ not found; falling back to system sdl3 version.")
  find_package(sdl3 REQUIRED)
endif()
find_package(sdl3_image REQUIRED)
find_package(sdl3_ttf REQUIRED)
find_package(sdl3_mixer QUIET)
find_package(CURL QUIET)
find_package(nlohmann_json REQUIRED)
message(STATUS "Found sdl3: ${sdl3_VERSION}")
message(STATUS "Found sdl3_image: ${sdl3_image_VERSION}")
message(STATUS "Found nlohmann_json: ${nlohmann_json_VERSION}")

target_link_libraries(platformer
  sdl3::sdl3
  sdl3_image::sdl3_image
  sdl3_ttf::sdl3_ttf
  nlohmann_json::nlohmann_json
)
if (CURL_FOUND)
  target_link_libraries(platformer CURL::libcurl)
  target_compile_definitions(platformer PRIVATE HAVE_CURL=1)
  message(STATUS "Linked platformer: CURL::libcurl")
else()
  target_compile_definitions(platformer PRIVATE HAVE_CURL=0)
  message(WARNING "libcurl not found: remote Firebase level fetch disabled")
endif()
if (sdl3_mixer_FOUND)
  target_link_libraries(platformer sdl3_mixer::sdl3_mixer)
  message(STATUS "Linked platformer: sdl3_mixer::sdl3_mixer")
else()
  message(WARNING "sdl3_mixer not found: music playback disabled")
endif()
message(STATUS "Linked platformer: sdl3::sdl3, nlohmann_json::nlohmann_json")

add_executable(sheet_config
  src/SheetConfigTool.cpp
)
message(STATUS "Added target: sheet_config")

target_link_libraries(sheet_config
  sdl3::sdl3
  sdl3_image::sdl3_image
)
message(STATUS "Linked sheet_config: sdl3::sdl3, sdl3_image::sdl3_image")
