if(NOT DEFINED APP_BUNDLE OR APP_BUNDLE STREQUAL "")
  message(FATAL_ERROR "APP_BUNDLE is required")
endif()
if(NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
  message(FATAL_ERROR "OUTPUT_DIR is required")
endif()
if(NOT DEFINED APP_NAME OR APP_NAME STREQUAL "")
  set(APP_NAME "platformer")
endif()
if(NOT DEFINED IOS_SDK)
  set(IOS_SDK "")
endif()
if(NOT DEFINED IOS_ARCHS)
  set(IOS_ARCHS "")
endif()
if(NOT DEFINED IOS_LIPO_TOOL)
  set(IOS_LIPO_TOOL "")
endif()
if(NOT DEFINED IOS_VTOOL_TOOL)
  set(IOS_VTOOL_TOOL "")
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
if(IOS_REQUIRE_CODE_SIGNATURE)
  set(_ios_code_signing_required_json "true")
else()
  set(_ios_code_signing_required_json "false")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/NormalizeIosSdk.cmake")
platformer_normalize_ios_sdk(IOS_SDK IOS_SDK)
if(IOS_SDK STREQUAL "iphoneos")
  set(_ios_device_artifact_json "true")
else()
  set(_ios_device_artifact_json "false")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/IosRequiredBundleEntries.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/IosBundledProjectFiles.cmake")
list(LENGTH PLATFORMER_IOS_REQUIRED_BUNDLE_ENTRIES _required_bundle_entry_count)
if(NOT EXISTS "${APP_BUNDLE}")
  message(FATAL_ERROR "APP_BUNDLE does not exist: ${APP_BUNDLE}")
endif()
if(NOT EXISTS "${APP_BUNDLE}/Info.plist")
  message(FATAL_ERROR "APP_BUNDLE is missing Info.plist: ${APP_BUNDLE}")
endif()
if(NOT EXISTS "${APP_BUNDLE}/${APP_NAME}")
  message(FATAL_ERROR "APP_BUNDLE is missing executable ${APP_NAME}: ${APP_BUNDLE}")
endif()
file(SIZE "${APP_BUNDLE}/${APP_NAME}" _app_executable_size)
if(_app_executable_size LESS 1)
  message(FATAL_ERROR "APP_BUNDLE executable is empty: ${APP_BUNDLE}/${APP_NAME}")
endif()
set(_launch_storyboard_candidates
  "LaunchScreen.storyboardc"
  "Base.lproj/LaunchScreen.storyboardc"
)
set(_found_launch_storyboard OFF)
foreach(_launch_storyboard IN LISTS _launch_storyboard_candidates)
  if(EXISTS "${APP_BUNDLE}/${_launch_storyboard}")
    set(_found_launch_storyboard ON)
  endif()
endforeach()
if(NOT _found_launch_storyboard)
  message(FATAL_ERROR "APP_BUNDLE is missing compiled LaunchScreen.storyboardc: ${APP_BUNDLE}")
endif()
if(NOT EXISTS "${APP_BUNDLE}/Assets.car")
  message(FATAL_ERROR "APP_BUNDLE is missing compiled asset catalog Assets.car: ${APP_BUNDLE}")
endif()
file(SIZE "${APP_BUNDLE}/Assets.car" _assets_car_size)
if(_assets_car_size LESS 1)
  message(FATAL_ERROR "APP_BUNDLE compiled asset catalog is empty: ${APP_BUNDLE}/Assets.car")
endif()

file(READ "${APP_BUNDLE}/Info.plist" _app_info_plist_text)
if(NOT IOS_PLUTIL_TOOL STREQUAL "")
  set(_plutil_tool "${IOS_PLUTIL_TOOL}")
else()
  find_program(_plutil_tool plutil)
endif()
if(_plutil_tool)
  execute_process(
    COMMAND "${_plutil_tool}" -lint "${APP_BUNDLE}/Info.plist"
    RESULT_VARIABLE _plutil_rc
    OUTPUT_VARIABLE _plutil_out
    ERROR_VARIABLE _plutil_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT _plutil_rc EQUAL 0)
    message(FATAL_ERROR "APP_BUNDLE Info.plist failed plutil validation: ${_plutil_out} ${_plutil_err}")
  endif()
  message(STATUS "Validated APP_BUNDLE Info.plist syntax")
endif()
string(FIND "${_app_info_plist_text}" "\${" _unresolved_plist_pos)
if(NOT _unresolved_plist_pos EQUAL -1)
  message(FATAL_ERROR "APP_BUNDLE Info.plist contains unresolved template variables: ${APP_BUNDLE}/Info.plist")
endif()
foreach(_plist_match IN ITEMS
    "<key>CFBundleExecutable</key>[ \r\n\t]*<string>${APP_NAME}</string>"
    "<key>CFBundleIdentifier</key>[ \r\n\t]*<string>[^<]+</string>"
    "<key>CFBundleIconName</key>[ \r\n\t]*<string>AppIcon</string>"
    "<key>CFBundleIcons</key>"
    "<key>CFBundleIcons~ipad</key>"
    "<key>LSRequiresIPhoneOS</key>[ \r\n\t]*<true/>"
    "<key>MinimumOSVersion</key>[ \r\n\t]*<string>[^<]+</string>"
    "<key>NSAllowsArbitraryLoads</key>[ \r\n\t]*<true/>"
    "<key>UILaunchStoryboardName</key>[ \r\n\t]*<string>LaunchScreen</string>"
    "<key>UIRequiresFullScreen</key>[ \r\n\t]*<true/>"
    "<key>UIFileSharingEnabled</key>[ \r\n\t]*<true/>"
    "<key>LSSupportsOpeningDocumentsInPlace</key>[ \r\n\t]*<true/>"
    "<key>UIApplicationSupportsIndirectInputEvents</key>[ \r\n\t]*<true/>"
    "<string>arm64</string>"
    "<string>UIInterfaceOrientationLandscapeLeft</string>"
    "<string>UIInterfaceOrientationLandscapeRight</string>")
  string(REGEX MATCH "${_plist_match}" _plist_match_result "${_app_info_plist_text}")
  if(_plist_match_result STREQUAL "")
    message(FATAL_ERROR "APP_BUNDLE Info.plist is missing required value matching: ${_plist_match}")
  endif()
endforeach()
foreach(_disallowed_plist_fragment IN ITEMS
    "<string>UIInterfaceOrientationPortrait</string>"
    "<string>UIInterfaceOrientationPortraitUpsideDown</string>")
  string(FIND "${_app_info_plist_text}" "${_disallowed_plist_fragment}" _disallowed_plist_pos)
  if(NOT _disallowed_plist_pos EQUAL -1)
    message(FATAL_ERROR "APP_BUNDLE Info.plist contains disallowed portrait orientation: ${_disallowed_plist_fragment}")
  endif()
endforeach()
function(platformer_plist_string_value plist_text key out_var)
  string(REGEX MATCH "<key>${key}</key>[ \r\n\t]*<string>([^<]+)</string>" _plist_value_match "${plist_text}")
  if(CMAKE_MATCH_1)
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

platformer_plist_string_value("${_app_info_plist_text}" "CFBundleIdentifier" _app_bundle_identifier)
platformer_plist_string_value("${_app_info_plist_text}" "CFBundleShortVersionString" _app_bundle_short_version)
platformer_plist_string_value("${_app_info_plist_text}" "CFBundleVersion" _app_bundle_version)
platformer_plist_string_value("${_app_info_plist_text}" "MinimumOSVersion" _app_minimum_os_version)
foreach(_bundle_metadata IN ITEMS
    _app_bundle_identifier
    _app_bundle_short_version
    _app_bundle_version
    _app_minimum_os_version)
  if("${${_bundle_metadata}}" STREQUAL "")
    message(FATAL_ERROR "APP_BUNDLE Info.plist is missing required manifest metadata: ${_bundle_metadata}")
  endif()
endforeach()

foreach(_required IN LISTS PLATFORMER_IOS_REQUIRED_BUNDLE_ENTRIES)
  if(NOT EXISTS "${APP_BUNDLE}/${_required}")
    message(FATAL_ERROR "APP_BUNDLE is missing required runtime asset ${_required}: ${APP_BUNDLE}")
  endif()
endforeach()

if(NOT SOURCE_ROOT_DIR STREQUAL "")
  if(NOT EXISTS "${SOURCE_ROOT_DIR}")
    message(FATAL_ERROR "SOURCE_ROOT_DIR does not exist: ${SOURCE_ROOT_DIR}")
  endif()
  foreach(_required IN LISTS PLATFORMER_IOS_REQUIRED_BUNDLE_ENTRIES)
    if(NOT EXISTS "${SOURCE_ROOT_DIR}/${_required}")
      message(FATAL_ERROR "Required iOS source bundle entry is missing from source tree: ${_required}")
    endif()
  endforeach()
  foreach(_project_file IN LISTS PLATFORMER_IOS_BUNDLED_PROJECT_FILES)
    set(_source_project_file "${SOURCE_ROOT_DIR}/${_project_file}")
    if(EXISTS "${_source_project_file}")
      set(_bundled_project_file "${APP_BUNDLE}/${_project_file}")
      if(NOT EXISTS "${_bundled_project_file}")
        message(FATAL_ERROR "APP_BUNDLE is missing bundled project file ${_project_file}: ${APP_BUNDLE}")
      endif()
      file(SIZE "${_source_project_file}" _source_project_file_size)
      file(SIZE "${_bundled_project_file}" _bundled_project_file_size)
      if(NOT _source_project_file_size EQUAL _bundled_project_file_size)
        message(FATAL_ERROR "APP_BUNDLE bundled project file size mismatch for ${_project_file}: source=${_source_project_file_size}, bundled=${_bundled_project_file_size}")
      endif()
    endif()
  endforeach()
endif()

set(_source_asset_count 0)
if(DEFINED SOURCE_ASSETS_DIR AND NOT SOURCE_ASSETS_DIR STREQUAL "")
  if(NOT EXISTS "${SOURCE_ASSETS_DIR}")
    message(FATAL_ERROR "SOURCE_ASSETS_DIR does not exist: ${SOURCE_ASSETS_DIR}")
  endif()
  file(GLOB_RECURSE _source_asset_files
    LIST_DIRECTORIES false
    RELATIVE "${SOURCE_ASSETS_DIR}"
    "${SOURCE_ASSETS_DIR}/*"
  )
  foreach(_source_asset IN LISTS _source_asset_files)
    string(REPLACE "\\" "/" _source_asset_rel "${_source_asset}")
    set(_source_asset_path "${SOURCE_ASSETS_DIR}/${_source_asset_rel}")
    set(_bundled_asset_path "${APP_BUNDLE}/assets/${_source_asset_rel}")
    if(NOT EXISTS "${_bundled_asset_path}")
      message(FATAL_ERROR "APP_BUNDLE is missing bundled source asset assets/${_source_asset_rel}: ${APP_BUNDLE}")
    endif()
    file(SIZE "${_source_asset_path}" _source_asset_size)
    file(SIZE "${_bundled_asset_path}" _bundled_asset_size)
    if(NOT _source_asset_size EQUAL _bundled_asset_size)
      message(FATAL_ERROR "APP_BUNDLE bundled source asset size mismatch for assets/${_source_asset_rel}: source=${_source_asset_size}, bundled=${_bundled_asset_size}")
    endif()
    math(EXPR _source_asset_count "${_source_asset_count} + 1")
  endforeach()
  message(STATUS "Validated bundled iOS assets: ${_source_asset_count} files")
endif()

set(_bundled_project_file_count 0)
if(NOT SOURCE_ROOT_DIR STREQUAL "")
  foreach(_project_file IN LISTS PLATFORMER_IOS_BUNDLED_PROJECT_FILES)
    if(EXISTS "${SOURCE_ROOT_DIR}/${_project_file}")
      math(EXPR _bundled_project_file_count "${_bundled_project_file_count} + 1")
    endif()
  endforeach()
endif()

set(STAGING_DIR "${OUTPUT_DIR}/_ios-package")
set(PAYLOAD_DIR "${STAGING_DIR}/Payload")
set(IPA_FILE "${OUTPUT_DIR}/${APP_NAME}.ipa")
set(IPSW_FILE "${OUTPUT_DIR}/${APP_NAME}.ipsw")
set(MANIFEST_FILE "${OUTPUT_DIR}/${APP_NAME}-ios-package-manifest.json")
set(CHECKSUM_FILE "${OUTPUT_DIR}/${APP_NAME}-ios-package-sha256.txt")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(REMOVE_RECURSE "${STAGING_DIR}")
file(MAKE_DIRECTORY "${PAYLOAD_DIR}")
file(COPY "${APP_BUNDLE}" DESTINATION "${PAYLOAD_DIR}")

file(REMOVE "${IPA_FILE}" "${IPSW_FILE}" "${CHECKSUM_FILE}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar "cf" "${IPA_FILE}" --format=zip "Payload"
  WORKING_DIRECTORY "${STAGING_DIR}"
  RESULT_VARIABLE _ipa_rc
)
if(NOT _ipa_rc EQUAL 0)
  message(FATAL_ERROR "Failed to create ${IPA_FILE}")
endif()

# IPSW files are normally Apple firmware images, not app packages. This copies
# the installable app archive to the requested extension for tooling that
# explicitly expects an .ipsw artifact from this project.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${IPA_FILE}" "${IPSW_FILE}"
  RESULT_VARIABLE _ipsw_rc
)
if(NOT _ipsw_rc EQUAL 0)
  message(FATAL_ERROR "Failed to create ${IPSW_FILE}")
