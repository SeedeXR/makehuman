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
    Ktx2Writer.cpp
    Ktx2Encode.cpp
    Etc1s.cpp
    FbxWriter.cpp
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

# libktx (Apache-2.0, KhronosGroup/KTX-Software) encodes KTX2/ETC1S+BasisLZ for
# `KHR_texture_basisu`. OPTIONAL like draco -- absent, glTF textures are written
# uncompressed rather than the configure failing.
#
# It differs from draco in one way that forces a different shape: it is NOT in
# Homebrew (verified 2026-09-20 -- `brew info ktx` reports no such formula, and
# no libktx exists on this machine), so there is nothing for find_package to
# find and it has to be fetched. A fetch costs network at CONFIGURE time, which
# every preset and every fresh CI job would pay, so this defaults OFF: opting in
# is deliberate, and a plain build is exactly as fast as before.
#
# GIT_TAG is the COMMIT SHA, not the tag name -- a tag can be moved, a SHA
# cannot. GIT_SUBMODULES "" skips `tests/cts`, which is nearly all of the
# 987 MB a full recursive clone pulls and is of no use to us.
#
# KTX_FEATURE_ETC_UNPACK=OFF IS A LICENCE CONTROL, NOT A SIZE ONE. It excludes
# external/etcdec/etcdec.cxx, which is the Ericsson SLA: field-of-use
# restricted, forbidden by LICENSING.md 5.2, and not distributable under
# AGPL-3.0. The option defaults ON upstream, so it must be set here explicitly.
# See third_party/licenses/README.md. A comment cannot enforce a licence, so
# `ktx_excludes_ericsson_sla` asserts it against the built archive.
option(MH_WITH_KTX2 "Fetch and build libktx for KTX2/BasisLZ texture compression" OFF)
if(MH_WITH_KTX2)
    # libktx is a C library and the top-level project() declares LANGUAGES CXX
    # only, so generating fails with "No known features for C compiler" --
    # measured, not guessed: that is exactly how the first MH_WITH_KTX2=ON
    # configure died. Enabled HERE rather than in project() so a default build
    # still never goes looking for a C compiler it has no use for.
    enable_language(C)
    include(FetchContent)
    set(KTX_FEATURE_ETC_UNPACK OFF CACHE BOOL "" FORCE)  # licence: see above
    set(KTX_FEATURE_TESTS      OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_TOOLS      OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_DOC        OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_JNI        OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_PY         OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_VK_UPLOAD  OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_GL_UPLOAD  OFF CACHE BOOL "" FORCE)
    # KTX1 stays ON although we only write KTX2. Turning it off is not a
    # supported combination: the build still compiles lib/writer1.c but drops
    # the ktxTexture1_* implementation, and linking dies with "symbol(s) not
    # found" on ktxTexture1_glTypeSize and friends. Measured, not assumed --
    # that link error is exactly how the first real build of this target
    # failed. An earlier standalone build with the same flag "passed" only
    # because a static archive is never linked.
    set(KTX_FEATURE_KTX1       ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(ktx
        GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
        GIT_TAG        4d6fc70eaf62ad0558e63e8d97eb9766118327a6  # v4.4.2
        GIT_SUBMODULES ""
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(ktx)
    target_link_libraries(mh_io PRIVATE ktx)
    target_compile_definitions(mh_io PUBLIC MH_HAVE_KTX2)
    message(STATUS "libktx built: KTX2/BasisLZ texture compression available")
else()
    message(STATUS "libktx NOT built: glTF textures will be written uncompressed")
endif()

set_target_properties(mh_io PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
