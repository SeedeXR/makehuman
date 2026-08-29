# SPDX-License-Identifier: AGPL-3.0-or-later
#
# mh_rig ports legacy/python/shared/skeleton.py, so it is AGPL-3.0. It may
# depend on the permissive modules; never the reverse. See LICENSING.md 4.

add_library(mh_rig STATIC Skeleton.cpp VertexWeights.cpp Skinning.cpp)
add_library(mh::rig ALIAS mh_rig)

target_include_directories(mh_rig PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(mh_rig
    PUBLIC  mh::foundation
    PRIVATE mh::warnings mh::sanitize mh_json)

set_target_properties(mh_rig PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