endif()
file(SHA256 "${IPA_FILE}" _ipa_sha256)
file(SHA256 "${IPSW_FILE}" _ipsw_sha256)
if(NOT _ipa_sha256 STREQUAL _ipsw_sha256)
  message(FATAL_ERROR "Generated IPA/IPSW artifacts differ unexpectedly")
endif()
file(SIZE "${IPA_FILE}" _ipa_size)
file(SIZE "${IPSW_FILE}" _ipsw_size)
file(WRITE "${CHECKSUM_FILE}" "${_ipa_sha256}  ${APP_NAME}.ipa\n")
file(APPEND "${CHECKSUM_FILE}" "${_ipsw_sha256}  ${APP_NAME}.ipsw\n")

file(REMOVE_RECURSE "${STAGING_DIR}")

foreach(_package IN ITEMS "${IPA_FILE}" "${IPSW_FILE}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DPACKAGE_FILE=${_package}"
      "-DAPP_NAME=${APP_NAME}"
      "-DSOURCE_ASSETS_DIR=${SOURCE_ASSETS_DIR}"
      "-DSOURCE_ROOT_DIR=${SOURCE_ROOT_DIR}"
      "-DEXPECTED_IOS_SDK=${IOS_SDK}"
      "-DEXPECTED_IOS_ARCHS=${IOS_ARCHS}"
      "-DIOS_LIPO_TOOL=${IOS_LIPO_TOOL}"
      "-DIOS_VTOOL_TOOL=${IOS_VTOOL_TOOL}"
      "-DIOS_PLUTIL_TOOL=${IOS_PLUTIL_TOOL}"
      "-DIOS_REQUIRE_CODE_SIGNATURE=${IOS_REQUIRE_CODE_SIGNATURE}"
      "-DIOS_CODESIGN_TOOL=${IOS_CODESIGN_TOOL}"
      -P "${CMAKE_CURRENT_LIST_DIR}/ValidateIosPackage.cmake"
    RESULT_VARIABLE _validate_rc
  )
  if(NOT _validate_rc EQUAL 0)
    message(FATAL_ERROR "Generated iOS package failed validation: ${_package}")
  endif()
