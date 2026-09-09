# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts two files ARE byte-identical.
#
# The mirror of files_differ.cmake, and it exists for the default skinning
# method. "The default is dual quaternion" is only testable as an EFFECT:
# exporting with no --skinning and asserting that file equals the --skinning dqs
# export, while app_skinning_dqs_differs proves the dqs export is not the linear
# one. Asserting a printed message instead would survive a default flipped back.
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
    message(FATAL_ERROR "${A} and ${B} differ; the default under test is not the one claimed")
endif()
