# SPDX-License-Identifier: AGPL-3.0-or-later
#
# mh-core is DERIVED from AGPL-3.0 MakeHuman (mesh semantics, target and
# modifier behaviour). It is therefore AGPL-3.0 and must never be relicensed.
# See LICENSING.md section 4.

add_library(mh_core STATIC
    Mesh.cpp
    ObjReader.cpp
    RenderMesh.cpp
    Subdivider.cpp
    Target.cpp
    Macro.cpp
    TargetIndex.cpp
    Modifier.cpp
    Mhm.cpp
    Proxy.cpp)
add_library(mh::core ALIAS mh_core)

target_include_directories(mh_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# No Eigen yet: the mesh is plain-old-data in struct-of-arrays form, which
# wants a trivially-copyable Vec3, not an expression-template type. Eigen gets
# linked when mh_rig needs quaternions, slerp, and matrix decomposition.
target_link_libraries(mh_core
    PRIVATE mh::warnings mh::sanitize)

set_target_properties(mh_core PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