endforeach()

file(WRITE "${MANIFEST_FILE}" "{\n")
file(APPEND "${MANIFEST_FILE}" "  \"app_name\": \"${APP_NAME}\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"sdk\": \"${IOS_SDK}\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"archs\": \"${IOS_ARCHS}\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"device_artifact\": ${_ios_device_artifact_json},\n")
file(APPEND "${MANIFEST_FILE}" "  \"code_signing_required\": ${_ios_code_signing_required_json},\n")
file(APPEND "${MANIFEST_FILE}" "  \"ipsw_format\": \"app_archive_compatibility_copy\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"checksum_file\": \"${APP_NAME}-ios-package-sha256.txt\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"bundle_identifier\": \"${_app_bundle_identifier}\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"bundle_short_version\": \"${_app_bundle_short_version}\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"bundle_version\": \"${_app_bundle_version}\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"minimum_os_version\": \"${_app_minimum_os_version}\",\n")
file(APPEND "${MANIFEST_FILE}" "  \"validated_required_bundle_entries\": ${_required_bundle_entry_count},\n")
file(APPEND "${MANIFEST_FILE}" "  \"validated_source_assets\": ${_source_asset_count},\n")
file(APPEND "${MANIFEST_FILE}" "  \"validated_project_files\": ${_bundled_project_file_count},\n")
file(APPEND "${MANIFEST_FILE}" "  \"ipa\": {\n")
file(APPEND "${MANIFEST_FILE}" "    \"file\": \"${APP_NAME}.ipa\",\n")
file(APPEND "${MANIFEST_FILE}" "    \"size_bytes\": ${_ipa_size},\n")
file(APPEND "${MANIFEST_FILE}" "    \"sha256\": \"${_ipa_sha256}\"\n")
file(APPEND "${MANIFEST_FILE}" "  },\n")
file(APPEND "${MANIFEST_FILE}" "  \"ipsw\": {\n")
file(APPEND "${MANIFEST_FILE}" "    \"file\": \"${APP_NAME}.ipsw\",\n")
file(APPEND "${MANIFEST_FILE}" "    \"size_bytes\": ${_ipsw_size},\n")
file(APPEND "${MANIFEST_FILE}" "    \"sha256\": \"${_ipsw_sha256}\",\n")
file(APPEND "${MANIFEST_FILE}" "    \"format_note\": \"Compatibility artifact containing the same installable app archive payload as the .ipa\"\n")
file(APPEND "${MANIFEST_FILE}" "  }\n")
file(APPEND "${MANIFEST_FILE}" "}\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DMANIFEST_FILE=${MANIFEST_FILE}"
    "-DAPP_NAME=${APP_NAME}"
    "-DEXPECTED_IOS_SDK=${IOS_SDK}"
    "-DEXPECTED_IOS_ARCHS=${IOS_ARCHS}"
    "-DEXPECTED_IOS_CODE_SIGNING_REQUIRED=${IOS_REQUIRE_CODE_SIGNATURE}"
    "-DEXPECTED_IOS_BUNDLE_IDENTIFIER=${_app_bundle_identifier}"
    "-DEXPECTED_IOS_MINIMUM_OS_VERSION=${_app_minimum_os_version}"
    "-DEXPECTED_REQUIRED_BUNDLE_ENTRIES=${_required_bundle_entry_count}"
    "-DEXPECTED_SOURCE_ASSETS=${_source_asset_count}"
    "-DEXPECTED_PROJECT_FILES=${_bundled_project_file_count}"
    -P "${CMAKE_CURRENT_LIST_DIR}/ValidateIosPackageManifest.cmake"
  RESULT_VARIABLE _manifest_validate_rc
)
if(NOT _manifest_validate_rc EQUAL 0)
  message(FATAL_ERROR "Generated iOS package manifest failed validation: ${MANIFEST_FILE}")
endif()

message(STATUS "Created iOS app archive: ${IPA_FILE}")
message(STATUS "Created requested IPSW artifact: ${IPSW_FILE}")
message(STATUS "Created iOS package manifest: ${MANIFEST_FILE}")
message(STATUS "Created iOS package checksums: ${CHECKSUM_FILE}")
