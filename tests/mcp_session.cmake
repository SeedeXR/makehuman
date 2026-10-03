# Drive a real MCP session through a pipe and assert stdout carried the protocol
# and NOTHING ELSE.
#
# THIS IS THE TEST THAT MATTERS, and it exists because the bug was real: loading
# a character prints progress with `std::printf`, so "applied 8 targets" and
# "asset groups: 16" landed in the middle of the JSON-RPC stream and a client
# saw a parse error before the first response. Every tool worked; the structured
# log on stderr showed a clean session; the renders came out right. A test that
# called a tool and checked the answer would have passed the whole time.
#
# So the assertion is on the SHAPE OF THE STREAM: every line begins with `{`.
# That is the invariant a client depends on, and it is the one that broke.
#
# Needs: MH_APP, MH_SESSION.

execute_process(COMMAND "${MH_APP}" --mcp
                INPUT_FILE "${MH_SESSION}"
                OUTPUT_VARIABLE out
                ERROR_VARIABLE err
                RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--mcp exited ${rc}\n${err}")
endif()

# A line starting with anything but `{` is chatter that leaked onto the
# protocol channel. Reported with the offending text, because "stdout is dirty"
# without saying what wrote there costs an hour.
string(REGEX MATCHALL "(^|\n)[^{\n][^\n]*" stray "${out}")
if(NOT stray STREQUAL "")
    message(FATAL_ERROR "non-protocol output on stdout: ${stray}")
endif()

# Exactly six responses for seven input lines: the notification must NOT be
# answered, and the unparseable line must still produce a ParseError rather than
# ending the session.
string(REGEX MATCHALL "\"jsonrpc\"" responses "${out}")
list(LENGTH responses count)
if(NOT count EQUAL 6)
    message(FATAL_ERROR "expected 6 responses, got ${count}:\n${out}")
endif()

foreach(expected
        "-32700"                      # the malformed line, answered and survived
        "\"protocolVersion\":\"2024-11-05\""  # the client's version, echoed back
        "list_parameters"             # tools/list reached the registry
        "\"isError\":true"            # a bad slider name is a tool error...
        "\"status\":\"ok\"")          # ...and the session kept going
    if(NOT out MATCHES "${expected}")
        message(FATAL_ERROR "missing ${expected} in:\n${out}")
    endif()
endforeach()

message(STATUS "mcp session: 6 responses, stdout clean")
