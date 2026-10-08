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

# AND THE SERVER IS NOT IN THE DOCK.
#
# One `--mcp` is started per client session and nobody launches one by hand,
# yet they were registering with macOS as FOREGROUND applications: a few open
# sessions put several MakeHuman icons in the Dock and in Cmd-Tab that nobody
# started. Measured with `lsappinfo`, which reported `type="Foreground"` for
# every one of them, and `type="UIElement"` once this was fixed.
#
# ASSERTED HERE rather than in a test of its own because this run already
# starts a server and already captures its stderr. A separate test would be a
# second process for no more information: the server reports the policy once,
# at startup, so starting one specially to read that line observes exactly what
# this line observes. Two lines beat a 44-line script that proves the same
# thing.
#
# What is NOT covered either way is a policy that changes LATER. Qt transforms
# the process when it builds a window, and --mcp builds none; a render through
# the server was measured to leave it at UIElement. If a tool is ever added
# that opens a real window, this assertion will not notice.
#
# The string is what the system reports when read back from NSApp, not what
# the setter was asked for -- see src/app/MacDock.mm. The failing report reads
# "dock icon NOT hidden", which does not match this.
if(NOT err MATCHES "--mcp: dock icon hidden")
    message(FATAL_ERROR
        "the MCP server did not leave the Dock -- it registers as a foreground "
        "application and shows an icon nobody asked for.\n"
        "Expected 'dock icon hidden' on stderr; got:\n${err}")
endif()

message(STATUS "mcp session: 6 responses, stdout clean, not in the dock")
