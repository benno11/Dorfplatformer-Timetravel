if(NOT DEFINED PACKAGE_FILE OR PACKAGE_FILE STREQUAL "")
  message(FATAL_ERROR "PACKAGE_FILE is required")
endif()
if(NOT EXISTS "${PACKAGE_FILE}")
  message(FATAL_ERROR "iOS package does not exist: ${PACKAGE_FILE}")
endif()
if(NOT DEFINED APP_NAME OR APP_NAME STREQUAL "")
  set(APP_NAME "platformer")
endif()
if(NOT DEFINED EXPECTED_IOS_SDK)
  set(EXPECTED_IOS_SDK "")
endif()
if(NOT DEFINED EXPECTED_IOS_ARCHS)
  set(EXPECTED_IOS_ARCHS "")
endif()
if(NOT DEFINED IOS_PLUTIL_TOOL)
  set(IOS_PLUTIL_TOOL "")
endif()
if(NOT DEFINED IOS_REQUIRE_CODE_SIGNATURE)
  set(IOS_REQUIRE_CODE_SIGNATURE "")
endif()
if(NOT DEFINED IOS_CODESIGN_TOOL)
  set(IOS_CODESIGN_TOOL "")
endif()
if(NOT DEFINED SOURCE_ROOT_DIR)
  set(SOURCE_ROOT_DIR "")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/NormalizeIosSdk.cmake")
platformer_normalize_ios_sdk(EXPECTED_IOS_SDK EXPECTED_IOS_SDK)
include("${CMAKE_CURRENT_LIST_DIR}/IosRequiredBundleEntries.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/IosBundledProjectFiles.cmake")

file(SIZE "${PACKAGE_FILE}" _package_size)
if(_package_size LESS 1)
  message(FATAL_ERROR "iOS package is empty: ${PACKAGE_FILE}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar "tf" "${PACKAGE_FILE}"
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _package_listing
  ERROR_VARIABLE _list_err
)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR "Could not inspect iOS package ${PACKAGE_FILE}: ${_list_err}")
endif()
set(_package_listing_lines "\n${_package_listing}\n")

function(platformer_package_listing_contains entry out_var)
  string(FIND "${_package_listing_lines}" "\n${entry}\n" _entry_pos)
  if(_entry_pos EQUAL -1)
    string(FIND "${_package_listing_lines}" "\n${entry}/\n" _entry_dir_pos)
    if(_entry_dir_pos EQUAL -1)
      set(${out_var} OFF PARENT_SCOPE)
      return()
    endif()
  endif()
  set(${out_var} ON PARENT_SCOPE)
endfunction()

set(_app_prefix "Payload/${APP_NAME}.app")
string(REGEX MATCHALL "(^|\n)Payload/[^/\n]+\\.app(/|\n|$)" _payload_app_entries "${_package_listing}")
set(_payload_app_names "")
foreach(_payload_app_entry IN LISTS _payload_app_entries)
  string(REGEX REPLACE "(^|\n)(Payload/[^/\n]+\\.app).*" "\\2" _payload_app_name "${_payload_app_entry}")
  list(APPEND _payload_app_names "${_payload_app_name}")
endforeach()
if(_payload_app_names)
  list(REMOVE_DUPLICATES _payload_app_names)
endif()
list(LENGTH _payload_app_names _payload_app_count)
if(NOT _payload_app_count EQUAL 1)
  message(FATAL_ERROR "iOS package ${PACKAGE_FILE} must contain exactly one Payload app bundle; found: ${_payload_app_names}")
endif()
list(GET _payload_app_names 0 _payload_app_name)
if(NOT _payload_app_name STREQUAL "${_app_prefix}")
  message(FATAL_ERROR "iOS package ${PACKAGE_FILE} contains unexpected app bundle ${_payload_app_name}; expected ${_app_prefix}")
endif()
set(_required_package_entries
  "Info.plist"
  "${APP_NAME}"
  "Assets.car"
)
list(APPEND _required_package_entries ${PLATFORMER_IOS_REQUIRED_BUNDLE_ENTRIES})
if(NOT SOURCE_ROOT_DIR STREQUAL "")
  foreach(_project_file IN LISTS PLATFORMER_IOS_BUNDLED_PROJECT_FILES)
    if(EXISTS "${SOURCE_ROOT_DIR}/${_project_file}")
      list(APPEND _required_package_entries "${_project_file}")
    endif()
  endforeach()
