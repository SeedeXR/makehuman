# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Counts the disjoint pieces of an .obj, by walking faces that share vertices.
#
# WHAT IT IS FOR. An eyebrow used to be ONE swept ridge and is now hundreds of
# separate hairs, each its own card. Those two are the same file format, the
# same material and a similar vertex count -- and they look nothing alike. A
# piece count is the cheapest statement of which one shipped, and it is the one
# thing a vertex count, a UV check and a material grep all agree on while being
# completely wrong about the geometry.
#
# Usage: cmake -DOBJ=<file> -DMIN_PIECES=<n> -P obj_piece_count.cmake

if(NOT EXISTS "${OBJ}")
    message(FATAL_ERROR "no such OBJ: ${OBJ}")
endif()

file(STRINGS "${OBJ}" faces REGEX "^f ")
list(LENGTH faces nfaces)
if(nfaces EQUAL 0)
    message(FATAL_ERROR "${OBJ}: no faces")
endif()

# A full union-find is more than this needs. Each hair is written as a run of
# consecutive quads over its own vertices, so a piece BOUNDARY is a face whose
# vertices do not overlap the previous face's -- which is exactly what a new
# ribbon looks like, and costs one pass.
set(pieces 0)
set(prev "")
foreach(line IN LISTS faces)
    # `[0-9]+/` REQUIRED A TRAILING SLASH, so on a UV-less `f 1 2 3 4` it
    # matched nothing: `shared` stayed FALSE, every face counted as its own
    # piece, and the gate cleared any MIN_PIECES. It passed hardest on exactly
    # the input it should reject. Match the vertex index whether or not a `vt`
    # follows it.
    string(REGEX MATCHALL "[0-9]+" verts "${line}")
    set(shared FALSE)
    foreach(v IN LISTS verts)
        if(prev MATCHES "(^|;)${v}(;|$)")
            set(shared TRUE)
            break()
        endif()
    endforeach()
    if(NOT shared)
        math(EXPR pieces "${pieces} + 1")
    endif()
    string(JOIN ";" prev ${verts})
endforeach()

if(pieces LESS MIN_PIECES)
    message(FATAL_ERROR
            "${OBJ}: ${pieces} disjoint pieces across ${nfaces} faces, fewer than "
            "${MIN_PIECES} -- this looks like a swept surface, not a head of hairs")
endif()
message(STATUS "${OBJ}: ${pieces} pieces across ${nfaces} faces")
