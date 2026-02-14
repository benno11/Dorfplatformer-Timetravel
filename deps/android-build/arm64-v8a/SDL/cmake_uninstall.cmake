if (NOT EXISTS "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/install_manifest.txt")
    message(FATAL_ERROR "Cannot find install manifest: \"/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/install_manifest.txt\"")
endif()

file(READ "/home/benno111/Documents/GitHub/DF-New/deps/android-build/arm64-v8a/SDL/install_manifest.txt" files)
string(REGEX REPLACE "\n" ";" files "${files}")
foreach(file ${files})
    message(STATUS "Uninstalling \"$ENV{DESTDIR}${file}\"")
    execute_process(
        COMMAND /usr/bin/cmake -E remove "$ENV{DESTDIR}${file}"
        OUTPUT_VARIABLE rm_out
        RESULT_VARIABLE rm_retval
    )
    if(NOT ${rm_retval} EQUAL 0)
        message(FATAL_ERROR "Problem when removing \"$ENV{DESTDIR}${file}\"")
    endif (NOT ${rm_retval} EQUAL 0)
endforeach()