endif()
foreach(_required IN LISTS _required_package_entries)
  set(_required_path "${_app_prefix}/${_required}")
  platformer_package_listing_contains("${_required_path}" _required_found)
  if(NOT _required_found)
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} is missing required entry: ${_required_path}")
  endif()
endforeach()

set(_launch_storyboard_package_candidates
  "${_app_prefix}/LaunchScreen.storyboardc"
  "${_app_prefix}/Base.lproj/LaunchScreen.storyboardc"
)
set(_found_launch_storyboard OFF)
foreach(_launch_storyboard IN LISTS _launch_storyboard_package_candidates)
  platformer_package_listing_contains("${_launch_storyboard}" _launch_storyboard_found)
  if(_launch_storyboard_found)
    set(_found_launch_storyboard ON)
  endif()
endforeach()
if(NOT _found_launch_storyboard)
  message(FATAL_ERROR "iOS package ${PACKAGE_FILE} is missing compiled LaunchScreen.storyboardc")
endif()

get_filename_component(_package_dir "${PACKAGE_FILE}" DIRECTORY)
set(_extract_dir "${_package_dir}/_validate-ios-package")
file(REMOVE_RECURSE "${_extract_dir}")
file(MAKE_DIRECTORY "${_extract_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar "xf" "${PACKAGE_FILE}"
  WORKING_DIRECTORY "${_extract_dir}"
  RESULT_VARIABLE _extract_rc
  ERROR_VARIABLE _extract_err
)
if(NOT _extract_rc EQUAL 0)
  file(REMOVE_RECURSE "${_extract_dir}")
  message(FATAL_ERROR "Could not extract iOS package ${PACKAGE_FILE}: ${_extract_err}")
endif()

set(_info_plist "${_extract_dir}/${_app_prefix}/Info.plist")
file(READ "${_info_plist}" _info_plist_text)
if(NOT IOS_PLUTIL_TOOL STREQUAL "")
  set(_plutil_tool "${IOS_PLUTIL_TOOL}")
else()
  find_program(_plutil_tool plutil)
endif()
if(_plutil_tool)
  execute_process(
    COMMAND "${_plutil_tool}" -lint "${_info_plist}"
    RESULT_VARIABLE _plutil_rc
    OUTPUT_VARIABLE _plutil_out
    ERROR_VARIABLE _plutil_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT _plutil_rc EQUAL 0)
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} Info.plist failed plutil validation: ${_plutil_out} ${_plutil_err}")
  endif()
  message(STATUS "Validated iOS package Info.plist syntax")
endif()
string(FIND "${_info_plist_text}" "\${" _unresolved_plist_pos)
if(NOT _unresolved_plist_pos EQUAL -1)
  file(REMOVE_RECURSE "${_extract_dir}")
  message(FATAL_ERROR "iOS package ${PACKAGE_FILE} Info.plist contains unresolved template variables")
endif()
foreach(_plist_fragment IN ITEMS
    "<key>CFBundleExecutable</key>"
    "<string>${APP_NAME}</string>"
    "<key>CFBundleIdentifier</key>"
    "<key>CFBundleIcons</key>"
    "<key>CFBundleIcons~ipad</key>"
    "<key>CFBundleIconName</key>"
    "<string>AppIcon</string>"
    "<key>CFBundlePackageType</key>"
    "<string>APPL</string>"
    "<key>CFBundleShortVersionString</key>"
    "<key>CFBundleVersion</key>"
    "<key>LSRequiresIPhoneOS</key>"
    "<key>LSSupportsOpeningDocumentsInPlace</key>"
    "<key>MinimumOSVersion</key>"
    "<key>NSAppTransportSecurity</key>"
    "<key>NSAllowsArbitraryLoads</key>"
    "<key>UILaunchStoryboardName</key>"
    "<string>LaunchScreen</string>"
    "<key>UIFileSharingEnabled</key>"
    "<key>UIRequiresFullScreen</key>"
    "<key>UIApplicationSupportsIndirectInputEvents</key>"
    "<key>UIRequiredDeviceCapabilities</key>"
    "<string>arm64</string>"
    "<key>UISupportedInterfaceOrientations</key>")
  string(FIND "${_info_plist_text}" "${_plist_fragment}" _plist_fragment_pos)
  if(_plist_fragment_pos EQUAL -1)
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} Info.plist is missing: ${_plist_fragment}")
  endif()
