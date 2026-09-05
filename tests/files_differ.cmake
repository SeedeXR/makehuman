# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts two files are NOT byte-identical.
#
# This exists because `--skin-material` chose the material the EXPORTERS wrote
# and nothing else: buildScene() read `skins/default.mhmat` by name, so every
# skin the picker offered rendered as the same untextured body. Both renders
# succeeded, both printed "rendered ... (1024x1024, pbr)", and the eight shipped
# skin tones were invisible on screen. Only comparing the two images catches it.
#
# Usage: cmake -DA=<path> -DB=<path> -P files_differ.cmake

foreach(f "${A}" "${B}")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "no such file: ${f}")
    endif()
endforeach()

file(SIZE "${A}" _size_a)
if(_size_a EQUAL 0)
    message(FATAL_ERROR "${A} is empty; a pair of empty files would 'differ' from nothing")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${A}" "${B}" RESULT_VARIABLE _same)
if(_same EQUAL 0)
    message(FATAL_ERROR "${A} and ${B} are byte-identical; the setting under test changed nothing")
endif()
