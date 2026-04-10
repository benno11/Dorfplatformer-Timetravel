cmake_minimum_required(VERSION 3.16)
project(platformer LANGUAGES CXX)
set(PROJECT_SDL_MIN_VERSION "3.4.0")
if(WIN32)
  enable_language(RC)
endif()

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
option(PLATFORMER_ENABLE_LOWERCASE_SDL_SHIM "Generate lowercase SDL include shim (<sdl3/...>)" OFF)
option(PLATFORMER_REQUIRE_SDL3_MIXER "Fail configure if SDL3_mixer is not found" ON)

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
if(WIN32)
  set_target_properties(platformer PROPERTIES WIN32_EXECUTABLE ON)
endif()
if(WIN32)
  set(PLATFORMER_ICON_FILE "${CMAKE_SOURCE_DIR}/icon.ico")
  if(EXISTS "${PLATFORMER_ICON_FILE}")
    set(PLATFORMER_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/platformer_icon.rc")
    file(WRITE "${PLATFORMER_ICON_RC}" "IDI_APP_ICON ICON \"${PLATFORMER_ICON_FILE}\"\n")
    target_sources(platformer PRIVATE "${PLATFORMER_ICON_RC}")
    message(STATUS "Embedded Windows icon: ${PLATFORMER_ICON_FILE}")
  else()
    message(WARNING "Windows icon file not found: ${PLATFORMER_ICON_FILE}")
  endif()
endif()
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
if(WIN32)
  if(DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND SDL3_MIXER_DIR_CANDIDATES
      "${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/share/sdl3-mixer"
      "${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/share/SDL3_mixer"
    )
  endif()
  if(DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED VCPKG_TARGET_TRIPLET)
    get_filename_component(_vcpkg_root_guess "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(_vcpkg_root_guess "${_vcpkg_root_guess}" DIRECTORY)
    list(APPEND SDL3_MIXER_DIR_CANDIDATES
      "${_vcpkg_root_guess}/installed/${VCPKG_TARGET_TRIPLET}/share/sdl3-mixer"
      "${_vcpkg_root_guess}/installed/${VCPKG_TARGET_TRIPLET}/share/SDL3_mixer"
    )
  endif()
  if(DEFINED ENV{VCPKG_INSTALLED_DIR} AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND SDL3_MIXER_DIR_CANDIDATES
      "$ENV{VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/sdl3-mixer"
      "$ENV{VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/SDL3_mixer"
    )
  endif()
  # If SDL3 is already found via a prefix/toolchain, probe common mixer config
  # locations under those same prefixes.
  foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    if(_prefix)
      list(APPEND SDL3_MIXER_DIR_CANDIDATES
        "${_prefix}/share/sdl3-mixer"
        "${_prefix}/share/SDL3_mixer"
        "${_prefix}/lib/cmake/SDL3_mixer"
        "${_prefix}/cmake/SDL3_mixer"
      )
    endif()
  endforeach()
  # Also try alongside an explicitly provided SDL3_DIR.
  if(DEFINED SDL3_DIR)
    get_filename_component(_sdl3_parent "${SDL3_DIR}" DIRECTORY)
    list(APPEND SDL3_MIXER_DIR_CANDIDATES
      "${_sdl3_parent}/SDL3_mixer"
      "${_sdl3_parent}/sdl3-mixer"
    )
  endif()
endif()
if(DEFINED SDL3_mixer_DIR)
  message(STATUS "SDL3_mixer_DIR preset: ${SDL3_mixer_DIR}")
endif()
if(DEFINED ENV{SDL3_MIXER_DIR})
  list(PREPEND SDL3_MIXER_DIR_CANDIDATES "$ENV{SDL3_MIXER_DIR}")
  message(STATUS "SDL3_MIXER_DIR env candidate: $ENV{SDL3_MIXER_DIR}")
endif()
# SDL3_mixer prebuilt VC config expects one SDL_CPU_* flag to be set.
if(WIN32 AND NOT (DEFINED SDL_CPU_X86 OR DEFINED SDL_CPU_X64 OR DEFINED SDL_CPU_ARM64 OR DEFINED SDL_CPU_ARM64EC))
  string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" _vs_platform_name_lc)
  if(_vs_platform_name_lc MATCHES "arm64ec")
    set(SDL_CPU_ARM64EC ON)
  elseif(_vs_platform_name_lc MATCHES "arm64")
    set(SDL_CPU_ARM64 ON)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SDL_CPU_X64 ON)
  else()
    set(SDL_CPU_X86 ON)
  endif()
endif()
find_package(SDL3_mixer QUIET CONFIG)
if(NOT SDL3_mixer_FOUND)
  foreach(_dir IN LISTS SDL3_MIXER_DIR_CANDIDATES)
    if(EXISTS "${_dir}/SDL3_mixerConfig.cmake" OR
       EXISTS "${_dir}/sdl3-mixer-config.cmake" OR
       EXISTS "${_dir}/SDL3_mixer-config.cmake")
      set(SDL3_mixer_DIR "${_dir}")
      find_package(SDL3_mixer QUIET CONFIG)
      if(SDL3_mixer_FOUND)
        message(STATUS "Resolved SDL3_mixer from ${_dir}")
        break()
      endif()
    endif()
  endforeach()
endif()
if(NOT SDL3_mixer_FOUND)
  set(_local_vcpkg_exe "")
  if(WIN32)
    if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/vcpkg.exe")
      set(_local_vcpkg_exe "${CMAKE_SOURCE_DIR}/vcpkg/vcpkg.exe")
    endif()
  else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/vcpkg")
      set(_local_vcpkg_exe "${CMAKE_SOURCE_DIR}/vcpkg/vcpkg")
    endif()
  endif()
  if(_local_vcpkg_exe)
    if(NOT DEFINED VCPKG_TARGET_TRIPLET OR "${VCPKG_TARGET_TRIPLET}" STREQUAL "")
      if(WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-windows")
      elseif(APPLE)
        set(VCPKG_TARGET_TRIPLET "x64-osx")
      else()
        set(VCPKG_TARGET_TRIPLET "x64-linux")
      endif()
    endif()
    message(STATUS "SDL3_mixer not found, attempting auto-install via local vcpkg (${VCPKG_TARGET_TRIPLET})")
    execute_process(
      COMMAND "${_local_vcpkg_exe}" install sdl3-mixer "--triplet=${VCPKG_TARGET_TRIPLET}"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/vcpkg"
      RESULT_VARIABLE _vcpkg_mixer_rc
      OUTPUT_VARIABLE _vcpkg_mixer_out
      ERROR_VARIABLE _vcpkg_mixer_err
    )
    if(_vcpkg_mixer_rc EQUAL 0)
      list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}")
      find_package(SDL3_mixer QUIET CONFIG)
      if(SDL3_mixer_FOUND)
        message(STATUS "Resolved SDL3_mixer via local vcpkg auto-install")
      endif()
    else()
      message(WARNING "Auto-install sdl3-mixer failed: ${_vcpkg_mixer_err}")
    endif()
  endif()
endif()
if(SDL3_mixer_FOUND)
  message(STATUS "Found SDL3_mixer: ${SDL3_mixer_VERSION}")
else()
  if(PLATFORMER_REQUIRE_SDL3_MIXER)
    message(FATAL_ERROR "SDL3_mixer not found, but PLATFORMER_REQUIRE_SDL3_MIXER=ON")
  endif()
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
set(SDL_LOWERCASE_DIR "${SDL_LOWERCASE_INCLUDE_ROOT}/sdl3")
if(PLATFORMER_ENABLE_LOWERCASE_SDL_SHIM)
  file(MAKE_DIRECTORY "${SDL_LOWERCASE_DIR}")

  set(SDL_INCLUDE_BASES
    "/usr/include"
    "/usr/local/include"
  )
  foreach(_tgt IN ITEMS ${SDL3_LINK_TARGET} ${SDL3_IMAGE_LINK_TARGET} ${SDL3_TTF_LINK_TARGET} ${SDL3_MIXER_LINK_TARGET})
    if(TARGET "${_tgt}")
      get_target_property(_inc_dirs "${_tgt}" INTERFACE_INCLUDE_DIRECTORIES)
      if(_inc_dirs AND NOT _inc_dirs STREQUAL "NOTFOUND")
        foreach(_inc IN LISTS _inc_dirs)
          if(NOT _inc MATCHES "^\\$<")
            list(APPEND SDL_INCLUDE_BASES "${_inc}")
          endif()
        endforeach()
      endif()
    endif()
  endforeach()
  list(REMOVE_DUPLICATES SDL_INCLUDE_BASES)

  set(SDL_SHIM_FOUND FALSE)
  foreach(_base IN LISTS SDL_INCLUDE_BASES)
    get_filename_component(_base_name "${_base}" NAME)
    if(_base_name STREQUAL "SDL3" OR _base_name STREQUAL "SDL3_image" OR _base_name STREQUAL "SDL3_ttf" OR _base_name STREQUAL "SDL3_mixer")
      file(GLOB _sdl_headers "${_base}/*.h")
      if(_sdl_headers)
        file(COPY ${_sdl_headers} DESTINATION "${SDL_LOWERCASE_DIR}")
        set(SDL_SHIM_FOUND TRUE)
      endif()
    endif()
    foreach(_sub IN ITEMS SDL3 SDL3_image SDL3_ttf SDL3_mixer)
      if(EXISTS "${_base}/${_sub}")
        file(GLOB _sdl_headers "${_base}/${_sub}/*.h")
        if(_sdl_headers)
          file(COPY ${_sdl_headers} DESTINATION "${SDL_LOWERCASE_DIR}")
          set(SDL_SHIM_FOUND TRUE)
        endif()
      endif()
    endforeach()
  endforeach()
  if(SDL_SHIM_FOUND)
    message(STATUS "Generated lowercase SDL include shim at ${SDL_LOWERCASE_DIR}")
  else()
    message(WARNING "Could not generate lowercase SDL include shim; <sdl3/...> includes may fail")
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
  if(WIN32 AND TARGET "${SDL3_MIXER_LINK_TARGET}")
    get_target_property(_sdl3_mixer_type "${SDL3_MIXER_LINK_TARGET}" TYPE)
    add_custom_command(TARGET platformer POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${SDL3_MIXER_LINK_TARGET}>"
        "$<TARGET_FILE_DIR:platformer>"
      VERBATIM
    )
    if(_sdl3_mixer_type STREQUAL "SHARED_LIBRARY")
      add_custom_command(TARGET platformer POST_BUILD
        COMMAND ${CMAKE_COMMAND}
          -DSRC_DIR="$<TARGET_FILE_DIR:${SDL3_MIXER_LINK_TARGET}>/optional"
          -DDST_DIR="$<TARGET_FILE_DIR:platformer>"
          -P "${CMAKE_SOURCE_DIR}/cmake/CopyOptionalMixerDlls.cmake"
        VERBATIM
      )
    endif()
    message(STATUS "Will copy SDL3_mixer runtime DLL next to platformer.exe")
  endif()
endif()
if(ANDROID)
  target_link_libraries(platformer
    OpenSLES
    android
    log
  )
endif()
if(PLATFORMER_ENABLE_LOWERCASE_SDL_SHIM AND EXISTS "${SDL_LOWERCASE_DIR}/SDL.h")
  target_include_directories(platformer PRIVATE "${SDL_LOWERCASE_INCLUDE_ROOT}")
endif()
target_compile_definitions(platformer PRIVATE SDL_ENABLE_OLD_NAMES=1)
if(WIN32)
  target_compile_definitions(platformer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
  target_compile_options(platformer PRIVATE /EHsc /FS)
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
if(PLATFORMER_ENABLE_LOWERCASE_SDL_SHIM AND EXISTS "${SDL_LOWERCASE_DIR}/SDL.h")
  target_include_directories(sheet_config PRIVATE "${SDL_LOWERCASE_INCLUDE_ROOT}")
endif()
target_compile_definitions(sheet_config PRIVATE SDL_ENABLE_OLD_NAMES=1)
if(WIN32)
  target_compile_definitions(sheet_config PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
  target_compile_options(sheet_config PRIVATE /EHsc /FS)
endif()
message(STATUS "Linked sheet_config: ${SDL3_LINK_TARGET}, ${SDL3_IMAGE_LINK_TARGET}")
