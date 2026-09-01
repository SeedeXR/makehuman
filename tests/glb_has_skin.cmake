# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts a GLB carries a skin: a skins array, a joint hierarchy, and the two
# vertex attributes that bind the mesh to it.
#
# This exists because the application built a complete rig -- loaded the
# skeleton, fitted the joints, compiled the weights, reported "clamped 3,725 of
# 19,158 vertices to 4 influences" -- and then exported none of it. Every
# export was a statue. `app_rig_superset` passed throughout, because it only
# checked that the app ANNOUNCED the rig.
#
# The GLB's JSON chunk is ASCII, so these keys are greppable without decoding
# the container.
#
# Usage: cmake -DGLB=<file> -P glb_has_skin.cmake

if(NOT EXISTS "${GLB}")
    message(FATAL_ERROR "no such GLB: ${GLB}")
endif()

file(READ "${GLB}" text)

foreach(key "\"skins\":[" "\"JOINTS_0\":" "\"WEIGHTS_0\":" "\"inverseBindMatrices\":")
    string(FIND "${text}" "${key}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "${GLB} has no ${key} -- the export dropped the rig")
    endif()
endforeach()
message(STATUS "skinned GLB verified: ${GLB}")
