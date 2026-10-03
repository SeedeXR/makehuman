# Rebuild a KNOWN character from its own renders, and check the recovered
# PARAMETERS -- not only the score.
#
# THE SCORE ALONE IS NOT ENOUGH, and that is the whole reason this gate exists.
# An earlier version of the search narrowed its probe window from the first
# pass, which locked in whatever the opening pass guessed. It scored HIGHER
# than the version before it (0.878 against 0.868) while the recovered
# parameters moved FURTHER from the truth (mean error 0.246 against 0.200).
# A gate watching the score would have called that an improvement.
#
# So: set six parameters to known values, photograph the character from all
# four angles, reset it, and fit against those photographs. The question is how
# close the fit gets to the values it can never see.
#
# Measured at the default four passes, deterministic across runs: score 0.9242,
# five of six parameters within 0.2, mean error 0.103. The bar below is "at
# least four of six", which passes the working search with room and fails the
# degenerate one, which managed three.
#
# Needs: MH_APP, MH_DIR.

set(TARGETS
    "macrodetails/Gender" 0.9
    "macrodetails/Age" 0.7
    "macrodetails-universal/Muscle" 0.8
    "macrodetails-universal/Weight" 0.75
    "macrodetails-height/Height" 0.8
    "macrodetails-proportions/BodyProportions" 0.3)

set(session "${MH_DIR}/mcp_fit_session.jsonl")
set(lines "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}\n")

# 1. The character to recover.
list(LENGTH TARGETS n)
math(EXPR last "${n} / 2 - 1")
foreach(i RANGE ${last})
    math(EXPR ki "${i} * 2")
    math(EXPR vi "${ki} + 1")
    list(GET TARGETS ${ki} key)
    list(GET TARGETS ${vi} value)
    string(APPEND lines "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\",\"params\":{\"name\":\"set_slider\",\"arguments\":{\"name\":\"${key}\",\"value\":${value}}}}\n")
endforeach()

# 2. Photograph it. These stand in for the creator's reference images.
foreach(view front back left right)
    string(APPEND lines "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"tools/call\",\"params\":{\"name\":\"render\",\"arguments\":{\"view\":\"${view}\",\"size\":512,\"path\":\"${MH_DIR}/mcp_fit_${view}.png\"}}}\n")
endforeach()

# 3. Forget it.
foreach(i RANGE ${last})
    math(EXPR ki "${i} * 2")
    list(GET TARGETS ${ki} key)
    string(APPEND lines "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\",\"params\":{\"name\":\"set_slider\",\"arguments\":{\"name\":\"${key}\",\"value\":0.5}}}\n")
endforeach()

# 4. Register the photographs and fit against them.
foreach(view front back left right)
    string(APPEND lines "{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\",\"params\":{\"name\":\"add_reference\",\"arguments\":{\"path\":\"${MH_DIR}/mcp_fit_${view}.png\",\"view\":\"${view}\"}}}\n")
endforeach()
string(APPEND lines "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"tools/call\",\"params\":{\"name\":\"fit_to_references\",\"arguments\":{}}}\n")
file(WRITE "${session}" "${lines}")

execute_process(COMMAND "${MH_APP}" --mcp INPUT_FILE "${session}"
                OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--mcp exited ${rc}\n${errors}")
endif()
if(text MATCHES "\"isError\":true")
    message(FATAL_ERROR "a tool refused:\n${text}")
endif()

if(NOT text MATCHES "\"scoreBefore\":([0-9.e+-]+),\"scoreAfter\":([0-9.e+-]+)")
    message(FATAL_ERROR "no fit result:\n${text}")
endif()
set(before "${CMAKE_MATCH_1}")
set(after "${CMAKE_MATCH_2}")

if(NOT after GREATER before)
    message(FATAL_ERROR "the fit did not improve the match (${before} -> ${after})")
endif()
if(after LESS 0.85)
    message(FATAL_ERROR "the fit reached only ${after}; it recovers a character it rendered "
                        "itself, so anything this low means the search or the score is broken")
endif()

# The assertion that matters: the VALUES, which the fit never saw.
#
# Each window is +/- 0.2 of the truth, written out rather than computed:
# math(EXPR) is integer-only. The COMPARISON is floating point, which was
# checked rather than assumed. The windows are not pinned digits -- the search
# is allowed to get better, and one parameter is allowed to stay stubborn.
set(lowers "0.7;0.5;0.6;0.55;0.6;0.1")
set(uppers "1.1;0.9;1.0;0.95;1.0;0.5")
set(close 0)
set(report "")
foreach(i RANGE ${last})
    math(EXPR ki "${i} * 2")
    math(EXPR vi "${ki} + 1")
    list(GET TARGETS ${ki} key)
    list(GET TARGETS ${vi} want)
    list(GET lowers ${i} lo)
    list(GET uppers ${i} hi)
    if(NOT text MATCHES "\"${key}\":([0-9.e+-]+)")
        message(FATAL_ERROR "the fit did not report ${key}:\n${text}")
    endif()
    set(got "${CMAKE_MATCH_1}")
    if(got GREATER lo AND got LESS hi)
        math(EXPR close "${close} + 1")
        string(APPEND report "  ok   ${key}: wanted ${want}, got ${got}\n")
    else()
        string(APPEND report "  OFF  ${key}: wanted ${want}, got ${got}\n")
    endif()
endforeach()

if(close LESS 4)
    message(FATAL_ERROR
        "only ${close} of 6 parameters came within 0.2 of the character the fit was "
        "rebuilding. The score reached ${after}, so the search is finding SOMETHING -- "
        "it is finding the wrong body.\n${report}")
endif()

message(STATUS "mcp fit: ${before} -> ${after}, ${close} of 6 parameters recovered")
