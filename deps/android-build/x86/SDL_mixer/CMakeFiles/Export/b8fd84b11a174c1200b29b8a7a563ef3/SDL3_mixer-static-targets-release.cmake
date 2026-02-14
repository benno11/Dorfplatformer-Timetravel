#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SDL3_mixer::SDL3_mixer-static" for configuration "Release"
set_property(TARGET SDL3_mixer::SDL3_mixer-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_mixer::SDL3_mixer-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libSDL3_mixer.a"
  )

list(APPEND _cmake_import_check_targets SDL3_mixer::SDL3_mixer-static )
list(APPEND _cmake_import_check_files_for_SDL3_mixer::SDL3_mixer-static "${_IMPORT_PREFIX}/lib/libSDL3_mixer.a" )

# Import target "SDL3_mixer::ogg" for configuration "Release"
set_property(TARGET SDL3_mixer::ogg APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_mixer::ogg PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libogg.a"
  )

list(APPEND _cmake_import_check_targets SDL3_mixer::ogg )
list(APPEND _cmake_import_check_files_for_SDL3_mixer::ogg "${_IMPORT_PREFIX}/lib/libogg.a" )

# Import target "SDL3_mixer::vorbis" for configuration "Release"
set_property(TARGET SDL3_mixer::vorbis APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_mixer::vorbis PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libvorbis.a"
  )

list(APPEND _cmake_import_check_targets SDL3_mixer::vorbis )
list(APPEND _cmake_import_check_files_for_SDL3_mixer::vorbis "${_IMPORT_PREFIX}/lib/libvorbis.a" )

# Import target "SDL3_mixer::vorbisfile" for configuration "Release"
set_property(TARGET SDL3_mixer::vorbisfile APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_mixer::vorbisfile PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libvorbisfile.a"
  )

list(APPEND _cmake_import_check_targets SDL3_mixer::vorbisfile )
list(APPEND _cmake_import_check_files_for_SDL3_mixer::vorbisfile "${_IMPORT_PREFIX}/lib/libvorbisfile.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
