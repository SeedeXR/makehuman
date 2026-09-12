# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts how many of an OBJ's vertices differ from another's, over the WHOLE
# file rather than one group.
#
# Written for the .mhpose round trip, where both halves of the claim need
# saying and only one of them is the interesting one:
#
#   * a file saved from --facs and re-loaded with --expression must give the
#     SAME mesh              -> EXPECT_DIFFERENT=0
#   * and that expression must actually move something, or the line above is
#     two identical rest meshes agreeing with each other
#                            -> EXPECT_DIFFERENT=2042
#
# The second test is the one that keeps the first from being decorative, which
# is why they share a script and are written next to each other.
#
# FIXED POINT, for the reason obj_group_moved.cmake gives at length: the writer
# prints four decimals, and a vertex on the midline comes out `0.0000` in a rest
# export and `-0.0000` in a posed one. The same point, different characters --
# so a text compare reports differences that are not there, and a true round
# trip would fail.
#
# ZIP_LISTS rather than an index loop: CMake's list(GET) is a linear scan, so
# walking 14,444 vertices by index is quadratic and takes minutes.
#
# Usage:
#   cmake -DA=<a.obj> -DB=<b.obj> -DEXPECT_DIFFERENT=<n> -P obj_verts_differ.cmake

if(NOT DEFINED EXPECT_DIFFERENT)
    message(FATAL_ERROR "EXPECT_DIFFERENT is required -- a compare with no expectation "
                        "passes on anything, which is the failure this file exists to avoid")
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
if(na EQUAL 0)
    message(FATAL_ERROR "no vertices in ${A}")
endif()

set(different 0)
foreach(a b IN ZIP_LISTS va vb)
    if(NOT a STREQUAL b)
        # Only the ones that differ as TEXT need the arithmetic, which keeps
        # this to a handful of conversions on an identical pair of files.
        string(REGEX MATCH "^v +([^ ]+) +([^ ]+) +([^ ]+)" _ "${a}")
        fixed_point("${CMAKE_MATCH_1}" xa)
        fixed_point("${CMAKE_MATCH_2}" ya)
        fixed_point("${CMAKE_MATCH_3}" za)
        string(REGEX MATCH "^v +([^ ]+) +([^ ]+) +([^ ]+)" _ "${b}")
        fixed_point("${CMAKE_MATCH_1}" xb)
        fixed_point("${CMAKE_MATCH_2}" yb)
        fixed_point("${CMAKE_MATCH_3}" zb)
        if(NOT (xa EQUAL xb AND ya EQUAL yb AND za EQUAL zb))
            math(EXPR different "${different} + 1")
        endif()
    endif()
endforeach()

if(NOT different EQUAL EXPECT_DIFFERENT)
    message(FATAL_ERROR "${different} of ${na} vertices differ, expected ${EXPECT_DIFFERENT}")
endif()
message(STATUS "${different} of ${na} vertices differ, as expected")
