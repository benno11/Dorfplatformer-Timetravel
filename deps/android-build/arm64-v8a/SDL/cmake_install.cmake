# Install script for directory: /home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/benno111/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/sdl3.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/libSDL3.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/libSDL3_test.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3headersTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3headersTargets.cmake"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3headersTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3headersTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3headersTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3headersTargets.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3staticTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3staticTargets.cmake"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3staticTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3staticTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3staticTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3staticTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3staticTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3testTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3testTargets.cmake"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3testTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3testTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3/SDL3testTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3testTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/35815d1d52a6ea1175d74784b559bdb6/SDL3testTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL3" TYPE FILE FILES
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/SDL3Config.cmake"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/SDL3ConfigVersion.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/SDL3" TYPE FILE FILES
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_assert.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_asyncio.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_atomic.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_audio.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_begin_code.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_bits.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_blendmode.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_camera.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_clipboard.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_close_code.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_copying.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_cpuinfo.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_dialog.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_dlopennote.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_egl.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_endian.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_error.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_events.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_filesystem.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_gamepad.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_gpu.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_guid.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_haptic.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_hidapi.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_hints.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_init.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_intrin.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_iostream.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_joystick.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_keyboard.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_keycode.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_loadso.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_locale.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_log.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_main.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_main_impl.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_messagebox.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_metal.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_misc.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_mouse.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_mutex.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_oldnames.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengl.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengl_glext.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengles.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengles2.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengles2_gl2.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengles2_gl2ext.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengles2_gl2platform.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_opengles2_khrplatform.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_pen.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_pixels.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_platform.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_platform_defines.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_power.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_process.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_properties.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_rect.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_render.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_scancode.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_sensor.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_stdinc.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_storage.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_surface.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_system.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_thread.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_time.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_timer.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_touch.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_tray.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_version.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_video.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_vulkan.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/include-revision/SDL3/SDL_revision.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/SDL3" TYPE FILE FILES
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_assert.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_common.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_compare.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_crc32.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_font.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_fuzzer.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_harness.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_log.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_md5.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL3/SDL_test_memory.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/licenses/SDL3" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/LICENSE.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
