# Return XML for validation while leaving the bundled plist (and signature)
# untouched. Xcode emits binary plists; source and smoke fixtures may use XML.
function(platformer_read_ios_plist path out_var)
  file(READ "${path}" _header LIMIT 8 HEX)
  if(_header STREQUAL "62706c6973743030")
    find_program(_ios_plist_python NAMES python3)
    if(NOT _ios_plist_python)
      message(FATAL_ERROR "Python 3 is required to read binary iOS Info.plist: ${path}")
    endif()
    execute_process(
      COMMAND "${_ios_plist_python}" -c
        "import plistlib, sys; data = plistlib.loads(open(sys.argv[1], 'rb').read()); sys.stdout.buffer.write(plistlib.dumps(data, fmt=plistlib.FMT_XML, sort_keys=False))"
        "${path}"
      RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _text
      ERROR_VARIABLE _error
    )
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "Could not decode binary iOS Info.plist ${path}: ${_error}")
    endif()
  else()
    file(READ "${path}" _text)
  endif()
  set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()
