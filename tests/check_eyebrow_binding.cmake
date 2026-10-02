# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The eyebrows must be bound to the FACE, and this is the gate that says so.
#
# THE BUG THIS EXISTS FOR, measured 2026-10-02. `--bind-points` bound every
# point to the scalp and only the scalp, so the brow arc at y 7.47..7.53 hung
# off 16 scalp vertices at y 7.87..8.11 with a mean offset of 50.3 mm and a max
# of 61.4. `fitProxy` scales an offset per axis but never rotates it
# (`Proxy.cpp:434-438`), so a 50 mm vector anchored to the hairline does not
# follow the brow ridge: on the default character the brow sank INSIDE the
# skin, and the only way to see it was to make it 9 mm proud -- about 4x
# anatomical. After `--bind-region` the same points bind to 68 vertices at
# y 7.394..7.765 with a mean offset of 1.0 mm and a max of 2.4.
#
# So the offset is the thing to pin, because the offset IS the standoff once
# the anchor is right. A per-component bar rather than a magnitude, because
# CMake has no sqrt and the claim does not need one: 0.050 dm is 5 mm, which is
# twice the real maximum and an order of magnitude under what the bug produced.
#
# This checks the SHIPPED asset, not a fresh run of the generator. Regenerating
# with the scalp binding restored would rewrite this file and this gate would
# catch it; a gate on the generator's output alone would not, because the file
# in `data/` is what the application actually loads.
file(READ "${MHCLO}" text)
string(REPLACE "\n" ";" lines "${text}")
set(seen 0)
set(worst 0.0)
foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    string(REPLACE " " ";" f "${line}")
    list(LENGTH f n)
    if(NOT n EQUAL 9)
        continue()  # header lines: name, uuid, basemesh, obj_file, ...
    endif()
    list(GET f 0 v0)
    if(NOT v0 MATCHES "^[0-9]+$")
        continue()
    endif()
    math(EXPR seen "${seen} + 1")
    foreach(i 6 7 8)
        list(GET f ${i} d)
        if(d GREATER "${BAR}" OR d LESS "-${BAR}")
            message(FATAL_ERROR
                    "offset component ${d} exceeds ${BAR} dm in \"${line}\" -- the brow is not "
                    "bound to the face. Was --bind-region dropped from make_eyebrows.py?")
        endif()
        # Track the largest magnitude seen, for the message below.
        if(d LESS 0.0)
            math(EXPR dummy "0")
            string(SUBSTRING "${d}" 1 -1 d)
        endif()
        if(d GREATER "${worst}")
            set(worst "${d}")
        endif()
    endforeach()
endforeach()
if(NOT seen EQUAL EXPECT)
    message(FATAL_ERROR "expected ${EXPECT} eyebrow bindings, got ${seen}")
endif()
message(STATUS "${seen} eyebrow bindings, every offset within ${BAR} dm (worst ${worst})")
