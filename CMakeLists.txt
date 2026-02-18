cmake_minimum_required(VERSION 3.16)
project(platformer LANGUAGES CXX)
set(PROJECT_SDL_MIN_VERSION "3.4.0")

# Keep generated files out of the source tree.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not supported. Use a separate build folder, for example: cmake -S . -B .build")
endif()

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
option(PLATFORMER_EMBED_LIBS "Prefer static dependency libraries in the main binary" ON)

option(PLATFORMER_ENABLE_UNITY_BUILD "Enable CMake unity build for faster compile times" ON)
if(PLATFORMER_ENABLE_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
  message(STATUS "Unity build enabled")
endif()

option(PLATFORMER_USE_CCACHE "Use ccache as compiler launcher when available" ON)
if(PLATFORMER_USE_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using compiler cache: ${CCACHE_PROGRAM}")
  endif()
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
  src/GameApp.cpp
  src/AndroidEntrypoints.cpp
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
  src/ParallaxRenderer.cpp
  src/World3PatternBackground.cpp
)
message(STATUS "Added target: platformer")

set(SDL3_DIR_CANDIDATES
  "${CMAKE_SOURCE_DIR}/deps/linux-sdl3-src/build"
  "/usr/local/lib/cmake/SDL3"
  "/usr/lib/x86_64-linux-gnu/cmake/SDL3"
  "/lib/x86_64-linux-gnu/cmake/SDL3"
)
find_package(SDL3 ${PROJECT_SDL_MIN_VERSION} QUIET CONFIG)
if(NOT SDL3_FOUND)
  foreach(_dir IN LISTS SDL3_DIR_CANDIDATES)
    if(EXISTS "${_dir}/SDL3Config.cmake")
      set(SDL3_DIR "${_dir}")
      find_package(SDL3 ${PROJECT_SDL_MIN_VERSION} QUIET CONFIG)
      if(SDL3_FOUND)
        message(STATUS "Resolved SDL3 from ${_dir}")
        break()
      endif()
    endif()
  endforeach()
endif()
if (NOT SDL3_FOUND)
  message(WARNING "SDL3 ${PROJECT_SDL_MIN_VERSION}+ not found; falling back to system SDL3 version.")
  find_package(SDL3 REQUIRED CONFIG)
endif()

set(SDL3_IMAGE_DIR_CANDIDATES
  "${CMAKE_SOURCE_DIR}/deps/linux-sdl3-image-src/build"
  "/usr/local/lib/cmake/SDL3_image"
  "/usr/lib/x86_64-linux-gnu/cmake/SDL3_image"
)
find_package(SDL3_image QUIET CONFIG)
if(NOT SDL3_image_FOUND)
  foreach(_dir IN LISTS SDL3_IMAGE_DIR_CANDIDATES)
    if(EXISTS "${_dir}/SDL3_imageConfig.cmake")
      set(SDL3_image_DIR "${_dir}")
      find_package(SDL3_image QUIET CONFIG)
      if(SDL3_image_FOUND)
        message(STATUS "Resolved SDL3_image from ${_dir}")
        break()
      endif()
    endif()
  endforeach()
endif()
find_package(SDL3_image REQUIRED CONFIG)

set(SDL3_TTF_DIR_CANDIDATES
  "${CMAKE_SOURCE_DIR}/deps/linux-sdl3-ttf-src/build"
  "/usr/local/lib/cmake/SDL3_ttf"
  "/usr/lib/x86_64-linux-gnu/cmake/SDL3_ttf"
)
find_package(SDL3_ttf QUIET CONFIG)
if(NOT SDL3_ttf_FOUND)
  foreach(_dir IN LISTS SDL3_TTF_DIR_CANDIDATES)
    if(EXISTS "${_dir}/SDL3_ttfConfig.cmake")
      set(SDL3_ttf_DIR "${_dir}")
      find_package(SDL3_ttf QUIET CONFIG)
      if(SDL3_ttf_FOUND)
        message(STATUS "Resolved SDL3_ttf from ${_dir}")
        break()
      endif()
    endif()
  endforeach()
endif()
find_package(SDL3_ttf REQUIRED CONFIG)

set(SDL3_MIXER_DIR_CANDIDATES
  "${CMAKE_SOURCE_DIR}/deps/linux-sdl3-mixer-src/build"
  "/usr/local/lib/cmake/SDL3_mixer"
  "/usr/lib/x86_64-linux-gnu/cmake/SDL3_mixer"
)
find_package(SDL3_mixer QUIET CONFIG)
if(NOT SDL3_mixer_FOUND)
  foreach(_dir IN LISTS SDL3_MIXER_DIR_CANDIDATES)
    if(EXISTS "${_dir}/SDL3_mixerConfig.cmake")
      set(SDL3_mixer_DIR "${_dir}")
      find_package(SDL3_mixer QUIET CONFIG)
      if(SDL3_mixer_FOUND)
        message(STATUS "Resolved SDL3_mixer from ${_dir}")
        break()
      endif()
    endif()
  endforeach()
endif()
if(SDL3_mixer_FOUND)
  message(STATUS "Found SDL3_mixer: ${SDL3_mixer_VERSION}")
else()
  message(WARNING "SDL3_mixer not found: building without mixer link target (runtime audio falls back accordingly)")
endif()
find_package(CURL QUIET)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp")
    if(NOT TARGET nlohmann_json::nlohmann_json)
      add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
      set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party"
      )
    endif()
    message(WARNING "nlohmann_json package not found; using vendored headers from third_party/nlohmann")
  else()
    message(FATAL_ERROR "nlohmann_json package not found and vendored headers are missing")
  endif()
