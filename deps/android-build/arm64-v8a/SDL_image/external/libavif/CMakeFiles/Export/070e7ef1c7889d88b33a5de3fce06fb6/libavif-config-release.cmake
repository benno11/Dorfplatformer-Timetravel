#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "external_libavif" for configuration "Release"
set_property(TARGET external_libavif APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(external_libavif PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "SDL2_image::dav1d"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libavif.so"
  IMPORTED_SONAME_RELEASE "libavif.so"
  )

list(APPEND _cmake_import_check_targets external_libavif )
list(APPEND _cmake_import_check_files_for_external_libavif "${_IMPORT_PREFIX}/lib/libavif.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
