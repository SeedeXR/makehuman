# SPDX-License-Identifier: AGPL-3.0-or-later
add_executable(mh_tests
    unit/test_mesh.cpp
    unit/test_obj_reader.cpp
    unit/test_tangents.cpp
    unit/test_render_mesh.cpp
    unit/test_subdivider.cpp
    unit/test_target.cpp
    unit/test_macro.cpp
    unit/test_target_index.cpp
    unit/test_material.cpp
    unit/test_asset_index.cpp
    golden/test_base_mesh_parity.cpp
    golden/test_mesh_fixture_parity.cpp
    golden/test_subdiv_fixture_parity.cpp
    golden/test_target_fixture_parity.cpp
    golden/test_macro_fixture_parity.cpp
    golden/test_target_index_parity.cpp
    golden/test_modifier_parity.cpp
    golden/test_character_parity.cpp
    golden/test_mhm_parity.cpp
    golden/test_proxy_parity.cpp
    golden/test_face_mask_parity.cpp
    golden/test_material_roundtrip.cpp
    golden/test_skeleton_parity.cpp
    golden/test_weights_parity.cpp
    golden/test_skinning_parity.cpp
    golden/test_transform_parity.cpp
    golden/test_bvh_parity.cpp
    golden/test_poseunits_parity.cpp
    golden/test_obj_writer_parity.cpp
    golden/test_gltf_writer.cpp
    golden/test_scene_io.cpp
    regression/test_review_findings.cpp
    regression/test_io_review_findings.cpp)

# Both are listed because mh::io no longer PUBLIC-links mh::core -- that
# inversion was the licence bug (LICENSING.md 4). The tests legitimately use
# both sides of the boundary: core to build a mesh, io to write it.
# mh::rig PUBLIC-links mh::io (PoseUnits.h takes a BvhFile), so listing io here
# too makes the linker see libmh_io.a twice and warn. AGPL -> Apache is the
# legal dependency direction, so inheriting it is correct.
target_link_libraries(mh_tests
    PRIVATE mh::core mh::rig mh_json Catch2::Catch2WithMain mh::warnings mh::sanitize)

target_compile_definitions(mh_tests PRIVATE
    MH_DATA_DIR="${MH_DATA_DIR}"
    MH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

# Catch2's floating-point matchers promote float to double inside their own
# headers. Marking the Catch2 include tree SYSTEM suppresses warnings from
# third-party code without weakening any warning on our own sources.
get_target_property(_catch2_inc Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)
if(_catch2_inc)
    target_include_directories(mh_tests SYSTEM PRIVATE ${_catch2_inc})
endif()

if(assimp_FOUND)
    target_link_libraries(mh_tests PRIVATE assimp::assimp)
    target_compile_definitions(mh_tests PRIVATE MH_HAVE_ASSIMP=1)
endif()

catch_discover_tests(mh_tests)
