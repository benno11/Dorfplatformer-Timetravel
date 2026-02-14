# Install script for directory: /home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/benno111/Documents/GitHub/DF-New/deps/android-install/arm64-v8a/SDL_mixer")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL_mixer/external/ogg-build/libogg.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL_mixer/external/ogg-build/ogg.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/SDL3_mixer/html" TYPE FILE FILES
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/framing.html"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/index.html"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/oggstream.html"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/ogg-multiplex.html"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/fish_xiph_org.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/multiplex1.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/packets.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/pages.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/stream.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/vorbisword2.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/white-ogg.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/white-xifish.png"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/rfc3533.txt"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/rfc5334.txt"
    "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/skeleton.html"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/SDL3_mixer/html" TYPE DIRECTORY FILES "/home/benno111/Documents/GitHub/DF-New/deps/android-src/SDL_mixer/external/ogg/doc/libogg")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL_mixer/external/ogg-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
