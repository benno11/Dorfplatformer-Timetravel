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

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/benno111/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSDL2.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSDL2.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSDL2.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/libSDL2.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSDL2.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSDL2.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/home/benno111/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libSDL2.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/libSDL2main.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/libSDL2_test.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2Targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2Targets.cmake"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2Targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2Targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2Targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2Targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2Targets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2/Android.mk")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2/Android.mk"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/bf966d96e9c75d4ecad2e929cd04551d/Android.mk")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2/Android-*.mk")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2/Android.mk\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/bf966d96e9c75d4ecad2e929cd04551d/Android.mk")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2mainTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2mainTargets.cmake"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2mainTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2mainTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2mainTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2mainTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2mainTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2main/Android.mk")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2main/Android.mk"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/aa7469c27a6d995cb7b772bf8ae41311/Android.mk")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2main/Android-*.mk")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2main/Android.mk\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2main" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/aa7469c27a6d995cb7b772bf8ae41311/Android.mk")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2testTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2testTargets.cmake"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2testTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2testTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2/SDL2testTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2testTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/f084604df1a27ef5b4fef7c7544737d1/SDL2testTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2test/Android.mk")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2test/Android.mk"
         "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/8ca5c825b8a9c95d778b7b114f96dc48/Android.mk")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2test/Android-*.mk")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2test/Android.mk\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/ndk-modules/SDL2test" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/CMakeFiles/Export/8ca5c825b8a9c95d778b7b114f96dc48/Android.mk")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SDL2" TYPE FILE FILES
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/SDL2Config.cmake"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/SDL2ConfigVersion.cmake"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/cmake/sdlfind.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/SDL2" TYPE FILE FILES
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_assert.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_atomic.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_audio.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_bits.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_blendmode.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_clipboard.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_copying.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_cpuinfo.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_egl.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_endian.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_error.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_events.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_filesystem.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_gamecontroller.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_gesture.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_guid.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_haptic.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_hidapi.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_hints.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_joystick.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_keyboard.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_keycode.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_loadso.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_locale.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_log.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_main.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_messagebox.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_metal.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_misc.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_mouse.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_mutex.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_name.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengl.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengl_glext.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengles.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengles2.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengles2_gl2.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengles2_gl2ext.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengles2_gl2platform.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_opengles2_khrplatform.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_pixels.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_platform.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_power.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_quit.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_rect.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_render.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_rwops.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_scancode.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_sensor.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_shape.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_stdinc.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_surface.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_system.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_syswm.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_assert.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_common.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_compare.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_crc32.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_font.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_fuzzer.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_harness.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_images.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_log.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_md5.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_memory.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_test_random.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_thread.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_timer.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_touch.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_types.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_version.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_video.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/SDL_vulkan.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/begin_code.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/include/close_code.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/include/SDL2/SDL_revision.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/include-config-release/SDL2/SDL_config.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/licenses/SDL2" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/LICENSE.txt")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/sdl2.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/sdl2-config")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/aclocal" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL/sdl2.m4")
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
