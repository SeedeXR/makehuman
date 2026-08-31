# SPDX-License-Identifier: Apache-2.0
#
# mh_foundation is the permissive base every other module may depend on. It
# contains no ported logic -- only platform and standard-library plumbing --
# so it is Apache-2.0 and depends on nothing of ours. See LICENSING.md 4.

add_library(mh_foundation STATIC Chars.cpp DataDir.cpp Transform.cpp)
add_library(mh::foundation ALIAS mh_foundation)

target_include_directories(mh_foundation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(mh_foundation PRIVATE mh::warnings mh::sanitize)

# Floating-point from_chars/to_chars are C++17 on paper and absent in practice:
# the Xcode 16.4 libc++ on the macos-15 runner has only the integral overloads,
# so the float call resolves to the deleted bool one. Detect rather than guess.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-std=c++23")
check_cxx_source_compiles("
#include <charconv>
int main() {
    char buf[32];
    float f = 0.0f;
    auto r = std::to_chars(buf, buf + sizeof(buf), f);
    auto p = std::from_chars(buf, r.ptr, f);
    return p.ec == std::errc{} ? 0 : 1;
}" MH_HAVE_FP_CHARCONV)
unset(CMAKE_REQUIRED_FLAGS)

if(MH_HAVE_FP_CHARCONV)
    target_compile_definitions(mh_foundation PRIVATE MH_HAVE_FP_CHARCONV=1)
    message(STATUS "charconv: floating-point from_chars/to_chars available")
else()
    message(STATUS "charconv: no floating-point support, using the C-locale fallback")
endif()

set_target_properties(mh_foundation PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
