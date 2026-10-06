# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts every texture a material names is actually on disk.
#
# WHY THIS IS NOT COVERED BY GREPPING THE .mhmat. `src/core/Material.cpp`
# contributes a texture channel "only when its texture exists", so a
# `diffuseTexture` pointing at a missing file is SILENTLY DROPPED -- the
# material loads, the render falls back to the untextured look, and nothing
# reports anything. A gate that only greps the material text stays green
# through exactly that.
#
# It is a real hazard rather than a hypothetical: these textures are generated
# by `tools/make_hair_alpha.py` into `data/`, and a generated file that never
# got committed is invisible on the machine that generated it and missing on
# every other one.
#
# Usage: cmake -DMHMAT=<file> -P material_texture_exists.cmake

if(NOT EXISTS "${MHMAT}")
    message(FATAL_ERROR "no such material: ${MHMAT}")
endif()

get_filename_component(_dir "${MHMAT}" DIRECTORY)
file(STRINGS "${MHMAT}" _lines REGEX "^[ \t]*(diffuse|normal|specular|transparency|bump|displacement)Texture ")
if(NOT _lines)
    message(STATUS "${MHMAT}: names no textures")
    return()
endif()

foreach(_line IN LISTS _lines)
    string(REGEX MATCH "^[ \t]*([A-Za-z]+)Texture[ \t]+(.+)$" _m "${_line}")
    set(_kind "${CMAKE_MATCH_1}")
    string(STRIP "${CMAKE_MATCH_2}" _rel)
    get_filename_component(_abs "${_dir}/${_rel}" ABSOLUTE)
    if(NOT EXISTS "${_abs}")
        message(FATAL_ERROR
                "${MHMAT}: ${_kind}Texture names ${_rel}, which is not on disk "
                "(${_abs}). The renderer drops a missing channel silently, so this "
                "would ship as an untextured material with nothing reported.")
    endif()
    message(STATUS "${MHMAT}: ${_kind}Texture ${_rel} exists")
endforeach()
