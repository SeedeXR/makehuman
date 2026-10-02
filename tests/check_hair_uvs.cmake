# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Every generated hair style must carry one UV per vertex.
#
# THE FAILURE THIS CATCHES. `data/hair/materials/hair.mhmat` asked for an
# "alpha-cut strand texture" from the day it was written, and it could not have
# one: the four generated styles carried NO `vt` lines at all. MEASURED on an
# export -- body 14,517 distinct UVs, eyes 808, teeth 136, hair ZERO -- so a
# texture would have sampled a single texel over the whole head. The sweeps emit
# UVs now (u around the tube, v along it, so strands run the way hair grows),
# and this is what stops a regenerate from silently dropping them again.
#
# One per VERTEX, not merely non-zero: the writer pairs `v` and `vt` by index,
# so a style with some UVs and not others would map most of itself to texel 0.
foreach(f IN LISTS OBJS)
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "no hair style at ${f}")
    endif()
    file(STRINGS "${f}" vlines REGEX "^v ")
    file(STRINGS "${f}" vtlines REGEX "^vt ")
    list(LENGTH vlines nv)
    list(LENGTH vtlines nvt)
    if(nv EQUAL 0)
        message(FATAL_ERROR "${f} has no vertices")
    endif()
    if(NOT nvt EQUAL nv)
        message(FATAL_ERROR
                "${f} has ${nv} vertices and ${nvt} UVs -- the strand texture needs one per "
                "vertex. Did tools/make_hair_styles.py lose its uv plumbing?")
    endif()
    message(STATUS "${f}: ${nv} vertices, ${nvt} UVs")
endforeach()
