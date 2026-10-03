# SPDX-License-Identifier: AGPL-3.0-or-later
#
# --print-parameters and --set-parameters must be each other's inverse.
#
# THE PAIR IS ONLY A SERIALISATION FORMAT IF IT SURVIVES A PIPE, and it did not
# at first: `--random` wrote its progress line to STDOUT, so a captured vector
# began with "randomised 245 modifiers" and the reader refused it as "not a
# number". Progress goes to stderr now, results to stdout.
#
# THE TOLERANCE IS THREE LINES, and which three is not arbitrary. The ethnic
# components are renormalised on apply, and three floats that sum to 1 in
# double do not sum to 1.0f, so each moves by about an ulp and lands on the
# other point of a two-cycle. MEASURED: every one of the other 340 dimensions
# is exact, and the residual moves 169 of 14,580 vertices by at most 0.01 mm.
# A tolerance of 3 therefore says "only the coupled triple may move"; a larger
# one would quietly admit a real regression.
execute_process(COMMAND "${CMD}" --random 42 --print-parameters
                OUTPUT_FILE "${WORK}/v1.txt" RESULT_VARIABLE rc1 ERROR_QUIET)
if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "--print-parameters exited ${rc1}")
endif()
execute_process(COMMAND "${CMD}" --set-parameters "${WORK}/v1.txt" --print-parameters
                OUTPUT_FILE "${WORK}/v2.txt" RESULT_VARIABLE rc2 ERROR_QUIET)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "--set-parameters exited ${rc2}")
endif()

file(STRINGS "${WORK}/v1.txt" a)
file(STRINGS "${WORK}/v2.txt" b)
list(LENGTH a na)
list(LENGTH b nb)
if(NOT na EQUAL nb)
    message(FATAL_ERROR "vector lengths differ: ${na} and ${nb}")
endif()
if(na LESS 300)
    message(FATAL_ERROR "only ${na} values -- the capture is not a parameter vector")
endif()

set(differing 0)
math(EXPR last "${na} - 1")
foreach(i RANGE ${last})
    list(GET a ${i} x)
    list(GET b ${i} y)
    if(NOT x STREQUAL y)
        math(EXPR differing "${differing} + 1")
    endif()
endforeach()
if(differing GREATER 3)
    message(FATAL_ERROR
            "${differing} of ${na} values changed on a round trip; at most the three coupled "
            "ethnic components may move")
endif()
message(STATUS "${na} values, ${differing} changed (the coupled triple at most)")
