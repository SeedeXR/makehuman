# The CLI and the MCP server must mean the SAME THING by "left".
#
# They very nearly did not. The MCP views were added as their own table, with
# angles measured independently, and only on going back to delete the
# duplicate did it turn out there had been a six-entry table since the View
# menu -- agreeing exactly, its -90 being the measured table's 270.
#
# Had they disagreed, nothing would have looked wrong. Both would render a
# correct human from a correct angle; only the LABEL would differ, so an agent
# matching a creator's "left side" photograph would fit the wrong profile and
# read the mismatch as a poor fit. That is why this compares PIXELS rather
# than trusting that one constant feeds both.
#
# Needs: MH_APP, MH_COMPARE, MH_DIR.

set(cli "${MH_DIR}/agree_cli_left.png")
set(mcp "${MH_DIR}/agree_mcp_left.png")

execute_process(COMMAND "${MH_APP}" --render "${cli}" --view left
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--render --view left exited ${rc}\n${out}${err}")
endif()

set(session "${MH_DIR}/agree_session.jsonl")
file(WRITE "${session}"
"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}
{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"render\",\"arguments\":{\"view\":\"left\",\"size\":1024,\"path\":\"${mcp}\"}}}
")
execute_process(COMMAND "${MH_APP}" --mcp INPUT_FILE "${session}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--mcp exited ${rc}\n${err}")
endif()
if(out MATCHES "\"isError\":true")
    message(FATAL_ERROR "the render tool refused:\n${out}")
endif()

# Two renders of the same scene on the same GPU are not promised to be
# bit-identical, so this is a PIXEL bound rather than a byte compare -- the
# rule this project learned twice. Measured at 0; 200 is the noise allowance
# the rest of the suite uses, and is still a thousandth of a half-turn error.
execute_process(COMMAND "${MH_COMPARE}" "${cli}" "${mcp}" --max-differing 200
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "the CLI and the MCP server disagree about which side is LEFT.\n"
        "Both images are a correct render from a correct angle; only the label "
        "differs, which is why this is checked at all.\n${out}${err}")
endif()
message(STATUS "mcp/cli agree on left: ${out}")
