# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts that an exported character is actually standing in the pose that was
# asked for, by measuring its shape rather than trusting the flag that produced
# it: armspan divided by height.
#
# A T-pose puts the arms straight out, so armspan lands within a few percent of
# height -- a human's arm span and height are famously about equal. An A-pose
# tucks them in, which takes a third of the width away and leaves height alone.
# Measured on this base mesh, with no character sliders moved:
#
#     --pose rest    x=10.5164 dm  y=16.5938 dm  ->  0.634
#     --pose tpose   x=16.8628 dm  y=16.6301 dm  ->  1.014
#
# Those are far enough apart that the gate needs no delicate threshold: the
# bands below are 0.500-0.750 and 0.950-1.100, and nothing lands between them.
# The point is to catch a pose that silently stopped being applied, which shows
# up as the T-pose export reading 0.634 -- the A-pose band -- not as a near miss.
#
# WHY AN OBJ AND NOT THE FBX. In FBX, glTF and USD the mesh is written at the
# A-pose bind and the requested pose rides on the skeleton, so the vertices are
# byte-identical whichever pose you ask for and a bounding box over them cannot
# see the difference; reading those needs the skinning evaluated. OBJ has no
# skeleton, so the pose is baked into the vertices and the geometry IS the
# answer. `app_export_pose_reaches_the_file` covers the rigged side by asserting
# the FBX changes at all between the two poses.
#
# Everything is FIXED POINT, because CMake has integer arithmetic and nothing
# else. The OBJ writer prints exactly four decimals, so `16.5938` is the integer
# 165938 in units of 1e-4 dm. The ratio is then carried in thousandths
# (x*1000/y), which keeps the largest intermediate near 1.7e8 and well inside a
# signed 32-bit int -- multiplying by 10000 for four digits of ratio would reach
# 1.7e9, which is close enough to the 2.1e9 ceiling to be worth avoiding.
#
# Usage:
#   cmake -DOBJ=<file> -DMIN_RATIO=<n> -DMAX_RATIO=<n> -DLABEL=<text>
#         -P obj_pose_shape.cmake
#
# MIN_RATIO and MAX_RATIO are armspan/height in thousandths.

if(NOT EXISTS "${OBJ}")
    message(FATAL_ERROR "no such OBJ: ${OBJ}")
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

file(STRINGS "${OBJ}" verts REGEX "^v ")
list(LENGTH verts n)
if(n LESS 1000)
    message(FATAL_ERROR "${OBJ}: only ${n} vertices -- that is not a character")
endif()

# The export is Y-up, so x is across the shoulders and y is head to heel.
set(first TRUE)
foreach(line IN LISTS verts)
    string(REGEX MATCH "^v +([^ ]+) +([^ ]+) +([^ ]+)" _ "${line}")
    fixed_point("${CMAKE_MATCH_1}" x)
    fixed_point("${CMAKE_MATCH_2}" y)
    if(first)
        set(minx ${x})
        set(maxx ${x})
        set(miny ${y})
        set(maxy ${y})
        set(first FALSE)
    else()
        if(x LESS minx)
            set(minx ${x})
        endif()
        if(x GREATER maxx)
            set(maxx ${x})
        endif()
        if(y LESS miny)
            set(miny ${y})
        endif()
        if(y GREATER maxy)
            set(maxy ${y})
        endif()
    endif()
endforeach()

math(EXPR span "${maxx} - ${minx}")
math(EXPR height "${maxy} - ${miny}")
if(height LESS 1)
    message(FATAL_ERROR "${OBJ}: zero height, cannot measure a pose")
endif()
math(EXPR ratio "${span} * 1000 / ${height}")

if(ratio LESS MIN_RATIO OR ratio GREATER MAX_RATIO)
    message(FATAL_ERROR
            "${LABEL}: armspan/height is ${ratio}/1000, outside ${MIN_RATIO}-${MAX_RATIO} "
            "(span ${span}, height ${height}, ${n} vertices in ${OBJ})")
endif()
message(STATUS "${LABEL}: armspan/height ${ratio}/1000, inside ${MIN_RATIO}-${MAX_RATIO}")
