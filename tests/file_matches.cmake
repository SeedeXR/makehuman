# Asserts that FILE contains a line matching PATTERN -- or, with ABSENT set,
# that nothing in it matches.
#
# A POSITIVE assertion by default, deliberately. The alternative shape here is
# a `grep` under `WILL_FAIL`, and `WILL_FAIL` accepts ANY non-zero exit: a
# missing file, a typo'd flag and a genuine mismatch all "pass" it. This says
# what it found instead.
if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "no such file: ${FILE}")
endif()

file(STRINGS "${FILE}" _lines)
set(_hit "")
foreach(_line IN LISTS _lines)
    if(_line MATCHES "${PATTERN}")
        set(_hit "${_line}")
        break()
    endif()
endforeach()

if(ABSENT)
    if(NOT _hit STREQUAL "")
        message(FATAL_ERROR "expected NO line matching\n  ${PATTERN}\nbut found\n  ${_hit}")
    endif()
    message(STATUS "absent as required: ${PATTERN}")
else()
    if(_hit STREQUAL "")
        # Print the file so a failure says what IS there, not merely what is not.
        string(REPLACE ";" "\n  " _all "${_lines}")
        message(FATAL_ERROR "no line matching\n  ${PATTERN}\nin ${FILE}:\n  ${_all}")
    endif()
    message(STATUS "matched: ${_hit}")
endif()
