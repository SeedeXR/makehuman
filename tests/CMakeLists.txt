# SPDX-License-Identifier: AGPL-3.0-or-later
# The Draco tests decode what the encoder produced -- a separate code path in
# the same library -- so the test binary needs draco's headers as well.
find_package(draco QUIET)

add_executable(mh_tests
    unit/test_mesh.cpp
    unit/test_random.cpp
    unit/test_symmetry.cpp
    unit/test_pose_blend.cpp
    unit/test_facs.cpp
    unit/test_save_expression.cpp
    unit/test_pose_frame.cpp
    unit/test_obj_reader.cpp
    unit/test_proxy_weights.cpp
    unit/test_tangents.cpp
    unit/test_render_mesh.cpp
    unit/test_subdivider.cpp
    unit/test_decimator.cpp
    unit/test_topology_hash.cpp
    unit/test_provenance.cpp
    unit/test_naming.cpp
    unit/test_target.cpp
    unit/test_macro.cpp
    unit/test_custom_targets.cpp
    unit/test_target_index.cpp
    unit/test_material.cpp
    unit/test_asset_index.cpp
    unit/test_obj_scene.cpp
    unit/test_data_dir.cpp
    unit/test_malformed_input.cpp
    unit/test_compact.cpp
    unit/test_transform.cpp
    unit/test_dqs.cpp
    unit/test_swing_twist.cpp
    unit/test_rbf.cpp
    unit/test_correctives.cpp
    unit/test_corrective_manifest.cpp
    unit/test_corrective_blob.cpp
    unit/test_pose_signal.cpp
    unit/test_corrective_runtime.cpp
    unit/test_wrinkles.cpp
    unit/test_normal_blend.cpp
    unit/test_eye_aim.cpp
    unit/test_corrective_cache.cpp
    unit/test_posed_correctives.cpp
    unit/test_version.cpp
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
    golden/test_draco.cpp
    golden/test_unit_correctness.cpp
    golden/test_poseunits_parity.cpp
    golden/test_body_measures.cpp
    golden/test_body_pose.cpp
    golden/test_slider_layout.cpp
    golden/test_obj_writer_parity.cpp
    golden/test_blendshapes.cpp
    golden/test_fbx_writer.cpp
    golden/test_gltf_writer.cpp
    golden/test_scene_io.cpp
    golden/test_roundtrip.cpp
    regression/test_repose.cpp
    regression/test_review_findings.cpp
    regression/test_io_review_findings.cpp
    regression/test_determinism.cpp)

# mh::rig alone. It PUBLIC-links mh::io (PoseUnits.h takes a BvhFile) and, since
# PosedMesh.h took a `core::Mesh&`, mh::core as well -- so the tests get both
# sides of the boundary by inheritance. Naming either of them here as well makes
# the linker see the archive twice and warn:
#   ld: warning: ignoring duplicate libraries: 'src/core/libmh_core.a'
# AGPL -> Apache is the legal direction and AGPL -> AGPL is unconstrained, so
# inheriting both is correct rather than merely convenient.
target_link_libraries(mh_tests
    PRIVATE mh::rig mh_json Catch2::Catch2WithMain mh::warnings mh::sanitize)

# MH_HAVE_DRACO reaches here through mh_io's PUBLIC definition; the DECODER is
# what the test needs on top of that, and it lives in the same library.
if(draco_FOUND)
    target_link_libraries(mh_tests PRIVATE mh::io draco::draco)
endif()

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
# Every test process gets a temp directory belonging to ITS BUILD, not the
# shared system one.
#
# The suite writes ~80 fixed names under `temp_directory_path()` --
# `mh_units_agree.obj`, `mh_fuzz_obj`, `mh_glb_*.glb` and so on -- through a
# dozen little per-file helpers. Two ctest runs of the same binary then fight
# over the same files, which is exactly what happened when an ASan run and a
# background TSan run overlapped: "all writers agree at the same unit" failed
# once and passed 465/465 serially, twice.
#
# Renaming 80 call sites would be the large fix. `TMPDIR` is the small one:
# `std::filesystem::temp_directory_path()` honours it, so every existing name
# becomes unique per preset without touching a single test.
set(MH_TEST_TMPDIR ${CMAKE_CURRENT_BINARY_DIR}/tmp)
file(MAKE_DIRECTORY ${MH_TEST_TMPDIR})

catch_discover_tests(mh_tests PROPERTIES ENVIRONMENT "TMPDIR=${MH_TEST_TMPDIR}")