endforeach()
foreach(_plist_match IN ITEMS
    "<key>CFBundleExecutable</key>[ \r\n\t]*<string>${APP_NAME}</string>"
    "<key>CFBundleIdentifier</key>[ \r\n\t]*<string>[^<]+</string>"
    "<key>CFBundleIconName</key>[ \r\n\t]*<string>AppIcon</string>"
    "<key>LSRequiresIPhoneOS</key>[ \r\n\t]*<true/>"
    "<key>MinimumOSVersion</key>[ \r\n\t]*<string>[^<]+</string>"
    "<key>NSAllowsArbitraryLoads</key>[ \r\n\t]*<true/>"
    "<key>UILaunchStoryboardName</key>[ \r\n\t]*<string>LaunchScreen</string>"
    "<key>UIFileSharingEnabled</key>[ \r\n\t]*<true/>"
    "<key>LSSupportsOpeningDocumentsInPlace</key>[ \r\n\t]*<true/>"
    "<key>UIRequiresFullScreen</key>[ \r\n\t]*<true/>"
    "<key>UIApplicationSupportsIndirectInputEvents</key>[ \r\n\t]*<true/>"
    "<string>UIInterfaceOrientationLandscapeLeft</string>"
    "<string>UIInterfaceOrientationLandscapeRight</string>")
  string(REGEX MATCH "${_plist_match}" _plist_match_result "${_info_plist_text}")
  if(_plist_match_result STREQUAL "")
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} Info.plist is missing required value matching: ${_plist_match}")
  endif()
endforeach()
foreach(_disallowed_plist_fragment IN ITEMS
    "<string>UIInterfaceOrientationPortrait</string>"
    "<string>UIInterfaceOrientationPortraitUpsideDown</string>")
  string(FIND "${_info_plist_text}" "${_disallowed_plist_fragment}" _disallowed_plist_pos)
  if(NOT _disallowed_plist_pos EQUAL -1)
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} Info.plist contains disallowed portrait orientation: ${_disallowed_plist_fragment}")
  endif()
endforeach()

set(_executable_path "${_extract_dir}/${_app_prefix}/${APP_NAME}")
file(SIZE "${_executable_path}" _executable_size)
if(_executable_size LESS 1)
  file(REMOVE_RECURSE "${_extract_dir}")
  message(FATAL_ERROR "iOS package ${PACKAGE_FILE} executable is empty: ${_app_prefix}/${APP_NAME}")
endif()
set(_assets_car_path "${_extract_dir}/${_app_prefix}/Assets.car")
file(SIZE "${_assets_car_path}" _assets_car_size)
if(_assets_car_size LESS 1)
  file(REMOVE_RECURSE "${_extract_dir}")
  message(FATAL_ERROR "iOS package ${PACKAGE_FILE} compiled asset catalog is empty: ${_app_prefix}/Assets.car")
endif()

if(NOT EXPECTED_IOS_ARCHS STREQUAL "")
  if(DEFINED IOS_LIPO_TOOL AND NOT IOS_LIPO_TOOL STREQUAL "")
    set(_lipo_tool "${IOS_LIPO_TOOL}")
  else()
    find_program(_lipo_tool lipo)
  endif()
  if(_lipo_tool)
    execute_process(
      COMMAND "${_lipo_tool}" -archs "${_executable_path}"
      RESULT_VARIABLE _lipo_rc
      OUTPUT_VARIABLE _lipo_out
      ERROR_VARIABLE _lipo_err
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _lipo_rc EQUAL 0)
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "Could not inspect iOS package executable architectures: ${_lipo_err}")
    endif()
    string(REPLACE ";" " " _expected_archs_text "${EXPECTED_IOS_ARCHS}")
    separate_arguments(_expected_archs_list NATIVE_COMMAND "${_expected_archs_text}")
    foreach(_expected_arch IN LISTS _expected_archs_list)
      string(FIND " ${_lipo_out} " " ${_expected_arch} " _arch_pos)
      if(_arch_pos EQUAL -1)
        file(REMOVE_RECURSE "${_extract_dir}")
        message(FATAL_ERROR "iOS package ${PACKAGE_FILE} executable missing expected architecture ${_expected_arch}; found: ${_lipo_out}")
      endif()
    endforeach()
    message(STATUS "Validated iOS executable architectures: ${_lipo_out}")
  endif()
