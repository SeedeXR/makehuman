# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts two files ARE byte-identical.
#
# The mirror of files_differ.cmake. Two callers, both asserting something that
# is only visible as an EFFECT on a written file.
#
# The default skinning method: "the default is dual quaternion" means
# exporting with no --skinning and asserting that file equals the --skinning dqs
# export, while app_skinning_dqs_differs proves the dqs export is not the linear
# one. Asserting a printed message instead would survive a default flipped back.
#
# And the live-rig corrective warning: the app tells the user that
# --correctives does not reach a .glb, and this is what pins that the file
# really is the same one. When it fails, the warning has become false and
# belongs deleted rather than weakened.
#
# The empty guard is the same one, for the same reason: a pair of files that
# failed to write would be "identical" and prove nothing.
#
# Usage: cmake -DA=<path> -DB=<path> -P files_identical.cmake

foreach(f "${A}" "${B}")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "no such file: ${f}")
    endif()
    file(SIZE "${f}" _size)
    if(_size EQUAL 0)
        message(FATAL_ERROR "${f} is empty; a pair of empty files matches nothing")
    endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${A}" "${B}" RESULT_VARIABLE _same)
if(NOT _same EQUAL 0)
    message(FATAL_ERROR "${A} and ${B} differ, and were expected to be byte-identical")
endif()
