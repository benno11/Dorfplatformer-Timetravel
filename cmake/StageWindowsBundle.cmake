if(NOT DEFINED ROOT_DIR OR NOT DEFINED APP_DIR OR NOT DEFINED ASSETS_DIR OR NOT DEFINED VERSION_ID)
  message(FATAL_ERROR "StageWindowsBundle.cmake requires ROOT_DIR, APP_DIR, ASSETS_DIR, and VERSION_ID.")
endif()

file(REMOVE_RECURSE "${ROOT_DIR}")
file(REMOVE_RECURSE "${APP_DIR}")
file(MAKE_DIRECTORY "${ROOT_DIR}")
file(MAKE_DIRECTORY "${APP_DIR}")

file(WRITE "${ROOT_DIR}/current_version.txt" "${VERSION_ID}")

if(EXISTS "${ASSETS_DIR}")
  file(COPY "${ASSETS_DIR}" DESTINATION "${APP_DIR}")
endif()

foreach(_pair
    "${PLATFORMER_EXE}|${APP_DIR}"
    "${SHEET_CONFIG_EXE}|${APP_DIR}"
    "${LAUNCHER_EXE}|${ROOT_DIR}"
    "${TRAY_EXE}|${ROOT_DIR}"
    "${UPDATER_EXE}|${ROOT_DIR}"
    "${README_FILE}|${APP_DIR}"
    "${LICENSE_FILE}|${APP_DIR}"
    "${OBJECT_TYPE_MAP_FILE}|${APP_DIR}")
  string(REPLACE "|" ";" _parts "${_pair}")
  list(LENGTH _parts _parts_len)
  if(_parts_len LESS 2)
    continue()
  endif()
  list(GET _parts 0 _src)
  list(GET _parts 1 _dst)
  if(_src AND EXISTS "${_src}")
    file(COPY "${_src}" DESTINATION "${_dst}")
  endif()
endforeach()

if(DEFINED RUNTIME_DLL_DIR AND RUNTIME_DLL_DIR AND EXISTS "${RUNTIME_DLL_DIR}")
  file(GLOB _runtime_dlls "${RUNTIME_DLL_DIR}/*.dll")
  foreach(_dll IN LISTS _runtime_dlls)
    file(COPY "${_dll}" DESTINATION "${APP_DIR}")
    file(COPY "${_dll}" DESTINATION "${ROOT_DIR}")
  endforeach()
endif()
