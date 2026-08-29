# SPDX-License-Identifier: Apache-2.0
#
# mh_io is written from published format specifications, NOT translated from
# the AGPL reference, so it is Apache-2.0 and separately reusable. See
# LICENSING.md section 4. It may be depended on by AGPL modules; it must never
# depend on one.

add_library(mh_io STATIC
    ObjWriter.cpp
    GltfWriter.cpp)
add_library(mh::io ALIAS mh_io)

target_include_directories(mh_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(mh_io
    PUBLIC  mh::core
    PRIVATE mh::warnings mh::sanitize)

set_target_properties(mh_io PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
