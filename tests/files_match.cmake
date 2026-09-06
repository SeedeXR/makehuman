# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts two files ARE byte-identical.
#
# The inverse of files_differ.cmake, and it exists for the live-rig export
# restore. `exportTo` swaps the mesh to its REST positions before writing a
# format that carries a skeleton, then puts the posed ones back. Nothing
# asserted the RIGHT vertices came back -- the CLI exited straight after the
# export and had nothing left to observe.
#
# Exporting twice in one run gives it something. The OBJ written AFTER a .glb
# must be identical to one written on its own: an OBJ carries no rig, so it
# always gets the baked posed mesh, and a restore that put back the rest
# positions, or stale normals, or unfitted proxies, produces a different file.
#
# Usage: cmake -DA=<path> -DB=<path> -P files_match.cmake

foreach(f "${A}" "${B}")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "no such file: ${f}")
    endif()
endforeach()

file(SIZE "${A}" _size_a)
if(_size_a EQUAL 0)
    # Two empty files match, and would prove nothing at all.
    message(FATAL_ERROR "${A} is empty")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${A}" "${B}" RESULT_VARIABLE _same)
if(NOT _same EQUAL 0)
    message(FATAL_ERROR
        "${A} and ${B} differ; the export left the character in a different state "
        "than it started in")
endif()
