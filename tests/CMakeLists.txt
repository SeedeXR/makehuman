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
    unit/test_obj_scene.cpp
    unit/test_data_dir.cpp
    golden/test_base_mesh_parity.cpp
    golden/test_mesh_fixture_parity.cpp
    golden/test_subdiv_fixture_parity.cpp
    golden/test_subdiv_masked_parity.cpp
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
    golden/test_usd_writer.cpp
    golden/test_unit_correctness.cpp
    golden/test_poseunits_parity.cpp
    golden/test_body_pose.cpp
    golden/test_slider_layout.cpp
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

# Writes the base mesh through every exporter so tools/blender_validate.py can
# check them in a third implementation. Not a test target: it produces files for
# another process rather than asserting anything.
add_executable(mh_export_fixture mh_export_fixture.cpp)
target_link_libraries(mh_export_fixture PRIVATE mh::core mh::rig mh::warnings)
target_compile_definitions(mh_export_fixture PRIVATE MH_DATA_DIR="${MH_DATA_DIR}")
set_target_properties(mh_export_fixture PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

# The renderer tests need Qt and a Metal device, so they are only built when the
# renderer is. They run offscreen -- no window is created.
if(MH_HAVE_RENDER)
    add_executable(mh_render_tests render/test_offscreen_render.cpp
                                   render/test_proxy_render.cpp)
    target_link_libraries(mh_render_tests
        PRIVATE mh::core mh::render mh::io Catch2::Catch2WithMain mh::warnings mh::sanitize)
    target_compile_definitions(mh_render_tests PRIVATE
        MH_DATA_DIR="${MH_DATA_DIR}"
        MH_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
        MH_SHADER_SRC_DIR="${CMAKE_SOURCE_DIR}/resources/shaders/rhi")
    set_target_properties(mh_render_tests PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
    add_dependencies(mh_render_tests mh_shaders)
    add_test(NAME render COMMAND mh_render_tests)
    # Offscreen platform: these must not try to open a window on a build box.
    set_tests_properties(render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The UI tests provide their own main() so a QApplication exists before any
    # widget is constructed -- Catch2's main would build the widgets first.
    add_executable(mh_ui_tests ui/test_ui.cpp ui/test_theme.cpp)
    target_link_libraries(mh_ui_tests
        PRIVATE mh::ui mh::core Catch2::Catch2 mh::warnings mh::sanitize)
    target_compile_definitions(mh_ui_tests PRIVATE
        MH_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
        MH_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources"
        MH_DATA_DIR="${MH_DATA_DIR}")
    set_target_properties(mh_ui_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
    if(_catch2_inc)
        target_include_directories(mh_ui_tests SYSTEM PRIVATE ${_catch2_inc})
    endif()
    add_test(NAME ui COMMAND mh_ui_tests)

    # The application binary itself. Nothing else covers main.cpp's wiring: a
    # scripted edit once deleted the entire Materials-dock setup -- including
    # setLitsphere -- and every one of the 328 tests still passed. Only running
    # the program caught it.
    #
    # --export needs no window, so this is deterministic on a build box. Since
    # the app learned to wear proxies it DOES reach the proxy wiring -- the
    # default character wears eyes, so this exports two groups -- but it still
    # does not cover the window path; `makehuman --screenshot` does, and needs a
    # real Metal device.
    add_test(NAME app_smoke COMMAND makehuman
             --load ${CMAKE_CURRENT_SOURCE_DIR}/golden/mhm/reference_save.mhm
             --set macrodetails/Gender=1.0
             --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_smoke.obj)
    # The window path. app_smoke now also exercises the proxy wiring through
    # --export, but only this reaches setMeshes and the viewport.
    #
    # PASS_REGULAR_EXPRESSION is what gives this teeth. Without it the test only
    # catches a proxy that fails to LOAD; a proxy silently dropped from the
    # scene still renders a valid picture and would pass. Asserting the app
    # announced what it is wearing pins that the chooser reached the viewport.
    #
    # Needs a real Metal device. SKIP_REGULAR_EXPRESSION on the app's own
    # "viewport error" makes a device-less machine skip rather than fail --
    # the same trade the render tests make with requireDevice(), and with the
    # same caveat: a genuine viewport failure skips too.
    add_test(NAME app_screenshot COMMAND makehuman
             --eyes high-poly
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_screenshot.png)
    set_tests_properties(app_screenshot PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing HighPolyEyes \\(1064 verts\\)"
        SKIP_REGULAR_EXPRESSION "viewport error"
        FAIL_REGULAR_EXPRESSION "cannot load proxy;unknown --eyes;references body vertex")

    # --rig. The 179-bone Mixamo superset shipped, had a CI staleness gate, and
    # could not skin at all -- because main.cpp hard-coded default.mhskel and
    # nothing in the application had ever loaded it. A test that only checked
    # the export succeeded would have passed throughout that, so this asserts
    # the app announced which rig it actually posed with.
    add_test(NAME app_rig_superset COMMAND makehuman
             --rig mixamo_superset
             --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig_superset.obj)
    set_tests_properties(app_rig_superset PROPERTIES
        PASS_REGULAR_EXPRESSION "rig mixamo_superset \\(179 bones\\)"
        FAIL_REGULAR_EXPRESSION "cannot load the rig;Unknown option;cannot build the rest pose")

    # A path, not a stem -- and deliberately without the .mhskel extension,
    # which resolved to an extensionless filename until the path and stem cases
    # were reduced to a common base.
    add_test(NAME app_rig_path COMMAND makehuman
             --rig ${MH_DATA_DIR}/rigs/mixamo_superset
             --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig_path.obj)
    set_tests_properties(app_rig_path PROPERTIES
        PASS_REGULAR_EXPRESSION "\\(179 bones\\)"
        FAIL_REGULAR_EXPRESSION "unknown --rig;cannot load the rig")

    # An unknown rig must say so, and say what IS available, rather than
    # silently falling back to the default rig and posing the wrong skeleton.
    add_test(NAME app_rig_unknown COMMAND makehuman
             --rig no_such_rig --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig_unknown.obj)
    # PASS_REGULAR_EXPRESSION alone: the app must SAY "unknown --rig". Pairing
    # it with WILL_FAIL passed vacuously on Qt's own "Unknown option" message,
    # which is a different failure entirely.
    set_tests_properties(app_rig_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --rig")

    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
