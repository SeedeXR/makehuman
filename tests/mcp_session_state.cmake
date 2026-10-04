# The MCP server must be able to KEEP a character, not only build one.
#
# Before this, `get_parameters` had no inverse: a model could snapshot a
# character and could not restore it, so "iterate until the creator is
# satisfied" had no way back to a better earlier state. And nothing could leave
# the session at all -- no .mhm, no exported asset -- which for a tool whose
# purpose is building an avatar is the gap that matters most.
#
# Each assertion below is a ROUND TRIP rather than an exit code. A save that
# wrote a truncated file, a load that applied nothing, or a set_parameters that
# silently dropped the tail would all exit 0 and leave a plausible stranger
# behind.
#
# Needs: MH_APP, MH_DIR.

set(session "${MH_DIR}/mcp_state_session.jsonl")
set(mhm "${MH_DIR}/mcp_state.mhm")
set(glb "${MH_DIR}/mcp_state.glb")
file(REMOVE "${mhm}" "${glb}")

file(WRITE "${session}"
"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}
{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"set_slider\",\"arguments\":{\"name\":\"macrodetails/Gender\",\"value\":0.95}}}
{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"get_parameters\"}}
{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"save\",\"arguments\":{\"path\":\"${mhm}\"}}}
{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"set_slider\",\"arguments\":{\"name\":\"macrodetails/Gender\",\"value\":0.0}}}
{\"jsonrpc\":\"2.0\",\"id\":51,\"method\":\"tools/call\",\"params\":{\"name\":\"set_slider\",\"arguments\":{\"name\":\"armslegs/l-upperarm-scale-horiz-decr|incr\",\"value\":0.8}}}
{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"load\",\"arguments\":{\"path\":\"${mhm}\"}}}
{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"get_parameters\"}}
{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\",\"params\":{\"name\":\"export\",\"arguments\":{\"path\":\"${glb}\"}}}
{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"export\",\"arguments\":{\"path\":\"${MH_DIR}/mcp_state.bogus\"}}}
{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\",\"params\":{\"name\":\"set_parameters\",\"arguments\":{\"values\":[0.1,0.2]}}}
")

execute_process(COMMAND "${MH_APP}" --mcp INPUT_FILE "${session}"
                OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--mcp exited ${rc}\n${errors}")
endif()

# Two sliders are disturbed between the save and the load, and they are
# different ON PURPOSE.
#
# Gender is IN the .mhm, so applying the file restores it whatever else
# happens. The upper-arm scale is at its default when the file is written, so
# the file never mentions it -- and the only thing that puts it back is `load`
# resetting to defaults first, as the reference does (human.py:1486).
#
# WITHOUT THAT SECOND SLIDER THIS TEST WAS GREEN ON BROKEN CODE: deleting the
# resetToDefaults call changed nothing it could see. A test whose premise is
# unmet reports the absence of its own coverage as a pass.
#
# The two parameter vectors, before the save and after the load. Compared as
# TEXT, which is exact: these come from the same build in the same order, so
# anything but equality is a real difference.
string(REGEX MATCHALL "\"values\":\\[[^]]*\\]" vectors "${text}")
list(LENGTH vectors n)
if(NOT n EQUAL 2)
    message(FATAL_ERROR "expected 2 parameter vectors, got ${n}:\n${text}")
endif()
list(GET vectors 0 before)
list(GET vectors 1 after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "save then load did not restore the character.\n"
                        "A .mhm that loses a modifier still loads without error, which is "
                        "why this compares the vectors rather than the exit code.")
endif()

# The load must have APPLIED something. An empty .mhm would round-trip
# perfectly and mean nothing.
if(NOT text MATCHES "\"applied\":([1-9][0-9]*)")
    message(FATAL_ERROR "the load applied no modifiers at all:\n${text}")
endif()
if(NOT text MATCHES "\"unknown\":0")
    message(FATAL_ERROR "the .mhm we just wrote contains modifiers we cannot read back:\n${text}")
endif()

# The exported asset must have real content. glTF writes a header whatever
# happens, so a byte count near zero is the failure to catch.
if(NOT text MATCHES "\"bytes\":([0-9]+)")
    message(FATAL_ERROR "no export result:\n${text}")
endif()
set(bytes "${CMAKE_MATCH_1}")
if(bytes LESS 100000)
    message(FATAL_ERROR "the exported GLB is only ${bytes} bytes; a body is ~2 MB")
endif()
if(NOT EXISTS "${glb}")
    message(FATAL_ERROR "export reported success and wrote no file")
endif()

# Two refusals, because both are silent corruption if they are not refused: an
# unknown extension, and a parameter vector of the wrong length -- which would
# otherwise be applied off-by-one down its whole length.
string(REGEX MATCHALL "\"isError\":true" refusals "${text}")
list(LENGTH refusals refused)
if(NOT refused EQUAL 2)
    message(FATAL_ERROR "expected the bad extension AND the short vector to be refused, "
                        "got ${refused} refusals:\n${text}")
endif()
if(NOT text MATCHES "expected 343 values, got 2")
    message(FATAL_ERROR "a short parameter vector was not refused by LENGTH:\n${text}")
endif()

message(STATUS "mcp state: round trip exact, export ${bytes} bytes, 2 refusals")
