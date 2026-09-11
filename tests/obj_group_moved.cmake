# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts how a named OBJ group responded to a pose: how many of its vertices
# moved, how many did not, and that every one that moved went DOWN.
#
# Written for the teeth proxy, where the interesting claim is anatomical rather
# than numerical: a jaw drop must carry the lower teeth with it and leave the
# upper set exactly where they were. Counting the whole file's changed vertices
# cannot say that -- the jaw drops the chin and the lips too -- and neither can
# a bounding box, because the two arches overlap in Y (measured: the lowest
# upper-teeth vertex sits at 14.8720 and the highest lower-teeth vertex at
# 14.9174, so no horizontal plane separates them).
#
# Everything here is done in FIXED POINT. The writer prints exactly four
# decimals, so `-0.0482` is the integer -482 in units of 1e-4 dm, and CMake --
# which has integer arithmetic and nothing else -- can then subtract and compare
# exactly. Two earlier attempts are worth naming because both were wrong:
#
#   * comparing the vertex LINES as text over-counted by four. A vertex on the
#     model's midline is written `0.0000` in the rest export and `-0.0000` in
#     the posed one, because the pose multiply produces a negative zero. Same
#     point, different characters.
#   * comparing with CMake's `LESS` on the decimal strings works, but cannot
#     express a tolerance, because there is no way to add two reals.
#
# RISE_TOLERANCE is that tolerance, in the same 1e-4 units, and it defaults to
# 0 so a caller that does not ask for one gets the exact rule. It exists because
# a group rigid-rotating about a joint has vertices ON the axis: the tongue's
# nearest vertex to the jaw pivot rises 0.0004 dm under a jaw drop, which is
# correct and which an exact "every mover went down" rule calls a failure. A
# sign error still fails loudly -- it lifts the far end by 0.23, six hundred
# times the tolerance.
#
# Usage:
#   cmake -DA=<rest.obj> -DB=<posed.obj> -DGROUP=<name>
#         -DEXPECT_MOVED=<n> -DEXPECT_STILL=<n> [-DRISE_TOLERANCE=<n>]
#         -P obj_group_moved.cmake
#
# A and B must be exports of the same character with the same topology.

if(NOT DEFINED RISE_TOLERANCE)
    set(RISE_TOLERANCE 0)
endif()

# "-0.0482" -> -482. Four decimals exactly, which is what the writer emits.
function(fixed_point text out)
    if(NOT text MATCHES "^(-?)([0-9]+)\\.([0-9][0-9][0-9][0-9])$")
        message(FATAL_ERROR "not a 4-decimal number: '${text}'")
    endif()
    math(EXPR value "${CMAKE_MATCH_2} * 10000 + ${CMAKE_MATCH_3}")
    if(CMAKE_MATCH_1 STREQUAL "-")
        math(EXPR value "0 - ${value}")
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

foreach(f "${A}" "${B}")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "no such OBJ: ${f}")
    endif()
endforeach()

file(STRINGS "${A}" va REGEX "^v ")
file(STRINGS "${B}" vb REGEX "^v ")
list(LENGTH va na)
list(LENGTH vb nb)
if(NOT na EQUAL nb)
    message(FATAL_ERROR "different topology: ${na} vertices in ${A}, ${nb} in ${B}")
endif()

# Only the group lines and the face lines, so the walk is over ~18k lines rather
# than the whole 60k-line file.
file(STRINGS "${A}" gf REGEX "^[gf] ")
set(current "")
set(indices "")
foreach(line IN LISTS gf)
    if(line MATCHES "^g +(.*)$")
        string(STRIP "${CMAKE_MATCH_1}" current)
    elseif(current STREQUAL "${GROUP}")
        string(REPLACE " " ";" tokens "${line}")
        list(REMOVE_AT tokens 0)  # the "f"
        foreach(tok IN LISTS tokens)
            string(REGEX MATCH "^[0-9]+" v "${tok}")
            if(v)
                math(EXPR v "${v} - 1")  # OBJ indices are 1-based
                list(APPEND indices "${v}")
            endif()
        endforeach()
    endif()
endforeach()
if(NOT indices)
    message(FATAL_ERROR "no group '${GROUP}' with faces in ${A}")
endif()
list(REMOVE_DUPLICATES indices)

set(moved 0)
set(still 0)
set(rose "")
foreach(i IN LISTS indices)
    list(GET va ${i} a)
    list(GET vb ${i} b)
    string(REGEX MATCH "^v +([^ ]+) +([^ ]+) +([^ ]+)" _ "${a}")
    fixed_point("${CMAKE_MATCH_1}" xa)
    fixed_point("${CMAKE_MATCH_2}" ya)
    fixed_point("${CMAKE_MATCH_3}" za)
    string(REGEX MATCH "^v +([^ ]+) +([^ ]+) +([^ ]+)" _ "${b}")
    fixed_point("${CMAKE_MATCH_1}" xb)
    fixed_point("${CMAKE_MATCH_2}" yb)
    fixed_point("${CMAKE_MATCH_3}" zb)
    if(xa EQUAL xb AND ya EQUAL yb AND za EQUAL zb)
        math(EXPR still "${still} + 1")
    else()
        math(EXPR moved "${moved} + 1")
        # Y-up, so a jaw drop is a decrease. Without this the gate passes on a
        # group that moved the wrong way, which is what a sign error produces.
        math(EXPR rise "${yb} - ${ya}")
        if(rise GREATER RISE_TOLERANCE)
            list(APPEND rose "${i}: rose ${rise} (tolerance ${RISE_TOLERANCE})")
        endif()
    endif()
endforeach()

if(NOT moved EQUAL EXPECT_MOVED)
    message(FATAL_ERROR "group '${GROUP}': ${moved} vertices moved, expected ${EXPECT_MOVED}")
endif()
if(NOT still EQUAL EXPECT_STILL)
    message(FATAL_ERROR "group '${GROUP}': ${still} vertices held still, expected ${EXPECT_STILL}")
endif()
if(rose)
    list(LENGTH rose n)
    list(GET rose 0 first)
    message(FATAL_ERROR "group '${GROUP}': ${n} moved vertices did not go down, e.g. ${first}")
endif()
message(STATUS "group '${GROUP}': ${moved} moved down, ${still} held still")
