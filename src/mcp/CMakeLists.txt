# SPDX-License-Identifier: AGPL-3.0-or-later
#
# mh_mcp is DERIVED work in the same sense mh_core is: it exists to drive the
# AGPL-3.0 character tooling, so it is AGPL-3.0 and must never be relicensed.
# See LICENSING.md section 4.
#
# The protocol layer knows NOTHING about characters -- it speaks JSON-RPC and
# owns a list of callbacks. That is what lets all of it be tested without a
# mesh, a GPU or a subprocess, and what stops a protocol bug from presenting as
# a rendering bug.
#
# `mh_json` is PUBLIC here, unlike in mh_core: the Tool callback signature is
# written in terms of nlohmann::ordered_json, so anything registering a tool
# needs the header too.
add_library(mh_mcp STATIC
    Server.cpp
)

target_link_libraries(mh_mcp
    PUBLIC  mh::foundation mh_json
    PRIVATE mh::warnings mh::sanitize)

set_target_properties(mh_mcp PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)

add_library(mh::mcp ALIAS mh_mcp)
