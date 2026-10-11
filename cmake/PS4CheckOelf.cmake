# cmake -DOELF=<file.oelf> -P PS4CheckOelf.cmake
#
# Fails if the OELF made by create-fself doesn't have exactly one read-write
# PT_LOAD. PacBrew's create-fself sometimes duplicates the data segment, and the
# console then refuses to start the app with CE-41839-5. (From DolphinPS4's
# toolchain/check-oelf.cmake.)
find_program(READELF NAMES llvm-readelf llvm-readelf-21 readelf
    HINTS /opt/pacbrew/ps4/openorbis/bin)
if(NOT READELF)
    message(WARNING "PS4CheckOelf: no readelf found, skipping the segment check")
    return()
endif()
execute_process(COMMAND ${READELF} -l -W ${OELF} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "PS4CheckOelf: ${READELF} failed on ${OELF}")
endif()
string(REGEX MATCHALL "\n *LOAD +[^\n]* RW  [^\n]*" rw_loads "${out}")
list(LENGTH rw_loads count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "PS4CheckOelf: ${OELF} has ${count} read-write PT_LOAD segments "
        "(expected 1); the console will refuse to start it (create-fself bug).\n${out}")
endif()
message(STATUS "PS4CheckOelf: ${OELF}: segments OK")
