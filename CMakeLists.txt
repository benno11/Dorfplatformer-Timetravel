cmake_minimum_required(VERSION 3.16)
project(platformer LANGUAGES CXX)
set(PROJECT_SDL_MIN_VERSION "3.4.0")
if(WIN32)
  enable_language(RC)
endif()

set(PLATFORMER_APP_VERSION_STRING "0.0.0")
set(PLATFORMER_APP_VERSION_MAJOR 0)
set(PLATFORMER_APP_VERSION_MINOR 0)
set(PLATFORMER_APP_VERSION_PATCH 0)
set(PLATFORMER_APP_VERSION_BUILD 0)
set(PLATFORMER_APP_VERSION_ID "0")
string(TIMESTAMP PLATFORMER_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
string(TIMESTAMP PLATFORMER_BUILD_TIMEZONE "%Z (%z)")
configure_file("${CMAKE_SOURCE_DIR}/cmake/BuildInfo.h.in" "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.h" @ONLY)
set(_platformer_config_json "${CMAKE_SOURCE_DIR}/assets/config.json")
if(EXISTS "${_platformer_config_json}")
  file(READ "${_platformer_config_json}" _platformer_config_text)
  string(REGEX MATCH "\"version\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" _platformer_version_match "${_platformer_config_text}")
  if(CMAKE_MATCH_1)
    set(PLATFORMER_APP_VERSION_STRING "${CMAKE_MATCH_1}")
    string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _platformer_numeric_version "${PLATFORMER_APP_VERSION_STRING}")
    if(CMAKE_MATCH_1)
      set(PLATFORMER_APP_VERSION_MAJOR "${CMAKE_MATCH_1}")
      set(PLATFORMER_APP_VERSION_MINOR "${CMAKE_MATCH_2}")
      set(PLATFORMER_APP_VERSION_PATCH "${CMAKE_MATCH_3}")
    endif()
    string(REGEX MATCHALL "[0-9]+" _platformer_all_version_parts "${PLATFORMER_APP_VERSION_STRING}")
    list(LENGTH _platformer_all_version_parts _platformer_part_count)
    if(_platformer_part_count GREATER 3)
      list(GET _platformer_all_version_parts 3 PLATFORMER_APP_VERSION_BUILD)
    endif()
  endif()
  string(REGEX MATCH "\"version_id\"[ \t\r\n]*:[ \t\r\n]*([0-9]+)" _platformer_version_id_match "${_platformer_config_text}")
  if(CMAKE_MATCH_1)
    set(PLATFORMER_APP_VERSION_ID "${CMAKE_MATCH_1}")
  endif()
endif()
message(STATUS "App version: ${PLATFORMER_APP_VERSION_STRING}")
message(STATUS "App version id: ${PLATFORMER_APP_VERSION_ID}")

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

function(platformer_apply_windows_exe_hardening target_name)
  if(NOT WIN32)
    return()
  endif()

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /guard:cf)
    target_link_options(${target_name} PRIVATE
      /NXCOMPAT
      /DYNAMICBASE
      /guard:cf
    )
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_link_options(${target_name} PRIVATE /HIGHENTROPYVA)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_link_options(${target_name} PRIVATE
      "-Wl,--nxcompat"
      "-Wl,--dynamicbase"
    )
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_link_options(${target_name} PRIVATE "-Wl,--high-entropy-va")
    endif()
  endif()
