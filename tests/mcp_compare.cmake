# The outline score must DISCRIMINATE through the whole pipeline.
#
# `test_silhouette.cpp` proves the metric on drawn shapes. This proves the
# thing those tests cannot see: that the score, computed on real renders
# through the MCP tool, still separates a character from a different one. A
# metric that is correct on a circle and saturated on a body would pass the
# unit tests and be useless for fitting -- and "the agent kept adjusting and
# the number never moved" is a very expensive way to find that out.
#
# The bars are deliberately far apart and neither is a pinned digit. The
# measured values were 0.9979 for the same character and 0.9465 after a single
# macro slider; the gate asks for a clear separation, not for those numbers,
# because the renderer is allowed to improve.
#
# Needs: MH_APP, MH_DIR.

set(session "${MH_DIR}/mcp_compare_session.jsonl")
set(ref "${MH_DIR}/mcp_compare_ref.png")
file(WRITE "${session}"
"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}
{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"render\",\"arguments\":{\"view\":\"front\",\"size\":512,\"path\":\"${ref}\"}}}
{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"compare_to_reference\",\"arguments\":{\"reference\":\"${ref}\"}}}
{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"set_slider\",\"arguments\":{\"name\":\"macrodetails-universal/Weight\",\"value\":1.0}}}
{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"compare_to_reference\",\"arguments\":{\"reference\":\"${ref}\"}}}
{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"compare_to_reference\",\"arguments\":{\"reference\":\"${ref}\",\"view\":\"left\"}}}
")

execute_process(COMMAND "${MH_APP}" --mcp INPUT_FILE "${session}"
                OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--mcp exited ${rc}\n${errors}")
endif()
if(text MATCHES "\"isError\":true")
    message(FATAL_ERROR "a tool refused:\n${text}")
endif()

# One response per line; ids 3, 5 and 6 are the three scores.
string(REGEX MATCHALL "\"iou\":[0-9.e+-]+" scores "${text}")
list(LENGTH scores n)
if(NOT n EQUAL 3)
    message(FATAL_ERROR "expected 3 scores, got ${n}:\n${text}")
endif()
list(GET scores 0 same)
list(GET scores 1 heavier)
list(GET scores 2 wrong_view)
foreach(v same heavier wrong_view)
    string(REPLACE "\"iou\":" "" ${v} "${${v}}")
endforeach()

# The character against its own render. Not 1.0 and never will be: an
# antialiased edge masks differently through alpha than through colour, which
# was measured at 45 pixels in the render's favour and 0 in the reference's.
if(same LESS 0.99)
    message(FATAL_ERROR "a character scored ${same} against its OWN render; the metric or "
                        "the renderer is not reproducing the same outline")
endif()

# A single macro slider must move it, or the score cannot drive a fit.
if(NOT heavier LESS same)
    message(FATAL_ERROR "Weight=1.0 did not lower the score (${heavier} vs ${same}); the "
                        "metric is saturated and an agent adjusting sliders would see "
                        "nothing change")
endif()
if(heavier GREATER 0.98)
    message(FATAL_ERROR "Weight=1.0 moved the score only to ${heavier}; too insensitive to "
                        "fit a body with")
endif()

# A gross mismatch must read as gross. Scoring a side view against a front
# photograph is the clearest case there is.
if(wrong_view GREATER 0.6)
    message(FATAL_ERROR "a LEFT render scored ${wrong_view} against a FRONT reference; the "
                        "metric does not distinguish a body from a profile")
endif()

message(STATUS "mcp compare: same ${same}, heavier ${heavier}, wrong view ${wrong_view}")
