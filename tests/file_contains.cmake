# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts a file contains every string in KEYS.
#
# This exists because the application built a complete rig -- loaded the
# skeleton, fitted the joints, compiled the weights, reported "clamped 3,725 of
# 19,158 vertices to 4 influences" -- and exported none of it. Every export was
# a statue, and `app_rig_superset` passed throughout because it only checked
# that the app ANNOUNCED the rig. Reading the file is the difference.
#
# Works on GLB and USDZ as well as text: a GLB's JSON chunk is ASCII, and a
# USDZ stores its stage uncompressed (that is a format requirement, not luck --
# a consumer memory-maps the archive), so both are greppable without unpacking.
#
# Usage: cmake -DFILE=<path> "-DKEYS=a;b;c" -P file_contains.cmake

if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "no such file: ${FILE}")
endif()

file(READ "${FILE}" text)
foreach(key IN LISTS KEYS)
    string(FIND "${text}" "${key}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "${FILE} does not contain '${key}'")
    endif()
endforeach()
message(STATUS "${FILE}: all ${CMAKE_ARGC} keys present")