endfunction()

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
  src/AndroidAudioBridge.cpp
  src/ParallaxRenderer.cpp
  src/World3PatternBackground.cpp
)
target_include_directories(platformer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(WIN32)
  set_target_properties(platformer PROPERTIES WIN32_EXECUTABLE ON)
endif()
if(WIN32)
  set(PLATFORMER_ICON_FILE "${CMAKE_SOURCE_DIR}/icon.ico")
  if(EXISTS "${PLATFORMER_ICON_FILE}")
    set(PLATFORMER_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/platformer_version.rc")
    set(WINDOWS_RESOURCE_ICON_FILE "${PLATFORMER_ICON_FILE}")
    set(WINDOWS_RESOURCE_FILE_DESCRIPTION "Dorfplatformer Timetravel")
    set(WINDOWS_RESOURCE_INTERNAL_NAME "platformer.exe")
    set(WINDOWS_RESOURCE_ORIGINAL_FILENAME "platformer.exe")
    set(WINDOWS_RESOURCE_PRODUCT_NAME "Dorfplatformer Timetravel")
    set(WINDOWS_RESOURCE_PRODUCT_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    set(WINDOWS_RESOURCE_FILE_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/WindowsVersionResource.rc.in" "${PLATFORMER_ICON_RC}" @ONLY)
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
  # Search for pre-bundled SDL3_mixer packages under deps/windows-sdl3-mixer/*/cmake.
  file(GLOB _windows_mixer_cmake_dirs "${CMAKE_SOURCE_DIR}/deps/windows-sdl3-mixer/*/cmake")
  foreach(_dir IN LISTS _windows_mixer_cmake_dirs)
    list(APPEND SDL3_MIXER_DIR_CANDIDATES "${_dir}")
  endforeach()
  unset(_windows_mixer_cmake_dirs)
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
  if(WIN32)
    foreach(_mixer_shared_candidate IN ITEMS
        "SDL3_mixer::SDL3_mixer"
        "SDL3_mixer::SDL3_mixer-shared"
        "SDL3_mixer::SDL3_mixer_shared")
      if(TARGET "${_mixer_shared_candidate}")
        set(SDL3_MIXER_LINK_TARGET "${_mixer_shared_candidate}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT SDL3_MIXER_LINK_TARGET)
    select_sdl_target(SDL3_MIXER_LINK_TARGET "SDL3_mixer" "SDL3_mixer")
  endif()
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

set(PLATFORMER_LINK_LIBS
  ${SDL3_LINK_TARGET}
  ${SDL3_IMAGE_LINK_TARGET}
  ${SDL3_TTF_LINK_TARGET}
  nlohmann_json::nlohmann_json
)
if(SDL3_MIXER_LINK_TARGET)
  list(APPEND PLATFORMER_LINK_LIBS ${SDL3_MIXER_LINK_TARGET})
endif()

target_link_libraries(platformer PRIVATE ${PLATFORMER_LINK_LIBS})

if(SDL3_MIXER_LINK_TARGET)
  target_compile_definitions(platformer PRIVATE PLATFORMER_HAS_SDL3_MIXER=1)
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
else()
  target_compile_definitions(platformer PRIVATE PLATFORMER_HAS_SDL3_MIXER=0)
endif()
if(ANDROID)
  target_link_libraries(platformer
    PRIVATE
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
platformer_apply_windows_exe_hardening(platformer)
if (CURL_FOUND)
  target_link_libraries(platformer PRIVATE CURL::libcurl)
  target_compile_definitions(platformer PRIVATE HAVE_CURL=1)
  message(STATUS "Linked platformer: CURL::libcurl")
else()
  target_compile_definitions(platformer PRIVATE HAVE_CURL=0)
  message(WARNING "libcurl not found: remote Firebase level fetch disabled")
endif()
message(STATUS "Linked platformer: ${PLATFORMER_LINK_LIBS}")

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
platformer_apply_windows_exe_hardening(sheet_config)
message(STATUS "Linked sheet_config: ${SDL3_LINK_TARGET}, ${SDL3_IMAGE_LINK_TARGET}")

if(WIN32 AND CURL_FOUND)
  add_executable(df-updater WIN32
    src/WindowsUpdater.cpp
  )
  if(EXISTS "${PLATFORMER_ICON_FILE}")
    set(DF_UPDATER_RC "${CMAKE_CURRENT_BINARY_DIR}/df_updater_version.rc")
    set(WINDOWS_RESOURCE_ICON_FILE "${PLATFORMER_ICON_FILE}")
    set(WINDOWS_RESOURCE_FILE_DESCRIPTION "Dorfplatformer Timetravel Updater")
    set(WINDOWS_RESOURCE_INTERNAL_NAME "df-updater.exe")
    set(WINDOWS_RESOURCE_ORIGINAL_FILENAME "df-updater.exe")
    set(WINDOWS_RESOURCE_PRODUCT_NAME "Dorfplatformer Timetravel")
    set(WINDOWS_RESOURCE_PRODUCT_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    set(WINDOWS_RESOURCE_FILE_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/WindowsVersionResource.rc.in" "${DF_UPDATER_RC}" @ONLY)
    target_sources(df-updater PRIVATE "${DF_UPDATER_RC}")
  endif()
  target_link_libraries(df-updater
    PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    shell32
  )
  target_compile_definitions(df-updater PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  if(MSVC)
    target_compile_options(df-updater PRIVATE /EHsc /FS)
  endif()
  platformer_apply_windows_exe_hardening(df-updater)
  message(STATUS "Added target: df-updater")
endif()

if(WIN32)
  add_executable(df-launcher WIN32
    src/WindowsLauncher.cpp
  )
  if(EXISTS "${PLATFORMER_ICON_FILE}")
    set(DF_LAUNCHER_RC "${CMAKE_CURRENT_BINARY_DIR}/df_launcher_version.rc")
    set(WINDOWS_RESOURCE_ICON_FILE "${PLATFORMER_ICON_FILE}")
    set(WINDOWS_RESOURCE_FILE_DESCRIPTION "Dorfplatformer Timetravel Launcher")
    set(WINDOWS_RESOURCE_INTERNAL_NAME "df-launcher.exe")
    set(WINDOWS_RESOURCE_ORIGINAL_FILENAME "df-launcher.exe")
    set(WINDOWS_RESOURCE_PRODUCT_NAME "Dorfplatformer Timetravel")
    set(WINDOWS_RESOURCE_PRODUCT_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    set(WINDOWS_RESOURCE_FILE_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/WindowsVersionResource.rc.in" "${DF_LAUNCHER_RC}" @ONLY)
    target_sources(df-launcher PRIVATE "${DF_LAUNCHER_RC}")
  endif()
  target_compile_definitions(df-launcher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  if(MSVC)
    target_compile_options(df-launcher PRIVATE /EHsc /FS)
  endif()
  platformer_apply_windows_exe_hardening(df-launcher)
  message(STATUS "Added target: df-launcher")
endif()

if(WIN32)
  add_executable(df-tray WIN32
    src/WindowsTrayApp.cpp
  )
  if(EXISTS "${PLATFORMER_ICON_FILE}")
    set(DF_TRAY_RC "${CMAKE_CURRENT_BINARY_DIR}/df_tray_version.rc")
    set(WINDOWS_RESOURCE_ICON_FILE "${PLATFORMER_ICON_FILE}")
    set(WINDOWS_RESOURCE_FILE_DESCRIPTION "Dorfplatformer Timetravel Tray")
    set(WINDOWS_RESOURCE_INTERNAL_NAME "df-tray.exe")
    set(WINDOWS_RESOURCE_ORIGINAL_FILENAME "df-tray.exe")
    set(WINDOWS_RESOURCE_PRODUCT_NAME "Dorfplatformer Timetravel")
    set(WINDOWS_RESOURCE_PRODUCT_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    set(WINDOWS_RESOURCE_FILE_VERSION "${PLATFORMER_APP_VERSION_STRING}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/WindowsVersionResource.rc.in" "${DF_TRAY_RC}" @ONLY)
    target_sources(df-tray PRIVATE "${DF_TRAY_RC}")
  endif()
  target_link_libraries(df-tray
    PRIVATE
    ${SDL3_LINK_TARGET}
    nlohmann_json::nlohmann_json
    shell32
  )
  target_compile_definitions(df-tray PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  if(MSVC)
    target_compile_options(df-tray PRIVATE /EHsc /FS)
  endif()
  platformer_apply_windows_exe_hardening(df-tray)
  message(STATUS "Added target: df-tray")
endif()

if(WIN32)
  set(PLATFORMER_WINDOWS_DIST_DIR "${CMAKE_SOURCE_DIR}/dist/windows-installer")
  set(PLATFORMER_WINDOWS_ROOT_DIR "${PLATFORMER_WINDOWS_DIST_DIR}/root")
  set(PLATFORMER_WINDOWS_APP_DIR "${PLATFORMER_WINDOWS_DIST_DIR}/app")
  set(PLATFORMER_WINDOWS_INSTALLER_OUTPUT "${PLATFORMER_WINDOWS_DIST_DIR}/output/df-platformer-windows-installer.exe")

  set(_platformer_windows_stage_deps
    platformer
    sheet_config
    df-launcher
    df-tray
  )
  set(_platformer_windows_updater_path "")
  if(TARGET df-updater)
    list(APPEND _platformer_windows_stage_deps df-updater)
    set(_platformer_windows_updater_path "$<TARGET_FILE:df-updater>")
  endif()

  add_custom_target(windows-package ALL
    COMMAND ${CMAKE_COMMAND}
      -DROOT_DIR=${PLATFORMER_WINDOWS_ROOT_DIR}
      -DAPP_DIR=${PLATFORMER_WINDOWS_APP_DIR}
      -DASSETS_DIR=${CMAKE_SOURCE_DIR}/assets
      -DVERSION_ID=${PLATFORMER_APP_VERSION_ID}
      -DPLATFORMER_EXE=$<TARGET_FILE:platformer>
      -DSHEET_CONFIG_EXE=$<TARGET_FILE:sheet_config>
      -DLAUNCHER_EXE=$<TARGET_FILE:df-launcher>
      -DTRAY_EXE=$<TARGET_FILE:df-tray>
      -DUPDATER_EXE=${_platformer_windows_updater_path}
      -DREADME_FILE=${CMAKE_SOURCE_DIR}/README.md
      -DLICENSE_FILE=${CMAKE_SOURCE_DIR}/LICENSE
      -DOBJECT_TYPE_MAP_FILE=${CMAKE_SOURCE_DIR}/object_type_map.json
      -DRUNTIME_DLL_DIR=$<TARGET_FILE_DIR:platformer>
      -P ${CMAKE_SOURCE_DIR}/cmake/StageWindowsBundle.cmake
    DEPENDS ${_platformer_windows_stage_deps}
    COMMENT "Staging Windows bundle in dist/windows-installer"
    VERBATIM
  )

  find_program(INNO_SETUP_COMPILER
    NAMES ISCC.exe ISCC
    PATHS
      "C:/Program Files (x86)/Inno Setup 6"
      "C:/Program Files/Inno Setup 6"
  )

  if(INNO_SETUP_COMPILER)
    add_custom_command(
      OUTPUT "${PLATFORMER_WINDOWS_INSTALLER_OUTPUT}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PLATFORMER_WINDOWS_DIST_DIR}/output"
      COMMAND "${INNO_SETUP_COMPILER}"
        /Qp
        /DMyAppVersion=${PLATFORMER_APP_VERSION_STRING}
        /DMyAppVersionId=${PLATFORMER_APP_VERSION_ID}
        "${CMAKE_SOURCE_DIR}/installer/windows/df-platformer.iss"
      DEPENDS windows-package "${CMAKE_SOURCE_DIR}/installer/windows/df-platformer.iss"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "Building Windows installer with Inno Setup"
      VERBATIM
    )
    add_custom_target(windows-installer
      DEPENDS "${PLATFORMER_WINDOWS_INSTALLER_OUTPUT}"
    )
  else()
    add_custom_target(windows-installer
      COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --yellow
        "Inno Setup was not found. Built the staged Windows bundle only."
      DEPENDS windows-package
      VERBATIM
    )
  endif()
endif()
