# Install script for directory: /home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_image/external/libtiff/libtiff

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image")
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
  if(EXISTS "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiff.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiff.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiff.so"
         RPATH "")
  endif()
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiff.so")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib" TYPE SHARED_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL_image/external/libtiff/libtiff/libtiff.so")
  if(EXISTS "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiff.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiff.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/home/benno111/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiff.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/include/tiff.h;/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/include/tiffio.h;/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/include/tiffvers.h;/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/include/tiffconf.h")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/include" TYPE FILE FILES
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_image/external/libtiff/libtiff/tiff.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_image/external/libtiff/libtiff/tiffio.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_image/external/libtiff/libtiff/tiffvers.h"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL_image/external/libtiff/libtiff/tiffconf.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiffxx.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiffxx.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiffxx.so"
         RPATH "")
  endif()
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiffxx.so")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib" TYPE SHARED_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL_image/external/libtiff/libtiff/libtiffxx.so")
  if(EXISTS "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiffxx.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiffxx.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/home/benno111/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$ENV{DESTDIR}/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/lib/libtiffxx.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/include/tiffio.hxx")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_image/include" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_image/external/libtiff/libtiff/tiffio.hxx")
endif()

