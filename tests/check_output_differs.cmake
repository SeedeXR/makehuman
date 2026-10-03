# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Two runs of the same command with different arguments must print different
# things. Used where "it printed something" is not the claim -- a parameter
# vector that ignores --random would print a full, plausible, WRONG answer.
execute_process(COMMAND "${CMD}" ${A} OUTPUT_VARIABLE outA RESULT_VARIABLE rcA)
execute_process(COMMAND "${CMD}" ${B} OUTPUT_VARIABLE outB RESULT_VARIABLE rcB)
if(NOT rcA EQUAL 0 OR NOT rcB EQUAL 0)
    message(FATAL_ERROR "a run failed: ${rcA} and ${rcB}")
endif()
if(outA STREQUAL "")
    message(FATAL_ERROR "the first run printed nothing, so 'differs' proves nothing")
endif()
if(outA STREQUAL outB)
    message(FATAL_ERROR "both runs printed the same thing; the argument had no effect")
endif()
message(STATUS "the two runs differ, as expected")
