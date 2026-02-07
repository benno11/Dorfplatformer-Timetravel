#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "WebP::cpufeatures-libwebp" for configuration "Release"
set_property(TARGET WebP::cpufeatures-libwebp APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(WebP::cpufeatures-libwebp PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libcpufeatures-libwebp.a"
  )

list(APPEND _cmake_import_check_targets WebP::cpufeatures-libwebp )
list(APPEND _cmake_import_check_files_for_WebP::cpufeatures-libwebp "${_IMPORT_PREFIX}/lib/libcpufeatures-libwebp.a" )

# Import target "WebP::webpdecoder" for configuration "Release"
set_property(TARGET WebP::webpdecoder APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(WebP::webpdecoder PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwebpdecoder.so"
  IMPORTED_SONAME_RELEASE "libwebpdecoder.so"
  )

list(APPEND _cmake_import_check_targets WebP::webpdecoder )
list(APPEND _cmake_import_check_files_for_WebP::webpdecoder "${_IMPORT_PREFIX}/lib/libwebpdecoder.so" )

# Import target "WebP::external_libwebp" for configuration "Release"
set_property(TARGET WebP::external_libwebp APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(WebP::external_libwebp PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwebp.so"
  IMPORTED_SONAME_RELEASE "libwebp.so"
  )

list(APPEND _cmake_import_check_targets WebP::external_libwebp )
list(APPEND _cmake_import_check_files_for_WebP::external_libwebp "${_IMPORT_PREFIX}/lib/libwebp.so" )

# Import target "WebP::webpdemux" for configuration "Release"
set_property(TARGET WebP::webpdemux APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(WebP::webpdemux PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwebpdemux.so"
  IMPORTED_SONAME_RELEASE "libwebpdemux.so"
  )

list(APPEND _cmake_import_check_targets WebP::webpdemux )
list(APPEND _cmake_import_check_files_for_WebP::webpdemux "${_IMPORT_PREFIX}/lib/libwebpdemux.so" )

# Import target "WebP::libwebpmux" for configuration "Release"
set_property(TARGET WebP::libwebpmux APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(WebP::libwebpmux PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libwebpmux.so"
  IMPORTED_SONAME_RELEASE "libwebpmux.so"
  )

list(APPEND _cmake_import_check_targets WebP::libwebpmux )
list(APPEND _cmake_import_check_files_for_WebP::libwebpmux "${_IMPORT_PREFIX}/lib/libwebpmux.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
