#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SDL3_image::SDL3_image-static" for configuration "Release"
set_property(TARGET SDL3_image::SDL3_image-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_image::SDL3_image-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libSDL3_image.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::SDL3_image-static )
list(APPEND _cmake_import_check_files_for_SDL3_image::SDL3_image-static "${_IMPORT_PREFIX}/lib/libSDL3_image.a" )

# Import target "SDL3_image::external_zlib" for configuration "Release"
set_property(TARGET SDL3_image::external_zlib APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_image::external_zlib PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libz.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::external_zlib )
list(APPEND _cmake_import_check_files_for_SDL3_image::external_zlib "${_IMPORT_PREFIX}/lib/libz.a" )

# Import target "SDL3_image::dav1d" for configuration "Release"
set_property(TARGET SDL3_image::dav1d APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_image::dav1d PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "ASM;C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdav1d.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::dav1d )
list(APPEND _cmake_import_check_files_for_SDL3_image::dav1d "${_IMPORT_PREFIX}/lib/libdav1d.a" )

# Import target "SDL3_image::aom" for configuration "Release"
set_property(TARGET SDL3_image::aom APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_image::aom PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libaom.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::aom )
list(APPEND _cmake_import_check_files_for_SDL3_image::aom "${_IMPORT_PREFIX}/lib/libaom.a" )

# Import target "SDL3_image::external_libavif" for configuration "Release"
set_property(TARGET SDL3_image::external_libavif APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_image::external_libavif PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libavif.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::external_libavif )
list(APPEND _cmake_import_check_files_for_SDL3_image::external_libavif "${_IMPORT_PREFIX}/lib/libavif.a" )

# Import target "SDL3_image::external_libpng" for configuration "Release"
set_property(TARGET SDL3_image::external_libpng APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_image::external_libpng PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libpng16.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::external_libpng )
list(APPEND _cmake_import_check_files_for_SDL3_image::external_libpng "${_IMPORT_PREFIX}/lib/libpng16.a" )

# Import target "SDL3_image::external_libtiff" for configuration "Release"
set_property(TARGET SDL3_image::external_libtiff APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL3_image::external_libtiff PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtiff.a"
  )

list(APPEND _cmake_import_check_targets SDL3_image::external_libtiff )
list(APPEND _cmake_import_check_files_for_SDL3_image::external_libtiff "${_IMPORT_PREFIX}/lib/libtiff.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