# Writes the base mesh through every exporter so tools/blender_validate.py can
# check them in a third implementation. Not a test target: it produces files for
# another process rather than asserting anything.
add_executable(mh_export_fixture mh_export_fixture.cpp)
# mh::core is NOT listed: mh::rig PUBLIC-links it since PosedMesh, so naming it
# here too makes the linker see libmh_core.a twice and warn -- the same trap the
# comment above records for libmh_io.a.
target_link_libraries(mh_export_fixture PRIVATE mh::rig mh::warnings)
target_compile_definitions(mh_export_fixture PRIVATE MH_DATA_DIR="${MH_DATA_DIR}")
set_target_properties(mh_export_fixture PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

# The renderer tests need Qt and a Metal device, so they are only built when the
# renderer is. They run offscreen -- no window is created.
if(MH_HAVE_RENDER)
    add_executable(mh_render_tests render/test_offscreen_render.cpp
                                   render/test_proxy_render.cpp
                                   unit/test_picking.cpp)
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
    # ONE set_tests_properties for ENVIRONMENT, not two: a second call REPLACES
    # the property rather than adding to it, so splitting these silently drops
    # the first. That is exactly what the previous commit did to TMPDIR here.
    set_tests_properties(render PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${MH_TEST_TMPDIR}")

    # The UI tests provide their own main() so a QApplication exists before any
    # widget is constructed -- Catch2's main would build the widgets first.
    add_executable(mh_ui_tests ui/test_ui.cpp ui/test_theme.cpp ui/test_language.cpp
                               ui/test_image_viewer.cpp ui/test_shortcuts.cpp
                               ui/test_mouse_bindings.cpp
                               ui/test_shortcuts_dialog.cpp
                               ui/test_render_request.cpp
                               ui/test_background.cpp)
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

    # The icon tests AGAIN at device pixel ratio 2. Not redundancy: `theme::icon`
    # rasterised every glyph against the painter's device-pixel viewport while
    # the painter's transform already carried the DPR, so on a retina display
    # every icon in the application was a cropped fragment -- and at DPR 1 the
    # wrong and the right code are byte-identical, so the whole suite was green
    # throughout. A second run under QT_SCALE_FACTOR=2 is the only thing that
    # can see it.
    add_test(NAME ui_icons_hidpi COMMAND mh_ui_tests "[icons]")

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
    # Randomisation is seeded, so it is reproducible -- which is what makes it
    # testable at all, and what lets a user keep a character they liked. Two
    # runs with the same seed must write byte-identical geometry.
    # SAME BASENAME, different directories. An OBJ's `mtllib` line names its own
    # output file, so exporting to app_random_a.obj and app_random_b.obj differs
    # on that line alone and the comparison fails on the filename rather than on
    # the geometry -- which is exactly what happened when this was first written.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rand_a
                        ${CMAKE_CURRENT_BINARY_DIR}/rand_b
                        ${CMAKE_CURRENT_BINARY_DIR}/rand_c)
    add_test(NAME app_random_a COMMAND makehuman
             --random 42 --export ${CMAKE_CURRENT_BINARY_DIR}/rand_a/person.obj)
    set_tests_properties(app_random_a PROPERTIES
        PASS_REGULAR_EXPRESSION "randomised [0-9]+ modifiers \\(seed 42\\)"
        FIXTURES_SETUP app_random_a_obj)
    add_test(NAME app_random_b COMMAND makehuman
             --random 42 --export ${CMAKE_CURRENT_BINARY_DIR}/rand_b/person.obj)
    set_tests_properties(app_random_b PROPERTIES FIXTURES_SETUP app_random_b_obj)
    add_test(NAME app_random_is_reproducible COMMAND ${CMAKE_COMMAND} -E compare_files
             ${CMAKE_CURRENT_BINARY_DIR}/rand_a/person.obj
             ${CMAKE_CURRENT_BINARY_DIR}/rand_b/person.obj)
    set_tests_properties(app_random_is_reproducible PROPERTIES
        FIXTURES_REQUIRED "app_random_a_obj;app_random_b_obj")

    # ...and a different seed must give a different person, or the seed is not
    # reaching the generator. files_differ.cmake rather than WILL_FAIL, for the
    # reason app_rig_unknown records: WILL_FAIL passes on the wrong failure.
    add_test(NAME app_random_c COMMAND makehuman
             --random 43 --export ${CMAKE_CURRENT_BINARY_DIR}/rand_c/person.obj)
    set_tests_properties(app_random_c PROPERTIES FIXTURES_SETUP app_random_c_obj)
    add_test(NAME app_random_seed_matters COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/rand_a/person.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/rand_c/person.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_random_seed_matters PROPERTIES
        FIXTURES_REQUIRED "app_random_a_obj;app_random_c_obj")

    # A non-numeric seed is refused rather than silently treated as 0, which
    # would make `--random abc` and `--random 0` the same person.
    add_test(NAME app_random_bad_seed COMMAND makehuman
             --random notanumber --export ${CMAKE_CURRENT_BINARY_DIR}/app_random_bad.obj)
    set_tests_properties(app_random_bad_seed PROPERTIES
        PASS_REGULAR_EXPRESSION "--random wants a number")

    # The eight generated skin materials had NO picker: --skin-material set them,
    # the .mhm saved them and the exporters wrote them, and the window could not
    # choose one -- so four shipped African tones were unreachable to anyone not
    # using the command line. A group that silently ends up empty renders as a
    # disabled combo box nobody notices, so the app announces what it built.
    #
    # 9 = the eight generated tones plus `default`. The number is asserted, not
    # just ">0": adding a skin material and forgetting to ship it would leave
    # this passing on a smaller set.
    add_test(NAME app_skin_material_picker COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_picker.png)
    set_tests_properties(app_skin_material_picker PROPERTIES
        PASS_REGULAR_EXPRESSION
            # The group NAMES sit between the count and the counts now; both halves
            # of what this case cared about are still pinned.
            "asset groups: 11 \\[.*\\] \\(skin materials: 9, rigs: 2\\)"
        SKIP_REGULAR_EXPRESSION "cannot render")

    # The rig had no picker either: two ship, --rig has always chosen between
    # them, the .mhm already round-trips the choice (`skeleton <name>.mhskel`,
    # written AND read), and the window could not select one.
    #
    # The picker must start on the rig actually in use, so this asserts the app
    # reports the superset rather than the default. Built from rigNameRef(),
    # which by then carries --rig or the loaded .mhm.
    add_test(NAME app_rig_picker COMMAND makehuman
             --rig mixamo_superset
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_rig_picker.png)
    set_tests_properties(app_rig_picker PROPERTIES
        PASS_REGULAR_EXPRESSION "rig mixamo_superset \\(179 bones\\)"
        FAIL_REGULAR_EXPRESSION "cannot load the rig;unknown --rig"
        SKIP_REGULAR_EXPRESSION "cannot render")

    # The picker must START on the material actually in use, not on `default`.
    # It is built from skinMaterialRef(), which by then carries whatever
    # --skin-material or the loaded .mhm chose; building it from the raw option
    # instead would show `default` for a character loaded from a file.
    add_test(NAME app_skin_material_from_mhm COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/app_skin.mhm
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_picker_mhm.png)
    set_tests_properties(app_skin_material_from_mhm PROPERTIES
        PASS_REGULAR_EXPRESSION "skin material african_rich \\(from the file\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_REQUIRED app_skin_mhm)

    # The LIVE RIG path, which is the one File > Export shares with --export and
    # the only one that mutates the character on its way out: the mesh goes back
    # to its rest positions before writing, and is restored afterwards. `.obj`
    # cannot carry a rig, so app_rig_superset below does NOT reach this branch
    # and .glb is needed to exercise it at all.
    add_test(NAME app_live_rig_export COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_live_rig.glb)
    set_tests_properties(app_live_rig_export PROPERTIES
        PASS_REGULAR_EXPRESSION "live rig: rest geometry \\+ posed armature"
        FAIL_REGULAR_EXPRESSION "cannot restore the rest mesh;export failed")

    # ... and the restore puts the RIGHT vertices back.
    #
    # This was recorded as unverifiable ("needs the window"): the CLI exited
    # immediately after exporting, so nothing could look at the character
    # afterwards. Exporting twice in one run gives it something to look at. The
    # .glb swaps the mesh to its rest positions and restores them; the .obj that
    # follows carries no rig, so it always gets the baked POSED mesh -- and must
    # therefore be byte-identical to one written with no .glb before it.
    #
    # A restore that put back the rest positions, or left stale normals, changes
    # that file -- both mutations fail it.
    #
    # WHAT IT STILL CANNOT SEE: the worn-proxy refit inside the restore. Every
    # export refits the proxies against the current body at its top, so the
    # second export fixes them whether or not the restore did. That line is
    # there for the SCREEN -- File > Export leaves the window showing the
    # character -- and only a rendered frame after an export would catch it.
    # Same BASENAME in two directories, deliberately: an OBJ names its material
    # library after itself, so `body.obj` vs `other.obj` differ on the `mtllib`
    # line and nowhere else -- which is exactly the false failure this test
    # produced first time out.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/restore_after
                        ${CMAKE_CURRENT_BINARY_DIR}/restore_alone)
    add_test(NAME app_restore_after_live_rig COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/restore_after/live.glb
             --export ${CMAKE_CURRENT_BINARY_DIR}/restore_after/body.obj)
    add_test(NAME app_restore_baseline COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/restore_alone/body.obj)
    add_test(NAME app_restore_matches COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/restore_after/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/restore_alone/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_match.cmake)
    set_tests_properties(app_restore_matches PROPERTIES
        DEPENDS "app_restore_after_live_rig;app_restore_baseline")

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
    # Blendshapes reach the file. 34, not the 102 `.target` files on disk: they
    # are 34 units x 3 ethnicities, and a character is a blend of the three.
    add_test(NAME app_blendshapes COMMAND makehuman
             --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_bs.glb)
    set_tests_properties(app_blendshapes PROPERTIES
        PASS_REGULAR_EXPRESSION "34 blendshapes"
        FIXTURES_SETUP app_bs_file)
    add_test(NAME app_blendshapes_named COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_bs.glb
             # targetNames is what a DCC labels its shape keys with; without it
             # they import as "Key 1", "Key 2" and are unusable.
             "-DKEYS=targets;targetNames;eye-left-closure;mouth-compression"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_blendshapes_named PROPERTIES FIXTURES_REQUIRED app_bs_file)

    # FBX too, through the assimp scene path. Verified in Blender: the body
    # carries 34 deforming shape keys and the worn eyes carry NONE -- one entry
    # may hold morphs, exactly as one entry may hold the skin.
    add_test(NAME app_blendshapes_fbx COMMAND makehuman
             --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_bs.fbx)
    set_tests_properties(app_blendshapes_fbx PROPERTIES
        PASS_REGULAR_EXPRESSION "34 blendshapes"
        FIXTURES_SETUP app_bs_fbx_file)
    add_test(NAME app_blendshapes_fbx_named COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_bs.fbx
             # FBX stores a blend shape as a deformer named after the target.
             "-DKEYS=eye-left-closure;mouth-compression;BlendShape"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_blendshapes_fbx_named PROPERTIES
        FIXTURES_REQUIRED app_bs_fbx_file)

    # ...and USD, through UsdSkel BlendShape.
    add_test(NAME app_blendshapes_usda COMMAND makehuman
             --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_bs.usda)
    set_tests_properties(app_blendshapes_usda PROPERTIES
        PASS_REGULAR_EXPRESSION "34 blendshapes"
        FIXTURES_SETUP app_bs_usda_file)
    add_test(NAME app_blendshapes_usda_bound COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_bs.usda
             # SkelRoot is the one that matters: usdchecker rejects
             # SkelBindingAPI on a prim not rooted at one, even with no
             # skeleton, and there is no skeleton in this export.
             "-DKEYS=def SkelRoot;SkelBindingAPI;skel:blendShapes;def BlendShape;pointIndices"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_blendshapes_usda_bound PROPERTIES
        FIXTURES_REQUIRED app_bs_usda_file)

    # Dual quaternion skinning is reachable from the command line, and an
    # unknown method is refused rather than silently falling back to linear --
    # a user who asked for DQS and got LBS has no way to tell.
    # The SAME basename in two directories, deliberately. An OBJ's third line
    # is `mtllib <stem>.mtl`, so exports to app_lbs.obj and app_dqs.obj differ
    # on that line whatever the skinning does -- measured: with the flag forced
    # off the files still differed, and the gate below stayed green while
    # testing nothing.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/skin_lbs
                        ${CMAKE_CURRENT_BINARY_DIR}/skin_dqs)
    add_test(NAME app_skinning_linear COMMAND makehuman
             --rig mixamo_superset --pose tpose --skinning linear
             --export ${CMAKE_CURRENT_BINARY_DIR}/skin_lbs/pose.obj)
    set_tests_properties(app_skinning_linear PROPERTIES FIXTURES_SETUP app_lbs_obj)
    add_test(NAME app_skinning_dqs COMMAND makehuman
             --rig mixamo_superset --pose tpose --skinning dqs
             --export ${CMAKE_CURRENT_BINARY_DIR}/skin_dqs/pose.obj)
    set_tests_properties(app_skinning_dqs PROPERTIES
        PASS_REGULAR_EXPRESSION "wrote .*pose\\.obj"
        FIXTURES_SETUP app_dqs_obj)
    # The flag has to CHANGE something. Asserting only that the export succeeded
    # passes with the flag disabled.
    add_test(NAME app_skinning_dqs_differs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/skin_lbs/pose.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/skin_dqs/pose.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_skinning_dqs_differs PROPERTIES
        FIXTURES_REQUIRED "app_lbs_obj;app_dqs_obj")
    # Pose-space correctives through the application, end to end: manifest ->
    # compile -> blob -> bind -> signal -> weights -> deltas -> skinning.
    #
    # The fixture is committed (tests/correctives/) and its bulge is keyed at
    # the shipped T-pose's OWN signal, so the pose lands on the example and the
    # corrective is fully on. Same basename in two directories, for the mtllib
    # reason spelled out above.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/corr_off
                        ${CMAKE_CURRENT_BINARY_DIR}/corr_on)
    add_test(NAME app_correctives_off COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_off/pose.obj)
    set_tests_properties(app_correctives_off PROPERTIES
        PASS_REGULAR_EXPRESSION "wrote .*pose\\.obj"
        FAIL_REGULAR_EXPRESSION "export failed"
        FIXTURES_SETUP app_corr_off)
    add_test(NAME app_correctives_on COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/correctives.json
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_on/pose.obj)
    set_tests_properties(app_correctives_on PROPERTIES
        PASS_REGULAR_EXPRESSION "correctives: 2 poses, 1 drivers"
        # The live-rig warning must NOT appear here: an .obj is baked, so this
        # file carries the deformation and telling the user it does not would be
        # a lie in the other direction.
        #
        # What it catches, measured: the warning moved OUT of the `if (liveRig)`
        # branch -- this test fails, and app_correctives_live_rig_says_so still
        # passes. What it CANNOT catch, also measured: the guard inside that
        # branch weakened to `if (true)`, which no .obj export ever reaches. The
        # condition that matters here is WHERE the warning sits, not what it
        # tests once it is there.
        FAIL_REGULAR_EXPRESSION "export failed;correctives could not be applied;do not reach a live-rig"
        FIXTURES_SETUP app_corr_on)
    # The flag has to CHANGE the geometry. Asserting only that the export
    # succeeded passes with the correctives silently doing nothing, which is
    # exactly what a weight of zero everywhere would look like -- measured: with
    # the runtime not handed to poseMesh, the two exports are identical and
    # every other assertion here still passes.
    add_test(NAME app_correctives_move_the_mesh COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/corr_off/pose.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/corr_on/pose.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_correctives_move_the_mesh PROPERTIES
        FIXTURES_REQUIRED "app_corr_off;app_corr_on")

    # ...and the export that CANNOT carry them says so.
    #
    # Found by the Blender round-trip, not by any of the tests above: a live-rig
    # .glb written with --correctives is BYTE-IDENTICAL to one written without.
    # The live-rig path swaps the mesh back to `rig.restCoords`, which is
    # captured before the correctives run and is uncorrected on purpose -- a
    # pose-space corrective is not a rest shape, and baking one into the rest
    # geometry would put the bulge on every pose the consumer sets afterwards.
    #
    # So the deformation is genuinely not expressible in the file, and the only
    # dishonest part was the silence. The app announced "correctives: 2 poses, 1
    # drivers" and then wrote a file with none in it -- "geometry that silently
    # does not appear", which the manifest spec names as the worst failure the
    # format has.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/corr_live
                        ${CMAKE_CURRENT_BINARY_DIR}/corr_live_off)
    #
    # Both sides use `nowrinkle.json`, and that is not incidental. The shipped
    # fixture gained a wrinkle sheet, and a wrinkle DOES reach a live-rig .glb
    # -- baked into the material's normal texture, which a GLB embeds. So with
    # `correctives.json` these two files differ for a reason that has nothing to
    # do with the geometry this case is about. Caught by the full gate run, one
    # chunk after the case was written.
    add_test(NAME app_correctives_live_rig_says_so COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/nowrinkle.json
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_live/pose.glb)
    set_tests_properties(app_correctives_live_rig_says_so PROPERTIES
        PASS_REGULAR_EXPRESSION "corrective geometry does not reach a live-rig \\.glb"
        FIXTURES_SETUP app_corr_live_on)
    add_test(NAME app_correctives_live_rig_baseline COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_live_off/pose.glb)
    set_tests_properties(app_correctives_live_rig_baseline PROPERTIES
        FIXTURES_SETUP app_corr_live_off)
    # The warning's own claim, pinned. Without this the message is a comment
    # that happens to be printed: someone could make the .glb carry the
    # corrective GEOMETRY and leave the warning in, and every other test here
    # would stay green while the app told users their deformation was missing
    # from a file that has it. When this one fails, DELETE the warning -- do not
    # weaken it.
    add_test(NAME app_correctives_live_rig_unchanged COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/corr_live_off/pose.glb
             -DB=${CMAKE_CURRENT_BINARY_DIR}/corr_live/pose.glb
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_correctives_live_rig_unchanged PROPERTIES
        FIXTURES_REQUIRED "app_corr_live_on;app_corr_live_off")

    # THE SECOND CONSUMER, through the application. The shipped fixture is a
    # version 2 manifest now: `arm_out` names a crease sheet, `rest` names none
    # with an explicit "".
    #
    # A render rather than an export, because a wrinkle map is texture-space
    # and no exported format carries one -- the screen is the only place this
    # is visible at all.
    #
    # The sheet is `creases.png` and NOT `shoulder.png`, which is what it was
    # called until the render was looked at: it is an unmasked field of ridges
    # covering the whole UV square, so it creases the entire body including the
    # face. That is the right shape for a fixture -- the largest possible signal
    # for the smallest possible file -- and the wrong name for one. A real
    # authored sheet is flat (128,128,255) everywhere except the region it
    # creases; nothing here needs that, and pretending otherwise in the filename
    # is how a fixture starts being read as an example.
    add_test(NAME app_wrinkle_render COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/correctives.json
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_wrinkle.png)
    set_tests_properties(app_wrinkle_render PROPERTIES
        # Named and QUANTIFIED. A weight near zero would mean the RBF weights
        # never reached the chooser, which is the failure a "rendered ..." check
        # cannot see.
        #
        # 0.98, not 1.00, and the difference was worth chasing rather than
        # rounding: measured weights at this pose are rest=0.002073,
        # arm_out=0.980632. An identity-RHS solve reproduces an example pose
        # EXACTLY at its own centre, so 0.98 says the T-pose sits slightly off
        # the signal the fixture recorded -- the signal is decomposed from a
        # float32 Mat4, and the recorded centre was measured through the same
        # path. 1.00 was a number I wrote down rather than ran.
        PASS_REGULAR_EXPRESSION "wrinkle: creases\\.png at 0\\.98"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "nothing to draw;cannot write;maps not shown"
        FIXTURES_SETUP app_wrinkle_on)
    # ...against the SAME corrective set with its wrinkle removed.
    #
    # `nowrinkle.json` is `correctives.json` with both `wrinkle` fields empty:
    # same drivers, same poses, same deltas, same geometry. It exists because
    # the obvious control -- no `--correctives` at all -- made the comparison
    # below DECORATIVE, and the mutation found it: forcing
    # `body.wrinkleWeight = 0` still left the two frames differing, because they
    # were differing by the deltoid BULGE that `app_correctives_move_the_mesh`
    # already covers. One variable per experiment, or the experiment measures
    # the other one.
    add_test(NAME app_wrinkle_render_off COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/nowrinkle.json
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_wrinkle_off.png)
    set_tests_properties(app_wrinkle_render_off PROPERTIES
        # No sheet named anywhere, so nothing may be reported as chosen.
        FAIL_REGULAR_EXPRESSION "wrinkle: ;maps not shown"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_wrinkle_off)
    # The sheet has to CHANGE the frame. Asserting only that a weight was
    # printed passes with the map never reaching the renderer -- which is
    # exactly what a chooser wired to nothing would look like.
    add_test(NAME app_wrinkle_changes_the_frame COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/app_wrinkle.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/app_wrinkle_off.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_wrinkle_changes_the_frame PROPERTIES
        FIXTURES_REQUIRED "app_wrinkle_on;app_wrinkle_off")

    # THE EXPORT, which is where a wrinkle map otherwise stops existing.
    #
    # No interchange format has a pose-driven normal map -- glTF, FBX and
    # UsdSkel each give a material ONE normal texture -- so the blend is baked
    # at the pose being written and the exported material points at the result.
    # Without this the app reports "wrinkle: creases.png at 0.98" and then
    # writes a file with no crease in it, which is the same silence the
    # live-rig corrective export was caught doing.
    #
    # `african_light` because it is one of the eight shipped skins that names a
    # normal map; `default.mhmat` has none, and the bake against a synthesised
    # flat base is a different path.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/wrbake)
    add_test(NAME app_wrinkle_export COMMAND makehuman
             --rig mixamo_superset --pose tpose --skin-material african_light
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/correctives.json
             --export ${CMAKE_CURRENT_BINARY_DIR}/wrbake/body.obj)
    set_tests_properties(app_wrinkle_export PROPERTIES
        # The TEXEL COUNT, not just the filename. A bake that copied the base
        # through unchanged writes a perfectly good file with the right name,
        # and it reports 0 here.
        #
        # At least six digits, spelled out one class at a time: CMake's regex
        # flavour has NO brace repetition, so `[0-9]{6,}` matched literally and
        # the case failed against a line that was plainly correct. Measured on
        # the shipped pairing -- a 1024x1024 skin normal under an unmasked
        # sheet -- 994,383 of 1,048,576 texels moved.
        #
        # The count is over X AND Y ONLY. Counting z as well left this
        # decorative in turn: z moves for any base that is not exactly unit
        # length, purely from renormalizing, so a bake mutated to copy x and y
        # through unchanged still reported 1,048,534 "moved".
        PASS_REGULAR_EXPRESSION
            "baked the wrinkle into body_normal\\.png at 0\\.98 \\([0-9][0-9][0-9][0-9][0-9][0-9][0-9]* of 1048576 texels moved\\)"
        FAIL_REGULAR_EXPRESSION "export failed;cannot bake"
        FIXTURES_SETUP app_wrinkle_export)
    # The exported MATERIAL has to point at the bake. Printing that a file was
    # written proves nothing about what the .mtl says, and an .mtl still naming
    # skin_normal.png is a file whose creases are only in a sidecar nothing
    # reads.
    add_test(NAME app_wrinkle_export_material COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/wrbake/body.mtl
             "-DKEYS=body_normal.png"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_wrinkle_export_material PROPERTIES
        FIXTURES_REQUIRED app_wrinkle_export)
    # There is deliberately NO files_differ between the bake and the skin's own
    # normal map here. It was written, and it was decorative: the sidecar is a
    # RE-ENCODED PNG, so it differs from the source byte-for-byte whatever its
    # pixels say. Measured -- a bake mutated to copy the base through unchanged
    # passed it. The texel count in the regex above is what replaced it.

    # AIMING THE EYES, through the application.
    #
    # Directive 12.3 puts eyes in the rig layer as skeleton and constraint work,
    # and this is the constraint reaching a character: `--look-at` writes the
    # two eye rotations into the bone-local pose the skinning already consumes.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lookat)
    add_test(NAME app_look_at COMMAND makehuman
             --look-at 1.5,7.8,6.0
             --render ${CMAKE_CURRENT_BINARY_DIR}/lookat/at.png)
    set_tests_properties(app_look_at PROPERTIES
        # Both figures, and they must DIFFER: the target is off to the
        # character's own left, so the left eye is already closer to it and
        # turns less. A single number here, or two equal ones, would be a
        # look-at computed from the midpoint between the eyes rather than from
        # each eye -- which is the whole thing a convergence constraint is for.
        #
        # 14.9 and 24.9, where test_eye_aim.cpp measures 6.8 and 16.9 for the
        # same target. Not a discrepancy: the unit test rigs the RAW base.obj
        # and the app poses the default CHARACTER, so `updateJoints` puts the
        # eyes somewhere else. Checked rather than assumed -- `--rig default`
        # and `--rig mixamo_superset` both report 14.9/24.9, so it is the mesh
        # and not the skeleton.
        PASS_REGULAR_EXPRESSION "look-at: eyes turned 14\\.9 and 24\\.9 degrees"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "cannot aim;nothing to draw"
        FIXTURES_SETUP app_look_at)
    add_test(NAME app_look_at_baseline COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/lookat/ahead.png)
    set_tests_properties(app_look_at_baseline PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_look_at_off)
    # The eyes have to actually MOVE in the frame. Printing two angles proves
    # the constraint ran, not that its output reached the skinning.
    add_test(NAME app_look_at_moves_the_eyes COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/lookat/ahead.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/lookat/at.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_look_at_moves_the_eyes PROPERTIES
        FIXTURES_REQUIRED "app_look_at;app_look_at_off")
    # A FACS gaze unit AND a look-at is a silent partial application, so it is
    # not silent any more.
    #
    # The eyes had a driver before --look-at existed: AU61-64 rotate the same
    # two bones. Measured: --facs AU61=1.0 alone moves 1,721 vertices,
    # --look-at alone 1,144, and together 1,722 -- neither. AU61 drives 6 bones
    # and the aim replaces 2, so the eyelids follow the unit while the eyeballs
    # ignore it. Overriding is correct; announcing it is the fix.
    # --litsphere, with --skin kept working as an alias (directive 13.6:
    # "ensure interoperability and aliases ... both new users and old users who
    # are used in certain ways").
    #
    # The flag picks a viewport MATCAP, not a material -- `--skin african` still
    # exports DefaultSkin, which is correct and reads as a bug. Renaming it says
    # what it is; the alias means nobody's muscle memory breaks.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lits)
    # The litsphere SURVIVES a save and reload.
    #
    # It was deliberately not saved while the flag was called `--skin`: the
    # reference's line is `skinMaterial <path to a .mhmat>` and a litsphere PNG
    # is not a material, so writing one there would have misdescribed the value.
    # The rename settled that, and the key is its own honest `litsphere` line.
    #
    # It goes in `unhandled`, which is how every app-level key here travels --
    # the reference keeps lines it does not interpret, so a file this writes
    # still loads in MakeHuman 1.x, which `core/Mhm.h` commits to outright.
    add_test(NAME app_litsphere_save COMMAND makehuman
             --litsphere african --skin-material african_light
             --save ${CMAKE_CURRENT_BINARY_DIR}/lits/saved.mhm)
    set_tests_properties(app_litsphere_save PROPERTIES FIXTURES_SETUP app_lits_saved)
    add_test(NAME app_litsphere_save_line COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/lits/saved.mhm
             "-DKEYS=litsphere african"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_litsphere_save_line PROPERTIES
        FIXTURES_REQUIRED app_lits_saved)
    # Reloading it has to APPLY the value, not merely announce it.
    #
    # This case asserted only the "(from the file)" message at first, and a
    # mutation that read the line, printed it and then threw the value away
    # passed all twelve cases. So it renders, and the render is compared: equal
    # to an explicit `--litsphere african`, and different from `caucasian`.
    #
    # Every render here names `--skin-material african_light` because the
    # DEFAULT material has autoBlendSkin on, which overrides the litsphere
    # entirely -- under it these three files would be identical whatever the
    # code did.
    add_test(NAME app_litsphere_reload COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/lits/saved.mhm
             --render ${CMAKE_CURRENT_BINARY_DIR}/lits/reloaded.png)
    set_tests_properties(app_litsphere_reload PROPERTIES
        PASS_REGULAR_EXPRESSION "litsphere african \\(from the file\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_REQUIRED app_lits_saved
        FIXTURES_SETUP app_lits_reloaded)
    # Both sides LOAD the same file, so the character, rig, modifiers and camera
    # are identical and the litsphere is the only variable. Comparing the reload
    # against a render that loaded NOTHING was the first attempt and it failed
    # for the uninteresting reason: the saved file also carries a skeleton and a
    # camera the direct render never had.
    add_test(NAME app_litsphere_reload_explicit COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/lits/saved.mhm --litsphere african
             --render ${CMAKE_CURRENT_BINARY_DIR}/lits/reload_explicit.png)
    set_tests_properties(app_litsphere_reload_explicit PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render" FIXTURES_SETUP app_lits_rex)
    add_test(NAME app_litsphere_reload_matches_explicit COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/lits/reloaded.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/lits/reload_explicit.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_litsphere_reload_matches_explicit PROPERTIES
        FIXTURES_REQUIRED "app_lits_reloaded;app_lits_rex")
    # ...and the file's value is not just the default arriving by another route.
    add_test(NAME app_litsphere_reload_is_not_the_default COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/lits/reloaded.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/lits/override.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_litsphere_reload_is_not_the_default PROPERTIES
        FIXTURES_REQUIRED "app_lits_reloaded;app_lits_override")
    # The command line WINS over the file -- same precedence as the rig, the
    # pose, the eye colour and the skin material. Without this the flag would be
    # silently ignored for any character that had ever been saved.
    add_test(NAME app_litsphere_flag_beats_file COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/lits/saved.mhm --litsphere caucasian
             --render ${CMAKE_CURRENT_BINARY_DIR}/lits/override.png)
    set_tests_properties(app_litsphere_flag_beats_file PROPERTIES
        FAIL_REGULAR_EXPRESSION "litsphere african \\(from the file\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_REQUIRED app_lits_saved
        FIXTURES_SETUP app_lits_override)


    # The picker group is called "Litsphere", not "Skin".
    #
    # "Skin" listed LITSPHERES -- viewport matcaps -- while "Skin material"
    # listed the eight textured .mhmat files, and the two sat next to each other
    # in the same panel. The FLAG collision went with the --litsphere rename;
    # this is the same collision one layer up.
    #
    # The group names are listed in the report line so this is observable at
    # all: nothing else printed them, and a rename nothing can see is a rename
    # nothing can gate. Bracketed and comma-separated so "Skin material" cannot
    # satisfy a check meant for "Skin".
    # THE TEETH SLOT -- the first of the seven empty proxy choosers to be filled.
    #
    # Directive 13.5: "if there are no assets or data from legacy, procedurally
    # generate and wire through". Legacy ships NOTHING here -- measured, zero
    # .mhclo under every one of the seven proxy directories, because upstream
    # distributes them as downloadable asset packs.
    #
    # But the SHAPE was never missing: base.obj carries `helper-upper-teeth` and
    # `helper-lower-teeth`, cage geometry that exists so a proxy can be fitted to
    # it. `tools/make_helper_proxies.py` extracts them, so nothing is invented.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/teeth)
    add_test(NAME app_teeth_group COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/teeth/g.mhm)
    set_tests_properties(app_teeth_group PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*Teeth")
    # Wearing them has to add GEOMETRY, not just a line of output.
    add_test(NAME app_teeth_worn COMMAND makehuman
             --teeth teeth --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/worn.obj)
    set_tests_properties(app_teeth_worn PROPERTIES
        # The MATCAP, not just the name: see the comment on the "wearing" line in
        # main.cpp. Without this a slot handed another slot's matcap passes.
        PASS_REGULAR_EXPRESSION "wearing Teeth \\(.*\\) lit by skinmat_teeth"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_teeth_worn)
    add_test(NAME app_teeth_none COMMAND makehuman
             --teeth none --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/none.obj)
    set_tests_properties(app_teeth_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Teeth"
        FIXTURES_SETUP app_teeth_none)
    add_test(NAME app_teeth_add_geometry COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/teeth/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/teeth/none.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_teeth_add_geometry PROPERTIES
        FIXTURES_REQUIRED "app_teeth_worn;app_teeth_none")
    # The reason this slot is worth filling from the helper cage rather than
    # from invented shape: the base mesh already weights `helper-upper-teeth`
    # entirely to `head` and `helper-lower-teeth` entirely to `jaw`, so a jaw
    # drop separates the two arches for free. This is the assertion that the
    # teeth are RIGGED and not merely present -- 68 of the proxy's 136 vertices
    # follow AU26 downwards and the other 68 do not move at all.
    #
    # `--facs AU26=1.0` is the jaw drop; the control export is the same
    # character with no FACS at all, so the only difference between the two
    # files is the pose.
    add_test(NAME app_teeth_jaw_rest COMMAND makehuman
             --teeth teeth --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/jaw_rest.obj)
    set_tests_properties(app_teeth_jaw_rest PROPERTIES FIXTURES_SETUP app_teeth_jaw_rest)
    add_test(NAME app_teeth_jaw_open COMMAND makehuman
             --teeth teeth --facs AU26=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/jaw_open.obj)
    set_tests_properties(app_teeth_jaw_open PROPERTIES FIXTURES_SETUP app_teeth_jaw_open)
    add_test(NAME app_teeth_lower_follows_jaw COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/teeth/jaw_rest.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/teeth/jaw_open.obj
             -DGROUP=teeth -DEXPECT_MOVED=68 -DEXPECT_STILL=68
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_teeth_lower_follows_jaw PROPERTIES
        FIXTURES_REQUIRED "app_teeth_jaw_rest;app_teeth_jaw_open")
    # And the round trip, which is what makes the `teeth` line in a .mhm worth
    # writing: saved wearing them, the file names them and reopens wearing them.
    add_test(NAME app_teeth_saved COMMAND makehuman
             --teeth teeth --save ${CMAKE_CURRENT_BINARY_DIR}/teeth/saved.mhm)
    set_tests_properties(app_teeth_saved PROPERTIES FIXTURES_SETUP app_teeth_saved)
    add_test(NAME app_teeth_save_names_it COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/teeth/saved.mhm
             "-DKEYS=teeth Teeth 7e3a6d94-2b51-4c8f-9a17-5d0c8e2f4b63"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_teeth_save_names_it PROPERTIES
        FIXTURES_REQUIRED app_teeth_saved)
    add_test(NAME app_teeth_reload_wears_them COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/teeth/saved.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/reloaded.obj)
    set_tests_properties(app_teeth_reload_wears_them PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Teeth"
        FIXTURES_REQUIRED app_teeth_saved)

    # THE TONGUE -- the second helper-cage slot, and the one that proves the
    # slot machinery is machinery rather than a special case for teeth.
    #
    # Its claim is a DIFFERENT SHAPE from the teeth's, which is why it is worth
    # its own assertion rather than a copy. The teeth split 68/68 because the two
    # arches hang off different bones. The tongue does not split at all: all 226
    # vertices are dominated by a `tongue*` bone, and the whole tongue chain
    # hangs off `jaw` (measured from data/rigs/default.mhskel), so a jaw drop
    # carries every one of them and leaves none behind.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tongue)
    add_test(NAME app_tongue_group COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/tongue/g.mhm)
    set_tests_properties(app_tongue_group PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*Tongue")
    add_test(NAME app_tongue_worn COMMAND makehuman
             --tongue tongue --export ${CMAKE_CURRENT_BINARY_DIR}/tongue/worn.obj)
    set_tests_properties(app_tongue_worn PROPERTIES
        # The MATCAP, not just the name: see the comment on the "wearing" line in
        # main.cpp. Without this a slot handed another slot's matcap passes.
        PASS_REGULAR_EXPRESSION "wearing Tongue \\(.*\\) lit by skinmat_tongue"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_tongue_worn)
    add_test(NAME app_tongue_none COMMAND makehuman
             --tongue none --export ${CMAKE_CURRENT_BINARY_DIR}/tongue/none.obj)
    set_tests_properties(app_tongue_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Tongue"
        FIXTURES_SETUP app_tongue_none)
    add_test(NAME app_tongue_add_geometry COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/tongue/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/tongue/none.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_tongue_add_geometry PROPERTIES
        FIXTURES_REQUIRED "app_tongue_worn;app_tongue_none")
    add_test(NAME app_tongue_jaw_open COMMAND makehuman
             --tongue tongue --facs AU26=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/tongue/jaw_open.obj)
    set_tests_properties(app_tongue_jaw_open PROPERTIES FIXTURES_SETUP app_tongue_jaw_open)
    # EXPECT_STILL 0 is the whole point: a tongue that kept vertices behind
    # would mean the chain had been re-parented off the jaw.
    add_test(NAME app_tongue_follows_jaw COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/tongue/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/tongue/jaw_open.obj
             -DGROUP=tongue -DEXPECT_MOVED=226 -DEXPECT_STILL=0
             # 4e-4 dm: the vertex nearest the jaw pivot rises by exactly that
             # under the rotation, measured. The far end drops 0.2334, so a sign
             # error is still 600x outside this.
             -DRISE_TOLERANCE=4
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_tongue_follows_jaw PROPERTIES
        FIXTURES_REQUIRED "app_tongue_worn;app_tongue_jaw_open")
    # And the round trip, the same way the teeth prove theirs.
    add_test(NAME app_tongue_saved COMMAND makehuman
             --tongue tongue --save ${CMAKE_CURRENT_BINARY_DIR}/tongue/saved.mhm)
    set_tests_properties(app_tongue_saved PROPERTIES FIXTURES_SETUP app_tongue_saved)
    add_test(NAME app_tongue_save_names_it COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/tongue/saved.mhm
             "-DKEYS=tongue Tongue b5f1c40a-9d27-4e63-8a1b-2c7e06f9d834"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_tongue_save_names_it PROPERTIES
        FIXTURES_REQUIRED app_tongue_saved)
    add_test(NAME app_tongue_reload_wears_them COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/tongue/saved.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/tongue/reloaded.obj)
    set_tests_properties(app_tongue_reload_wears_them PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Tongue"
        FIXTURES_REQUIRED app_tongue_saved)
    # Both slots at once: they are separate meshes with separate materials, and
    # a table that overwrote one with the other would still pass every test above.
    add_test(NAME app_teeth_and_tongue COMMAND makehuman
             --teeth teeth --tongue tongue
             --export ${CMAKE_CURRENT_BINARY_DIR}/tongue/both.obj)
    set_tests_properties(app_teeth_and_tongue PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Teeth"
        FIXTURES_SETUP app_teeth_and_tongue)
    add_test(NAME app_teeth_and_tongue_both_groups COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/tongue/both.obj
             "-DKEYS=g teeth;g tongue"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_teeth_and_tongue_both_groups PROPERTIES
        FIXTURES_REQUIRED app_teeth_and_tongue)

    # THE HAIR -- the third helper-cage slot, and the first whose cage is NOT
    # rigid with one body part. Measured from data/rigs/default_weights.mhw:
    # of its 428 vertices, 272 are dominated by `head` and 116 by spine01-03,
    # because the cage covers hair falling down the BACK. So the hair is the
    # slot that proves the fitting follows the body rather than a single bone.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/hair)
    add_test(NAME app_hair_group COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/hair/g.mhm)
    set_tests_properties(app_hair_group PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*Hair")
    add_test(NAME app_hair_worn COMMAND makehuman
             --hair hair --export ${CMAKE_CURRENT_BINARY_DIR}/hair/worn.obj)
    set_tests_properties(app_hair_worn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Hair \\(.*\\) lit by skinmat_hair"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_hair_worn)
    add_test(NAME app_hair_none COMMAND makehuman
             --hair none --export ${CMAKE_CURRENT_BINARY_DIR}/hair/none.obj)
    set_tests_properties(app_hair_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Hair"
        FIXTURES_SETUP app_hair_none)
    add_test(NAME app_hair_add_geometry COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/hair/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/hair/none.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_hair_add_geometry PROPERTIES
        FIXTURES_REQUIRED "app_hair_worn;app_hair_none")
    add_test(NAME app_hair_saved COMMAND makehuman
             --hair hair --save ${CMAKE_CURRENT_BINARY_DIR}/hair/saved.mhm)
    set_tests_properties(app_hair_saved PROPERTIES FIXTURES_SETUP app_hair_saved)
    add_test(NAME app_hair_save_names_it COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/hair/saved.mhm
             "-DKEYS=hair Hair 3f8c21d5-6b04-4e79-9c52-1a7de0b46f38"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_hair_save_names_it PROPERTIES
        FIXTURES_REQUIRED app_hair_saved)
    # The SAME jaw drop that moves 68 of the teeth and all 226 of the tongue
    # moves NONE of the hair. Three slots, three different answers to one pose,
    # which is what shows the fitting is per-vertex from the base mesh's own
    # weights rather than a special case written per slot.
    add_test(NAME app_hair_jaw_open COMMAND makehuman
             --hair hair --facs AU26=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/hair/jaw_open.obj)
    set_tests_properties(app_hair_jaw_open PROPERTIES FIXTURES_SETUP app_hair_jaw_open)
    add_test(NAME app_hair_ignores_the_jaw COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/hair/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/hair/jaw_open.obj
             -DGROUP=hair -DEXPECT_MOVED=0 -DEXPECT_STILL=428
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_hair_ignores_the_jaw PROPERTIES
        FIXTURES_REQUIRED "app_hair_worn;app_hair_jaw_open")
    # ...and the positive half, because "nothing moved" on its own also
    # describes a proxy that is frozen, or absent. Under a T-pose every one of
    # the 428 follows the body. Direction is deliberately not asserted here:
    # measured, the cage falls 0.5698 at one end and rises 0.0957 at the other,
    # which is the spine straightening while the shoulders come up.
    add_test(NAME app_hair_tpose COMMAND makehuman
             --hair hair --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/hair/tpose.obj)
    set_tests_properties(app_hair_tpose PROPERTIES FIXTURES_SETUP app_hair_tpose)
    add_test(NAME app_hair_follows_the_body COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/hair/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/hair/tpose.obj
             -DGROUP=hair -DEXPECT_MOVED=428 -DEXPECT_STILL=0
             -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_hair_follows_the_body PROPERTIES
        FIXTURES_REQUIRED "app_hair_worn;app_hair_tpose")
    add_test(NAME app_hair_reload_wears_it COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/hair/saved.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/hair/reloaded.obj)
    set_tests_properties(app_hair_reload_wears_it PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Hair"
        FIXTURES_REQUIRED app_hair_saved)

    # ------------------------------------------------------------------
    # Clothes: the first slot holding SEVERAL choosable assets. Teeth combined
    # two cages into ONE proxy; here two cages stay two garments the user picks
    # between, which is what made `Slot.slot` separate from `Slot.key`.
    #
    # Measured from data/rigs/default_weights.mhw, the two cages have
    # different shapes: `helper-skirt` is 720 vertices spread across both
    # upperlegs, both lowerlegs and spine04, and `helper-tights` is 2674
    # reaching from spine01/02 down to foot.L and foot.R (140 each).
    #
    # `helper-genital` is deliberately NOT here. It is a genitalia fitting
    # cage, not a garment: RENDERED AND LOOKED AT, a proxy cut from it is an
    # anatomical bulge in fabric colour, and calling it "Briefs" would have
    # shipped a mislabel that every count and every gate passed happily. It
    # belongs to a future genitalia slot, not to Clothes.
    #
    # So one input separates them, and it is a MORPH rather than a pose, which
    # is the half the teeth/tongue/hair trio never exercised. Measured on the
    # exports, `r-foot-trans-in|out=1.0` moves 118 of the skirt's 720 and 412
    # of the tights' 2674, while the body moves the same 1324 vertices in both
    # cases: two garments in one slot, two different answers to one input,
    # which a slot that special-cased its garments could not produce. The zero
    # case is the jaw drop below.
    #
    # The morph is a TRANSLATION on purpose. `r-foot-scale` was the obvious
    # choice and is confounded: the tights cover the feet, so they become the
    # model's lowest point, and scaling the foot re-grounds the whole character
    # -- measured, every one of the 13380 body vertices and all 2674 tights
    # vertices shifted by a uniform 0.0138. The skirt showed 0 under the same
    # morph only because it does not reach the sole, so the gate would have
    # looked like it was discriminating when it was reading a global
    # translation. Moving the foot sideways changes no ground contact.
    # ------------------------------------------------------------------
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/clothes)
    add_test(NAME app_clothes_group COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/clothes/g.mhm)
    set_tests_properties(app_clothes_group PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*Clothes")

    add_test(NAME app_clothes_none COMMAND makehuman
             --clothes none --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/none.obj)
    set_tests_properties(app_clothes_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Skirt;wearing Tights"
        FIXTURES_SETUP app_clothes_none)

    # Skirt -- both legs and the lower spine, but no feet.
    add_test(NAME app_clothes_skirt_worn COMMAND makehuman
             --clothes skirt --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/skirt.obj)
    set_tests_properties(app_clothes_skirt_worn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Skirt \\(720 verts\\) lit by skinmat_clothes"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_clothes_skirt_worn)
    add_test(NAME app_clothes_skirt_foot COMMAND makehuman
             --clothes skirt "--set=armslegs/r-foot-trans-in|out=1.0"
             --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/skirt_foot.obj)
    set_tests_properties(app_clothes_skirt_foot PROPERTIES
        FIXTURES_SETUP app_clothes_skirt_foot)
    add_test(NAME app_clothes_skirt_partly_follows_the_foot COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/clothes/skirt.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/clothes/skirt_foot.obj
             -DGROUP=clothes -DEXPECT_MOVED=118 -DEXPECT_STILL=602
             -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_clothes_skirt_partly_follows_the_foot PROPERTIES
        FIXTURES_REQUIRED "app_clothes_skirt_worn;app_clothes_skirt_foot")

    # Tights -- the only one of the three that reaches the feet.
    add_test(NAME app_clothes_tights_worn COMMAND makehuman
             --clothes tights --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/tights.obj)
    set_tests_properties(app_clothes_tights_worn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Tights \\(2674 verts\\) lit by skinmat_clothes"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_clothes_tights_worn)
    add_test(NAME app_clothes_tights_foot COMMAND makehuman
             --clothes tights "--set=armslegs/r-foot-trans-in|out=1.0"
             --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/tights_foot.obj)
    set_tests_properties(app_clothes_tights_foot PROPERTIES
        FIXTURES_SETUP app_clothes_tights_foot)
    add_test(NAME app_clothes_tights_follow_the_foot COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/clothes/tights.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/clothes/tights_foot.obj
             -DGROUP=clothes -DEXPECT_MOVED=412 -DEXPECT_STILL=2262
             -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_clothes_tights_follow_the_foot PROPERTIES
        FIXTURES_REQUIRED "app_clothes_tights_worn;app_clothes_tights_foot")

    # The positive half for the slot as a whole: "nothing moved" also describes
    # a garment that is frozen or absent, so one of them has to follow a pose.
    add_test(NAME app_clothes_tpose COMMAND makehuman
             --clothes tights --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/tpose.obj)
    set_tests_properties(app_clothes_tpose PROPERTIES FIXTURES_SETUP app_clothes_tpose)
    add_test(NAME app_clothes_follow_the_body COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/clothes/tights.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/clothes/tpose.obj
             -DGROUP=clothes -DEXPECT_MOVED=2674 -DEXPECT_STILL=0
             -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_clothes_follow_the_body PROPERTIES
        FIXTURES_REQUIRED "app_clothes_tights_worn;app_clothes_tpose")

    add_test(NAME app_clothes_add_geometry COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/clothes/tights.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/clothes/none.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_clothes_add_geometry PROPERTIES
        FIXTURES_REQUIRED "app_clothes_tights_worn;app_clothes_none")

    # Round trip: the slot holds several garments, so saving has to name WHICH.
    add_test(NAME app_clothes_saved COMMAND makehuman
             --clothes skirt --save ${CMAKE_CURRENT_BINARY_DIR}/clothes/saved.mhm)
    set_tests_properties(app_clothes_saved PROPERTIES FIXTURES_SETUP app_clothes_saved)
    add_test(NAME app_clothes_save_names_it COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/clothes/saved.mhm
             "-DKEYS=clothes Skirt 1b6d47a2-90c3-4e15-8f72-3ad05e9c6b41"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_clothes_save_names_it PROPERTIES
        FIXTURES_REQUIRED app_clothes_saved)
    add_test(NAME app_clothes_reload_wears_it COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/clothes/saved.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/reloaded.obj)
    set_tests_properties(app_clothes_reload_wears_it PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Skirt"
        FAIL_REGULAR_EXPRESSION "wearing Tights"
        FIXTURES_REQUIRED app_clothes_saved)

    # The zero case: the same jaw drop that moves 68 of the teeth and all 226
    # of the tongue moves none of the skirt, which starts at the waist.
    #
    # The SKIRT, not the tights, and that distinction is measured rather than
    # stylistic. The tights reach the upper chest, and a jaw drop moves 14 of
    # their 2674 -- at Y 13.967..14.440, by 0.017 to 0.068, which is real
    # motion carried up the neck and not float noise. Asserting 0 on the
    # tights would have been asserting something false; asserting 14 would
    # have pinned a number whose cause is the cage's reach rather than
    # anything this slot does.
    add_test(NAME app_clothes_jaw_open COMMAND makehuman
             --clothes skirt --facs AU26=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/clothes/jaw_open.obj)
    set_tests_properties(app_clothes_jaw_open PROPERTIES FIXTURES_SETUP app_clothes_jaw_open)
    add_test(NAME app_clothes_ignores_the_jaw COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/clothes/skirt.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/clothes/jaw_open.obj
             -DGROUP=clothes -DEXPECT_MOVED=0 -DEXPECT_STILL=720
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_clothes_ignores_the_jaw PROPERTIES
        FIXTURES_REQUIRED "app_clothes_skirt_worn;app_clothes_jaw_open")

    # ------------------------------------------------------------------
    # Eyelashes: a fifth weighting shape, and the first slot driven by the
    # EYELID rather than the jaw, the head or the legs.
    #
    # Four cages make one proxy, the way upper and lower teeth make one:
    # helper-{l,r}-eyelashes-1 are 60 vertices / 44 faces each and
    # helper-{l,r}-eyelashes-2 are 65 / 48, with no vertex shared between
    # them -- 250 and 184 in total, measured as a union rather than a sum.
    #
    # Their dominant bones, measured from data/rigs/default_weights.mhw, are
    # NOT what memory/todo.md claimed. The `-1` pair is orbicularis04 (49 of
    # 60 per side) and the `-2` pair is orbicularis03 (54 of 65), with 11
    # vertices per cage falling to `head`. That is the whole reason this slot
    # is worth its own gate: no proxy before it is driven by an eyelid.
    #
    # So AU43 (eyes closed) is the discriminating input, and it is the first
    # one that leaves every other slot alone.
    #
    # It moves 142 of the 250, and the split is the point rather than the
    # total: measured per cage, BOTH `-2` cages move entirely (65 a side, by
    # up to 0.1208) while the `-1` cages move 6 of 60 by at most 0.0079. AU43
    # drives orbicularis03 hard and only grazes orbicularis04, which is the
    # geometry telling us the `-2` pair is the lash on the lid that closes and
    # the `-1` pair is the one that barely travels. A gate asserting 250 would
    # be asserting something false about the anatomy.
    # ------------------------------------------------------------------
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/eyelashes)
    add_test(NAME app_eyelashes_group COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/g.mhm)
    set_tests_properties(app_eyelashes_group PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*Eyelashes")

    add_test(NAME app_eyelashes_worn COMMAND makehuman
             --eyelashes eyelashes
             --export ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/worn.obj)
    set_tests_properties(app_eyelashes_worn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Eyelashes \\(250 verts\\) lit by skinmat_eyelashes"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_eyelashes_worn)

    add_test(NAME app_eyelashes_none COMMAND makehuman
             --eyelashes none
             --export ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/none.obj)
    set_tests_properties(app_eyelashes_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Eyelashes"
        FIXTURES_SETUP app_eyelashes_none)

    add_test(NAME app_eyelashes_add_geometry COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/eyelashes/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/eyelashes/none.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_eyelashes_add_geometry PROPERTIES
        FIXTURES_REQUIRED "app_eyelashes_worn;app_eyelashes_none")

    # The positive claim: closing the eyes carries the lashes on the lid that
    # moves. 142 = 65 + 65 fully, plus 6 + 6 that travel marginally.
    add_test(NAME app_eyelashes_closed COMMAND makehuman
             --eyelashes eyelashes --facs AU43=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/closed.obj)
    set_tests_properties(app_eyelashes_closed PROPERTIES FIXTURES_SETUP app_eyelashes_closed)
    add_test(NAME app_eyelashes_follow_the_lid COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/eyelashes/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/eyelashes/closed.obj
             -DGROUP=eyelashes -DEXPECT_MOVED=142 -DEXPECT_STILL=108
             -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_eyelashes_follow_the_lid PROPERTIES
        FIXTURES_REQUIRED "app_eyelashes_worn;app_eyelashes_closed")

    # The negative half, and the one that separates this slot from every
    # earlier one: a jaw drop moves the teeth and the whole tongue and leaves
    # the lashes exactly where they were.
    add_test(NAME app_eyelashes_jaw_open COMMAND makehuman
             --eyelashes eyelashes --facs AU26=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/jaw_open.obj)
    set_tests_properties(app_eyelashes_jaw_open PROPERTIES FIXTURES_SETUP app_eyelashes_jaw_open)
    add_test(NAME app_eyelashes_ignore_the_jaw COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/eyelashes/worn.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/eyelashes/jaw_open.obj
             -DGROUP=eyelashes -DEXPECT_MOVED=0 -DEXPECT_STILL=250
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_eyelashes_ignore_the_jaw PROPERTIES
        FIXTURES_REQUIRED "app_eyelashes_worn;app_eyelashes_jaw_open")

    # Round trip through .mhm.
    add_test(NAME app_eyelashes_saved COMMAND makehuman
             --eyelashes eyelashes --save ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/saved.mhm)
    set_tests_properties(app_eyelashes_saved PROPERTIES FIXTURES_SETUP app_eyelashes_saved)
    add_test(NAME app_eyelashes_save_names_it COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/eyelashes/saved.mhm
             "-DKEYS=eyelashes Eyelashes 9d4f60b8-13ac-4e27-b5f1-08e7c62a3d95"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_eyelashes_save_names_it PROPERTIES
        FIXTURES_REQUIRED app_eyelashes_saved)
    add_test(NAME app_eyelashes_reload_wears_them COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/saved.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/eyelashes/reloaded.obj)
    set_tests_properties(app_eyelashes_reload_wears_them PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Eyelashes"
        FIXTURES_REQUIRED app_eyelashes_saved)

    # ------------------------------------------------------------------
    # The stored skinning preference is a WINDOW preference, and the headless
    # path builds no window. That is deliberate -- a scripted export must not
    # change meaning because of what someone last clicked in a menu, or the
    # same command would produce different geometry on two machines.
    #
    # What was wrong is that it happened SILENTLY. A user who picks Linear in
    # the menu and then scripts an export got DQS without being told. The flag
    # stays authoritative; the run now says so.
    #
    # HOME is redirected per test because QSettings(IniFormat, UserScope)
    # resolves to $HOME/.config/<org>/<app>.ini, so each case gets its own
    # preference file and none of them touch the real one.
    # ------------------------------------------------------------------
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pref_linear/.config/MakeHuman)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pref_dqs/.config/MakeHuman)
    file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pref_skinning_linear.ini
         ${CMAKE_CURRENT_BINARY_DIR}/pref_linear/.config/MakeHuman/MakeHumanCpp.ini)
    file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pref_skinning_dqs.ini
         ${CMAKE_CURRENT_BINARY_DIR}/pref_dqs/.config/MakeHuman/MakeHumanCpp.ini)

    # Stored linear, no flag: the run is DQS and must SAY it is overriding.
    add_test(NAME app_skinning_stored_linear_warns COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/pref_linear/out.obj)
    set_tests_properties(app_skinning_stored_linear_warns PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/pref_linear"
        PASS_REGULAR_EXPRESSION "stored skinning preference is linear.*using dqs")

    # The same stored preference, but the flag AGREES with it: no warning,
    # because there is nothing to warn about.
    add_test(NAME app_skinning_stored_linear_flag_agrees COMMAND makehuman
             --skinning linear
             --export ${CMAKE_CURRENT_BINARY_DIR}/pref_linear/agree.obj)
    set_tests_properties(app_skinning_stored_linear_flag_agrees PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/pref_linear"
        FAIL_REGULAR_EXPRESSION "stored skinning preference")

    # The flag DISAGREES and wins, which is the case a user most needs told.
    add_test(NAME app_skinning_stored_linear_flag_overrides COMMAND makehuman
             --skinning dqs
             --export ${CMAKE_CURRENT_BINARY_DIR}/pref_linear/override.obj)
    set_tests_properties(app_skinning_stored_linear_flag_overrides PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/pref_linear"
        PASS_REGULAR_EXPRESSION "stored skinning preference is linear.*using dqs")

    # Stored dqs, no flag: agrees with the default, so the run stays quiet.
    add_test(NAME app_skinning_stored_dqs_is_quiet COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/pref_dqs/out.obj)
    set_tests_properties(app_skinning_stored_dqs_is_quiet PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/pref_dqs"
        FAIL_REGULAR_EXPRESSION "stored skinning preference")

    # No preference at all -- the overwhelmingly common case -- says nothing.
    add_test(NAME app_skinning_no_preference_is_quiet COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/pref_none.obj)
    set_tests_properties(app_skinning_no_preference_is_quiet PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/pref_missing"
        FAIL_REGULAR_EXPRESSION "stored skinning preference")


    add_test(NAME app_group_is_litsphere COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/lits/groups.mhm)
    set_tests_properties(app_group_is_litsphere PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*\\[Litsphere,"
        FAIL_REGULAR_EXPRESSION "\\[Skin,")


    add_test(NAME app_litsphere_new_name COMMAND makehuman
             --skin-material african_light --litsphere african
             --render ${CMAKE_CURRENT_BINARY_DIR}/lits/new.png)
    set_tests_properties(app_litsphere_new_name PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "Unknown option"
        FIXTURES_SETUP app_lits_new)
    add_test(NAME app_litsphere_old_name COMMAND makehuman
             --skin-material african_light --skin african
             --render ${CMAKE_CURRENT_BINARY_DIR}/lits/old.png)
    set_tests_properties(app_litsphere_old_name PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "Unknown option"
        FIXTURES_SETUP app_lits_old)
    # The alias has to mean the SAME thing, not merely be accepted. An option
    # that parses and is then ignored would pass a "no Unknown option" check.
    add_test(NAME app_litsphere_alias_is_the_same COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/lits/new.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/lits/old.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_litsphere_alias_is_the_same PROPERTIES
        FIXTURES_REQUIRED "app_lits_new;app_lits_old")
    # ...and it has to DO something, or the two files above would be identical
    # for the uninteresting reason.
    add_test(NAME app_litsphere_other COMMAND makehuman
             --skin-material african_light --litsphere caucasian
             --render ${CMAKE_CURRENT_BINARY_DIR}/lits/other.png)
    set_tests_properties(app_litsphere_other PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_lits_other)
    add_test(NAME app_litsphere_changes_the_frame COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/lits/new.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/lits/other.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_litsphere_changes_the_frame PROPERTIES
        FIXTURES_REQUIRED "app_lits_new;app_lits_other")

    # AND the case that explains why every render above names a material.
    #
    # Under `default.mhmat` the flag is INERT, and by design: that material sets
    # `autoBlendSkin true`, so the litsphere is blended from the ethnicity
    # macros and whatever was asked for is overridden. Measured while writing
    # this -- `--skin african` and `--skin caucasian` render byte-identically on
    # the default material, which looks exactly like a dead flag until you check
    # the material. Pinned so the next person does not file it as a bug, and so
    # that turning autoBlendSkin off by accident is loud.
    add_test(NAME app_litsphere_autoblend_a COMMAND makehuman
             --litsphere african --render ${CMAKE_CURRENT_BINARY_DIR}/lits/ab_a.png)
    set_tests_properties(app_litsphere_autoblend_a PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render" FIXTURES_SETUP app_lits_ab_a)
    add_test(NAME app_litsphere_autoblend_b COMMAND makehuman
             --litsphere caucasian --render ${CMAKE_CURRENT_BINARY_DIR}/lits/ab_b.png)
    set_tests_properties(app_litsphere_autoblend_b PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render" FIXTURES_SETUP app_lits_ab_b)
    add_test(NAME app_litsphere_inert_under_autoblend COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/lits/ab_a.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/lits/ab_b.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_litsphere_inert_under_autoblend PROPERTIES
        FIXTURES_REQUIRED "app_lits_ab_a;app_lits_ab_b")


    add_test(NAME app_look_at_over_facs COMMAND makehuman
             --facs AU61=1.0 --look-at 1.5,7.8,6.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/lookat/over.obj)
    set_tests_properties(app_look_at_over_facs PROPERTIES
        PASS_REGULAR_EXPRESSION "look-at replaced an eye rotation")
    # ...and a look-at with nothing under it must NOT claim to have replaced
    # anything. Without this the warning could be unconditional and the case
    # above would still pass.
    add_test(NAME app_look_at_alone_is_quiet COMMAND makehuman
             --look-at 1.5,7.8,6.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/lookat/alone.obj)
    set_tests_properties(app_look_at_alone_is_quiet PROPERTIES
        PASS_REGULAR_EXPRESSION "look-at: eyes turned"
        FAIL_REGULAR_EXPRESSION "replaced an eye rotation")


    add_test(NAME app_look_at_bad COMMAND makehuman
             --look-at nonsense --render ${CMAKE_CURRENT_BINARY_DIR}/lookat/never.png)
    set_tests_properties(app_look_at_bad PROPERTIES
        PASS_REGULAR_EXPRESSION "--look-at wants three numbers")

    add_test(NAME app_correctives_missing COMMAND makehuman
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/nope.json
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_never.obj)
    set_tests_properties(app_correctives_missing PROPERTIES
        PASS_REGULAR_EXPRESSION "correctives: cannot read the manifest")

    add_test(NAME app_skinning_unknown COMMAND makehuman
             --skinning nonsense --export ${CMAKE_CURRENT_BINARY_DIR}/app_never.obj)
    set_tests_properties(app_skinning_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown skinning method")

    # Dual quaternion is the DEFAULT: no flag at all must produce the dqs file
    # and not the linear one. Tested as an effect rather than as a printed word,
    # because a message would still print with the default flipped back -- and
    # `app_skinning_dqs_differs` above is what makes this claim mean something,
    # since it proves the two files are not equal to begin with.
    #
    # Same basename again, for the mtllib reason spelled out above.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/skin_default)
    add_test(NAME app_skinning_default COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/skin_default/pose.obj)
    # FAIL alongside PASS for the reason spelled out further down: PASS
    # REPLACES the exit-code check, so the previous run's file would do.
    set_tests_properties(app_skinning_default PROPERTIES
        PASS_REGULAR_EXPRESSION "wrote .*pose\\.obj"
        FAIL_REGULAR_EXPRESSION "export failed"
        FIXTURES_SETUP app_default_obj)
    add_test(NAME app_skinning_default_is_dqs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/skin_default/pose.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/skin_dqs/pose.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_skinning_default_is_dqs PROPERTIES
        FIXTURES_REQUIRED "app_default_obj;app_dqs_obj")

    # The grid reaches the VIEWPORT and nothing else. Two claims, and the
    # second is the one the reference makes for us: its grids are
    # `excludeFromProduction` (`core/mhmain.py:671,687`), so a production render
    # must be identical with and without the flag while the window is not.
    #
    # --screenshot needs a real Metal device and a surface; it skips where
    # app_screenshot does.
    add_test(NAME app_grid_window COMMAND makehuman
             --grid --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_grid.png)
    set_tests_properties(app_grid_window PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_SETUP app_grid_png)
    add_test(NAME app_grid_window_plain COMMAND makehuman
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_nogrid.png)
    set_tests_properties(app_grid_window_plain PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_SETUP app_nogrid_png)
    add_test(NAME app_grid_changes_the_window COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/app_grid.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/app_nogrid.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_grid_changes_the_window PROPERTIES
        FIXTURES_REQUIRED "app_grid_png;app_nogrid_png")
    # ...and --render ignores it. `--grid --render` must be byte-identical to
    # `--render`, which is what stops the flag leaking into production output.
    add_test(NAME app_grid_render COMMAND makehuman
             --grid --render ${CMAKE_CURRENT_BINARY_DIR}/app_grid_render.png)
    set_tests_properties(app_grid_render PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_grid_render_png)
    add_test(NAME app_grid_not_in_render COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/app_grid_render.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/app_solid.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_match.cmake)
    set_tests_properties(app_grid_not_in_render PROPERTIES
        FIXTURES_REQUIRED "app_grid_render_png;app_solid_png")

    # `--expression`: a `.mhpose` reaching the mesh. The loader landed with
    # oracle parity but NOTHING in the shipped binary called it, so the format
    # was still unusable -- machinery with a test, which is only half the
    # objection the item raised.
    #
    # The fixture is the one the oracle wrote (`tools/capture_fixture.py
    # mhpose`); it names four face units, so the face bones move and nothing
    # else does. Asserting the bone COUNT is what gives this teeth: an
    # expression that resolved to no units at all would still export happily.
    add_test(NAME app_expression COMMAND makehuman
             --expression ${CMAKE_CURRENT_SOURCE_DIR}/golden/mhpose/smirk.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_expr.obj)
    set_tests_properties(app_expression PROPERTIES
        PASS_REGULAR_EXPRESSION "expression Fixture Smirk \\(5 units, [0-9]+ bones\\)"
        FIXTURES_SETUP app_expr_obj)
    # ...and it CHANGED the mesh. Compared against the same character exported
    # without the expression: same basename in two directories, because an OBJ
    # names its material library after itself and would differ on that line
    # alone (the trap `app_skinning_dqs_differs` fell into).
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/expr_plain
                        ${CMAKE_CURRENT_BINARY_DIR}/expr_face)
    add_test(NAME app_expression_plain COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/expr_plain/body.obj)
    set_tests_properties(app_expression_plain PROPERTIES FIXTURES_SETUP app_expr_plain_obj)
    add_test(NAME app_expression_posed COMMAND makehuman
             --expression ${CMAKE_CURRENT_SOURCE_DIR}/golden/mhpose/smirk.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/expr_face/body.obj)
    set_tests_properties(app_expression_posed PROPERTIES FIXTURES_SETUP app_expr_face_obj)
    add_test(NAME app_expression_differs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/expr_plain/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/expr_face/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_expression_differs PROPERTIES
        FIXTURES_REQUIRED "app_expr_plain_obj;app_expr_face_obj")
    # Pose AND expression together -- the `mixPoses` path, which neither of the
    # cases above reaches: with no `--pose` the expression simply BECOMES the
    # pose, so a layering bug is invisible. This one must differ from the T-pose
    # alone AND from the expression alone.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/expr_tpose
                        ${CMAKE_CURRENT_BINARY_DIR}/expr_tpose_face)
    add_test(NAME app_expression_tpose COMMAND makehuman
             --pose tpose --export ${CMAKE_CURRENT_BINARY_DIR}/expr_tpose/body.obj)
    set_tests_properties(app_expression_tpose PROPERTIES FIXTURES_SETUP app_expr_tpose_obj)
    add_test(NAME app_expression_tpose_face COMMAND makehuman
             --pose tpose
             --expression ${CMAKE_CURRENT_SOURCE_DIR}/golden/mhpose/smirk.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/expr_tpose_face/body.obj)
    set_tests_properties(app_expression_tpose_face PROPERTIES
        FIXTURES_SETUP app_expr_tpose_face_obj)
    # Differs from the pose alone: the expression reached the face.
    add_test(NAME app_expression_layers_on_pose COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/expr_tpose/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/expr_tpose_face/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_expression_layers_on_pose PROPERTIES
        FIXTURES_REQUIRED "app_expr_tpose_obj;app_expr_tpose_face_obj")
    # ...and differs from the expression alone: the BODY kept its pose. Without
    # this, an implementation that threw the body pose away and used the
    # expression as the whole pose would pass the check above.
    add_test(NAME app_expression_keeps_the_pose COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/expr_face/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/expr_tpose_face/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_expression_keeps_the_pose PROPERTIES
        FIXTURES_REQUIRED "app_expr_face_obj;app_expr_tpose_face_obj")

    # The WEIGHTS reach the blend. Nothing above could see this: an
    # implementation that applied every named unit at full strength still moves
    # the face, still layers onto a pose, and still differs from every other
    # export -- it passed all ten checks. Half the weights, the same five units,
    # and the mesh must differ.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/expr_half)
    add_test(NAME app_expression_half COMMAND makehuman
             --expression ${CMAKE_CURRENT_SOURCE_DIR}/expression_half_weights.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/expr_half/body.obj)
    set_tests_properties(app_expression_half PROPERTIES FIXTURES_SETUP app_expr_half_obj)
    add_test(NAME app_expression_weights_matter COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/expr_face/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/expr_half/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_expression_weights_matter PROPERTIES
        FIXTURES_REQUIRED "app_expr_face_obj;app_expr_half_obj")

    # A unit the library does not have is named, not ignored: a typo in an
    # expression file otherwise produces a subtly wrong face.
    add_test(NAME app_expression_unknown_unit COMMAND makehuman
             --expression ${CMAKE_CURRENT_SOURCE_DIR}/expression_bad_unit.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_expr.obj)
    set_tests_properties(app_expression_unknown_unit PROPERTIES
        PASS_REGULAR_EXPRESSION "NoSuchUnit")
    add_test(NAME app_expression_missing_file COMMAND makehuman
             --expression ${CMAKE_CURRENT_BINARY_DIR}/nope.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_expr2.obj)
    set_tests_properties(app_expression_missing_file PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot load expression")

    # ---------------------------------------------------------------------
    # FACS Action Units. The unit tests cover the table and the resolver; what
    # only the application can show is that an AU reaches the MESH, and that it
    # reaches it through the SAME path a .mhpose does.
    #
    # Same basename in different directories throughout: an OBJ names its
    # material library after itself, so `au12.obj` and `plain.obj` would differ
    # on the mtllib line and nowhere else, and every comparison below would
    # pass for that reason instead of the one it is asking about.
    # ---------------------------------------------------------------------
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/facs_au12)
    add_test(NAME app_facs COMMAND makehuman
             --facs AU12=1.0 --export ${CMAKE_CURRENT_BINARY_DIR}/facs_au12/body.obj)
    set_tests_properties(app_facs PROPERTIES
        PASS_REGULAR_EXPRESSION "expression FACS \\(2 units, 23 bones\\)"
        FIXTURES_SETUP app_facs_obj)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/facs_plain)
    add_test(NAME app_facs_plain COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/facs_plain/body.obj)
    set_tests_properties(app_facs_plain PROPERTIES FIXTURES_SETUP app_facs_plain_obj)

    add_test(NAME app_facs_moves_the_face COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/facs_plain/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/facs_au12/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_facs_moves_the_face PROPERTIES
        FIXTURES_REQUIRED "app_facs_plain_obj;app_facs_obj")

    # THE test of this chunk. `tests/facs_au12.mhpose` holds the two pose units
    # AU12 resolves to, at the same weights, in the same order, written by hand.
    # The two meshes must be BYTE-IDENTICAL: FACS is a naming layer over the
    # existing units, and anything else -- a second blend, a normalisation, a
    # different order -- shows up here and nowhere else.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/facs_by_hand)
    add_test(NAME app_facs_by_hand COMMAND makehuman
             --expression ${CMAKE_CURRENT_SOURCE_DIR}/facs_au12.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/facs_by_hand/body.obj)
    set_tests_properties(app_facs_by_hand PROPERTIES FIXTURES_SETUP app_facs_by_hand_obj)
    add_test(NAME app_facs_equals_the_units COMMAND ${CMAKE_COMMAND} -E compare_files
             ${CMAKE_CURRENT_BINARY_DIR}/facs_au12/body.obj
             ${CMAKE_CURRENT_BINARY_DIR}/facs_by_hand/body.obj)
    set_tests_properties(app_facs_equals_the_units PROPERTIES
        FIXTURES_REQUIRED "app_facs_obj;app_facs_by_hand_obj")

    # One side only. Bilateral and unilateral both move the face, so "it moved"
    # cannot tell them apart; the meshes have to differ from each other.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/facs_left)
    add_test(NAME app_facs_sided COMMAND makehuman
             --facs AU12L=1.0 --export ${CMAKE_CURRENT_BINARY_DIR}/facs_left/body.obj)
    set_tests_properties(app_facs_sided PROPERTIES
        PASS_REGULAR_EXPRESSION "expression FACS \\(1 units, 15 bones\\)"
        FIXTURES_SETUP app_facs_left_obj)
    add_test(NAME app_facs_one_side_differs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/facs_au12/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/facs_left/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_facs_one_side_differs PROPERTIES
        FIXTURES_REQUIRED "app_facs_obj;app_facs_left_obj")

    # The intensity reaches the blend. Same lesson as app_expression_half: a
    # resolver that ignored the weight and applied every AU at full strength
    # passes every check above.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/facs_weak)
    add_test(NAME app_facs_weak COMMAND makehuman
             --facs AU12=0.35 --export ${CMAKE_CURRENT_BINARY_DIR}/facs_weak/body.obj)
    set_tests_properties(app_facs_weak PROPERTIES FIXTURES_SETUP app_facs_weak_obj)
    add_test(NAME app_facs_intensity_matters COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/facs_au12/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/facs_weak/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_facs_intensity_matters PROPERTIES
        FIXTURES_REQUIRED "app_facs_obj;app_facs_weak_obj")

    # Two AUs at once, in the order asked for. PoseUnits::blend multiplies and
    # does not commute, so the reverse must give a different face -- if it did
    # not, the resolver would be sorting or de-duplicating behind the caller.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/facs_ab)
    add_test(NAME app_facs_two COMMAND makehuman
             --facs AU12=0.8 --facs AU26=0.6
             --export ${CMAKE_CURRENT_BINARY_DIR}/facs_ab/body.obj)
    set_tests_properties(app_facs_two PROPERTIES FIXTURES_SETUP app_facs_ab_obj)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/facs_ba)
    add_test(NAME app_facs_two_reversed COMMAND makehuman
             --facs AU26=0.6 --facs AU12=0.8
             --export ${CMAKE_CURRENT_BINARY_DIR}/facs_ba/body.obj)
    set_tests_properties(app_facs_two_reversed PROPERTIES FIXTURES_SETUP app_facs_ba_obj)
    add_test(NAME app_facs_order_survives COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/facs_ab/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/facs_ba/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_facs_order_survives PROPERTIES
        FIXTURES_REQUIRED "app_facs_ab_obj;app_facs_ba_obj")

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/punit)

    # Setting a face pose unit DIRECTLY -- the reference's expression mixer is
    # sixty sliders, one per unit (`7_expression_mixer.py:180`), and --facs only
    # reaches the thirty Action Units that name them. A unit with no AU was
    # unreachable, and the sixty NAMES were undiscoverable besides.
    add_test(NAME app_list_pose_units COMMAND makehuman --list-pose-units)
    set_tests_properties(app_list_pose_units PROPERTIES
        PASS_REGULAR_EXPRESSION "LeftBrowDown")

    add_test(NAME app_pose_unit_neutral COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/neutral.obj)
    set_tests_properties(app_pose_unit_neutral PROPERTIES FIXTURES_SETUP app_punit_neutral)
    add_test(NAME app_pose_unit_brow COMMAND makehuman
             --pose-unit LeftBrowDown=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/brow.obj)
    set_tests_properties(app_pose_unit_brow PROPERTIES FIXTURES_SETUP app_punit_brow)
    # The premise: a unit actually moves the face. Without this every assertion
    # below passes on a flag that parsed and did nothing.
    add_test(NAME app_pose_unit_moves_the_face COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/punit/neutral.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/punit/brow.obj
             -DEXPECT_DIFFERENT=425
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_pose_unit_moves_the_face PROPERTIES
        FIXTURES_REQUIRED "app_punit_neutral;app_punit_brow")
    # A unit no Action Unit names, which is the whole reason this flag exists.
    # Measured: the FACS table names 48 of the 60 units, so 11 (excluding Rest)
    # were unreachable before -- ChinDown, MouthMoveLeft/Right, the five tongue
    # units, and three more.
    add_test(NAME app_pose_unit_without_an_au COMMAND makehuman
             --pose-unit TongueUp=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/tongue.obj)
    set_tests_properties(app_pose_unit_without_an_au PROPERTIES FIXTURES_SETUP app_punit_tongue)
    add_test(NAME app_pose_unit_au_free_unit_moves COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/punit/neutral.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/punit/tongue.obj
             -DEXPECT_DIFFERENT=110
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_pose_unit_au_free_unit_moves PROPERTIES
        FIXTURES_REQUIRED "app_punit_neutral;app_punit_tongue")

    # Saving what was composed, and reloading it: the mixer's Save button, for
    # raw units rather than Action Units.
    add_test(NAME app_pose_unit_saved COMMAND makehuman
             --pose-unit LeftBrowDown=1.0
             --save-expression ${CMAKE_CURRENT_BINARY_DIR}/punit/brow.mhpose)
    set_tests_properties(app_pose_unit_saved PROPERTIES FIXTURES_SETUP app_punit_saved)
    add_test(NAME app_pose_unit_save_names_the_unit COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/punit/brow.mhpose
             "-DKEYS=LeftBrowDown;1.0"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_pose_unit_save_names_the_unit PROPERTIES
        FIXTURES_REQUIRED app_punit_saved)
    add_test(NAME app_pose_unit_reloaded COMMAND makehuman
             --expression ${CMAKE_CURRENT_BINARY_DIR}/punit/brow.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/reloaded.obj)
    set_tests_properties(app_pose_unit_reloaded PROPERTIES
        FIXTURES_SETUP app_punit_reloaded FIXTURES_REQUIRED app_punit_saved)
    add_test(NAME app_pose_unit_round_trips COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/punit/brow.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/punit/reloaded.obj
             -DEXPECT_DIFFERENT=0
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_pose_unit_round_trips PROPERTIES
        FIXTURES_REQUIRED "app_punit_brow;app_punit_reloaded")
    # ORDER survives, because the blend multiplies quaternions and does not
    # commute -- the same rule saveExpression's own tests pin.
    add_test(NAME app_pose_unit_saved_two COMMAND makehuman
             --pose-unit RightOuterBrowUp=0.25 --pose-unit LeftBrowDown=0.5
             --save-expression ${CMAKE_CURRENT_BINARY_DIR}/punit/two.mhpose)
    set_tests_properties(app_pose_unit_saved_two PROPERTIES FIXTURES_SETUP app_punit_two)
    add_test(NAME app_pose_unit_save_keeps_order COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/punit/two.mhpose
             "-DKEYS=RightOuterBrowUp;LeftBrowDown" -DORDERED=1
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_pose_unit_save_keeps_order PROPERTIES
        FIXTURES_REQUIRED app_punit_two)

    # The four refusals.
    add_test(NAME app_pose_unit_unknown COMMAND makehuman
             --pose-unit NoSuchUnit=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/never.obj)
    set_tests_properties(app_pose_unit_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "no such pose unit .NoSuchUnit.")
    add_test(NAME app_pose_unit_bad_weight COMMAND makehuman
             --pose-unit LeftBrowDown=5
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/never.obj)
    set_tests_properties(app_pose_unit_bad_weight PROPERTIES
        PASS_REGULAR_EXPRESSION "--pose-unit wants <unit>=<weight in 0..1>")
    add_test(NAME app_pose_unit_and_facs COMMAND makehuman
             --pose-unit LeftBrowDown=1.0 --facs AU12=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/never.obj)
    set_tests_properties(app_pose_unit_and_facs PROPERTIES
        PASS_REGULAR_EXPRESSION "describe the face; give one")
    add_test(NAME app_pose_unit_and_expression COMMAND makehuman
             --pose-unit LeftBrowDown=1.0
             --expression ${CMAKE_CURRENT_BINARY_DIR}/punit/brow.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/never.obj)
    set_tests_properties(app_pose_unit_and_expression PROPERTIES
        FIXTURES_REQUIRED app_punit_saved
        PASS_REGULAR_EXPRESSION "describe the face; give one")

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bg)

    # A backdrop behind a production render. The reference puts an image behind
    # the model to model TO a photograph (`0_modeling_background.py:104`); the
    # same image turns a render into a composite.
    #
    # The backdrop is a render of the character in a DIFFERENT POSE. Nothing
    # under data/ is a backdrop -- the user supplies one, exactly like
    # --custom-targets -- and a render is already a PNG of the right kind, so
    # this needs no fixture generator and no new tool.
    #
    # The pose matters. The first version used a plain render of the SAME
    # character as its own backdrop, and the composite came out byte-identical
    # to the plain render -- correctly, since an A-pose over an identical A-pose
    # is that A-pose. The comparison below then failed, which is the gate doing
    # its job on a fixture whose premise was unmet rather than on the code.
    add_test(NAME app_background_backdrop COMMAND makehuman
             --pose tpose --render ${CMAKE_CURRENT_BINARY_DIR}/bg/backdrop.png)
    set_tests_properties(app_background_backdrop PROPERTIES FIXTURES_SETUP app_bg_backdrop)
    add_test(NAME app_background_plain COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/bg/without.png)
    set_tests_properties(app_background_plain PROPERTIES FIXTURES_SETUP app_bg_without)

    # THE regression, and the reason this is a CLI test at all: the coverage
    # figure must still be the CHARACTER's -- measured, 8.2% of a 1024 square.
    # The blank-render guard reads pixel (0, 0) as the clear colour and calls a
    # flat frame blank, so compositing the backdrop BEFORE that check would make
    # a render that drew nothing look like a frame full of photograph and sail
    # through. A single-digit percentage is only possible if the frame was
    # judged before the backdrop went on.
    add_test(NAME app_background_render COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/bg/with.png
             --background ${CMAKE_CURRENT_BINARY_DIR}/bg/backdrop.png)
    set_tests_properties(app_background_render PROPERTIES
        FIXTURES_SETUP app_bg_with
        FIXTURES_REQUIRED app_bg_backdrop
        PASS_REGULAR_EXPRESSION
            "1024x1024 covered [0-9]+ of 1048576 pixels \\([0-9]\\.[0-9]%\\)")
    # And the backdrop actually reached the file. Byte-comparing two PNGs is
    # only safe because both come from the SAME binary in one run: a different
    # zlib is what made an earlier byte-compare green locally and red in CI, and
    # that bites on "identical", never on "differs".
    add_test(NAME app_background_reaches_the_file COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/bg/with.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/bg/without.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_background_reaches_the_file PROPERTIES
        FIXTURES_REQUIRED "app_bg_with;app_bg_without")

    # The two refusals.
    add_test(NAME app_background_unreadable COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/bg/never.png
             --background ${CMAKE_CURRENT_BINARY_DIR}/bg/not-an-image.png)
    set_tests_properties(app_background_unreadable PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot read --background image")
    add_test(NAME app_background_needs_render COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/bg/backdrop.png
             --export ${CMAKE_CURRENT_BINARY_DIR}/bg/never.obj)
    set_tests_properties(app_background_needs_render PROPERTIES
        FIXTURES_REQUIRED app_bg_backdrop
        PASS_REGULAR_EXPRESSION "--background is the backdrop for --render")

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pframe)

    # Posing from one frame of a multi-frame .bvh. `loadBodyPose` refuses an
    # animation outright -- rightly, since frame 0 of a walk cycle is a
    # plausible wrong pose -- which left every multi-frame file unreachable.
    # Naming a frame is the caller saying they know what the file is.
    #
    # The fixture is `face-poseunits.bvh`: 60 frames, and measured, 163 of its
    # 163 joints name a bone of the rig. The shipped walks CANNOT serve here --
    # 0 of their 75 joints match either rig, so every frame of them is identity.
    # That is asserted in the unit tests, not assumed here.
    add_test(NAME app_pose_frame_0 COMMAND makehuman
             --pose ${MH_DATA_DIR}/poseunits/face-poseunits.bvh --pose-frame 0
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/f0.obj)
    set_tests_properties(app_pose_frame_0 PROPERTIES FIXTURES_SETUP app_pframe_0)
    add_test(NAME app_pose_frame_40 COMMAND makehuman
             --pose ${MH_DATA_DIR}/poseunits/face-poseunits.bvh --pose-frame 40
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/f40.obj)
    set_tests_properties(app_pose_frame_40 PROPERTIES FIXTURES_SETUP app_pframe_40)
    # Measured: 2,265 of 14,444 vertices. Frame 40 is a wide-open jaw, which is
    # visible in a render -- the number is here so a loader that quietly
    # returned frame 0 for every index fails instead of passing.
    add_test(NAME app_pose_frame_differs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/pframe/f0.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/pframe/f40.obj
             -DEXPECT_DIFFERENT=2265
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_pose_frame_differs PROPERTIES
        FIXTURES_REQUIRED "app_pframe_0;app_pframe_40")
    # The three refusals, each with its own message.
    add_test(NAME app_pose_frame_needs_pose COMMAND makehuman
             --pose-frame 3 --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/never.obj)
    set_tests_properties(app_pose_frame_needs_pose PROPERTIES
        PASS_REGULAR_EXPRESSION "--pose-frame needs a --pose")
    add_test(NAME app_pose_frame_not_a_number COMMAND makehuman
             --pose ${MH_DATA_DIR}/poseunits/face-poseunits.bvh --pose-frame seven
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/never.obj)
    set_tests_properties(app_pose_frame_not_a_number PROPERTIES
        PASS_REGULAR_EXPRESSION "--pose-frame wants a frame index from 0")
    # A negative index is refused HERE rather than wrapping: without the
    # `frame < 0` guard, `-2` reaches the loader as 18446744073709551614 and the
    # user is told there is no frame 18446744073709551614.
    add_test(NAME app_pose_frame_negative COMMAND makehuman
             --pose ${MH_DATA_DIR}/poseunits/face-poseunits.bvh --pose-frame -2
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/never.obj)
    set_tests_properties(app_pose_frame_negative PROPERTIES
        PASS_REGULAR_EXPRESSION "--pose-frame wants a frame index from 0")
    add_test(NAME app_pose_frame_past_the_end COMMAND makehuman
             --pose ${MH_DATA_DIR}/poseunits/face-poseunits.bvh --pose-frame 60
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/never.obj)
    set_tests_properties(app_pose_frame_past_the_end PROPERTIES
        PASS_REGULAR_EXPRESSION "no frame 60; the file has 60 frames")
    # Regression: without a frame, a multi-frame file is still an error, and a
    # single-frame pose still works exactly as before.
    add_test(NAME app_pose_frame_absent_still_refuses COMMAND makehuman
             --pose ${MH_DATA_DIR}/poseunits/face-poseunits.bvh
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/never.obj)
    set_tests_properties(app_pose_frame_absent_still_refuses PROPERTIES
        PASS_REGULAR_EXPRESSION "is an animation, not a pose")
    add_test(NAME app_pose_frame_tpose_unaffected COMMAND makehuman
             --pose tpose --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/tpose.obj)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/expr)

    # Saving an expression, which is the half of the reference's mixer this
    # port never had: it could read a .mhpose and derive one from Action Units,
    # and could not write one. That is also why nothing under data/ is a
    # .mhpose -- there was no way to author one.
    #
    # The claim is a ROUND TRIP, so it takes three exports. The saved file
    # re-loaded with --expression must give the same mesh as the --facs request
    # it came from; and the expression must actually move something, or that is
    # two rest meshes agreeing with each other. The second test is what stops
    # the first being decorative.
    add_test(NAME app_save_expression COMMAND makehuman
             --facs AU12=1.0 --facs AU6=0.5
             --save-expression ${CMAKE_CURRENT_BINARY_DIR}/expr/smile.mhpose)
    set_tests_properties(app_save_expression PROPERTIES FIXTURES_SETUP app_save_expression)
    # The units come out in the order the Action Units named them, not sorted.
    # AU12 gives the two mouth units and AU6 the two cheek units, so an
    # alphabetical writer would put LeftCheekUp first -- and `blend`
    # multiplies quaternions, which do not commute, so that is a different face.
    add_test(NAME app_save_expression_order COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/expr/smile.mhpose
             "-DKEYS=MouthLeftPullUp;MouthRightPullUp;LeftCheekUp;RightCheekUp"
             -DORDERED=1
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_save_expression_order PROPERTIES
        FIXTURES_REQUIRED app_save_expression)
    add_test(NAME app_save_expression_from_facs COMMAND makehuman
             --facs AU12=1.0 --facs AU6=0.5
             --export ${CMAKE_CURRENT_BINARY_DIR}/expr/from_facs.obj)
    set_tests_properties(app_save_expression_from_facs PROPERTIES
        FIXTURES_SETUP app_expr_facs_obj)
    add_test(NAME app_save_expression_reloaded COMMAND makehuman
             --expression ${CMAKE_CURRENT_BINARY_DIR}/expr/smile.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/expr/from_file.obj)
    set_tests_properties(app_save_expression_reloaded PROPERTIES
        FIXTURES_SETUP app_expr_file_obj
        FIXTURES_REQUIRED app_save_expression)
    add_test(NAME app_save_expression_neutral COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/expr/neutral.obj)
    set_tests_properties(app_save_expression_neutral PROPERTIES
        FIXTURES_SETUP app_expr_neutral_obj)
    # The round trip: byte-for-byte the same face.
    add_test(NAME app_save_expression_round_trips COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/expr/from_facs.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/expr/from_file.obj
             -DEXPECT_DIFFERENT=0
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_save_expression_round_trips PROPERTIES
        FIXTURES_REQUIRED "app_expr_facs_obj;app_expr_file_obj")
    # And the premise, measured: 1,949 of 14,444 vertices. Counting differing
    # TEXT lines says 2,042 -- the extra 93 are midline vertices written
    # `0.0000` in one export and `-0.0000` in the other, which is why the
    # comparison is done in fixed point.
    add_test(NAME app_save_expression_moves_the_face COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/expr/neutral.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/expr/from_facs.obj
             -DEXPECT_DIFFERENT=1949
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_save_expression_moves_the_face PROPERTIES
        FIXTURES_REQUIRED "app_expr_neutral_obj;app_expr_facs_obj")
    # The same Action Unit twice asks for the same pose units twice, and the
    # blend COMPOSES them -- it does not add the weights. A JSON object keyed by
    # unit name cannot say that, so saving it kept the last weight and produced
    # a file that loaded cleanly into a different face: measured, 1,793 of
    # 14,444 vertices away from what was asked for. Found by reading the diff,
    # not by a failing test, which is why it gets one.
    add_test(NAME app_save_expression_repeated_unit COMMAND makehuman
             --facs AU12=0.3 --facs AU12=0.7
             --save-expression ${CMAKE_CURRENT_BINARY_DIR}/expr/never.mhpose)
    set_tests_properties(app_save_expression_repeated_unit PROPERTIES
        PASS_REGULAR_EXPRESSION "appears more than once")

    # Without --facs there is nothing to save, and a copy of an --expression
    # file is not worth a flag.
    add_test(NAME app_save_expression_needs_facs COMMAND makehuman
             --save-expression ${CMAKE_CURRENT_BINARY_DIR}/expr/never.mhpose)
    set_tests_properties(app_save_expression_needs_facs PROPERTIES
        PASS_REGULAR_EXPRESSION "give at least one --facs or --pose-unit")

    # The four refusals, each with its own message, because a shared "bad
    # --facs" would not say which mistake was made.
    add_test(NAME app_facs_unknown COMMAND makehuman
             --facs AU99=1.0 --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_facs.obj)
    set_tests_properties(app_facs_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "no such Action Unit \"AU99\"")
    add_test(NAME app_facs_unsided COMMAND makehuman
             --facs AU9L=1.0 --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_facs2.obj)
    set_tests_properties(app_facs_unsided PROPERTIES
        PASS_REGULAR_EXPRESSION "\"AU9\" has one midline muscle")
    add_test(NAME app_facs_bad_weight COMMAND makehuman
             --facs AU12=5 --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_facs3.obj)
    set_tests_properties(app_facs_bad_weight PROPERTIES
        PASS_REGULAR_EXPRESSION "--facs wants <AU>=<weight in 0..1>")
    add_test(NAME app_facs_and_expression COMMAND makehuman
             --facs AU12=1.0
             --expression ${CMAKE_CURRENT_SOURCE_DIR}/facs_au12.mhpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_facs4.obj)
    set_tests_properties(app_facs_and_expression PROPERTIES
        PASS_REGULAR_EXPRESSION "all describe the face; give one")

    # ---------------------------------------------------------------------
    # --decimate: the LOD reaches a FILE.
    #
    # The decimator itself is covered by [core][decimate] invariants. What only
    # the application can show is the interaction that made this a separate
    # chunk: the face mask has to be baked into the geometry BEFORE the
    # collapse, because an edge collapse destroys the face-to-face
    # correspondence a mask is expressed in. If that order is wrong the export
    # either carries the helper cages or crashes on a mask of the wrong length.
    #
    # Same basename in separate directories, for the mtllib reason the
    # expression tests give.
    # ---------------------------------------------------------------------
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lod_full)
    add_test(NAME app_decimate_full COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/lod_full/body.obj)
    set_tests_properties(app_decimate_full PROPERTIES FIXTURES_SETUP app_lod_full_obj)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lod_quarter)
    add_test(NAME app_decimate COMMAND makehuman
             --decimate 0.25
             --export ${CMAKE_CURRENT_BINARY_DIR}/lod_quarter/body.obj)
    # FAIL_REGULAR_EXPRESSION alongside the PASS one, because PASS_REGULAR_
    # EXPRESSION REPLACES the exit-code check: without it a run that printed
    # this line and then failed to write the file still passes. That is not
    # hypothetical -- the LOD's skin was compacted with the wrong mesh's remap,
    # glTF refused it as "skin does not describe this mesh", the export exited
    # 1, and both of these tests were green because the announcement had
    # already been printed and the .glb from the previous run was still on
    # disk for the inspect to read.
    set_tests_properties(app_decimate PROPERTIES
        PASS_REGULAR_EXPRESSION "decimated 13378 faces to 6688 triangles"
        FAIL_REGULAR_EXPRESSION "export failed"
        FIXTURES_SETUP app_lod_quarter_obj)

    add_test(NAME app_decimate_differs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/lod_full/body.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/lod_quarter/body.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_decimate_differs PROPERTIES
        FIXTURES_REQUIRED "app_lod_full_obj;app_lod_quarter_obj")

    # ...and the FILE holds what the app said it wrote. "the app printed 6,688"
    # and "the OBJ has 6,688 f lines" are different claims, and it was the
    # second that was false the last time an export lost its mask.
    #
    # 6,688 and not 6,689: a quarter of 26,756 is 6,689, and a collapse removes
    # exactly TWO triangles, so the count can only land on the same parity as
    # the input. This test was written expecting 6,689 and the measurement
    # corrected it.
    #
    # **7,708 and not 6,688**, because this helper counts every `f` line and the
    # scene has two objects: the body at 6,688 and the EYE PROXY at 1,020,
    # undecimated. Measured per group: full = body 13,378 + eyes 1,020 = 14,398,
    # quarter = body 6,688 + eyes 1,020 = 7,708. A worn proxy is a separate mesh
    # with its own fit and its own mask, and eyes are the last geometry anyone
    # wants to simplify, so `--decimate` reduces the BODY. The total is the
    # better gate for it either way: it moves if the body is wrong AND if the
    # eyes are ever decimated by accident.
    add_test(NAME app_decimate_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/lod_quarter/body.obj -DEXPECT=7708
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_body_faces.cmake)
    set_tests_properties(app_decimate_obj_faces PROPERTIES
        FIXTURES_REQUIRED app_lod_quarter_obj)

    # The undecimated total, from the same helper, so the pair is a comparison
    # rather than one number in isolation.
    add_test(NAME app_decimate_full_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/lod_full/body.obj -DEXPECT=14398
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_body_faces.cmake)
    set_tests_properties(app_decimate_full_obj_faces PROPERTIES
        FIXTURES_REQUIRED app_lod_full_obj)

    # The mask really is baked in, and this is the assertion that says so: the
    # masked body is 13,378 faces. Built from the UNMASKED mesh the line would
    # say 18,486 -- which is the bug of exporting the helper cages, in numeric
    # form.
    add_test(NAME app_decimate_started_from_the_masked_body COMMAND makehuman
             --decimate 0.5
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_lod_half.obj)
    set_tests_properties(app_decimate_started_from_the_masked_body PROPERTIES
        PASS_REGULAR_EXPRESSION "decimated 13378 faces to ")

    # A decimated mesh CARRIES the rig now. It could not until the decimator
    # reported where each surviving vertex came from: weights are per base
    # vertex, a collapse renumbers everything, and the honest answer without a
    # mapping was to refuse. The previous chunk's `app_decimate_refuses_the_rig`
    # is this test -- the behaviour changed deliberately, so the assertion did.
    add_test(NAME app_decimate_carries_the_rig COMMAND makehuman
             --decimate 0.5 --export ${CMAKE_CURRENT_BINARY_DIR}/app_lod.glb)
    set_tests_properties(app_decimate_carries_the_rig PROPERTIES
        PASS_REGULAR_EXPRESSION "skin: 179 joints, 4 influences/vertex"
        FAIL_REGULAR_EXPRESSION "export failed"
        FIXTURES_SETUP app_lod_glb)

    # ...and the FILE carries it, which is a different claim: the app printing
    # a skin line and the writer accepting one are two things, and the writer
    # rejected the first attempt here ("skin does not describe this mesh")
    # because the skin was built per LOD render vertex while the geometry went
    # through a further compaction.
    add_test(NAME app_decimate_glb_has_a_skin COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/app_lod.glb)
    set_tests_properties(app_decimate_glb_has_a_skin PROPERTIES
        PASS_REGULAR_EXPRESSION "skin of 179 bones"
        FIXTURES_REQUIRED app_lod_glb)

    # **Whether the weights are RIGHT is not decided here.** A skin of 179
    # bones built from a shifted mapping is still a skin of 179 bones, and
    # nothing this suite can compute would tell the difference. Blender does:
    # `tools/run_blender_validation.sh` exports `posed_lod.glb` and applies the
    # armature itself, landing at 1.6849 x 0.3008 x 1.6634 against the full
    # mesh's 1.6863 x 0.3009 x 1.663 -- within 1.4 mm. That harness is not in
    # CI (no Blender on the runner), so this comment is the pointer to it.

    # Subdivision still refuses, and refuses even WITH --decimate: a subdivided
    # mesh's vertices are ones the weights know nothing about, which no
    # provenance mapping through the decimator can fix.
    add_test(NAME app_decimate_subdivided_still_refuses COMMAND makehuman
             --subdivide --decimate 0.25
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_lod_subdiv.glb)
    set_tests_properties(app_decimate_subdivided_still_refuses PROPERTIES
        PASS_REGULAR_EXPRESSION "subdivided mesh cannot carry the rig")

    # A decimated mesh CARRIES blendshapes now, through the same composed
    # mapping that gave it a skin: `buildExpressionBlendshapes` takes a vmap,
    # so handing it the LOD's puts each delta on the vertex it belongs to. The
    # previous chunk's `app_decimate_refuses_blendshapes` is this test -- the
    # behaviour changed deliberately, so the assertion did.
    #
    # FAIL on the two refusals as well as PASS on the count: "34 blendshapes"
    # is printed before the writer runs, so on its own it cannot tell a written
    # file from a refused one.
    add_test(NAME app_decimate_blendshapes COMMAND makehuman
             --decimate 0.5 --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_lod_shapes.glb)
    set_tests_properties(app_decimate_blendshapes PROPERTIES
        PASS_REGULAR_EXPRESSION "34 blendshapes"
        FAIL_REGULAR_EXPRESSION "export failed;cannot carry blendshapes")

    # **Whether each shape key moves the RIGHT vertices is not decided here.**
    # `--inspect` does not report morph targets at all, so nothing in this suite
    # can see a delta that landed on the wrong vertex. Blender can, and does:
    # `run_blender_validation.sh` exports `expressions_lod.glb` and counts the
    # vertices each of the 34 keys moves, per key.

    # Subdivision still refuses both, and refuses even WITH --decimate.
    add_test(NAME app_decimate_subdivided_refuses_blendshapes COMMAND makehuman
             --subdivide --decimate 0.5 --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_lod_subdiv_shapes.glb)
    set_tests_properties(app_decimate_subdivided_refuses_blendshapes PROPERTIES
        PASS_REGULAR_EXPRESSION "subdivided mesh cannot carry blendshapes")

    add_test(NAME app_decimate_bad_ratio COMMAND makehuman
             --decimate 2 --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_lod.obj)
    set_tests_properties(app_decimate_bad_ratio PROPERTIES
        PASS_REGULAR_EXPRESSION "--decimate wants a fraction in")

    add_test(NAME app_decimate_bad_ratio_text COMMAND makehuman
             --decimate half --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_lod2.obj)
    set_tests_properties(app_decimate_bad_ratio_text PROPERTIES
        PASS_REGULAR_EXPRESSION "--decimate wants a fraction in")

    # ---------------------------------------------------------------------
    # --lod: a CHAIN of levels from one run.
    #
    # OWNER DECISION 2026-09-08 (directive 11): separate files, GLB and FBX.
    # The level index comes from the ORDER the ratios were given, not from
    # sorting them, so the caller decides which level is which.
    # ---------------------------------------------------------------------
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chain)
    add_test(NAME app_lod_chain COMMAND makehuman
             --lod 1.0 --lod 0.5 --lod 0.25
             --export ${CMAKE_CURRENT_BINARY_DIR}/chain/body.glb)
    set_tests_properties(app_lod_chain PROPERTIES
        PASS_REGULAR_EXPRESSION "chain/body_lod2.glb"
        FAIL_REGULAR_EXPRESSION "export failed"
        FIXTURES_SETUP app_lod_chain_glb)

    # Each level is a FILE, and each is smaller than the one before. Reading
    # the triangle counts back is what says the ratios reached the geometry
    # rather than three copies of the same mesh under three names.
    add_test(NAME app_lod_chain_level0 COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/chain/body_lod0.glb)
    set_tests_properties(app_lod_chain_level0 PROPERTIES
        PASS_REGULAR_EXPRESSION "26756 tris"
        FIXTURES_REQUIRED app_lod_chain_glb)
    add_test(NAME app_lod_chain_level1 COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/chain/body_lod1.glb)
    set_tests_properties(app_lod_chain_level1 PROPERTIES
        PASS_REGULAR_EXPRESSION "13378 tris"
        FIXTURES_REQUIRED app_lod_chain_glb)
    add_test(NAME app_lod_chain_level2 COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/chain/body_lod2.glb)
    set_tests_properties(app_lod_chain_level2 PROPERTIES
        PASS_REGULAR_EXPRESSION "6688 tris"
        FIXTURES_REQUIRED app_lod_chain_glb)

    # **Level 0 at ratio 1.0 is not "decimation that removed nothing" -- it is
    # the ORDINARY export.** The decimator at 1.0 still triangulates and
    # recompacts, so it would produce the same shape in a different vertex
    # order; taking the plain path instead makes LOD0 byte-identical to what
    # `--export` alone writes, which is the property a chain wants and the
    # cheapest way to get it.
    add_test(NAME app_lod_chain_plain COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/chain/plain.glb)
    set_tests_properties(app_lod_chain_plain PROPERTIES FIXTURES_SETUP app_lod_chain_plain_glb)
    add_test(NAME app_lod0_is_the_plain_export COMMAND ${CMAKE_COMMAND} -E compare_files
             ${CMAKE_CURRENT_BINARY_DIR}/chain/body_lod0.glb
             ${CMAKE_CURRENT_BINARY_DIR}/chain/plain.glb)
    set_tests_properties(app_lod0_is_the_plain_export PROPERTIES
        FIXTURES_REQUIRED "app_lod_chain_glb;app_lod_chain_plain_glb")

    # FBX too, through OUR OWN writer -- the second format the owner named, and
    # the one no third party wrote for us.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chain_fbx)
    add_test(NAME app_lod_chain_fbx COMMAND makehuman
             --lod 1.0 --lod 0.25
             --export ${CMAKE_CURRENT_BINARY_DIR}/chain_fbx/body.fbx)
    set_tests_properties(app_lod_chain_fbx PROPERTIES
        PASS_REGULAR_EXPRESSION "chain_fbx/body_lod1.fbx"
        FAIL_REGULAR_EXPRESSION "export failed"
        FIXTURES_SETUP app_lod_chain_fbx)
    add_test(NAME app_lod_chain_fbx_level1 COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/chain_fbx/body_lod1.fbx)
    set_tests_properties(app_lod_chain_fbx_level1 PROPERTIES
        PASS_REGULAR_EXPRESSION "6688 tris"
        FIXTURES_REQUIRED app_lod_chain_fbx)

    # The three refusals. Each says which mistake was made, because a shared
    # "bad --lod" would not.
    add_test(NAME app_lod_needs_export COMMAND makehuman --lod 0.5
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_never_chain.mhm)
    set_tests_properties(app_lod_needs_export PROPERTIES
        PASS_REGULAR_EXPRESSION "--lod writes a chain of files, so it needs --export")
    add_test(NAME app_lod_with_decimate COMMAND makehuman --lod 0.5 --decimate 0.5
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_chain.glb)
    set_tests_properties(app_lod_with_decimate PROPERTIES
        PASS_REGULAR_EXPRESSION "--lod and --decimate both reduce the body; give one")
    add_test(NAME app_lod_wrong_format COMMAND makehuman --lod 0.5
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_chain.obj)
    set_tests_properties(app_lod_wrong_format PROPERTIES
        PASS_REGULAR_EXPRESSION "an LOD chain is written as .glb or .fbx")
    add_test(NAME app_lod_bad_ratio COMMAND makehuman --lod 0 --lod 0.5
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_chain.glb)
    set_tests_properties(app_lod_bad_ratio PROPERTIES
        PASS_REGULAR_EXPRESSION "--lod wants a fraction in")

    # ---------------------------------------------------------------------
    # Provenance: every export says what it is (owner directive 12.4).
    #
    # Three facts, one spelling, four formats: the product version, the
    # CONTENT-FORMAT version, and the base topology hash. They were four
    # different strings saying only "MakeHuman C++ <format> writer", which is
    # the drift this exists to stop.
    #
    # `${PROJECT_VERSION}` is interpolated so the expectation derives from
    # /VERSION too -- a literal here would be the second declaration
    # `tools/audit_version.py` exists to forbid.
    # ---------------------------------------------------------------------
    set(MH_STAMP "${PROJECT_VERSION} (content-format 1, topology e38c060123b5d0db)")
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/prov)
    foreach(fmt obj glb usda fbx)
        add_test(NAME app_provenance_${fmt} COMMAND makehuman
                 --export ${CMAKE_CURRENT_BINARY_DIR}/prov/body.${fmt})
        set_tests_properties(app_provenance_${fmt} PROPERTIES
            FIXTURES_SETUP app_prov_${fmt}
            FAIL_REGULAR_EXPRESSION "export failed")
        add_test(NAME app_provenance_${fmt}_stamped COMMAND ${CMAKE_COMMAND}
                 -DFILE=${CMAKE_CURRENT_BINARY_DIR}/prov/body.${fmt}
                 "-DKEYS=${MH_STAMP}"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
        set_tests_properties(app_provenance_${fmt}_stamped PROPERTIES
            FIXTURES_REQUIRED app_prov_${fmt})
    endforeach()

    # The .mtl too: it is a separate file a consumer may keep without the .obj.
    add_test(NAME app_provenance_mtl COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/prov/body.mtl "-DKEYS=${MH_STAMP}"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_provenance_mtl PROPERTIES FIXTURES_REQUIRED app_prov_obj)

    # **A DECIMATED export carries the BASE topology hash, not its own.** The
    # number's job is to say which topology the deltas, weights and correctives
    # in the file are INDEXED AGAINST, and an LOD is still indexed against the
    # base. Hashing the mesh being written would give every level a different
    # number and make the field useless for exactly the thing it is for.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/prov_lod)
    add_test(NAME app_provenance_lod COMMAND makehuman --decimate 0.25
             --export ${CMAKE_CURRENT_BINARY_DIR}/prov_lod/body.obj)
    set_tests_properties(app_provenance_lod PROPERTIES
        FIXTURES_SETUP app_prov_lod FAIL_REGULAR_EXPRESSION "export failed")
    add_test(NAME app_provenance_lod_uses_the_base_topology COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/prov_lod/body.obj "-DKEYS=${MH_STAMP}"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_provenance_lod_uses_the_base_topology PROPERTIES
        FIXTURES_REQUIRED app_prov_lod)

    # ...and a MORPHED character carries the same hash as an unmorphed one.
    # Positions are not topology; if they were, every character would claim to
    # be incompatible with every other.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/prov_morph)
    add_test(NAME app_provenance_morphed COMMAND makehuman
             --set "macrodetails/Gender=0.9" --set "macrodetails-universal/Muscle=0.8"
             --export ${CMAKE_CURRENT_BINARY_DIR}/prov_morph/body.obj)
    set_tests_properties(app_provenance_morphed PROPERTIES
        FIXTURES_SETUP app_prov_morph FAIL_REGULAR_EXPRESSION "export failed")
    add_test(NAME app_provenance_morph_keeps_the_topology COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/prov_morph/body.obj "-DKEYS=${MH_STAMP}"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_provenance_morph_keeps_the_topology PROPERTIES
        FIXTURES_REQUIRED app_prov_morph)

    # ---------------------------------------------------------------------
    # Naming profiles (owner directive 12.1): legacy by default, modern opt-in,
    # canonical ids underneath, and either profile still accepting the other's
    # names with a warning.
    #
    # The resolver and its table are unit-tested in `[naming]`; these say the
    # APPLICATION goes through it. `--screenshot` needs a real device, so each
    # skips where `app_screenshot` skips.
    # ---------------------------------------------------------------------
    add_test(NAME app_naming_legacy_default COMMAND makehuman
             --workspace Materials --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_name_legacy.png)
    set_tests_properties(app_naming_legacy_default PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FAIL_REGULAR_EXPRESSION "no such workspace preset;other profile's name")

    # The MODERN name in the DEFAULT profile: it works, and it says so once.
    # This is the pair that makes migration painless -- refusing it would make
    # a workspace holding both kinds of name unusable.
    add_test(NAME app_naming_modern_name_in_legacy COMMAND makehuman
             --workspace Assets --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_name_ml.png)
    set_tests_properties(app_naming_modern_name_in_legacy PROPERTIES
        PASS_REGULAR_EXPRESSION "\"Assets\" is the other profile's name for this; Materials"
        SKIP_REGULAR_EXPRESSION "viewport error"
        FAIL_REGULAR_EXPRESSION "no such workspace preset")

    add_test(NAME app_naming_modern COMMAND makehuman
             --naming modern --workspace Assets
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_name_modern.png)
    set_tests_properties(app_naming_modern PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FAIL_REGULAR_EXPRESSION "no such workspace preset;other profile's name")

    add_test(NAME app_naming_legacy_name_in_modern COMMAND makehuman
             --naming modern --workspace Materials
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_name_lm.png)
    set_tests_properties(app_naming_legacy_name_in_modern PROPERTIES
        PASS_REGULAR_EXPRESSION "\"Materials\" is the other profile's name for this; Assets"
        SKIP_REGULAR_EXPRESSION "viewport error"
        FAIL_REGULAR_EXPRESSION "no such workspace preset")

    # A CANONICAL id works in either profile and warns in neither: it is what
    # save files persist, so reading one back is the normal case.
    add_test(NAME app_naming_canonical_id COMMAND makehuman
             --workspace workspace.assets
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_name_canon.png)
    set_tests_properties(app_naming_canonical_id PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FAIL_REGULAR_EXPRESSION "no such workspace preset;other profile's name")

    # Refused rather than defaulted: `--naming moderm` silently giving the old
    # names would look like the flag did nothing.
    add_test(NAME app_naming_unknown_profile COMMAND makehuman
             --naming moderm --workspace Assets
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_never_name.png)
    set_tests_properties(app_naming_unknown_profile PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --naming profile")

    add_test(NAME app_naming_unknown_preset COMMAND makehuman
             --workspace Nonsense --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_never_name2.png)
    set_tests_properties(app_naming_unknown_preset PROPERTIES
        PASS_REGULAR_EXPRESSION "no such workspace preset"
        SKIP_REGULAR_EXPRESSION "viewport error")

    # The table reaches the MENU, which is the half a user reads. The LABELS
    # are the assertion, not a count: "5 workspace names" passed on a hardcoded
    # 5, so the count could not tell a relabelling that happened from one that
    # never did. The names can -- and legacy vs modern differ in exactly one of
    # them, which is the whole point of the profile.
    #
    # The label itself is asserted in `[ui][naming]`, which builds a window and
    # reads the QAction; nothing the application prints can show text on a menu.
    add_test(NAME app_naming_reaches_the_menu COMMAND makehuman
             --naming modern --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_name_menu.png)
    set_tests_properties(app_naming_reaches_the_menu PROPERTIES
        PASS_REGULAR_EXPRESSION "naming: modern profile \\(Modelling, Rigging, Assets, Export, Tabbed\\)"
        SKIP_REGULAR_EXPRESSION "viewport error")
    add_test(NAME app_naming_default_reaches_the_menu COMMAND makehuman
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_name_menu_legacy.png)
    set_tests_properties(app_naming_default_reaches_the_menu PROPERTIES
        PASS_REGULAR_EXPRESSION "naming: legacy profile \\(Modelling, Rigging, Materials, Export, Tabbed\\)"
        SKIP_REGULAR_EXPRESSION "viewport error")

    # Symmetry, end to end. The menu commands cannot be driven from a test --
    # `symmetryRequested` is wired in main.cpp -- so `--symmetry` is what makes
    # the whole path observable: one asymmetric modifier in, both sides equal
    # out, in a file that can be read back.
    add_test(NAME app_symmetry COMMAND makehuman
             --set "eyes/l-eye-bag-decr|incr=0.8" --symmetry l2r
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_symmetry.mhm)
    set_tests_properties(app_symmetry PROPERTIES
        PASS_REGULAR_EXPRESSION "mirrored 1 modifiers"
        FIXTURES_SETUP app_symmetry_mhm)
    # ...and the file, because "the app said it mirrored one" and "the right eye
    # actually carries the left one's value" are different claims. Both lines
    # are asserted: the SOURCE has to survive untouched, or a command that
    # simply zeroed both sides would pass.
    add_test(NAME app_symmetry_file COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_symmetry.mhm
             "-DKEYS=modifier eyes/r-eye-bag-decr|incr 0.800000;modifier eyes/l-eye-bag-decr|incr 0.800000"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_symmetry_file PROPERTIES FIXTURES_REQUIRED app_symmetry_mhm)
    add_test(NAME app_symmetry_unknown COMMAND makehuman
             --symmetry sideways --export ${CMAKE_CURRENT_BINARY_DIR}/app_never_sym.obj)
    set_tests_properties(app_symmetry_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --symmetry direction")

    # `--subdivide` had NO app-level test. Catmull-Clark itself is covered by
    # unit and golden fixtures; nothing checked that the flag reaches the scene,
    # which is precisely what the new Smooth toolbar toggle drives. The face
    # count is what makes it observable: 53,512 of 73,944 against the base
    # mesh's 13,378 of 18,486, and asserting it here is what stops the toggle
    # becoming a button that ticks and changes nothing.
    add_test(NAME app_subdivide COMMAND makehuman
             --subdivide --export ${CMAKE_CURRENT_BINARY_DIR}/app_subdivide.obj)
    set_tests_properties(app_subdivide PROPERTIES
        PASS_REGULAR_EXPRESSION "body: 53512 of 73944 faces visible"
        FIXTURES_SETUP app_subdivide_obj)
    # ...and then read the file, for the reason app_smoke_obj_faces gives: "the
    # app said 53512" and "the file holds 53512" are different claims, and it
    # was the second one that was false last time. 54,532 = 53,512 subdivided
    # body + 1,020 eyes, which are a separate proxy mesh and are NOT subdivided.
    add_test(NAME app_subdivide_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/app_subdivide.obj -DEXPECT=54532
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_body_faces.cmake)
    set_tests_properties(app_subdivide_obj_faces PROPERTIES
        FIXTURES_REQUIRED app_subdivide_obj)

    # A texture path is written INTO an exported file, so it has to be clean.
    # Measured before `lexically_normal` landed: every shipped skin says
    # `diffuseTexture ../textures/skin/<tone>.png`, and this export embedded
    # `.../data/skins/../textures/skin/african_deep.png` four times over. The
    # same unnormalised path was the exporters' dedup key, so one image named
    # from two directories was embedded twice.
    #
    # `--count 0` is an ABSENCE check, and it is counted in Python for a reason:
    # `tests/file_count.cmake` used to do this and silently checked nothing on a
    # binary. CMake's `string(REGEX MATCHALL)` stops at the first NUL, and an
    # FBX has one at byte 20 -- so it saw twenty bytes and passed a file holding
    # four copies of the bad path. Proven by running it against that file.
    add_test(NAME app_texture_path_export COMMAND makehuman
             --skin-material african_deep
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_skinmat.fbx)
    set_tests_properties(app_texture_path_export PROPERTIES
        FIXTURES_SETUP app_skinmat_fbx)
    add_test(NAME app_texture_path_normalised COMMAND python3
             ${CMAKE_SOURCE_DIR}/tools/count_in_file.py
             --file ${CMAKE_CURRENT_BINARY_DIR}/app_skinmat.fbx
             "--needle=/../"
             --count 0)
    set_tests_properties(app_texture_path_normalised PROPERTIES
        FIXTURES_REQUIRED app_skinmat_fbx)
    # ...and the texture it names is really there, which is the point of a clean
    # path. A test that only forbade `..` would pass on a path to nowhere.
    add_test(NAME app_texture_path_resolves COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_skinmat.fbx
             "-DKEYS=data/textures/skin/african_deep.png"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_texture_path_resolves PROPERTIES
        FIXTURES_REQUIRED app_skinmat_fbx)

    # ONE product version, in /VERSION, and every other declaration derived
    # from it. The gate is a ctest as well as a CI job because the thing it
    # catches -- a version that is right in three places and wrong in a fourth
    # -- is found by a user reading an About box, not by a compiler.
    add_test(NAME audit_version
             COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/audit_version.py)

    # The task-view inventory is a ROADMAP: its buckets decide what gets built
    # next, so a stale one sends work at something already shipped or hides work
    # behind a blocker that is gone. Both happened -- corrected 2026-09-12, when
    # the evidence gate found FOURTEEN views misfiled in two directions.
    #
    # It was a CI-only job until now, which is why four local preset runs never
    # once exercised it.
    add_test(NAME audit_taskviews
             COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/audit_taskviews.py)

    # ------------------------------------------------------------------
    # Custom targets: a user's own .target morphs, from a directory of their
    # choosing. The unit tests cover the modifier; these cover the only thing
    # that makes it a FEATURE -- that the flag reaches the mesh.
    #
    # The fixture is written by CMake rather than committed, because a target
    # is just indices and offsets and an invented one states its own intent:
    # two vertices, half a decimetre along +X.
    # ------------------------------------------------------------------
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/dir)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/dir/nosejob.target
         "11000 0.500000 0.000000 0.000000\n11001 0.500000 0.000000 0.000000\n")

    add_test(NAME app_custom_targets_found COMMAND makehuman
             --custom-targets ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/dir
             --export ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/off.obj)
    set_tests_properties(app_custom_targets_found PROPERTIES
        PASS_REGULAR_EXPRESSION "custom targets: 1 in"
        FIXTURES_SETUP app_custom_targets_off)

    add_test(NAME app_custom_targets_applied COMMAND makehuman
             --custom-targets ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/dir
             --set custom/nosejob=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/on.obj)
    set_tests_properties(app_custom_targets_applied PROPERTIES
        FAIL_REGULAR_EXPRESSION "no such modifier"
        FIXTURES_SETUP app_custom_targets_on)

    # The claim that matters: setting it moves EXACTLY the two vertices the
    # target names, and nothing else.
    #
    # This was `files_differ.cmake` first, and that was DECORATIVE -- proven by
    # mutating the stack entry away and watching it still pass. Two exports to
    # different paths always differ, because the writer bakes the output stem
    # into its `mtllib <stem>.mtl` line, so the comparison was reading the
    # FILENAME rather than the geometry. Counting the moved vertices reads the
    # thing the feature is about.
    add_test(NAME app_custom_targets_change_the_mesh COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/custom_targets/off.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/custom_targets/on.obj
             -DGROUP=body -DEXPECT_MOVED=2 -DEXPECT_STILL=13378
             -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_custom_targets_change_the_mesh PROPERTIES
        FIXTURES_REQUIRED "app_custom_targets_on;app_custom_targets_off")

    # A directory with no .target in it is not an error -- most users never
    # make one -- but it must say so rather than pretend it loaded something.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/empty)
    add_test(NAME app_custom_targets_empty_dir COMMAND makehuman
             --custom-targets ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/empty
             --export ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/empty.obj)
    set_tests_properties(app_custom_targets_empty_dir PROPERTIES
        PASS_REGULAR_EXPRESSION "custom targets: 0 in")

    # And without the flag nothing changes at all: no custom line, and the
    # shipped sliders are untouched.
    add_test(NAME app_custom_targets_absent_is_quiet COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/custom_targets/none.obj)
    set_tests_properties(app_custom_targets_absent_is_quiet PROPERTIES
        FAIL_REGULAR_EXPRESSION "custom targets:")

    # foundation, core, rig and io must build without a GUI stack -- that is
    # what makes the headless `parameters -> mesh` path (M10) usable from a
    # batch sampler or a server. They are Qt-free today and nothing enforced
    # it, which is the position `<charconv>` was in when it reached CI twice.
    add_test(NAME audit_headless
             COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/audit_headless.py)

    # Every dependency recorded in LICENSING.md 5.1, and none that 5.2 forbids.
    # A ctest as well as a CI job because the hole it closes was opened by a
    # DOCUMENTATION edit: the inline shell it replaces grepped the whole file,
    # so recording FFTW as refused made `find_package(FFTW)` pass the gate.
    # Measured at the time -- exit 0 with a GPL-2+ dependency in the build.
    add_test(NAME audit_dependencies
             COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/audit_dependencies.py)

    # `makehuman --version` did not work at all: no addVersionOption(), and the
    # version was a PRIVATE macro in mh_core that main.cpp could not read.
    #
    # The expected string is interpolated from PROJECT_VERSION, so the TEST
    # derives from /VERSION too -- a literal here would be the fourth
    # declaration this whole chunk exists to remove.
    add_test(NAME app_version COMMAND makehuman --version)
    set_tests_properties(app_version PROPERTIES
        PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")

    # ...and it answers on a machine with NO asset tree, which is the whole
    # point of `--version`: it is what a bug report is asked for, and it used to
    # die with "cannot find the asset tree" because the data check ran before
    # QCommandLineParser existed. Same for `--help`, and same for `--inspect`,
    # whose own comment in main.cpp already promised "no base mesh, no rig, no
    # assets" while the check upstream of it made that false.
    #
    # An empty directory named by MH_DATA_DIR is what makes "no asset tree"
    # reachable at all: every other candidate comes from the executable's path
    # or from the compile-time default, and both exist here. The override is
    # authoritative -- see test_data_dir.cpp -- so this genuinely runs the
    # binary with nowhere to load assets from.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/no_assets)
    add_test(NAME app_version_without_assets COMMAND makehuman --version)
    set_tests_properties(app_version_without_assets PROPERTIES
        ENVIRONMENT "MH_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/no_assets"
        PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")

    add_test(NAME app_help_without_assets COMMAND makehuman --help)
    set_tests_properties(app_help_without_assets PROPERTIES
        ENVIRONMENT "MH_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/no_assets"
        PASS_REGULAR_EXPRESSION "Usage")

    add_test(NAME app_inspect_without_assets COMMAND makehuman
             --inspect ${CMAKE_CURRENT_SOURCE_DIR}/golden/obj/base_ref.obj)
    set_tests_properties(app_inspect_without_assets PROPERTIES
        ENVIRONMENT "MH_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/no_assets"
        PASS_REGULAR_EXPRESSION "base_ref.obj: 1 meshes")

    # The guard still guards. Moving a check later is exactly the change that
    # deletes it by accident, so anything that DOES need the assets must still
    # refuse and must name the directory it looked in. Deleting the guard does
    # not make this pass by falling through: the next failure is "cannot load
    # the base mesh", which this expression does not match.
    add_test(NAME app_without_assets_refuses COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_no_assets.mhm)
    set_tests_properties(app_without_assets_refuses PROPERTIES
        ENVIRONMENT "MH_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/no_assets"
        PASS_REGULAR_EXPRESSION "cannot find the asset tree.*no_assets")

    # ...and it reaches the one export format that has always carried it. The
    # .mhm header is written from foundation::kVersion; nothing asserted that
    # before, so the version could have been empty and every test stayed green.
    add_test(NAME app_mhm_carries_version COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_version.mhm)
    set_tests_properties(app_mhm_carries_version PROPERTIES
        FIXTURES_SETUP app_version_mhm)
    add_test(NAME app_mhm_version_header COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_version.mhm
             "-DKEYS=# Written by MakeHuman ${PROJECT_VERSION}"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_mhm_version_header PROPERTIES
        FIXTURES_REQUIRED app_version_mhm)

    # Pixar's own validator on the stage we generate. It is the tool that caught
    # the SkelRoot requirement -- Blender imports the non-conformant version
    # without complaint -- so it earns a place in the automated gate rather than
    # staying a thing someone remembers to run.
    find_program(MH_USDCHECKER usdchecker)
    if(MH_USDCHECKER)
        add_test(NAME app_blendshapes_usda_valid
                 COMMAND ${MH_USDCHECKER} ${CMAKE_CURRENT_BINARY_DIR}/app_bs.usda)
        set_tests_properties(app_blendshapes_usda_valid PROPERTIES
            FIXTURES_REQUIRED app_bs_usda_file)
    endif()

    # OBJ is the only format left with no blendshape channel, and it says so
    # rather than writing an expressionless mesh in silence.
    add_test(NAME app_blendshapes_obj_warns COMMAND makehuman
             --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_bs.obj)
    set_tests_properties(app_blendshapes_obj_warns PROPERTIES
        PASS_REGULAR_EXPRESSION "carries no blendshapes")

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

    # Everything WORN follows the pose too, on the same skeleton. A live-rig
    # export ships rest geometry and lets the consumer pose it, so an unskinned
    # entry simply stays put: measured in Maya on 2026-09-07, the body deformed
    # and the eyes did not, so they protruded from their sockets.
    #
    # assimp reads this back, not our own reader, and reports the bone count per
    # MESH -- which is also what proves the joints are shared rather than
    # duplicated, since 179 is the whole rig and there is one skeleton in it.
    add_test(NAME app_worn_skin_export COMMAND makehuman
             --rig mixamo_superset
             --eyes high-poly
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_worn.fbx)
    set_tests_properties(app_worn_skin_export PROPERTIES
        PASS_REGULAR_EXPRESSION "eyes skin: 179 joints"
        FIXTURES_SETUP app_worn_fbx_file)
    add_test(NAME app_worn_skin COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/app_worn.fbx)
    set_tests_properties(app_worn_skin PROPERTIES
        PASS_REGULAR_EXPRESSION "mesh \"eyes\":.*skin of 179 bones"
        FIXTURES_REQUIRED app_worn_fbx_file)

    # ...and the same in glTF, which had the identical defect: writeGlbScene
    # allowed exactly one skinned entry, so Blender read posed.glb as 14,517
    # skinned vertices of 15,593 -- the body alone.
    add_test(NAME app_worn_skin_glb_export COMMAND makehuman
             --rig mixamo_superset
             --eyes high-poly
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_worn.glb)
    set_tests_properties(app_worn_skin_glb_export PROPERTIES
        PASS_REGULAR_EXPRESSION "eyes skin: 179 joints"
        FIXTURES_SETUP app_worn_glb_file)
    add_test(NAME app_worn_skin_glb COMMAND makehuman
             --inspect ${CMAKE_CURRENT_BINARY_DIR}/app_worn.glb)
    set_tests_properties(app_worn_skin_glb PROPERTIES
        PASS_REGULAR_EXPRESSION "mesh \"eyes\":.*skin of 179 bones"
        FIXTURES_REQUIRED app_worn_glb_file)

    # ...and USD, which had it worst: `writeUsdaScene` took the skin as a
    # PARAMETER and bound it to the first entry, so nothing worn could ever be
    # skinned. Counted rather than merely found: a stage with one Skeleton prim
    # and ONE mesh bound to it contains every key a correct one does.
    add_test(NAME app_worn_skin_usda_export COMMAND makehuman
             --rig mixamo_superset
             --eyes high-poly
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_worn.usda)
    set_tests_properties(app_worn_skin_usda_export PROPERTIES
        PASS_REGULAR_EXPRESSION "eyes skin: 179 joints"
        FIXTURES_SETUP app_worn_usda_file)
    add_test(NAME app_worn_skin_usda_bound COMMAND python3
             ${CMAKE_SOURCE_DIR}/tools/count_in_file.py
             --file ${CMAKE_CURRENT_BINARY_DIR}/app_worn.usda
             "--needle=rel skel:skeleton = </"
             --count 2)
    set_tests_properties(app_worn_skin_usda_bound PROPERTIES
        FIXTURES_REQUIRED app_worn_usda_file)
    # ONE skeleton, shared. Two prims would be two rigs.
    add_test(NAME app_worn_skin_usda_one_skeleton COMMAND python3
             ${CMAKE_SOURCE_DIR}/tools/count_in_file.py
             --file ${CMAKE_CURRENT_BINARY_DIR}/app_worn.usda
             "--needle=def Skeleton"
             --count 1)
    set_tests_properties(app_worn_skin_usda_one_skeleton PROPERTIES
        FIXTURES_REQUIRED app_worn_usda_file)
    if(MH_USDCHECKER)
        add_test(NAME app_worn_skin_usda_valid
                 COMMAND ${MH_USDCHECKER} ${CMAKE_CURRENT_BINARY_DIR}/app_worn.usda)
        set_tests_properties(app_worn_skin_usda_valid PROPERTIES
            FIXTURES_REQUIRED app_worn_usda_file)
    endif()

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

    # The app icon has to BE in the bundle, not merely named in Info.plist.
    # Naming it and forgetting to install it is the failure that shows a
    # generic icon with no error anywhere -- and "AppIcon.icns" in
    # CFBundleIconFile resolves to AppIcon.icns.icns and does the same.
    if(APPLE)
        add_test(NAME app_bundle_icon COMMAND ${CMAKE_COMMAND}
                 -DFILE=$<TARGET_BUNDLE_CONTENT_DIR:makehuman>/Resources/AppIcon.icns
                 "-DKEYS=icns"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)

        add_test(NAME app_bundle_plist COMMAND ${CMAKE_COMMAND}
                 -DFILE=$<TARGET_BUNDLE_CONTENT_DIR:makehuman>/Info.plist
                 # No trailing .icns on the value -- see Info.plist.in.
                 "-DKEYS=<key>CFBundleIconFile</key>;<string>AppIcon</string>;org.makehuman.makehuman"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    endif()

    # Skin MATERIALS. Eight generated tones plus the untextured default; before
    # them data/skins held one material naming no texture at all, and the three
    # "Skin" choices in the UI were litspheres (matcaps), not skin colour.
    add_test(NAME app_skin_material COMMAND makehuman
             --skin-material african_deep
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_skin.glb)
    set_tests_properties(app_skin_material PROPERTIES FIXTURES_SETUP app_skin_glb)
    add_test(NAME app_skin_material_textured COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_skin.glb
             # The albedo AND the shared normal map must both reach the file;
             # naming a texture the writer never embeds is the failure that
             # renders as untextured grey with no error.
             "-DKEYS=baseColorTexture;normalTexture;AfricanDeep"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_skin_material_textured PROPERTIES FIXTURES_REQUIRED app_skin_glb)

    # An unknown name is refused with the list, not silently ignored.
    add_test(NAME app_skin_material_unknown COMMAND makehuman
             --skin-material no_such_tone
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_skin_bad.glb)
    set_tests_properties(app_skin_material_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --skin-material no_such_tone")

    # The choice survives a save/reload round trip. The reference writes
    # `skinMaterial <relative path>` (3_libraries_material_chooser.py:305);
    # without it a textured character reopened as the untextured default.
    add_test(NAME app_skin_material_save COMMAND makehuman
             --skin-material african_rich
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_skin.mhm)
    set_tests_properties(app_skin_material_save PROPERTIES FIXTURES_SETUP app_skin_mhm)
    add_test(NAME app_skin_material_line COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_skin.mhm
             "-DKEYS=skinMaterial skins/african_rich.mhmat"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_skin_material_line PROPERTIES FIXTURES_REQUIRED app_skin_mhm)

    add_test(NAME app_skin_material_reload COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/app_skin.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_skin_reload.glb)
    set_tests_properties(app_skin_material_reload PROPERTIES
        PASS_REGULAR_EXPRESSION "skin material african_rich \\(from the file\\)"
        FIXTURES_SETUP app_skin_reload_glb
        FIXTURES_REQUIRED app_skin_mhm)

    # The EFFECT, not the announcement. Checking only the printed line let a
    # mutation that printed the message and never applied the material pass --
    # measured. The exported material name is the thing that proves it landed,
    # and it is distinct per tone because .mhmat `name` is one token.
    add_test(NAME app_skin_material_reload_applied COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_skin_reload.glb
             "-DKEYS=AfricanRich;baseColorTexture"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_skin_material_reload_applied PROPERTIES
        FIXTURES_REQUIRED app_skin_reload_glb)

    # The DEFAULT rig, asserted because nothing else does: every other rig test
    # passes --rig explicitly, so a change to the default would move every
    # export's skeleton and no test would notice.
    #
    # 179 = MakeHuman's own 163 plus the 16 bones Mixamo names and it lacks, so
    # defaulting here gives up nothing and makes a Mixamo retarget total rather
    # than lossy (owner decision, 2026-09-05).
    add_test(NAME app_default_rig COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/app_default_rig.glb)
    set_tests_properties(app_default_rig PROPERTIES
        PASS_REGULAR_EXPRESSION "rig mixamo_superset \\(179 bones\\)")

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
        PASS_REGULAR_EXPRESSION
            "rendered .*app_render\\.png \\(1024x1024, transparent, litsphere\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "nothing to draw;cannot write")

    # Wireframe, at the level where it silently failed. The render-level test
    # (`[render][wireframe]`) proves the PIPELINE draws edges; this proves the
    # FLAG reaches it. It caught the real thing: `rs.wireframe = req.wireframe`
    # was missing from renderImage, the viewport drew edges, `--wireframe
    # --render` wrote a byte-identical solid image, and every unit test passed.
    #
    # files_differ is enough here only because two renders of the same scene
    # ARE byte-identical -- checked, not assumed -- so the difference can come
    # from nothing else.
    add_test(NAME app_wireframe COMMAND makehuman
             --wireframe --render ${CMAKE_CURRENT_BINARY_DIR}/app_wire.png)
    set_tests_properties(app_wireframe PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered .*app_wire\\.png"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_wire_png)
    add_test(NAME app_wireframe_solid COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_solid.png)
    set_tests_properties(app_wireframe_solid PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered .*app_solid\\.png"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_solid_png)
    add_test(NAME app_wireframe_differs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/app_wire.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/app_solid.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_wireframe_differs PROPERTIES
        FIXTURES_REQUIRED "app_wire_png;app_solid_png")

    # `--render` checks its own frame now. It did not: `describe` was reachable
    # only from `--screenshot`, so a production render of nothing at all saved a
    # valid PNG and exited 0 -- the failure that looks like success in a log.
    # Asserting the coverage line is what proves the guard RAN; the guard itself
    # refuses to write the file when a frame is blank.
    add_test(NAME app_render_not_blank COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_render_checked.png)
    set_tests_properties(app_render_not_blank PROPERTIES
        PASS_REGULAR_EXPRESSION "1024x1024 covered [0-9]+ of 1048576 pixels"
        SKIP_REGULAR_EXPRESSION "cannot render")

    # Owner directive 4: the viewport shades with metallic-roughness as well as
    # with the reference matcap. --render goes through the SAME buildScene() and
    # the same SceneResources as the window, so this covers both.
    # The shipped eye material is `transparent True`, and until now that flag
    # never left the material: `ViewportMaps::transparent` was unassigned, so
    # every worn thing drew in the OPAQUE pass with its alpha discarded. The
    # eye's front geometry is a cornea whose texel is alpha 0 -- invisible when
    # blended, an opaque pale disc when not, which is why the eyes rendered as
    # blank white ovals.
    #
    # Asserted from the count the SCENE reports, not from the material: an
    # earlier version of this printed the material's own flag and was blind to
    # exactly the wiring it was meant to cover.
    # Eye COLOUR is independent of the Eyes group, which picks the geometry.
    # Six groups now: Skin, Pose, Eyes, Skin material, Eye colour, Skeleton.
    add_test(NAME app_eye_colour COMMAND makehuman
             --eye-colour green
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_eye_colour.png)
    set_tests_properties(app_eye_colour PROPERTIES
        PASS_REGULAR_EXPRESSION
            # The group NAMES sit between the count and the counts now; both halves
            # of what this case cared about are still pinned.
            "asset groups: 11 \\[.*\\] \\(skin materials: 9, rigs: 2\\)"
        SKIP_REGULAR_EXPRESSION "cannot render")

    # Refused rather than silently defaulted to brown, which would make a typo
    # produce a plausible character that is not the one asked for.
    add_test(NAME app_eye_colour_unknown COMMAND makehuman
             --eye-colour chartreuse
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_eye_bad.png)
    set_tests_properties(app_eye_colour_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --eye-colour")

    # Saved in the .mhm and restored, like the skin material. The eye GEOMETRY
    # and the eye COLOUR are separate lines and both have to survive.
    add_test(NAME app_eye_colour_save COMMAND makehuman
             --eye-colour blue --save ${CMAKE_CURRENT_BINARY_DIR}/app_eye_colour.mhm)
    set_tests_properties(app_eye_colour_save PROPERTIES FIXTURES_SETUP app_eye_colour_mhm)
    add_test(NAME app_eye_colour_saved_line COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_eye_colour.mhm
             "-DKEYS=eyeMaterial eyes/materials/blue.mhmat"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_eye_colour_saved_line PROPERTIES
        FIXTURES_REQUIRED app_eye_colour_mhm)
    add_test(NAME app_eye_colour_reload COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/app_eye_colour.mhm
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_eye_colour_reload.png)
    set_tests_properties(app_eye_colour_reload PROPERTIES
        PASS_REGULAR_EXPRESSION "eye colour blue \\(from the file\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_REQUIRED app_eye_colour_mhm)

    add_test(NAME app_eye_is_blended COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_blend.png)
    set_tests_properties(app_eye_is_blended PROPERTIES
        PASS_REGULAR_EXPRESSION "scene: 2 meshes \\(1 blended\\)"
        SKIP_REGULAR_EXPRESSION "cannot render")

    add_test(NAME app_render_pbr COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_render_pbr.png --shading pbr)
    set_tests_properties(app_render_pbr PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered .*app_render_pbr\\.png \\(1024x1024, pbr\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "nothing to draw;cannot write"
        FIXTURES_SETUP app_pbr_default_png)

    # An unrecognised model must be REFUSED. Defaulting to the litsphere would
    # make a typo produce a plausible image that is not what was asked for, and
    # nothing downstream could tell.
    add_test(NAME app_shading_unknown COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_shading_unknown.png --shading pbrr)
    set_tests_properties(app_shading_unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --shading")

    # --skin-material picked the material the EXPORTERS wrote and nothing else:
    # buildScene() read skins/default.mhmat by NAME, so all eight shipped tones
    # rendered as the same untextured body. Two renders that differ only in the
    # skin must not come out byte-identical.
    add_test(NAME app_render_pbr_skin COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_render_pbr_skin.png
             --shading pbr --skin-material african_deep)
    set_tests_properties(app_render_pbr_skin PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered .*app_render_pbr_skin\\.png \\(1024x1024, pbr\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_pbr_skin_png)

    add_test(NAME app_skin_material_reaches_the_viewport COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/app_render_pbr.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/app_render_pbr_skin.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_skin_material_reaches_the_viewport PROPERTIES
        FIXTURES_REQUIRED "app_pbr_default_png;app_pbr_skin_png")

    set_tests_properties(ui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${MH_TEST_TMPDIR}")
    set_tests_properties(ui_icons_hidpi PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2;TMPDIR=${MH_TEST_TMPDIR}")
endif()
