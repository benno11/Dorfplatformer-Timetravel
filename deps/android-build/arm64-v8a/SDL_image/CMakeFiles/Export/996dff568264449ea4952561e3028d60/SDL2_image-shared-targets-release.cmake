#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SDL2_image::SDL2_image" for configuration "Release"
set_property(TARGET SDL2_image::SDL2_image APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_image::SDL2_image PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "SDL2::SDL2"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libSDL2_image.so"
  IMPORTED_SONAME_RELEASE "libSDL2_image.so"
  )

list(APPEND _cmake_import_check_targets SDL2_image::SDL2_image )
list(APPEND _cmake_import_check_files_for_SDL2_image::SDL2_image "${_IMPORT_PREFIX}/lib/libSDL2_image.so" )

# Import target "SDL2_image::dav1d" for configuration "Release"
set_property(TARGET SDL2_image::dav1d APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_image::dav1d PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdav1d.so"
  IMPORTED_SONAME_RELEASE "libdav1d.so"
  )

list(APPEND _cmake_import_check_targets SDL2_image::dav1d )
list(APPEND _cmake_import_check_files_for_SDL2_image::dav1d "${_IMPORT_PREFIX}/lib/libdav1d.so" )

# Import target "SDL2_image::external_libavif" for configuration "Release"
set_property(TARGET SDL2_image::external_libavif APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_image::external_libavif PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "SDL2_image::dav1d"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libavif.so"
  IMPORTED_SONAME_RELEASE "libavif.so"
  )

list(APPEND _cmake_import_check_targets SDL2_image::external_libavif )
list(APPEND _cmake_import_check_files_for_SDL2_image::external_libavif "${_IMPORT_PREFIX}/lib/libavif.so" )

# Import target "SDL2_image::external_libtiff" for configuration "Release"
set_property(TARGET SDL2_image::external_libtiff APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_image::external_libtiff PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libtiff.so"
  IMPORTED_SONAME_RELEASE "libtiff.so"
  )

list(APPEND _cmake_import_check_targets SDL2_image::external_libtiff )
list(APPEND _cmake_import_check_files_for_SDL2_image::external_libtiff "${_IMPORT_PREFIX}/lib/libtiff.so" )

# Import target "SDL2_image::external_libwebp" for configuration "Release"
set_property(TARGET SDL2_image::external_libwebp APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_image::external_libwebp PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwebp.so"
  IMPORTED_SONAME_RELEASE "libwebp.so"
  )

list(APPEND _cmake_import_check_targets SDL2_image::external_libwebp )
list(APPEND _cmake_import_check_files_for_SDL2_image::external_libwebp "${_IMPORT_PREFIX}/lib/libwebp.so" )

# Import target "SDL2_image::webpdemux" for configuration "Release"
set_property(TARGET SDL2_image::webpdemux APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SDL2_image::webpdemux PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwebpdemux.so"
  IMPORTED_SONAME_RELEASE "libwebpdemux.so"
  )

list(APPEND _cmake_import_check_targets SDL2_image::webpdemux )
list(APPEND _cmake_import_check_files_for_SDL2_image::webpdemux "${_IMPORT_PREFIX}/lib/libwebpdemux.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