endif()
message(STATUS "Found SDL3: ${SDL3_VERSION}")
message(STATUS "Found SDL3_image: ${SDL3_image_VERSION}")
if(nlohmann_json_FOUND)
  message(STATUS "Found nlohmann_json: ${nlohmann_json_VERSION}")
else()
  message(STATUS "Found nlohmann_json: vendored")
endif()

function(select_sdl_target OUT NAMESPACE BASE)
  set(static_candidates
    "${NAMESPACE}::${BASE}-static"
    "${NAMESPACE}::${BASE}_static"
    "${NAMESPACE}::${BASE}-a"
  )
  set(shared_candidates
    "${NAMESPACE}::${BASE}"
    "${NAMESPACE}::${BASE}-shared"
    "${NAMESPACE}::${BASE}_shared"
  )
  if(PLATFORMER_EMBED_LIBS)
    foreach(c IN LISTS static_candidates)
      if(TARGET "${c}")
        set(${OUT} "${c}" PARENT_SCOPE)
        return()
      endif()
    endforeach()
  endif()
  foreach(c IN LISTS shared_candidates)
    if(TARGET "${c}")
      set(${OUT} "${c}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "No suitable target found for ${NAMESPACE}::${BASE}")
endfunction()

select_sdl_target(SDL3_LINK_TARGET "SDL3" "SDL3")
select_sdl_target(SDL3_IMAGE_LINK_TARGET "SDL3_image" "SDL3_image")
select_sdl_target(SDL3_TTF_LINK_TARGET "SDL3_ttf" "SDL3_ttf")

message(STATUS "Using SDL link target: ${SDL3_LINK_TARGET}")
message(STATUS "Using SDL_image link target: ${SDL3_IMAGE_LINK_TARGET}")
message(STATUS "Using SDL_ttf link target: ${SDL3_TTF_LINK_TARGET}")
set(SDL3_MIXER_LINK_TARGET "")
if(SDL3_mixer_FOUND)
  select_sdl_target(SDL3_MIXER_LINK_TARGET "SDL3_mixer" "SDL3_mixer")
  message(STATUS "Using SDL_mixer link target: ${SDL3_MIXER_LINK_TARGET}")
else()
  message(STATUS "SDL_mixer link target disabled")
endif()

set(SDL_LOWERCASE_INCLUDE_ROOT "${CMAKE_BINARY_DIR}/include")
if(EXISTS "/usr/include/SDL3/SDL.h")
  file(MAKE_DIRECTORY "${SDL_LOWERCASE_INCLUDE_ROOT}")
  if(IS_SYMLINK "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
    file(REMOVE "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
  endif()
  file(MAKE_DIRECTORY "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
  file(GLOB SDL3_HEADER_FILES "/usr/include/SDL3/*.h")
  if(SDL3_HEADER_FILES)
    file(COPY ${SDL3_HEADER_FILES} DESTINATION "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
  endif()
  if(EXISTS "/usr/include/SDL3_image/SDL_image.h")
    file(COPY "/usr/include/SDL3_image/SDL_image.h" DESTINATION "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
  endif()
  if(EXISTS "/usr/include/SDL3_ttf/SDL_ttf.h")
    file(COPY "/usr/include/SDL3_ttf/SDL_ttf.h" DESTINATION "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
  endif()
  if(EXISTS "/usr/include/SDL3_ttf/SDL_textengine.h")
    file(COPY "/usr/include/SDL3_ttf/SDL_textengine.h" DESTINATION "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
  endif()
endif()

target_link_libraries(platformer
  ${SDL3_LINK_TARGET}
  ${SDL3_IMAGE_LINK_TARGET}
  ${SDL3_TTF_LINK_TARGET}
  nlohmann_json::nlohmann_json
)
if(SDL3_MIXER_LINK_TARGET)
  target_link_libraries(platformer ${SDL3_MIXER_LINK_TARGET})
endif()
if(ANDROID)
  target_link_libraries(platformer
    OpenSLES
    android
    log
  )
endif()
if(EXISTS "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3/SDL.h")
  target_include_directories(platformer PRIVATE "${SDL_LOWERCASE_INCLUDE_ROOT}")
endif()
if (CURL_FOUND)
  target_link_libraries(platformer CURL::libcurl)
  target_compile_definitions(platformer PRIVATE HAVE_CURL=1)
  message(STATUS "Linked platformer: CURL::libcurl")
else()
  target_compile_definitions(platformer PRIVATE HAVE_CURL=0)
  message(WARNING "libcurl not found: remote Firebase level fetch disabled")
endif()
message(STATUS "Linked platformer: ${SDL3_LINK_TARGET}, nlohmann_json::nlohmann_json")

add_executable(sheet_config
  src/SheetConfigTool.cpp
)
message(STATUS "Added target: sheet_config")

target_link_libraries(sheet_config
  ${SDL3_LINK_TARGET}
  ${SDL3_IMAGE_LINK_TARGET}
)
if(EXISTS "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3/SDL.h")
  target_include_directories(sheet_config PRIVATE "${SDL_LOWERCASE_INCLUDE_ROOT}")
endif()
message(STATUS "Linked sheet_config: ${SDL3_LINK_TARGET}, ${SDL3_IMAGE_LINK_TARGET}")
