# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs a flag that prints "index x y z" scalp vertices -- `--spread-roots` or
# `--scalp-path` -- and checks that what it printed is actually on the body
# scalp, optionally saving the output so two runs can be compared.
#
# Printing `coords[i]` instead of `coords[picks[i]]` -- the vertex at the loop
# index rather than the vertex that was picked -- passes a line-count check,
# passes the format check and is perfectly deterministic. It prints the first N
# vertices of the mesh, and only the coordinates tell the two apart.
#
# An earlier version caught that with `y > 7.75`, which worked only because the
# region was the cranium cap. The region is now the hair-bearing scalp, which
# reaches the nape at y=6.99 -- and MEASURED, the first 157 vertices of the mesh
# all sit at y 7.29..7.36, INSIDE any band wide enough to admit the nape. A
# height band can no longer tell them apart, so the check is now identity: the
# coordinates printed must be the base mesh's own text for the index printed.
# The app prints `%.4f` and every coordinate in `base.obj` is written to four
# decimals, so this is an exact string compare with nothing to tune.

# The base mesh's own vertex lines, in order, so line N is vertex N.
file(STRINGS "${BASE}" vlines REGEX "^v ")

execute_process(COMMAND "${APP}" "${FLAG}" "${ARG}"
                OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${FLAG} failed: ${rc}")
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
    # THE discriminating assertion: these coordinates are that vertex's.
    list(GET vlines ${index} vline)
    list(GET parts 1 x)
    list(GET parts 2 y)
    list(GET parts 3 z)
    if(NOT vline STREQUAL "v ${x} ${y} ${z}")
        message(FATAL_ERROR "\"${line}\" does not carry vertex ${index}'s position, "
                            "which the base mesh writes as \"${vline}\"")
    endif()
    # ...and the vertex is on the scalp rather than merely somewhere on the body.
    # MEASURED on the shipped base mesh, the hair-bearing region's box is
    # x -0.7521..0.7521, y 6.9890..8.4913, z -0.3916..1.3948.
    if(y LESS 6.98 OR y GREATER 8.50 OR z LESS -0.40 OR z GREATER 1.40
       OR x LESS -0.76 OR x GREATER 0.76)
        message(FATAL_ERROR "root \"${line}\" is outside the hair-bearing scalp")
    endif()
endforeach()
list(LENGTH indices seen)
if(NOT seen EQUAL EXPECT)
    message(FATAL_ERROR "expected ${EXPECT} vertices, got ${seen}")
endif()
# A PATH additionally has ends that must be the ones asked for; roots do not.
if(DEFINED FIRST)
    list(GET indices 0 got)
    if(NOT got EQUAL FIRST)
        message(FATAL_ERROR "path starts at ${got}, not the requested ${FIRST}")
    endif()
endif()
if(DEFINED LAST)
    list(GET indices -1 got)
    if(NOT got EQUAL LAST)
        message(FATAL_ERROR "path ends at ${got}, not the requested ${LAST}")
    endif()
endif()
message(STATUS "${seen} vertices, all on the body scalp")
if(DEFINED OUT)
    file(WRITE "${OUT}" "${out}")
endif()
