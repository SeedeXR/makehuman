# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)

project(MakeHuman
    VERSION 2.0.0
    DESCRIPTION "Parametric 3D human generator"
    HOMEPAGE_URL "http://www.makehumancommunity.org"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- options ----
option(MH_BUILD_TESTS      "Build the test suite"        ON)
option(MH_BUILD_BENCHMARKS "Build benchmark targets"     ON)
option(MH_ENABLE_ASAN      "AddressSanitizer + UBSan"    OFF)

set(MH_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data"
    CACHE PATH "Asset root used by development builds")

# ------------------------------------------------------- warnings/sanitize ---
add_library(mh_warnings INTERFACE)
add_library(mh::warnings ALIAS mh_warnings)
option(MH_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
target_compile_options(mh_warnings INTERFACE
    $<$<BOOL:${MH_WARNINGS_AS_ERRORS}>:-Werror>
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
    -Wcast-align -Wunused -Wconversion -Wsign-conversion
    -Wdouble-promotion -Wformat=2)

add_library(mh_sanitize INTERFACE)
add_library(mh::sanitize ALIAS mh_sanitize)
if(MH_ENABLE_ASAN)
    target_compile_options(mh_sanitize INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(mh_sanitize INTERFACE -fsanitize=address,undefined)
endif()

# ------------------------------------------------------------ dependencies ---
# Eigen, TBB, assimp and Qt are found by the module that first needs them,
# not up front. Nothing links them yet.

# assimp (BSD-3-Clause) provides the multi-format import/export path (FBX,
# Collada, STL, PLY, 3MF) and doubles as an INDEPENDENT reader in tests for the
# formats we write ourselves. It is licence-compatible with both AGPL and
# Apache-2.0; the Autodesk FBX SDK is not -- see LICENSING.md section 5.
find_package(assimp REQUIRED CONFIG)
message(STATUS "assimp: found, multi-format IO enabled")

# nlohmann/json (MIT) parses .mhskel, which is JSON.
#
# Hand-rolled here would be a mistake: .mhskel is user-editable and community
# rigs are real, so the parser sits on a trust boundary. We also need
# `ordered_json` specifically -- the reference loads with OrderedDict and its
# bone ordering pass iterates in FILE order (skeleton.py:112-121), so a parser
# that sorts or hashes keys silently produces a different bone order.
#
# Pinned to a release archive of just the headers (300 KB) rather than the git
# repo, and to a SHA256, so the build is reproducible and cannot be changed
# under us. See LICENSING.md section 5.1.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
    URL_HASH SHA256=a22461d13119ac5c78f205d3df1db13403e58ce1bb1794edc9313677313f4a9d)
FetchContent_MakeAvailable(nlohmann_json)
add_library(mh_json INTERFACE)
target_include_directories(mh_json SYSTEM INTERFACE ${nlohmann_json_SOURCE_DIR}/include)

if(MH_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1                     # pinned; never a moving branch
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    enable_testing()
endif()

# ---------------------------------------------------------------- modules ----
add_subdirectory(src/foundation)
add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/rig)

# Qt is optional: the geometry, rig and io layers build and test without it, so
# the charconv-fallback job can stay small. The main CI matrix DOES install Qt --
# without it this whole subtree is skipped and the render/ui/app code is never
# compiled, which is exactly how it went unbuilt on CI for weeks.
option(MH_BUILD_RENDER "Build the Qt RHI renderer (requires Qt6)" ON)
if(MH_BUILD_RENDER)
    find_package(Qt6 QUIET COMPONENTS Gui GuiPrivate Widgets ShaderTools)
    if(Qt6_FOUND AND TARGET Qt6::GuiPrivate)
        message(STATUS "Qt ${Qt6_VERSION}: renderer, UI and app enabled")
        add_subdirectory(src/render)
        add_subdirectory(src/ui)
        add_subdirectory(src/app)
        add_subdirectory(tools)
        set(MH_HAVE_RENDER TRUE)
    else()
        message(STATUS "Qt6 (Gui, GuiPrivate, Widgets, ShaderTools) not found: renderer and UI disabled")
        set(MH_HAVE_RENDER FALSE)
    endif()
else()
    set(MH_HAVE_RENDER FALSE)
endif()

if(MH_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(MH_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------- summary ----
message(STATUS "")
message(STATUS "MakeHuman ${PROJECT_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  tests      : ${MH_BUILD_TESTS}")
message(STATUS "  benchmarks : ${MH_BUILD_BENCHMARKS}")
message(STATUS "  asan+ubsan : ${MH_ENABLE_ASAN}")
message(STATUS "  data dir   : ${MH_DATA_DIR}")
message(STATUS "")
