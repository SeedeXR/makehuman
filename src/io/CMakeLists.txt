# SPDX-License-Identifier: Apache-2.0
#
# mh_io is written from published format specifications, NOT translated from
# the AGPL reference, so it is Apache-2.0 and separately reusable. See
# LICENSING.md section 4. It may be depended on by AGPL modules; it must never
# depend on one.

add_library(mh_io STATIC
    Compact.cpp
    ObjWriter.cpp
    Transform.cpp
    BvhReader.cpp
    BvhWriter.cpp
    UsdWriter.cpp
    GltfWriter.cpp
    DracoMesh.cpp
    SceneIO.cpp)
add_library(mh::io ALIAS mh_io)

target_include_directories(mh_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# assimp (BSD-3-Clause) backs the multi-format path. It is licence-compatible
# with both AGPL and Apache-2.0, unlike the Autodesk FBX SDK -- see the note at
# the top of include/makehuman/io/SceneIO.h and LICENSING.md section 5.
# mh_io links mh::foundation (Apache-2.0) and NOTHING of ours besides.
#
# It used to PUBLIC-link mh::core, which is AGPL -- so its Apache-2.0 stamp
# bought nobody anything: anyone linking io pulled in AGPL regardless. The
# boundary is now real, and CMake is what enforces it: adding mh::core here
# would make the io headers compile again and silently re-break it, so don't.
target_link_libraries(mh_io
    PUBLIC  mh::foundation
    PRIVATE mh::warnings mh::sanitize assimp::assimp)

# Draco (Apache-2.0, google/draco) compresses glTF geometry. OPTIONAL, on the
# same terms as assimp: a build without it writes glTF uncompressed rather than
# failing to configure, so a contributor without the library is not blocked and
# CI can choose whether to pay for it. `dracoAvailable()` reports which build
# this is, so nothing can declare the extension without an encoder behind it.
find_package(draco QUIET)
if(draco_FOUND)
    target_link_libraries(mh_io PRIVATE draco::draco)
    target_compile_definitions(mh_io PUBLIC MH_HAVE_DRACO)
    message(STATUS "draco found: glTF geometry compression available")
else()
    message(STATUS "draco NOT found: glTF will be written uncompressed")
endif()

set_target_properties(mh_io PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
