# A head framing must actually frame a head.
#
# The numbers behind `framing: head` were measured -- distance 9, panY -6.5 --
# and the first guess at the SIGN was wrong: positive panY pans DOWN the body,
# so an early sweep produced nine renders of shins. A test that only checked
# the call succeeded would have been perfectly happy with those.
#
# So this asserts the picture changed, and changed a lot: a head shot and a
# full-body shot of the same character share almost nothing.
#
# Needs: MH_APP, MH_COMPARE, MH_DIR.

set(session "${MH_DIR}/framing_session.jsonl")
file(WRITE "${session}"
"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}
{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"render\",\"arguments\":{\"framing\":\"full\",\"size\":512,\"path\":\"${MH_DIR}/framing_a.png\"}}}
{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"render\",\"arguments\":{\"framing\":\"head\",\"size\":512,\"path\":\"${MH_DIR}/framing_b.png\"}}}
{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"render\",\"arguments\":{\"framing\":\"sideways\",\"size\":128,\"path\":\"${MH_DIR}/framing_never.png\"}}}
")
execute_process(COMMAND "${MH_APP}" --mcp INPUT_FILE "${session}"
                OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--mcp exited ${rc}\n${errors}")
endif()

# An unknown framing is REFUSED, not defaulted to full -- the same rule
# `--view sideways` obeys. A typo that silently means "full" is the painted
# no-op this codebase keeps finding.
if(NOT text MATCHES "framing must be full, head or torso")
    message(FATAL_ERROR "an unknown framing was not refused:\n${text}")
endif()

execute_process(COMMAND "${MH_COMPARE}" "${MH_DIR}/framing_a.png" "${MH_DIR}/framing_b.png"
                        --min-differing 50000
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "framing head rendered the same picture as framing full:\n${out}${err}")
endif()
message(STATUS "mcp framing: ${out}")
