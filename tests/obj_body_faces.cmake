# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Counts the faces in an OBJ and compares against EXPECT.
#
# This exists because the app computing a face mask and the app WRITING it are
# two different things: every format took its geometry from the masked
# RenderMesh except OBJ, which wrote the Mesh directly and shipped 5,108 helper
# faces the GLB of the same character did not. Asserting on the app's own
# announcement cannot catch that -- only reading the file can.
#
# A plain total, not a per-group count: the regression is helper faces leaking
# IN, which moves the total just as surely and needs no group tracking.
#
# Usage: cmake -DOBJ=<file> -DEXPECT=<n> -P obj_body_faces.cmake

if(NOT EXISTS "${OBJ}")
    message(FATAL_ERROR "no such OBJ: ${OBJ}")
endif()

file(STRINGS "${OBJ}" faces REGEX "^f ")
list(LENGTH faces count)

if(NOT count EQUAL EXPECT)
    message(FATAL_ERROR "${OBJ} has ${count} faces, expected ${EXPECT}")
endif()
message(STATUS "faces: ${count}")