endif()

if(NOT EXPECTED_IOS_SDK STREQUAL "")
  if(DEFINED IOS_VTOOL_TOOL AND NOT IOS_VTOOL_TOOL STREQUAL "")
    set(_vtool_tool "${IOS_VTOOL_TOOL}")
  else()
    find_program(_vtool_tool vtool)
  endif()
  if(_vtool_tool)
    execute_process(
      COMMAND "${_vtool_tool}" -show-build "${_executable_path}"
      RESULT_VARIABLE _vtool_rc
      OUTPUT_VARIABLE _vtool_out
      ERROR_VARIABLE _vtool_err
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(_vtool_rc EQUAL 0)
      if(EXPECTED_IOS_SDK STREQUAL "iphoneos")
        set(_expected_vtool_platform "IOS")
      elseif(EXPECTED_IOS_SDK STREQUAL "iphonesimulator")
        set(_expected_vtool_platform "IOSSIMULATOR")
      else()
        set(_expected_vtool_platform "")
      endif()
      if(NOT _expected_vtool_platform STREQUAL "")
        string(REGEX MATCHALL "platform[ \t]+([A-Za-z0-9_]+)" _vtool_platform_lines "${_vtool_out}")
        set(_platform_match OFF)
        set(_vtool_platforms "")
        foreach(_vtool_platform_line IN LISTS _vtool_platform_lines)
          string(REGEX REPLACE ".*platform[ \t]+([A-Za-z0-9_]+).*" "\\1" _vtool_platform "${_vtool_platform_line}")
          list(APPEND _vtool_platforms "${_vtool_platform}")
          if(_vtool_platform STREQUAL _expected_vtool_platform)
            set(_platform_match ON)
          endif()
        endforeach()
        if(NOT _platform_match)
          file(REMOVE_RECURSE "${_extract_dir}")
          message(FATAL_ERROR "iOS package ${PACKAGE_FILE} executable does not match expected SDK ${EXPECTED_IOS_SDK}; found platforms: ${_vtool_platforms}")
        endif()
      endif()
      message(STATUS "Validated iOS executable SDK platform: ${EXPECTED_IOS_SDK}")
    else()
      message(STATUS "Skipping iOS executable SDK platform validation; vtool failed: ${_vtool_err}")
    endif()
  endif()
endif()

if(IOS_REQUIRE_CODE_SIGNATURE)
  if(NOT IOS_CODESIGN_TOOL STREQUAL "")
    set(_codesign_tool "${IOS_CODESIGN_TOOL}")
  else()
    find_program(_codesign_tool codesign)
  endif()
  if(NOT _codesign_tool)
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} requires code signature validation, but codesign was not found")
  endif()
  execute_process(
    COMMAND "${_codesign_tool}" --verify --deep --strict --verbose=2 "${_extract_dir}/${_app_prefix}"
    RESULT_VARIABLE _codesign_rc
    OUTPUT_VARIABLE _codesign_out
    ERROR_VARIABLE _codesign_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT _codesign_rc EQUAL 0)
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} app bundle failed code signature validation: ${_codesign_out} ${_codesign_err}")
  endif()
  message(STATUS "Validated iOS app bundle code signature")
endif()

if(NOT SOURCE_ROOT_DIR STREQUAL "")
  if(NOT EXISTS "${SOURCE_ROOT_DIR}")
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "SOURCE_ROOT_DIR does not exist: ${SOURCE_ROOT_DIR}")
  endif()
  foreach(_required IN LISTS PLATFORMER_IOS_REQUIRED_BUNDLE_ENTRIES)
    if(NOT EXISTS "${SOURCE_ROOT_DIR}/${_required}")
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "Required iOS source bundle entry is missing from source tree: ${_required}")
    endif()
  endforeach()
  foreach(_project_file IN LISTS PLATFORMER_IOS_BUNDLED_PROJECT_FILES)
    set(_source_project_file "${SOURCE_ROOT_DIR}/${_project_file}")
    if(EXISTS "${_source_project_file}")
      set(_packaged_project_file "${_extract_dir}/${_app_prefix}/${_project_file}")
      if(NOT EXISTS "${_packaged_project_file}")
        file(REMOVE_RECURSE "${_extract_dir}")
        message(FATAL_ERROR "iOS package ${PACKAGE_FILE} is missing bundled project file: ${_app_prefix}/${_project_file}")
      endif()
      file(SIZE "${_source_project_file}" _source_project_file_size)
      file(SIZE "${_packaged_project_file}" _packaged_project_file_size)
      if(NOT _source_project_file_size EQUAL _packaged_project_file_size)
        file(REMOVE_RECURSE "${_extract_dir}")
        message(FATAL_ERROR "iOS package ${PACKAGE_FILE} bundled project file size mismatch for ${_app_prefix}/${_project_file}: source=${_source_project_file_size}, packaged=${_packaged_project_file_size}")
      endif()
    endif()
  endforeach()
