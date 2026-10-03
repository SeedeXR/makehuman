# Pin the one thing about the four views that could be wrong and still look
# right: which side is the LEFT side.
#
# A flipped left and right is silently plausible. Both images are a correct
# render of a correct character from a correct angle; only the LABEL is wrong,
# and nothing in the picture says so. An agent matching a creator's "left side"
# photograph would fit the wrong profile, and the mismatch would read as a poor
# fit rather than a swapped axis.
#
# So this does not assert the angles. It asserts the measurement the angles
# were derived FROM, and that measurement needs no coordinate bookkeeping: a
# LEFT-side-only modifier must move the screen-RIGHT half of a FRONT render,
# because a person facing you wears their left hand on your right. Get that
# backwards and all four names are mirrored.
#
# Written after reading the nose direction off a 256 px contact sheet gave the
# OPPOSITE answer to a crop of the same two heads. The small image was the
# thing that lied, which is why the gate measures a modifier instead.
#
# Needs: MH_APP, MH_COMPARE, MH_DIR.

function(mh_render_front out slider)
    set(session "${MH_DIR}/${out}_session.jsonl")
    set(lines "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}\n")
    if(NOT slider STREQUAL "")
        string(APPEND lines "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"set_slider\",\"arguments\":{\"name\":\"${slider}\",\"value\":1.0}}}\n")
    endif()
    string(APPEND lines "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"render\",\"arguments\":{\"view\":\"front\",\"size\":384,\"path\":\"${MH_DIR}/${out}.png\"}}}\n")
    file(WRITE "${session}" "${lines}")
    execute_process(COMMAND "${MH_APP}" --mcp INPUT_FILE "${session}"
                    OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "--mcp exited ${rc} rendering ${out}\n${errors}")
    endif()
    # A refusal must not pass as a render. Without this, a renamed modifier
    # would make both images identical and the direction check would never run.
    if(text MATCHES "\"isError\":true")
        message(FATAL_ERROR "a tool refused while rendering ${out}:\n${text}")
    endif()
endfunction()

mh_render_front(mcp_views_plain "")
mh_render_front(mcp_views_left_arm "armslegs/l-upperarm-scale-horiz-decr|incr")

# --min-differing first: if the modifier changed nothing, the bounding box
# below would be meaningless rather than wrong, and this says which it was.
execute_process(COMMAND "${MH_COMPARE}" "${MH_DIR}/mcp_views_plain.png"
                        "${MH_DIR}/mcp_views_left_arm.png" --min-differing 100
                RESULT_VARIABLE rc OUTPUT_VARIABLE text ERROR_VARIABLE errors)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "a left-arm modifier changed nothing in a front render:\n${text}${errors}")
endif()

if(NOT text MATCHES "differing region x ([0-9]+)\\.\\.")
    message(FATAL_ERROR "no differing region in the comparator's output:\n${text}${errors}")
endif()
set(x_min "${CMAKE_MATCH_1}")
if(x_min LESS 192)
    message(FATAL_ERROR
        "a LEFT-side modifier moved the screen-LEFT half: the differing region starts "
        "at x=${x_min} of 384. The model's left must face the viewer's right, so the "
        "four view names in main.cpp are mirrored.\n${text}${errors}")
endif()

message(STATUS "mcp views: the model's left faces the viewer's right (region from x=${x_min})")
