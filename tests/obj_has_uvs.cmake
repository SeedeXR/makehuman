# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts a shipped .obj carries usable texture coordinates.
#
# WHAT THIS CAUGHT. `tools/make_eyebrows.py` was DEAD at head: `ridge()` grew a
# third return value when the swept styles learned to carry a strand texture,
# and the eyebrow caller still unpacked two, so every run ended in
# `ValueError: too many values to unpack`. Nothing noticed, because the brow is
# generated rarely and `data/eyebrows/eyebrows.obj` simply stayed as whatever
# the last successful run had written -- 264 vertices and ZERO `vt`. A mesh
# with no UVs cannot carry an alpha, which is why the brow had to be a solid
# ridge, and the material said so in a comment that read as a design decision
# rather than as a consequence.
#
# NOT "one vt per vertex", which is what this asserted first and is simply
# WRONG about OBJ: faces index `v` and `vt` independently, so a legal file may
# carry fewer texture coordinates than vertices -- `data/hair/hair.obj` has 428
# and 425. The first version of this gate would have failed that valid file
# while still PASSING `data/eyelashes/eyelashes.obj`, whose 250 uvs are a
# body-atlas sliver that no strand texture can be sampled through. Counting is
# not the property; being usable is.
#
# So: there must be some, and every face must index one that exists. SPAN is
# checked separately by the caller, because "the uvs run along the sweep"
# is only meaningful for an asset whose texture runs that way.
#
# Usage: cmake -DOBJ=<file> [-DMIN_V_SPAN=<0..1>] -P obj_has_uvs.cmake

if(NOT EXISTS "${OBJ}")
    message(FATAL_ERROR "no such OBJ: ${OBJ}")
endif()

file(STRINGS "${OBJ}" verts REGEX "^v ")
file(STRINGS "${OBJ}" uvs REGEX "^vt ")
list(LENGTH verts nv)
list(LENGTH uvs nt)

if(nv EQUAL 0)
    message(FATAL_ERROR "${OBJ}: no vertices at all")
endif()
if(nt EQUAL 0)
    message(FATAL_ERROR
            "${OBJ}: ${nv} vertices and NO texture coordinates -- a mesh that cannot "
            "carry a strand alpha. A generator that stopped running leaves exactly "
            "this trace.")
endif()

# Every face must index a `vt` that is there. A face pointing past the list is
# a file loaders disagree about rather than reject.
file(STRINGS "${OBJ}" faces REGEX "^f ")
set(worst 0)
foreach(line IN LISTS faces)
    string(REPLACE " " ";" tokens "${line}")
    list(REMOVE_AT tokens 0)
    foreach(tok IN LISTS tokens)
        if(tok MATCHES "^[0-9]+/([0-9]+)")
            if(CMAKE_MATCH_1 GREATER worst)
                set(worst ${CMAKE_MATCH_1})
            endif()
        endif()
    endforeach()
endforeach()
if(worst GREATER nt)
    message(FATAL_ERROR
            "${OBJ}: a face indexes texture coordinate ${worst} but only ${nt} exist")
endif()

# How far the uvs reach along `v`. A strand texture runs root-to-tip in `v`, so
# an asset meant to wear one has to span most of it; a sliver of a shared body
# atlas spans almost none and samples a few transparent texels forever. That is
# exactly what `data/eyelashes/eyelashes.obj` does -- u 0.658..0.758,
# v 0.927..0.985 -- and it is why it was given a strand material and rendered
# no differently.
if(DEFINED MIN_V_SPAN)
    set(vmin 2.0)
    set(vmax -1.0)
    foreach(line IN LISTS uvs)
        string(REGEX MATCH "^vt +([^ ]+) +([^ ]+)" _ "${line}")
        if(CMAKE_MATCH_2 LESS vmin)
            set(vmin ${CMAKE_MATCH_2})
        endif()
        if(CMAKE_MATCH_2 GREATER vmax)
            set(vmax ${CMAKE_MATCH_2})
        endif()
    endforeach()
    # BOUNDED FROM BOTH ENDS rather than subtracted, because CMake has integer
    # arithmetic and these are fractions. Near 0 at one end and past MIN_V_SPAN
    # at the other is the same statement as "it spans most of v", without
    # needing to compute the difference.
    if(vmin GREATER 0.15 OR vmax LESS MIN_V_SPAN)
        message(FATAL_ERROR
                "${OBJ}: uvs run v ${vmin}..${vmax}, not from ~0 to at least ${MIN_V_SPAN} -- "
                "a strand texture sampled through that window barely varies along the hair")
    endif()
endif()

message(STATUS "${OBJ}: ${nv} vertices, ${nt} uvs, max face vt index ${worst}")