endif()

if(DEFINED SOURCE_ASSETS_DIR AND NOT SOURCE_ASSETS_DIR STREQUAL "")
  if(NOT EXISTS "${SOURCE_ASSETS_DIR}")
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "SOURCE_ASSETS_DIR does not exist: ${SOURCE_ASSETS_DIR}")
  endif()
  file(GLOB_RECURSE _source_asset_files
    LIST_DIRECTORIES false
    RELATIVE "${SOURCE_ASSETS_DIR}"
    "${SOURCE_ASSETS_DIR}/*"
  )
  set(_source_asset_count 0)
  foreach(_source_asset IN LISTS _source_asset_files)
    string(REPLACE "\\" "/" _source_asset_rel "${_source_asset}")
    set(_source_asset_path "${SOURCE_ASSETS_DIR}/${_source_asset_rel}")
    set(_packaged_asset_path "${_extract_dir}/${_app_prefix}/assets/${_source_asset_rel}")
    if(NOT EXISTS "${_packaged_asset_path}")
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "iOS package ${PACKAGE_FILE} is missing bundled source asset: ${_app_prefix}/assets/${_source_asset_rel}")
    endif()
    file(SIZE "${_source_asset_path}" _source_asset_size)
    file(SIZE "${_packaged_asset_path}" _packaged_asset_size)
    if(NOT _source_asset_size EQUAL _packaged_asset_size)
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "iOS package ${PACKAGE_FILE} bundled source asset size mismatch for ${_app_prefix}/assets/${_source_asset_rel}: source=${_source_asset_size}, packaged=${_packaged_asset_size}")
    endif()
    math(EXPR _source_asset_count "${_source_asset_count} + 1")
  endforeach()
  message(STATUS "Validated iOS package assets: ${_source_asset_count} files")
endif()

function(platformer_validate_level_manifest manifest_asset_path base_asset_dir)
  set(_manifest_path "${_extract_dir}/${_app_prefix}/${manifest_asset_path}")
  if(NOT EXISTS "${_manifest_path}")
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} is missing level manifest: ${_app_prefix}/${manifest_asset_path}")
  endif()
  file(READ "${_manifest_path}" _level_manifest_text)
  string(JSON _levels_type ERROR_VARIABLE _levels_type_error TYPE "${_level_manifest_text}" levels)
  if(_levels_type_error OR NOT _levels_type STREQUAL "ARRAY")
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} level manifest ${manifest_asset_path} must contain a levels array")
  endif()
  string(JSON _level_count LENGTH "${_level_manifest_text}" levels)
  if(_level_count LESS 1)
    message(STATUS "Validated iOS level manifest ${manifest_asset_path}: 0 entries")
    return()
  endif()
  math(EXPR _last_level_index "${_level_count} - 1")
  foreach(_level_index RANGE 0 ${_last_level_index})
    string(JSON _level_type TYPE "${_level_manifest_text}" levels ${_level_index})
    if(NOT _level_type STREQUAL "STRING")
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "iOS package ${PACKAGE_FILE} level manifest ${manifest_asset_path} entry ${_level_index} must be a string")
    endif()
    string(JSON _level_entry GET "${_level_manifest_text}" levels ${_level_index})
    if(_level_entry STREQUAL "" OR _level_entry MATCHES "^/" OR _level_entry MATCHES "^[A-Za-z]:")
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "iOS package ${PACKAGE_FILE} level manifest ${manifest_asset_path} contains invalid level path: ${_level_entry}")
    endif()
    set(_packaged_level "${_extract_dir}/${_app_prefix}/${base_asset_dir}/${_level_entry}")
    if(NOT EXISTS "${_packaged_level}")
      set(_resolved_packaged_level "")
      string(REGEX MATCH "\\.bnnlvl$" _is_bnn_level "${_level_entry}")
      if(_is_bnn_level)
        string(REGEX REPLACE "\\.bnnlvl$" ".txt" _level_txt_entry "${_level_entry}")
        set(_packaged_level_txt "${_extract_dir}/${_app_prefix}/${base_asset_dir}/${_level_txt_entry}")
        if(EXISTS "${_packaged_level_txt}")
          set(_resolved_packaged_level "${_packaged_level_txt}")
        endif()
      endif()
      if(_resolved_packaged_level STREQUAL "")
        file(REMOVE_RECURSE "${_extract_dir}")
        message(FATAL_ERROR "iOS package ${PACKAGE_FILE} level manifest ${manifest_asset_path} references missing bundled level: ${base_asset_dir}/${_level_entry}")
      endif()
    endif()
  endforeach()
  message(STATUS "Validated iOS level manifest ${manifest_asset_path}: ${_level_count} entries")
