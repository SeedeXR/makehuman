# SPDX-License-Identifier: AGPL-3.0-or-later
add_executable(mh_tests
    unit/test_mesh.cpp
    unit/test_obj_reader.cpp
    golden/test_base_mesh_parity.cpp
    regression/test_review_findings.cpp)

target_link_libraries(mh_tests
    PRIVATE mh::core Catch2::Catch2WithMain mh::warnings mh::sanitize)

target_compile_definitions(mh_tests PRIVATE
    MH_DATA_DIR="${MH_DATA_DIR}")

# Catch2's floating-point matchers promote float to double inside their own
# headers. Marking the Catch2 include tree SYSTEM suppresses warnings from
# third-party code without weakening any warning on our own sources.
get_target_property(_catch2_inc Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)
if(_catch2_inc)
    target_include_directories(mh_tests SYSTEM PRIVATE ${_catch2_inc})
endif()

catch_discover_tests(mh_tests)
