#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SDL2_mixer::SDL2_mixer" for configuration "Release"
set_property(TARGET SDL2_mixer::SDL2_mixer APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_mixer::SDL2_mixer PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "SDL2::SDL2"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libSDL2_mixer.so"
  IMPORTED_SONAME_RELEASE "libSDL2_mixer.so"
  )

list(APPEND _cmake_import_check_targets SDL2_mixer::SDL2_mixer )
list(APPEND _cmake_import_check_files_for_SDL2_mixer::SDL2_mixer "${_IMPORT_PREFIX}/lib/libSDL2_mixer.so" )

# Import target "SDL2_mixer::ogg" for configuration "Release"
set_property(TARGET SDL2_mixer::ogg APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_mixer::ogg PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libogg.so"
  IMPORTED_SONAME_RELEASE "libogg.so"
  )

list(APPEND _cmake_import_check_targets SDL2_mixer::ogg )
list(APPEND _cmake_import_check_files_for_SDL2_mixer::ogg "${_IMPORT_PREFIX}/lib/libogg.so" )

# Import target "SDL2_mixer::opus" for configuration "Release"
set_property(TARGET SDL2_mixer::opus APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_mixer::opus PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopus.so"
  IMPORTED_SONAME_RELEASE "libopus.so"
  )

list(APPEND _cmake_import_check_targets SDL2_mixer::opus )
list(APPEND _cmake_import_check_files_for_SDL2_mixer::opus "${_IMPORT_PREFIX}/lib/libopus.so" )

# Import target "SDL2_mixer::opusfile" for configuration "Release"
set_property(TARGET SDL2_mixer::opusfile APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_mixer::opusfile PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libopusfile.so"
  IMPORTED_SONAME_RELEASE "libopusfile.so"
  )

list(APPEND _cmake_import_check_targets SDL2_mixer::opusfile )
list(APPEND _cmake_import_check_files_for_SDL2_mixer::opusfile "${_IMPORT_PREFIX}/lib/libopusfile.so" )

# Import target "SDL2_mixer::WavPack" for configuration "Release"
set_property(TARGET SDL2_mixer::WavPack APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_mixer::WavPack PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwavpack.so"
  IMPORTED_SONAME_RELEASE "libwavpack.so"
  )

list(APPEND _cmake_import_check_targets SDL2_mixer::WavPack )
list(APPEND _cmake_import_check_files_for_SDL2_mixer::WavPack "${_IMPORT_PREFIX}/lib/libwavpack.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