endfunction()

platformer_validate_level_manifest("assets/levels/levels.json" "assets/levels")
platformer_validate_level_manifest("assets/custom_levels/levels.json" "assets/custom_levels")

function(platformer_validate_texture_manifest_object manifest_text object_key)
  string(JSON _object_type ERROR_VARIABLE _object_type_error TYPE "${manifest_text}" ${object_key})
  if(_object_type_error)
    message(STATUS "iOS texture manifest has no ${object_key} object")
    return()
  endif()
  if(NOT _object_type STREQUAL "OBJECT")
    file(REMOVE_RECURSE "${_extract_dir}")
    message(FATAL_ERROR "iOS package ${PACKAGE_FILE} texture manifest ${object_key} must be an object")
  endif()
  string(JSON _object_text GET "${manifest_text}" ${object_key})
  string(JSON _object_member_count LENGTH "${_object_text}")
  set(_validated_texture_manifest_entries 0)
  if(_object_member_count LESS 1)
    message(STATUS "Validated iOS texture manifest ${object_key}: 0 entries")
    return()
  endif()
  math(EXPR _last_object_member_index "${_object_member_count} - 1")
  foreach(_object_member_index RANGE 0 ${_last_object_member_index})
    string(JSON _member MEMBER "${_object_text}" ${_object_member_index})
    string(JSON _asset_type TYPE "${_object_text}" ${_member})
    if(NOT _asset_type STREQUAL "STRING")
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "iOS package ${PACKAGE_FILE} texture manifest ${object_key}.${_member} must be a string")
    endif()
    string(JSON _asset_entry GET "${_object_text}" ${_member})
    if(_asset_entry STREQUAL "" OR _asset_entry MATCHES "^/" OR _asset_entry MATCHES "^[A-Za-z]:")
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "iOS package ${PACKAGE_FILE} texture manifest ${object_key}.${_member} contains invalid path: ${_asset_entry}")
    endif()
    set(_packaged_asset_entry "${_extract_dir}/${_app_prefix}/${_asset_entry}")
    if(NOT EXISTS "${_packaged_asset_entry}")
      file(REMOVE_RECURSE "${_extract_dir}")
      message(FATAL_ERROR "iOS package ${PACKAGE_FILE} texture manifest ${object_key}.${_member} references missing bundled asset: ${_asset_entry}")
    endif()
    math(EXPR _validated_texture_manifest_entries "${_validated_texture_manifest_entries} + 1")
  endforeach()
  message(STATUS "Validated iOS texture manifest ${object_key}: ${_validated_texture_manifest_entries} entries")
endfunction()

set(_texture_manifest_path "${_extract_dir}/${_app_prefix}/assets/textures.json")
if(NOT EXISTS "${_texture_manifest_path}")
  file(REMOVE_RECURSE "${_extract_dir}")
  message(FATAL_ERROR "iOS package ${PACKAGE_FILE} is missing texture manifest: ${_app_prefix}/assets/textures.json")
endif()
file(READ "${_texture_manifest_path}" _texture_manifest_text)
platformer_validate_texture_manifest_object("${_texture_manifest_text}" "textures")
platformer_validate_texture_manifest_object("${_texture_manifest_text}" "plists")

file(REMOVE_RECURSE "${_extract_dir}")
message(STATUS "Validated iOS package: ${PACKAGE_FILE}")
