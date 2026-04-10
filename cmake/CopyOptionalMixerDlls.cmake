if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
  message(FATAL_ERROR "SRC_DIR and DST_DIR are required")
endif()

if(NOT IS_DIRECTORY "${SRC_DIR}")
  return()
endif()

file(GLOB OPTIONAL_DLLS "${SRC_DIR}/*.dll")
foreach(dll IN LISTS OPTIONAL_DLLS)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dll}" "${DST_DIR}"
    RESULT_VARIABLE copy_result
  )
  if(NOT copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to copy optional mixer DLL: ${dll}")
  endif()
endforeach()
