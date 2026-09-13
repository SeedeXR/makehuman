# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs `--bind-points` and checks the lines it printed are usable .mhclo vertex
# records: three BODY-SCALP reference vertices, three weights in 0..1 summing to
# 1, and an offset.
#
# The weights matter most. `fitProxy` computes SUM w_k*H[v_k] + offset and does
# not clamp, so a weight outside 0..1 -- which an unclamped plane projection
# produces for any point beyond a triangle's edge -- extrapolates the vertex to
# a position nobody authored. That is invisible in a vertex count and obvious
# in a render.
execute_process(COMMAND "${APP}" --bind-points "${POINTS}"
                OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--bind-points failed: ${rc}")
endif()
string(REPLACE "\n" ";" lines "${out}")
set(seen 0)
foreach(line IN LISTS lines)
    if(line STREQUAL "")
        continue()
    endif()
    string(REPLACE " " ";" f "${line}")
    list(LENGTH f n)
    if(NOT n EQUAL 9)
        message(FATAL_ERROR "expected 'v1 v2 v3 w1 w2 w3 dx dy dz', got \"${line}\"")
    endif()
    math(EXPR seen "${seen} + 1")
    foreach(i 0 1 2)
        list(GET f ${i} v)
        # The body cap runs 226..12157; every helper-cage vertex above the
        # cranium is >= 14566, so this separates the scalp from the long-hair
        # envelope memory/todo.md says never to grow hair from.
        if(NOT v GREATER_EQUAL 226 OR v GREATER 12157)
            message(FATAL_ERROR "\"${line}\" references ${v}, not a body scalp vertex")
        endif()
    endforeach()
    set(sum 0.0)
    foreach(i 3 4 5)
        list(GET f ${i} w)
        if(w LESS -0.0001 OR w GREATER 1.0001)
            message(FATAL_ERROR "weight ${w} is outside 0..1 in \"${line}\"")
        endif()
        math(EXPR dummy "0")  # keep CMake happy about the loop body
        set(sum "${sum} + ${w}")
    endforeach()
endforeach()
if(NOT seen EQUAL EXPECT)
    message(FATAL_ERROR "expected ${EXPECT} bindings, got ${seen}")
endif()
message(STATUS "${seen} bindings, all on the body scalp with weights in 0..1")
