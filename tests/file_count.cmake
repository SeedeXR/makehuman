# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Asserts a file contains EXACTLY N occurrences of a needle.
#
# `file_contains.cmake` answers "is it there", which is the wrong question for
# anything shared. A stage with one `Skeleton` prim and one mesh bound to it
# contains every key a stage with one skeleton and THREE bound meshes does --
# and the difference is whether what a character is wearing follows the pose or
# stands still while the body moves. Only the count separates them.
#
# Usage: cmake -DFILE=<path> -DNEEDLE=<text> -DCOUNT=<n> -P file_count.cmake
#
# NEEDLE is a regular expression, so escape any metacharacters in it.

if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "no such file: ${FILE}")
endif()

file(READ "${FILE}" text)
string(REGEX MATCHALL "${NEEDLE}" hits "${text}")
list(LENGTH hits found)
if(NOT found EQUAL COUNT)
    message(FATAL_ERROR "${FILE}: '${NEEDLE}' occurs ${found} times, expected ${COUNT}")
endif()
message(STATUS "${FILE}: '${NEEDLE}' occurs ${found} times")
