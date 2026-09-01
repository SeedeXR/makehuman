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
    unit/test_malformed_input.cpp
    unit/test_compact.cpp
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
    golden/test_skintone_parity.cpp
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
    golden/test_roundtrip.cpp
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

# A hang is the one malformed-input failure a test cannot assert on: an
# unbounded loop in a parser never returns. ctest's own 1500 s per-test default
# is what turns that into a failure, so no property is set here -- and
# set_tests_properties could not see these tests anyway, since
# catch_discover_tests registers them at test time, not configure time.
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
    # The face mask has to REACH the writer, not merely be computed. It did not:
    # every format took its geometry from the masked RenderMesh except OBJ,
    # which wrote the Mesh directly and so shipped 18,486 faces -- the helper
    # cages and joint cubes included -- where the GLB of the same character
    # shipped 13,378. An export test that only checked the file parsed would
    # have passed throughout, so assert the count the app announces.
    set_tests_properties(app_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "body: 13378 of 18486 faces visible"
        FAIL_REGULAR_EXPRESSION "cannot apply the body face mask"
        FIXTURES_SETUP app_smoke_obj)
    # ...and then read the file, because "the app said 13378" and "the file
    # holds 13378" are different claims, and it was the second one that was
    # false. 14,398 = 13,378 body + 1,020 eyes, and 13,378 is exactly what the
    # GLB of the same character writes -- the two exports agreeing is the
    # property under test.
    add_test(NAME app_smoke_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/app_smoke.obj -DEXPECT=14398
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_body_faces.cmake)
    set_tests_properties(app_smoke_obj_faces PROPERTIES FIXTURES_REQUIRED app_smoke_obj)
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

    # A rigged export must actually be rigged. The app built the whole rig and
    # then wrote none of it -- 0 skins, 2 nodes, POSITION/NORMAL/TEXCOORD_0 and
    # nothing else -- while app_rig_superset passed on the announcement alone.
    add_test(NAME app_rig_glb COMMAND makehuman
             --rig mixamo_superset
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig.glb)
    set_tests_properties(app_rig_glb PROPERTIES
        PASS_REGULAR_EXPRESSION "skin: 179 joints"
        FIXTURES_SETUP app_rig_glb_file)
    add_test(NAME app_rig_glb_skinned COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_rig.glb
             # No embedded quotes: a `"` in the value stops CMake splitting the
             # rest on `;`, and these four names appear in a GLB only when it
             # carries a skin, so the quotes bought nothing.
             "-DKEYS=skins;JOINTS_0;WEIGHTS_0;inverseBindMatrices"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_rig_glb_skinned PROPERTIES FIXTURES_REQUIRED app_rig_glb_file)

    # The same for USD, which was the last format still dropping the rig
    # silently: writeUsdaScene has taken a skin all along and main.cpp passed
    # none. A skinned stage must be rooted at a SkelRoot -- UsdSkel requires it
    # -- so its absence is the whole failure in one token.
    add_test(NAME app_rig_usda COMMAND makehuman
             --rig mixamo_superset --eyes high-poly
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig.usda)
    set_tests_properties(app_rig_usda PROPERTIES
        PASS_REGULAR_EXPRESSION "skin: 179 joints"
        FIXTURES_SETUP app_rig_usda_file)
    add_test(NAME app_rig_usda_skinned COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_rig.usda
             "-DKEYS=def SkelRoot;def Skeleton;primvars:skel:jointIndices;primvars:skel:jointWeights"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_rig_usda_skinned PROPERTIES FIXTURES_REQUIRED app_rig_usda_file)

    # .usdz was implemented in the writer and unreachable from the application.
    # It is the format an Apple or AR pipeline takes, and the one Apple's own
    # usdchecker --arkit validates.
    add_test(NAME app_rig_usdz COMMAND makehuman
             --rig mixamo_superset --eyes high-poly
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig.usdz)
    set_tests_properties(app_rig_usdz PROPERTIES
        PASS_REGULAR_EXPRESSION "skin: 179 joints"
        FIXTURES_SETUP app_rig_usdz_file)
    add_test(NAME app_rig_usdz_skinned COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_rig.usdz
             "-DKEYS=def SkelRoot;primvars:skel:jointIndices"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_rig_usdz_skinned PROPERTIES FIXTURES_REQUIRED app_rig_usdz_file)

    # `io::importScene` is five sessions of work -- multi-mesh, node transforms,
    # materials, skins, a unit contract -- and until `--inspect` NOTHING in the
    # application called it. Its only consumers were tests.
    #
    # The assertion is the unit line, not just "it read something": the GLB is
    # written in metres and the FBX in centimetres, and both must report the
    # same 1.6594 m character. A reader that ignored the file's declared unit
    # would still list the meshes correctly.
    add_test(NAME app_inspect_glb COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/app_rig.glb)
    set_tests_properties(app_inspect_glb PROPERTIES
        PASS_REGULAR_EXPRESSION "units: 1 = 1 m, so 1\\.6[0-9]+ m tall"
        FIXTURES_REQUIRED app_rig_glb_file)

    # ...and the FBX, which is where the conversion has teeth. The GLB is
    # written in METRES, so `height * metersPerUnit` is a no-op there and a
    # writer that dropped the multiplication would still pass the case above --
    # verified by mutation. The FBX is written in CENTIMETRES, so the same
    # character only reads as 1.6594 m if the unit is actually applied.
    add_test(NAME app_inspect_fbx_export COMMAND makehuman
             --rig mixamo_superset
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig.fbx)
    set_tests_properties(app_inspect_fbx_export PROPERTIES
        PASS_REGULAR_EXPRESSION "skin: 179 joints"
        FIXTURES_SETUP app_rig_fbx_file)
    add_test(NAME app_inspect_fbx COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/app_rig.fbx)
    set_tests_properties(app_inspect_fbx PROPERTIES
        PASS_REGULAR_EXPRESSION "units: 1 = 0\\.01 m, so 1\\.6[0-9]+ m tall"
        FIXTURES_REQUIRED app_rig_fbx_file)

    # What a character is WEARING was not saved, and reopening it silently
    # substituted the default. Measured before the fix: saved wearing Low-Poly,
    # reopened wearing HighPolyEyes.
    #
    # Two causes. `--save` ran BEFORE the choosers, so it could not know what
    # was worn; and nothing wrote or read the reference's
    # `<slot> <name> <uuid>` line (`proxychooser.py:554-556`), so
    # `AssetIndex::findByUuid` -- built for exactly this -- had no caller.
    add_test(NAME app_save_eyes COMMAND makehuman
             --eyes low-poly
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_eyes.mhm)
    set_tests_properties(app_save_eyes PROPERTIES
        PASS_REGULAR_EXPRESSION "eyes: Low-Poly"
        FIXTURES_SETUP app_eyes_mhm)
    # The UUID, not the name, is what the loader resolves -- the reference
    # refuses a filename outright.
    add_test(NAME app_save_eyes_line COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_eyes.mhm
             "-DKEYS=eyes Low-Poly 1cc97a30-85a1-42e6-a9f7-b3e753732baa"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_save_eyes_line PROPERTIES FIXTURES_REQUIRED app_eyes_mhm)

    # ...and reopening it must wear what it says, not the chooser's default.
    # Low-Poly is 96 verts against High-Poly's 1,064, so the two cannot be
    # confused.
    add_test(NAME app_reload_eyes COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/app_eyes.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_eyes_reload.obj)
    set_tests_properties(app_reload_eyes PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Low-Poly \\(96 verts\\)"
        FIXTURES_REQUIRED app_eyes_mhm)

    # An explicit flag still beats the file: a user who typed --eyes must not be
    # overridden by what the document happens to record.
    add_test(NAME app_reload_eyes_override COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/app_eyes.mhm
             --eyes high-poly
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_eyes_override.obj)
    set_tests_properties(app_reload_eyes_override PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing HighPolyEyes"
        FIXTURES_REQUIRED app_eyes_mhm)

    # The rig and the pose were lost the same way the clothes were. Saved with
    # `--rig mixamo_superset --pose tpose`, the file named neither and reopened
    # as the 163-bone default in the rest pose.
    #
    # Lines from the reference: `skeleton <relative path>`
    # (`skeletonlibrary.py:336-339`) and `pose <relative path>`
    # (`3_libraries_pose.py:265-268`).
    add_test(NAME app_save_rig COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_rig.mhm)
    set_tests_properties(app_save_rig PROPERTIES FIXTURES_SETUP app_rig_mhm)
    add_test(NAME app_save_rig_lines COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_rig.mhm
             "-DKEYS=skeleton mixamo_superset.mhskel;pose tpose"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_save_rig_lines PROPERTIES FIXTURES_REQUIRED app_rig_mhm)

    # 179 bones against the default's 163, so the two rigs cannot be confused.
    add_test(NAME app_reload_rig COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/app_rig.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig_reload.obj)
    set_tests_properties(app_reload_rig PROPERTIES
        PASS_REGULAR_EXPRESSION "rig mixamo_superset \\(179 bones\\)"
        FIXTURES_REQUIRED app_rig_mhm)

    add_test(NAME app_reload_rig_override COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/app_rig.mhm --rig default
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_rig_override.obj)
    set_tests_properties(app_reload_rig_override PROPERTIES
        PASS_REGULAR_EXPRESSION "rig default \\(163 bones\\)"
        FIXTURES_REQUIRED app_rig_mhm)

    # BVH export: the app could READ a pose and never write the one it was
    # showing. `writeBvh` had no caller outside tests.
    #
    # 212 joints for a 163-bone rig -- one per bone plus an End Site for each of
    # the 49 childless ones, which is what tells a consumer how long a leaf bone
    # is. Blender reads it back as a 163-bone armature with 40 bones posed.
    add_test(NAME app_export_bvh COMMAND makehuman
             --rig default --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_pose.bvh)
    set_tests_properties(app_export_bvh PROPERTIES
        PASS_REGULAR_EXPRESSION "bvh: 212 joints, 1 frame"
        FIXTURES_SETUP app_pose_bvh)
    add_test(NAME app_export_bvh_content COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_pose.bvh
             "-DKEYS=HIERARCHY;ROOT root;CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation;End Site;MOTION;Frames: 1"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_bvh_content PROPERTIES FIXTURES_REQUIRED app_pose_bvh)

    # A file that is not a mesh must be reported and must exit non-zero, not
    # print an empty summary.
    add_test(NAME app_inspect_bad COMMAND makehuman
             --inspect ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
    set_tests_properties(app_inspect_bad PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot read"
        WILL_FAIL FALSE)

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

    # The headless production render. Unlike app_screenshot this needs a GPU but
    # NO window, so it is the path that works on a machine with no display.
    # PASS_REGULAR_EXPRESSION gives it teeth: a render that silently produced an
    # empty or opaque image would otherwise still exit 0.
    add_test(NAME app_render COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_render.png --transparent)
    set_tests_properties(app_render PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered .*app_render\\.png \\(1024x1024, transparent\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "nothing to draw;cannot write")

    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
