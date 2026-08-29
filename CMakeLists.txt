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
add_subdirectory(src/core)
add_subdirectory(src/io)

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
