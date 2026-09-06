# SPDX-License-Identifier: AGPL-3.0-or-later
# The Draco tests decode what the encoder produced -- a separate code path in
# the same library -- so the test binary needs draco's headers as well.
find_package(draco QUIET)

add_executable(mh_tests
    unit/test_mesh.cpp
    unit/test_random.cpp
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
    unit/test_transform.cpp
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
    golden/test_body_pose.cpp
    golden/test_slider_layout.cpp
    golden/test_obj_writer_parity.cpp
    golden/test_blendshapes.cpp
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
target_link_libraries(mh_export_fixture PRIVATE mh::core mh::rig mh::warnings)
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
    add_executable(mh_ui_tests ui/test_ui.cpp ui/test_theme.cpp ui/test_language.cpp)
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
        PASS_REGULAR_EXPRESSION "asset groups: 6 \\(skin materials: 9, rigs: 2\\)"
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
        PASS_REGULAR_EXPRESSION "asset groups: 6 \\(skin materials: 9, rigs: 2\\)"
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
