# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs `--vertex-bones` and checks it answers the question it exists for.
#
# A line-count check is not enough here. Printing bone 0's name for every
# vertex, or the bone at the loop index, gives 19,158 well-formed lines of
# "<index> <name>" in order and is perfectly deterministic -- and tells a
# generator nothing. So the assertion is the SEPARATION itself, measured
# against the base mesh: excluding arm-dominated vertices must collapse the
# body's apparent half-width at chest height.
#
# MEASURED on the shipped base mesh and the default rig, 2026-09-23:
#
#     y band   max|x| all   max|x| no-arm   shrink
#     3.0..3.5      4.052           1.332    67.1%
#     4.0..4.5      3.139           1.557    50.4%
#     6.0..6.5      1.132           1.102     2.7%
#
# That 4.052 IS the locs bug: a generator following the silhouette saw the
# body as three times wider than it is at chest height, so a rope dropped from
# the skull ran out along the arm. Two attempts were spent tuning parameters
# against it. The band above the shoulders is barely affected, which is the
# other half of the claim -- this discriminates by anatomy, not by height.

file(STRINGS "${BASE}" vlines REGEX "^v ")
list(LENGTH vlines nverts)

execute_process(COMMAND "${APP}" --vertex-bones
                OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--vertex-bones failed: ${rc}")
endif()
string(REPLACE "\n" ";" lines "${out}")

# The arm chain of the shipped rig, from the shoulder out. `shoulder01` is the
# scapula and `upperarm01` the humerus -- the retarget fix of 2026-09-20 turns
# on that distinction, so both are named rather than matched by a prefix that
# might quietly pick up neither.
set(arm_prefixes upperarm lowerarm shoulder clavicle hand finger metacarpal thumb)

set(expect 0)
set(arm_count 0)
set(all_max 0.0)
set(noarm_max 0.0)
set(high_all_max 0.0)
set(high_noarm_max 0.0)
foreach(line IN LISTS lines)
    if(line STREQUAL "")
        continue()
    endif()
    string(REPLACE " " ";" parts "${line}")
    list(LENGTH parts count)
    if(NOT count EQUAL 2)
        message(FATAL_ERROR "expected '<index> <bone>', got \"${line}\"")
    endif()
    list(GET parts 0 index)
    list(GET parts 1 bone)
    # One line per vertex, in vertex order: the generator joins on this index,
    # so a gap or a reordering silently mislabels the mesh.
    if(NOT index EQUAL expect)
        message(FATAL_ERROR "expected vertex ${expect}, got \"${line}\"")
    endif()
    if(bone STREQUAL "" OR bone STREQUAL "?")
        message(FATAL_ERROR "vertex ${index} has no bone: \"${line}\"")
    endif()

    set(is_arm FALSE)
    foreach(p IN LISTS arm_prefixes)
        if(bone MATCHES "^${p}")
            set(is_arm TRUE)
            break()
        endif()
    endforeach()
    if(is_arm)
        math(EXPR arm_count "${arm_count} + 1")
    endif()

    # This vertex's own position, from the mesh rather than from the app.
    list(GET vlines ${index} vline)
    string(REPLACE " " ";" vparts "${vline}")
    list(GET vparts 1 x)
    list(GET vparts 2 y)
    if(x LESS 0.0)
        string(SUBSTRING "${x}" 1 -1 x)
    endif()

    if(y GREATER_EQUAL 3.0 AND y LESS 3.5)
        if(x GREATER all_max)
            set(all_max "${x}")
        endif()
        if(NOT is_arm AND x GREATER noarm_max)
            set(noarm_max "${x}")
        endif()
    elseif(y GREATER_EQUAL 6.0 AND y LESS 6.5)
        if(x GREATER high_all_max)
            set(high_all_max "${x}")
        endif()
        if(NOT is_arm AND x GREATER high_noarm_max)
            set(high_noarm_max "${x}")
        endif()
    endif()
    math(EXPR expect "${expect} + 1")
endforeach()

if(NOT expect EQUAL nverts)
    message(FATAL_ERROR "printed ${expect} vertices, the base mesh has ${nverts}")
endif()

# The rig binds a real share of the mesh to the arms. Zero would mean the
# prefixes stopped matching the rig's names and every check below passed
# vacuously.
if(arm_count LESS 3000 OR arm_count GREATER 8000)
    message(FATAL_ERROR "${arm_count} arm-dominated vertices; measured 5090, so the "
                        "bone names or the rig have changed")
endif()

# THE discriminating assertion. Measured 4.052 -> 1.332; the bar is a halving,
# far from both the measurement and from the no-op this would be if the flag
# printed one bone for the whole mesh.
if(NOT all_max GREATER 3.5)
    message(FATAL_ERROR "chest band is only ${all_max} wide including arms; "
                        "measured 4.052, so the band has moved")
endif()
if(NOT noarm_max LESS 2.0)
    message(FATAL_ERROR "excluding arm vertices leaves the chest band ${noarm_max} wide "
                        "(measured 1.332) -- the dominant bone is not separating "
                        "arm from torso")
endif()

# ...and the other half: above the shoulders the arms are not there to exclude,
# so this must NOT be a height filter wearing a bone's name.
if(NOT high_noarm_max GREATER 1.0)
    message(FATAL_ERROR "the band above the shoulders collapsed to ${high_noarm_max}; "
                        "arm exclusion is removing torso, not arms")
endif()

message(STATUS "${expect} vertices, ${arm_count} on the arm chain; "
               "chest half-width ${all_max} -> ${noarm_max}, "
               "above the shoulders ${high_all_max} -> ${high_noarm_max}")
