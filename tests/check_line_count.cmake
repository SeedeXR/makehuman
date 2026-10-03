# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs a command and asserts how many lines it printed, optionally counting
# only the lines containing FILTER.
#
# A count is a weak claim on its own; what makes these worth having is that the
# numbers MEAN something. The parameter count is the length of every saved
# vector, so a silent change to data/modifiers changes what an old vector
# decodes to. The coupled count is the size of the ethnic simplex, and a fourth
# member would be renormalised with the others.
execute_process(COMMAND "${CMD}" ${ARG} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${CMD} ${ARG} exited ${rc}")
endif()
string(REPLACE "\n" ";" lines "${out}")
set(n 0)
foreach(line IN LISTS lines)
    if(line STREQUAL "")
        continue()
    endif()
    if(DEFINED FILTER)
        string(FIND "${line}" "${FILTER}" hit)
        if(hit EQUAL -1)
            continue()
        endif()
    endif()
    math(EXPR n "${n} + 1")
endforeach()
if(NOT n EQUAL EXPECT)
    message(FATAL_ERROR "expected ${EXPECT} lines, got ${n}")
endif()
message(STATUS "${n} lines, as expected")
