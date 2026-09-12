# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs `--spread-roots` and checks that what it printed is actually on the
# scalp, optionally saving the output so two runs can be compared.
#
# The y check is THE discriminating assertion, and the first version of these
# tests did not have it. Printing `coords[i]` instead of `coords[picks[i]]` --
# the vertex at the loop index rather than the vertex that was picked -- passes
# a line-count check, passes the format check and is perfectly deterministic.
# It prints the first N vertices of the mesh, which are nowhere near the head.
# Only the coordinates themselves tell the two apart.
execute_process(COMMAND "${APP}" --spread-roots "${N}"
                OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--spread-roots failed: ${rc}")
endif()
string(REPLACE "\n" ";" lines "${out}")
set(indices "")
foreach(line IN LISTS lines)
    if(line STREQUAL "")
        continue()
    endif()
    string(REPLACE " " ";" parts "${line}")
    list(LENGTH parts count)
    if(NOT count EQUAL 4)
        message(FATAL_ERROR "expected 'index x y z', got \"${line}\"")
    endif()
    list(GET parts 0 index)
    # A vertex id, not a position in the output. Printing the loop counter
    # instead of the pick gives 0,1,2... with the RIGHT coordinates beside it,
    # which passes the count, the format, the y check and determinism -- and
    # hands the generator roots it would place at the feet. MEASURED: the
    # lowest-numbered vertex of the cranium cap is 226, so anything below that
    # is not a cap vertex at all. Re-measure if the region ever changes.
    # Non-numeric fails this too (CMake makes GREATER_EQUAL false for "abc" and
    # for empty), so it is also the check that the field IS an index.
    if(NOT index GREATER_EQUAL 226)
        message(FATAL_ERROR "\"${line}\" does not name a cap vertex (lowest is 226)")
    endif()
    # ...and an upper bound, which is what separates the BODY scalp from the
    # helper cages sitting above it. MEASURED: body cap vertices run 226..12157,
    # while every `helper-hair` (138) and `joint-head-2` (8) vertex above
    # y=7.75 is >= 14566. Height alone cannot tell them apart -- both sit on
    # the cranium -- so without this a root on the long-hair envelope, the one
    # source `memory/todo.md` says never to use, reads as a good root.
    if(index GREATER 12157)
        message(FATAL_ERROR "\"${line}\" names vertex ${index}, a helper cage rather "
                            "than the body scalp (body cap ends at 12157)")
    endif()
    if(index IN_LIST indices)
        message(FATAL_ERROR "vertex ${index} was printed twice")
    endif()
    list(APPEND indices "${index}")
    list(GET parts 2 y)
    # 7.75 dm is the cranium centre's height, which is how the region is
    # defined. A root below it is not on the cap.
    if(y LESS_EQUAL 7.75)
        message(FATAL_ERROR "root \"${line}\" is not on the cranium cap (y <= 7.75)")
    endif()
endforeach()
list(LENGTH indices seen)
if(NOT seen EQUAL EXPECT)
    message(FATAL_ERROR "expected ${EXPECT} roots, got ${seen}")
endif()
message(STATUS "${seen} roots, all on the cranium cap")
if(DEFINED OUT)
    file(WRITE "${OUT}" "${out}")
endif()
