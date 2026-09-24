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
    unit/test_makehuman1_retarget.cpp
    unit/test_retarget_import.cpp
    unit/test_asset_meta.cpp
    unit/test_body_poseunits.cpp
    unit/test_centers_of_rotation.cpp
    unit/test_retarget_export.cpp
    unit/test_retarget_map.cpp
    unit/test_rest_alignment.cpp
    unit/test_scalp.cpp
    unit/test_surface_walk.cpp
    unit/test_surface_bind.cpp
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
    golden/test_etc1s.cpp
    golden/test_scene_io.cpp
    golden/test_roundtrip.cpp
    regression/test_repose.cpp
    regression/test_review_findings.cpp
    regression/test_io_review_findings.cpp
    regression/test_determinism.cpp
    regression/test_animation_upaxis.cpp
    regression/test_animation_no_tear.cpp
    regression/test_afro_shape.cpp
    regression/test_cornrows_shape.cpp
    regression/test_locs_shape.cpp)

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

# The bundled ARKit face units. `tools/make_faceunits.py` derives their modifier
# and slider declarations FROM the target files on disk, so 52 names cannot
# drift out of step with 52 files across two JSON documents in two shapes.
#
# Needs Python but NOT Qt, so it lives out here where the Qt-less CI jobs can
# run it -- the lesson from the commit that put `mh_png_compare` in the wrong
# place, applied in the other direction.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME faceunit_declarations_are_generated
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/make_faceunits.py --check)
else()
    # SAID OUT LOUD. A bare `if(FOUND)` makes the gate vanish where Python is
    # absent and the CI log then shows a green run with one fewer test, which is
    # indistinguishable from a passing one.
    message(WARNING "no Python3: faceunit_declarations_are_generated will not run")
endif()

# The licence FILES must still say what the About box claims. These are the
# half of that pair which needs neither Qt nor a GPU -- they are `cmake -P`
# scripts -- so they live OUTSIDE the render guard and run on the two CI jobs
# that install only ninja and assimp. Putting them inside it was the same
# mistake as the commit before this one, in the direction that matters less
# but for the same reason: the check that could run everywhere did not.
#
# Relicense the project and these fail, which is the prompt to go and fix the
# application's own text rather than ship it asserting a licence it no longer
# carries.
add_test(NAME licence_file_still_says_affero COMMAND ${CMAKE_COMMAND}
         -DFILE=${CMAKE_SOURCE_DIR}/LICENSE.CODE.md
         "-DKEYS=GNU Affero General Public License"
         -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
add_test(NAME asset_licence_file_still_says_cc0 COMMAND ${CMAKE_COMMAND}
         -DFILE=${CMAKE_SOURCE_DIR}/LICENSE.ASSETS.md
         "-DKEYS=Creative Commons CC0 1.0 Universal"
         -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)

# Tolerant image comparison, because two GPU renders of one scene are not
# bit-identical. C++ and Qt rather than Pillow so it runs in CI, where the
# Python image checks skip for want of a wheel.
#
# GUARDED, because "Qt rather than Pillow" cuts both ways: two CI jobs -- the
# forced-charconv build and the benchmark -- install only ninja and assimp, so
# `Qt6::Gui` does not exist there and an unguarded `target_link_libraries`
# failed their CONFIGURE outright. A four-preset local gate cannot see that;
# every preset here has Qt. Reproduce it with
# `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE`.
if(MH_HAVE_RENDER)
    add_executable(mh_png_compare mh_png_compare.cpp)
    target_link_libraries(mh_png_compare PRIVATE Qt6::Gui mh::warnings mh::sanitize)
    set_target_properties(mh_png_compare PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

    # The metric checked against arithmetic, not against another screenshot.
    #
    # Needed because the two frame comparisons further down cannot pin the
    # FORMULA: they separate infinity from 11 dB and nothing finer, so five
    # mutations that merely RESCALED the result -- dividing by pixels instead
    # of samples, MAX instead of MAX squared, not squaring the error -- passed
    # both while making every reported number wrong. The KTX2 encoder has to
    # hold a specific 38.92 dB, so a constant factor would certify the wrong
    # quality in silence.
    #
    # Two images differing by exactly d in every channel have MSE = d squared,
    # so PSNR = 20 log10(255/d): 48.13, 42.11 and 34.15 dB for d of 1, 2 and 5.
    # No fixture, no GPU, no screenshot.
    add_test(NAME png_compare_psnr_matches_arithmetic COMMAND mh_png_compare --selftest)

    # ETC1S round-trip, for the QUALITY gate below. It asserts nothing itself:
    # "how far did the image move" is exactly what `mh_png_compare --min-psnr`
    # answers, so this only produces the second image and the existing, already
    # mutation-tested comparer does the judging.
    add_executable(mh_etc1s_roundtrip mh_etc1s_roundtrip.cpp)
    target_link_libraries(mh_etc1s_roundtrip PRIVATE mh::io Qt6::Gui mh::warnings mh::sanitize)
    set_target_properties(mh_etc1s_roundtrip PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

    # Encodes through libktx. Registered only when there IS an encoder --
    # a target that cannot run is worse than an absent one.
    if(MH_WITH_KTX2)
        add_executable(mh_ktx2_encode mh_ktx2_encode.cpp)
        target_link_libraries(mh_ktx2_encode PRIVATE mh::io Qt6::Gui mh::warnings mh::sanitize)
        set_target_properties(mh_ktx2_encode PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
    endif()
endif()

# NOT guarded, unlike mh_png_compare above: this one needs no Qt, only the
# header-only JSON already fetched for `.mhskel` parsing. Keeping it out of the
# guard means the no-Qt configure builds it too, so a GLB written there can
# still be measured.
add_executable(mh_glb_stat mh_glb_stat.cpp)
target_link_libraries(mh_glb_stat PRIVATE mh_json mh::warnings mh::sanitize)
set_target_properties(mh_glb_stat PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
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
                                   unit/test_picking.cpp
                                   render/test_backdrop_uv.cpp)
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
                               ui/test_background.cpp
                               ui/test_backdrop.cpp
                               ui/test_about.cpp
                               ui/test_material_panel.cpp
                               ui/test_frame_scrubber.cpp
                               ui/test_slider_search.cpp)
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

    # A CAPTURE RUN MUST IGNORE THE STORED WINDOW LAYOUT.
    #
    # `--screenshot` already refuses to SAVE the layout, so a capture cannot
    # clobber the arrangement a user made. It was restoring one, though, which
    # is the other half of the same idea and was missing: the image then
    # depended on whatever the last interactive session left in
    # `~/.config/MakeHuman/MakeHumanCpp.ini`.
    #
    # MEASURED 2026-09-20: after one interactive run the viewport had grown
    # 1402x1648 -> 1402x1686 and two pixel gates failed on 2,414 of 2,363,772
    # pixels against `--max-differing 0`. CI escaped it only by having no
    # settings file -- luck, not hermeticity.
    #
    # The fixture is produced by Qt itself. A hand-written geometry value
    # cannot gate this: `restoreGeometry` rejects a malformed blob and falls
    # back to the defaults, so the test would pass either way. Only a real
    # `saveGeometry()` blob, for a size that is NOT the default, can tell the
    # two behaviours apart.
    add_test(NAME ui_geometry_fixture COMMAND mh_ui_tests "[geomfixture]")
    set_tests_properties(ui_geometry_fixture PROPERTIES
        ENVIRONMENT "MH_GEOM_FIXTURE_DIR=${CMAKE_CURRENT_BINARY_DIR}/stored_layout"
        FIXTURES_SETUP app_stored_layout)
    # HOME is redirected so this never reads or writes the developer's own
    # preferences -- the same reason the `--skinning` preference tests do it.
    add_test(NAME app_screenshot_ignores_stored_layout COMMAND makehuman
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shot_stored_layout.png
             --screenshot-viewport)
    set_tests_properties(app_screenshot_ignores_stored_layout PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/stored_layout"
        FIXTURES_REQUIRED app_stored_layout)
    # ...and the image must match the one taken with NO stored layout at all.
    # That comparison is the assertion; the run above only produces the file.
    add_test(NAME app_screenshot_plain COMMAND makehuman
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shot_plain.png
             --screenshot-viewport)
    set_tests_properties(app_screenshot_plain PROPERTIES FIXTURES_SETUP app_shot_plain)
    add_test(NAME app_screenshot_layout_independent COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/shot_plain.png
             -DB=${CMAKE_CURRENT_BINARY_DIR}/shot_stored_layout.png
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_screenshot_layout_independent PROPERTIES
        FIXTURES_REQUIRED "app_shot_plain;app_stored_layout"
        DEPENDS "app_screenshot_ignores_stored_layout;app_screenshot_plain")

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
    # false. 14,494 = 13,378 body + 1,020 eyes + 96 teeth, and 13,378 is
    # exactly what the GLB of the same character writes -- the two exports
    # agreeing is the property under test. Teeth are the only proxy worn
    # unasked; they are neither decimated nor subdivided, so that term is a
    # constant in all three of these counts.
    #
    # Was 14,676 while genitals were also default-on. They are not any more
    # (`ProxySlot::defaultChoice`), and the 182 faces of that proxy came out
    # of all three totals. MEASURED after the change, not subtracted.
    add_test(NAME app_smoke_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/app_smoke.obj -DEXPECT=14494
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
            "asset groups: 14 \\[.*\\] \\(skin materials: 9, rigs: 2\\)"
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

    # Loading a document must not change a character the document says nothing
    # about. This one holds a `version` line and NOTHING else -- no modifiers,
    # no pose -- so the character it produces with --pose tpose has to be the
    # character --pose tpose produces on its own.
    #
    # It did not. `applyStack` walked `stack_`, an unordered container, and
    # float addition is not associative, so the bucket order decided the last
    # bit of every vertex; `resetToDefaults()`, which every --load runs, clears
    # and refills the map and need not hand back the same order. 254 of 14,780
    # vertices differed, each by exactly 0.0001 dm -- one unit in the last place
    # of the writer's four decimals, invisible at rest and pushed over the
    # rounding boundary by a pose.
    #
    # The empty document is the point: any difference at all is the bug, with no
    # argument about which of two characters is right.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/emptydoc)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/emptydoc/empty.mhm "version v1.3.0\n")
    add_test(NAME app_empty_document_baseline COMMAND makehuman
             --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/emptydoc/direct.obj)
    set_tests_properties(app_empty_document_baseline PROPERTIES
        FIXTURES_SETUP app_emptydoc_direct)
    add_test(NAME app_empty_document_loaded COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/emptydoc/empty.mhm --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/emptydoc/loaded.obj)
    set_tests_properties(app_empty_document_loaded PROPERTIES
        FIXTURES_SETUP app_emptydoc_loaded)
    add_test(NAME app_empty_document_changes_nothing COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/emptydoc/direct.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/emptydoc/loaded.obj
             -DEXPECT_DIFFERENT=0
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_empty_document_changes_nothing PROPERTIES
        FIXTURES_REQUIRED "app_emptydoc_direct;app_emptydoc_loaded")

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
    # Combination presets. "Six-pack" and "chiselled" are not single controls
    # and cannot be -- they are emergent from Muscle, Weight and Stomach tone
    # pulling together -- so finding one meant knowing all three names.
    add_test(NAME app_list_presets COMMAND makehuman --list-presets)
    set_tests_properties(app_list_presets PROPERTIES
        PASS_REGULAR_EXPRESSION "Six-pack\tmacrodetails-universal/Muscle")
    # An unknown name is refused rather than ignored: a preset that silently
    # did nothing would look exactly like one that worked.
    add_test(NAME app_preset_unknown_refused COMMAND makehuman --preset nope)
    set_tests_properties(app_preset_unknown_refused PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown preset")
    # It has to MOVE THE BODY. Asserting only that the run succeeded passes on
    # a preset that sets nothing at all.
    add_test(NAME app_preset_plain COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/preset_plain.png)
    set_tests_properties(app_preset_plain PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error" FIXTURES_SETUP app_preset_plain)
    add_test(NAME app_preset_sixpack COMMAND makehuman --preset Six-pack
             --render ${CMAKE_CURRENT_BINARY_DIR}/preset_six.png)
    set_tests_properties(app_preset_sixpack PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error" FIXTURES_SETUP app_preset_six)
    # 60,384 pixels MEASURED; the bound is a tenth of that so it gates
    # "the preset reached the body" without pinning a render.
    add_test(NAME app_preset_changes_the_body COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/preset_plain.png
             ${CMAKE_CURRENT_BINARY_DIR}/preset_six.png --min-differing 6000)
    set_tests_properties(app_preset_changes_the_body PROPERTIES
        FIXTURES_REQUIRED "app_preset_plain;app_preset_six")
    # An explicit --set WINS over a preset, which is the documented order and
    # the reason both may be given at once. Read out of the saved document, so
    # the assertion is on the value the application actually kept.
    add_test(NAME app_preset_then_set_write COMMAND makehuman
             --preset Slim --set macrodetails-universal/Weight=0.85
             --save ${CMAKE_CURRENT_BINARY_DIR}/preset_set.mhm)
    set_tests_properties(app_preset_then_set_write PROPERTIES FIXTURES_SETUP app_preset_set)
    add_test(NAME app_preset_set_wins COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/preset_set.mhm
             # Muscle from the preset, Weight from the --set that overrode it.
             "-DKEYS=modifier macrodetails-universal/Muscle 0.400000;modifier macrodetails-universal/Weight 0.850000"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_preset_set_wins PROPERTIES FIXTURES_REQUIRED app_preset_set)

    # Centres of rotation, the third method. It shipped as a library with tests
    # and NO caller for a milestone -- `skinPositionsCor` was reachable only
    # from its own unit test -- so these gates exist to keep it reachable.
    #
    # Same basename in its own directory, for the reason the DQS pair records
    # above: differing `mtllib` lines would make any two exports differ.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/skin_cor)
    add_test(NAME app_skinning_cor COMMAND makehuman
             --rig mixamo_superset --pose tpose --skinning cor
             --export ${CMAKE_CURRENT_BINARY_DIR}/skin_cor/pose.obj)
    set_tests_properties(app_skinning_cor PROPERTIES
        PASS_REGULAR_EXPRESSION "wrote .*pose\\.obj"
        FIXTURES_SETUP app_cor_obj)
    # It must differ from BOTH the others. Against linear alone, a `cor` that
    # quietly fell through to DQS would pass -- and falling through is exactly
    # what the empty-centres path would do if `poseMesh` defaulted instead of
    # refusing.
    add_test(NAME app_skinning_cor_differs_from_linear COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/skin_lbs/pose.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/skin_cor/pose.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_skinning_cor_differs_from_linear PROPERTIES
        FIXTURES_REQUIRED "app_lbs_obj;app_cor_obj")
    add_test(NAME app_skinning_cor_differs_from_dqs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/skin_dqs/pose.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/skin_cor/pose.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_skinning_cor_differs_from_dqs PROPERTIES
        FIXTURES_REQUIRED "app_dqs_obj;app_cor_obj")
    # An unknown method is refused rather than defaulted, and the message now
    # has to name all three.
    add_test(NAME app_skinning_unknown_names_cor COMMAND makehuman --skinning nope)
    set_tests_properties(app_skinning_unknown_names_cor PROPERTIES
        PASS_REGULAR_EXPRESSION "linear, dqs or cor")

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
        # The live-rig warning this once guarded against is GONE (2026-09-14):
        # correctives now travel as shape keys, so the claim it made is false
        # and it was deleted rather than softened. The phrase is kept in the
        # FAIL list below deliberately -- it is now an assertion that the
        # deleted message has not been reintroduced, which costs nothing and
        # catches a revert.
        #
        # An .obj is baked and carries the deformation in its positions, so
        # nothing here should ever claim otherwise.
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
    # DELETE the outputs first, and this is load-bearing rather than tidiness.
    #
    # MEASURED: with the export compaction removed, the app printed "corrective
    # `arm_out` exported as a shape key" and then wrote NO FILE -- the writer
    # rejected the wrong-length deltas. Every check below still passed, because
    # PASS_REGULAR_EXPRESSION makes ctest ignore the exit code and the checks
    # then read the STALE .glb an earlier run had left in the build directory.
    # Announcing a thing and not writing it is the exact failure this group
    # exists to catch, and it had grown a hole in the shape of it.
    #
    # With the files removed first, a failed export leaves nothing and
    # `files_differ` and `file_contains` fail on a file that is not there.
    add_test(NAME app_correctives_live_rig_clean COMMAND ${CMAKE_COMMAND} -E rm -f
             ${CMAKE_CURRENT_BINARY_DIR}/corr_live/pose.glb
             ${CMAKE_CURRENT_BINARY_DIR}/corr_live_off/pose.glb)
    set_tests_properties(app_correctives_live_rig_clean PROPERTIES
        FIXTURES_SETUP app_corr_live_clean)

    # RENAMED from `..._says_so` 2026-09-14. It asserted the warning that said
    # corrective geometry could not travel in a live-rig file. It can now, as a
    # shape key, so the warning is deleted and this pins the replacement: the
    # app must SAY it exported the key, and the two tests below must find it in
    # the file. Announcing without writing is the exact failure the original
    # chunk was built to stop, and it is still the one being guarded.
    add_test(NAME app_correctives_live_rig_exports_the_key COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/nowrinkle.json
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_live/pose.glb)
    set_tests_properties(app_correctives_live_rig_exports_the_key PROPERTIES
        PASS_REGULAR_EXPRESSION "corrective .arm_out. exported as a shape key"
        FIXTURES_REQUIRED app_corr_live_clean
        FIXTURES_SETUP app_corr_live_on)
    add_test(NAME app_correctives_live_rig_baseline COMMAND makehuman
             --rig mixamo_superset --pose tpose
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_live_off/pose.glb)
    set_tests_properties(app_correctives_live_rig_baseline PROPERTIES
        FIXTURES_REQUIRED app_corr_live_clean
        FIXTURES_SETUP app_corr_live_off)
    # INVERTED 2026-09-14, on the explicit instruction the previous version of
    # this test carried: "when this one fails, DELETE the warning -- do not
    # weaken it". It pinned that a live-rig .glb was byte-identical with and
    # without --correctives, which was true and was the LIMITATION the warning
    # described. Correctives now travel as shape keys, so the old assertion
    # pinned a bug and the warning became a lie; both are gone.
    #
    # This is not a weakened test. It is the same property from the other side:
    # the file must now DIFFER, and must name the corrective.
    add_test(NAME app_correctives_live_rig_carries_the_key COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/corr_live_off/pose.glb
             -DB=${CMAKE_CURRENT_BINARY_DIR}/corr_live/pose.glb
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_correctives_live_rig_carries_the_key PROPERTIES
        FIXTURES_REQUIRED "app_corr_live_on;app_corr_live_off")

    # A NEGATIVE weight is still a weight, and this is the only case that says
    # so. An RBF extrapolating past an example pose returns negative
    # coefficients, and the runtime applies them -- so the export has to as
    # well, or the file's baked positions and its shape keys disagree.
    #
    # MEASURED on the shipped `benchmark` pose: `rest` comes out at
    # -0.000112098984, magnitude 1.12e-4, which is a hundred times the 1e-6
    # threshold. Testing the magnitude rather than the value is what keeps it.
    # A plain `< 1e-6` drops it, leaving `arm_out` alone in the stage -- which
    # is exactly what this fails on. Text .usda because the weights are
    # human-readable there; the GLB above covers the binary path.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/corr_neg)
    add_test(NAME app_correctives_negative_weight_clean COMMAND ${CMAKE_COMMAND} -E rm -f
             ${CMAKE_CURRENT_BINARY_DIR}/corr_neg/pose.usda)
    set_tests_properties(app_correctives_negative_weight_clean PROPERTIES
        FIXTURES_SETUP app_corr_neg_clean)
    add_test(NAME app_correctives_negative_weight COMMAND makehuman
             --rig mixamo_superset --pose benchmark
             --correctives ${CMAKE_CURRENT_SOURCE_DIR}/correctives/nowrinkle.json
             --export ${CMAKE_CURRENT_BINARY_DIR}/corr_neg/pose.usda)
    set_tests_properties(app_correctives_negative_weight PROPERTIES
        FIXTURES_REQUIRED app_corr_neg_clean FIXTURES_SETUP app_corr_neg)
    add_test(NAME app_correctives_negative_weight_survives COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/corr_neg/pose.usda
             "-DKEYS=\"rest\";\"arm_out\""
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_correctives_negative_weight_survives PROPERTIES
        FIXTURES_REQUIRED app_corr_neg)

    # ...and the key is NAMED, because a shape key a DCC cannot identify is not
    # much better than no key. glTF puts targetNames in the JSON chunk, which is
    # plain text near the head of the GLB.
    add_test(NAME app_correctives_live_rig_names_the_pose COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/corr_live/pose.glb
             "-DKEYS=arm_out"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_correctives_live_rig_names_the_pose PROPERTIES
        FIXTURES_REQUIRED "app_corr_live_on")

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
             --teeth teeth --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/expl/c.obj)
    set_tests_properties(app_teeth_worn PROPERTIES
        # The MATCAP, not just the name: see the comment on the "wearing" line in
        # main.cpp. Without this a slot handed another slot's matcap passes.
        PASS_REGULAR_EXPRESSION "wearing Teeth \\(.*\\) lit by skinmat_teeth"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_teeth_worn)
    add_test(NAME app_teeth_none COMMAND makehuman
             --teeth none --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/off/c.obj)
    set_tests_properties(app_teeth_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Teeth"
        FIXTURES_SETUP app_teeth_none)
    # SAME BASENAME, different directory. This compared `worn.obj` against
    # `none.obj` and COULD NOT FAIL: an OBJ's `mtllib` line is derived from the
    # output filename, so the two files differed on that line whatever the
    # geometry did. PROVEN by exporting both with `--teeth none` -- identical
    # geometry, 14398 faces each, and the test still passed. It was asserting
    # that two filenames differ.
    add_test(NAME app_teeth_add_geometry COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/teeth/expl/c.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/teeth/off/c.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_teeth_add_geometry PROPERTIES
        FIXTURES_REQUIRED "app_teeth_worn;app_teeth_none")
    # TEETH ARE WORN BY DEFAULT. A character opened toothless and an open mouth
    # was empty, which is wrong about anatomy rather than about configuration:
    # every other helper cage the body ships is opt-in because it is a GARMENT,
    # and teeth are not.
    #
    # All three exports below share the BASENAME `c.obj` and differ only in
    # directory. That is not tidiness: an OBJ's `mtllib` line is derived from
    # the output FILENAME, so `default.obj` and `worn.obj` differ on that line
    # even when every vertex matches -- measured, the two files differed by
    # exactly 3 bytes, `len("default") - len("worn")`. Byte-comparing exports
    # with different names would make the identical-check impossible to pass
    # AND the differ-check impossible to fail, so the differ-check would have
    # passed even if `--teeth none` did nothing at all.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/teeth/dflt)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/teeth/expl)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/teeth/off)
    add_test(NAME app_teeth_default_wears_them COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/dflt/c.obj)
    set_tests_properties(app_teeth_default_wears_them PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Teeth \\(.*\\) lit by skinmat_teeth"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_teeth_default)
    # The printed line proves the slot was filled; this proves the default
    # resolves to THE SAME asset rather than merely to something. A default
    # that silently picked a different .mhclo passes the first and fails this.
    add_test(NAME app_teeth_default_is_the_explicit_one COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/teeth/dflt/c.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/teeth/expl/c.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_teeth_default_is_the_explicit_one PROPERTIES
        FIXTURES_REQUIRED "app_teeth_default;app_teeth_worn")
    # ...and every OTHER slot stays off. Found by mutation: flipping the
    # tongue default to "tongue" left all seventeen teeth tests green, because
    # tongue is default-on in the default export AND the explicit one, so the
    # teeth comparison cannot see it. Nothing anywhere asserted that a garment
    # slot is unworn, and `ProxySlot` now makes flipping one a one-word change.
    #
    # Teeth are anatomy and are worn; Tongue, Hair, Clothes and Eyelashes are
    # choices and are not. This is the assertion that keeps that line where the
    # table says it is.
    # Its OWN directory. The OBJ exporter drops a FIXED-NAME texture sidecar
    # beside the file (`brown_eye.png`, observed in the build tree), so sharing
    # a directory with the app_teeth_default fixture means two processes write
    # that identical path under `ctest -j`.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/teeth/only)
    # A TYPO MUST SAY SO. Silent until now, and harmless while every slot
    # defaulted to none -- the typo produced the default character anyway. With
    # teeth default-on, `--teeth teth` silently ships a body with no teeth and
    # the user gets no cue at all. The warning is proven to fire, and proven
    # NOT to fire for a valid value or for `none`.
    add_test(NAME app_teeth_unknown_warns COMMAND makehuman
             --teeth no_such_proxy
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/only/bogus.obj)
    set_tests_properties(app_teeth_unknown_warns PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --teeth \"no_such_proxy\"; wearing none")
    add_test(NAME app_teeth_valid_does_not_warn COMMAND makehuman
             --teeth teeth
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/only/valid.obj)
    set_tests_properties(app_teeth_valid_does_not_warn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Teeth"
        FAIL_REGULAR_EXPRESSION "unknown --teeth")

    # THE GENITALS SLOT. Anatomy, and it is the FIRST proxy to carry
    # `autoBlendSkin true`, because it is the first one made of skin.
    #
    # NOT worn by default, unlike teeth -- the one place the anatomy-is-
    # default-on rule is deliberately broken (`src/app/main.cpp`,
    # `ProxySlot::defaultChoice`). What a character shows on first launch is a
    # presentation decision, not an anatomical one.
    #
    # So every test below that needs the proxy PRESENT passes `--genitals
    # genitals` explicitly. That is what keeps them testing the slot rather
    # than the default: when the default moved, a test relying on it would
    # have gone quietly vacuous instead of failing.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/genitals)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/genitals/on)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/genitals/off)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/genitals/flagoff)
    add_test(NAME app_genitals_group COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/genitals/g.mhm)
    set_tests_properties(app_genitals_group PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*Genitals")
    # Opting IN, and this export is the "present" reference every deformation
    # test below compares against.
    add_test(NAME app_genitals_worn COMMAND makehuman
             --genitals genitals --export ${CMAKE_CURRENT_BINARY_DIR}/genitals/on/c.obj)
    set_tests_properties(app_genitals_worn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Genitals \\(.*\\) lit by"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_genitals_on)
    # NOT worn by default. The PASS pattern is on TEETH, not on the absence:
    # without it this would pass just as well on an export that wore nothing
    # at all, or that failed before it got as far as choosing proxies.
    add_test(NAME app_genitals_default_omits_them COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/genitals/off/c.obj)
    set_tests_properties(app_genitals_default_omits_them PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Teeth"
        FAIL_REGULAR_EXPRESSION "wearing Genitals"
        FIXTURES_SETUP app_genitals_off)
    # `--genitals none` still parses and still means none. Redundant with the
    # default now, and kept anyway: the flag is public and a default can move.
    add_test(NAME app_genitals_none COMMAND makehuman
             --genitals none --export ${CMAKE_CURRENT_BINARY_DIR}/genitals/flagoff/c.obj)
    set_tests_properties(app_genitals_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Genitals")
    # Same basename in different directories -- an OBJ's `mtllib` line is
    # derived from the output FILENAME, so comparing `dflt.obj` with `off.obj`
    # would differ on that line whatever the geometry did.
    add_test(NAME app_genitals_optin_adds_geometry COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/genitals/on/c.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/genitals/off/c.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_genitals_optin_adds_geometry PROPERTIES
        FIXTURES_REQUIRED "app_genitals_on;app_genitals_off")
    # WHAT DRIVES THE CAGE, measured rather than assumed -- and the first
    # version of this comment was WRONG.
    #
    # All 200 vertices take their LARGEST weight from `spine05`, which is true
    # and which I first read as "rigid, rides the pelvis whole". It is not the
    # same claim. Measured in default_weights.mhw, **97 of the 200 carry a
    # SECOND bone**: `pelvis.L`/`pelvis.R` on 42 each and
    # `upperleg01.L`/`upperleg01.R` on 59 each. The cage deforms with the hips.
    #
    # The error surfaced because `app_body_pose_unit_moves_the_leg` moved 59
    # more vertices than before and 59 is not 200 -- a number that did not fit
    # the story. Three poses pin the real behaviour:
    #   T-pose            -> all 200 move (the whole pelvis goes with the body)
    #   torso bend        -> 0 move (the torso bends ABOVE the pelvis)
    #   one leg forward   -> exactly 59 move, the count weighted to upperleg01.L
    # The last is the discriminating one: a cage genuinely rigid on one bone
    # would move 0 there, and the earlier two tests would not have noticed.
    #
    # RISE_TOLERANCE=ANY because this is not a jaw drop: the helper defaults to
    # requiring downward motion and the pelvis does not go down in a T-pose.
    # No separate "rest" export: `app_genitals_worn` above already runs the
    # identical command apart from the pose, and its output is the rest pose.
    add_test(NAME app_genitals_tposed COMMAND makehuman --genitals genitals
             --pose tpose --export ${CMAKE_CURRENT_BINARY_DIR}/genitals/tpose.obj)
    set_tests_properties(app_genitals_tposed PROPERTIES FIXTURES_SETUP app_genitals_tposed)
    add_test(NAME app_genitals_bent COMMAND makehuman --genitals genitals
             --pose-unit TorsoLeft=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/genitals/bend.obj)
    set_tests_properties(app_genitals_bent PROPERTIES FIXTURES_SETUP app_genitals_bent)
    add_test(NAME app_genitals_ride_the_pelvis COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/genitals/on/c.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/genitals/tpose.obj
             -DGROUP=genitals -DEXPECT_MOVED=200 -DEXPECT_STILL=0 -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_genitals_ride_the_pelvis PROPERTIES
        FIXTURES_REQUIRED "app_genitals_on;app_genitals_tposed")
    add_test(NAME app_genitals_ignore_a_torso_bend COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/genitals/on/c.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/genitals/bend.obj
             -DGROUP=genitals -DEXPECT_MOVED=0 -DEXPECT_STILL=200 -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_genitals_ignore_a_torso_bend PROPERTIES
        FIXTURES_REQUIRED "app_genitals_on;app_genitals_bent")
    add_test(NAME app_genitals_leg_carries_59 COMMAND makehuman --genitals genitals
             --pose-unit UpperLegForwardLeft=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/genitals/leg.obj)
    set_tests_properties(app_genitals_leg_carries_59 PROPERTIES
        FIXTURES_SETUP app_genitals_leg)
    add_test(NAME app_genitals_hip_deforms_the_cage COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/genitals/on/c.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/genitals/leg.obj
             -DGROUP=genitals -DEXPECT_MOVED=59 -DEXPECT_STILL=141 -DRISE_TOLERANCE=ANY
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_group_moved.cmake)
    set_tests_properties(app_genitals_hip_deforms_the_cage PROPERTIES
        FIXTURES_REQUIRED "app_genitals_on;app_genitals_leg")
    # THE SKIN TONE FOLLOWS THE BODY, and this is the first proxy for which that
    # is true. `autoBlendSkin` parsed and exported for the whole project's life
    # and was applied ONLY to the body, so a genital proxy would have kept a
    # fixed caucasian-derived matcap while the body darkened -- RENDERED at
    # African=1.0 it was a pale patch at the crotch. The printed line is the one
    # place the app's own choice is observable, and teeth must NOT claim it.
    add_test(NAME app_genitals_blend_to_skin_tone COMMAND makehuman --genitals genitals
             --export ${CMAKE_CURRENT_BINARY_DIR}/genitals/blend.obj)
    set_tests_properties(app_genitals_blend_to_skin_tone PROPERTIES
        # Both patterns are anchored to the MATCAP STEM, which is unique per
        # slot and on the same line. `wearing Teeth .* blended` looked right
        # and matched ACROSS THE NEWLINE onto the genitals line, failing on
        # correct output -- ctest's `.` spans lines here.
        PASS_REGULAR_EXPRESSION "skinmat_genitals blended to the skin tone"
        FAIL_REGULAR_EXPRESSION "skinmat_teeth blended to the skin tone")
    # ...and the PIXELS follow, which the printed line above cannot show.
    #
    # The same character at two ethnicities, so the sampled box has the SAME
    # surface normals in both -- a matcap shades by normal, so comparing two
    # different body parts would measure the normals instead of the tone.
    # MEASURED at (512,508) r6, the crotch: response 54.2 with the blend
    # reaching worn proxies and 28.1 without it (the residue is body pixels
    # inside the box and the lighting, not the proxy following). The threshold
    # sits between them.
    add_test(NAME app_genitals_render_caucasian COMMAND makehuman --genitals genitals
             --render ${CMAKE_CURRENT_BINARY_DIR}/genitals/cauc.png)
    set_tests_properties(app_genitals_render_caucasian PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_genitals_render_cauc)
    add_test(NAME app_genitals_render_african COMMAND makehuman --genitals genitals
             --set macrodetails/African=1.0
             --render ${CMAKE_CURRENT_BINARY_DIR}/genitals/afr.png)
    set_tests_properties(app_genitals_render_african PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_genitals_render_afr)
    # `.venv-mh` when it exists, because the wheels this needs live there and
    # NOT in the system interpreter -- pointing at `python3` made the check
    # skip on the developer machine too, which is the one place it can run.
    set(MH_PIL_PYTHON python3)
    if(EXISTS ${CMAKE_SOURCE_DIR}/.venv-mh/bin/python)
        set(MH_PIL_PYTHON ${CMAKE_SOURCE_DIR}/.venv-mh/bin/python)
    endif()
    # THE OWNER'S QUESTION, ANSWERED IN PIXELS: with the default character the
    # crotch shows nothing, and turning the slot on visibly changes it.
    #
    # `app_genitals_default_omits_them` already proves the GEOMETRY is absent,
    # which is the stronger claim about the file. This is here because absent
    # geometry and an unchanged PICTURE are different failures, and this
    # project has shipped four visual bugs past a green suite. If the default
    # ever silently starts wearing the proxy again, the two renders converge
    # and this response collapses toward zero.
    #
    # MEASURED 7.5 at (512,508) r6 between the default render and the same
    # character with `--genitals genitals`. The bar is 5.0. It is a small
    # number because the probe sits at the TOP of the region where the two
    # surfaces still nearly agree -- the eye sees a far larger difference than
    # 7.5 suggests, and the box is deliberately not moved to flatter ground.
    # If a render change moves it, RE-MEASURE rather than widening the bound.
    add_test(NAME app_genitals_render_default COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/genitals/dflt.png)
    set_tests_properties(app_genitals_render_default PROPERTIES
        SKIP_REGULAR_EXPRESSION "cannot render"
        FAIL_REGULAR_EXPRESSION "wearing Genitals"
        FIXTURES_SETUP app_genitals_render_dflt)
    add_test(NAME app_genitals_hidden_by_default_in_pixels COMMAND ${MH_PIL_PYTHON}
             ${CMAKE_SOURCE_DIR}/tools/png_region_response.py
             --a ${CMAKE_CURRENT_BINARY_DIR}/genitals/dflt.png
             --b ${CMAKE_CURRENT_BINARY_DIR}/genitals/cauc.png
             --cx 512 --cy 508 --radius 6 --min-response 5)
    set_tests_properties(app_genitals_hidden_by_default_in_pixels PROPERTIES
        # Skips in CI for the reason the ethnicity check below gives: the
        # Pillow/NumPy wheels are installed for `inventories`, not for the
        # jobs that run ctest. Said out loud rather than left looking green.
        SKIP_RETURN_CODE 77
        FIXTURES_REQUIRED "app_genitals_render_dflt;app_genitals_render_cauc")
    add_test(NAME app_genitals_tone_follows_ethnicity COMMAND ${MH_PIL_PYTHON}
             ${CMAKE_SOURCE_DIR}/tools/png_region_response.py
             --a ${CMAKE_CURRENT_BINARY_DIR}/genitals/cauc.png
             --b ${CMAKE_CURRENT_BINARY_DIR}/genitals/afr.png
             --cx 512 --cy 508 --radius 6 --min-response 40)
    set_tests_properties(app_genitals_tone_follows_ethnicity PROPERTIES
        # 77 = Pillow/NumPy absent, or the render was skipped. Those wheels are
        # installed for the `inventories` CI job, not for the macOS build+test
        # jobs that run ctest, so this SKIPS in CI and runs locally. Said out
        # loud rather than left to look green everywhere.
        SKIP_RETURN_CODE 77
        FIXTURES_REQUIRED "app_genitals_render_cauc;app_genitals_render_afr")
    # Named `..._are_teeth_only` until genitals shipped, at which point the NAME
    # was false while the test still passed -- it never asserted anything about
    # genitals either way. Renamed and widened rather than left reading as a
    # claim it was not making. PASS_REGULAR_EXPRESSION takes ONE pattern, so the
    # second anatomy slot needs its own entry.
    add_test(NAME app_proxy_defaults_are_anatomy_only COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/only/c.obj)
    set_tests_properties(app_proxy_defaults_are_anatomy_only PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Teeth"
        FAIL_REGULAR_EXPRESSION "wearing Tongue;wearing Hair;wearing Clothes;wearing Eyelashes")
    # Genitals moved OUT of the defaults. Teeth are still the whole of what a
    # character wears unasked, which is what the PASS pattern pins -- without
    # it this would also pass on an export that wore nothing at all.
    add_test(NAME app_proxy_defaults_exclude_genitals COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/only/c2.obj)
    set_tests_properties(app_proxy_defaults_exclude_genitals PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Teeth"
        FAIL_REGULAR_EXPRESSION "wearing Genitals;wearing Tongue;wearing Hair;wearing Clothes;wearing Eyelashes")
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
    # THE NEGATIVE ROUND TRIP, which is the one that matters now.
    #
    # `app_teeth_reload_wears_them` above went VACUOUS the moment teeth became
    # the default: it passes whether or not the `teeth` line is read at all.
    # This is the pair that still has teeth in it.
    #
    # "Nothing worn" used to be recorded as the ABSENCE of a line, which
    # conflated "the user took this off" with "this file predates the slot".
    # Harmless while every slot defaulted to none; the moment one defaulted to
    # WORN, a character saved deliberately toothless reopened with teeth.
    # MEASURED before the fix: `--teeth none --save` wrote no teeth line and
    # the reload printed "wearing Teeth (136 verts)".
    add_test(NAME app_teeth_none_saved COMMAND makehuman
             --teeth none --save ${CMAKE_CURRENT_BINARY_DIR}/teeth/saved_none.mhm)
    set_tests_properties(app_teeth_none_saved PROPERTIES FIXTURES_SETUP app_teeth_none_saved)
    # The sentinel is written, not merely implied by silence.
    add_test(NAME app_teeth_none_save_records_it COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/teeth/saved_none.mhm
             "-DKEYS=teeth none"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_teeth_none_save_records_it PROPERTIES
        FIXTURES_REQUIRED app_teeth_none_saved)
    add_test(NAME app_teeth_none_reload_stays_none COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/teeth/saved_none.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/reloaded_none.obj)
    set_tests_properties(app_teeth_none_reload_stays_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing Teeth"
        FIXTURES_REQUIRED app_teeth_none_saved)
    # The SAME defect, for eyes, and it predates the teeth slot entirely --
    # `--eyes none --save` then reload wore eyes again, while the docstring on
    # `proxyFromDocument` already claimed that case was fixed. Only the by-UUID
    # half was. Gated here so the sentinel cannot be removed for one slot.
    add_test(NAME app_eyes_none_saved COMMAND makehuman
             --eyes none --save ${CMAKE_CURRENT_BINARY_DIR}/teeth/eyes_none.mhm)
    set_tests_properties(app_eyes_none_saved PROPERTIES FIXTURES_SETUP app_eyes_none_saved)
    add_test(NAME app_eyes_none_reload_stays_none COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/teeth/eyes_none.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/teeth/eyes_none.obj)
    set_tests_properties(app_eyes_none_reload_stays_none PROPERTIES
        FAIL_REGULAR_EXPRESSION "wearing HighPolyEyes"
        FIXTURES_REQUIRED app_eyes_none_saved)

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
    # The afro. `--hair afro` must actually WEAR it: a vertex referenced by no
    # face makes loadObj reject the proxy and the character renders BALD rather
    # than erroring (ObjReader.cpp:235-243), which no count check would catch.
    add_test(NAME app_afro_worn COMMAND makehuman
             --hair afro --export ${CMAKE_CURRENT_BINARY_DIR}/hair/afro.obj)
    set_tests_properties(app_afro_worn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Afro \\(475 verts\\) lit by skinmat_hair"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_afro_worn)
    # ...and it must be a DIFFERENT asset from the placeholder cage, not the
    # same geometry under a new name.
    add_test(NAME app_afro_differs_from_cage COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/hair/afro.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/hair/worn.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_afro_differs_from_cage PROPERTIES
        FIXTURES_REQUIRED "app_afro_worn;app_hair_worn")

    # "Generated" is a claim LICENSING.md makes about data/hair/afro.*, and
    # without this it is an aspiration: a hand-edit would go unnoticed for ever.
    # Proven to bite -- appending one comment line to afro.obj fails it.
    #
    # GUARDED on the interpreter rather than requiring one. No other test needs
    # Python, the end state ships none, and the generator uses only the standard
    # library -- so this runs where an interpreter happens to exist and is
    # simply absent where it does not, instead of turning a missing tool into a
    # red suite.
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        # `--app` rather than letting the generator guess: cornrows are routed
        # and bound BY the application, and its own default points at the debug
        # tree, so under any other preset the check would have run against a
        # binary that is not the one being tested -- or against none at all.
        add_test(NAME hair_assets_are_generated
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_SOURCE_DIR}/tools/make_hair_styles.py --check
                         --app $<TARGET_FILE:makehuman>)
    endif()

    # CORNROWS -- the first style whose geometry is not in the base mesh at all.
    # It is bound to the scalp by three base vertices and barycentric weights
    # (`mh::core::bindToSurface`), so unlike every earlier proxy it can place a
    # vertex nobody authored if a weight escapes 0..1; `fitProxy` does no
    # clamping. `tests/regression/test_cornrows_shape.cpp` is where the SHAPE is
    # gated -- these two only establish that the app wears it and that it is its
    # own asset.
    add_test(NAME app_cornrows_worn COMMAND makehuman
             --hair cornrows --export ${CMAKE_CURRENT_BINARY_DIR}/hair/cornrows.obj)
    set_tests_properties(app_cornrows_worn PROPERTIES
        PASS_REGULAR_EXPRESSION "wearing Cornrows \\(2148 verts\\) lit by skinmat_hair"
        FAIL_REGULAR_EXPRESSION "export failed;cannot"
        FIXTURES_SETUP app_cornrows_worn)
    add_test(NAME app_cornrows_differ_from_afro COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/hair/cornrows.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/hair/afro.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_differ.cmake)
    set_tests_properties(app_cornrows_differ_from_afro PROPERTIES
        FIXTURES_REQUIRED "app_cornrows_worn;app_afro_worn")

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

    # WHICH FORMATS ACTUALLY CARRY BLENDSHAPES. `kMorphCapable` listed `.dae`
    # and it does not belong: MEASURED by exporting the 34 expression units and
    # counting markers in the bytes -- .glb 34 targets, .fbx 35 `BlendShape`
    # records, .usda 38 `blendShape`, and **.dae ZERO**. assimp takes the
    # aiAnimMeshes and its Collada writer drops them, so the file has one
    # `<skin>` controller and no `<morph>`.
    #
    # A user was therefore told "34 blendshapes" and handed a .dae with none --
    # the same hole the reference has from the other side, where its Collada
    # morph controller is dead code behind a NameError (project_context.md 8).
    # This is the gate for that claim, in both directions.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/morphcap)
    add_test(NAME app_dae_says_it_drops_blendshapes COMMAND makehuman --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/morphcap/m.dae)
    set_tests_properties(app_dae_says_it_drops_blendshapes PROPERTIES
        PASS_REGULAR_EXPRESSION "dae carries no blendshapes")
    # What a .glb is actually made of, measured rather than asserted from
    # memory. A figure -- "74.5% of the GLB is PNG (1,749,562 of 2,348,760
    # bytes)" -- was carried in memory for two days as the whole justification
    # for adopting KTX2, and NOTHING in the tree reproduced it: the export
    # fixture writes its GLB with no material, so it has no textures at all.
    #
    # Re-measured with mh_glb_stat: the image bytes are 1,749,562 EXACTLY, as
    # recorded. The denominator had drifted (the geometry grew), so the share
    # is 53.0% here and 73.4% with --draco, against the 74.5% written down.
    # `--skin-material` matters: the DEFAULT skin names no texture at all, so a
    # plain export carries one image (the eye) and reads 28.3%.
    #
    # Draco is deliberately NOT used here. CI installs only ninja/assimp/qt, so
    # draco_FOUND is false there and a --draco fixture would never run --
    # the same blind spot test_draco.cpp already sits in.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/glbstat)
    add_test(NAME app_glb_textured COMMAND makehuman
             --skin-material african_deep
             --export ${CMAKE_CURRENT_BINARY_DIR}/glbstat/textured.glb)
    set_tests_properties(app_glb_textured PROPERTIES FIXTURES_SETUP app_glb_textured)

    # BOTH bounds, for two different reasons.
    #
    # The FLOOR carries the claim: textures really are most of this file, which
    # is the whole premise for compressing them. It is also the non-vacuous
    # direction -- `--max-image-share` alone passes on a GLB with NO images at
    # all, since 0% is under any ceiling, the trap obj_verts_differ.cmake
    # documents as "a compare with no expectation passes on anything".
    #
    # The CEILING is here for coverage, and that is the honest reason rather
    # than a regression anyone has seen: it is the only thing exercising the
    # `--max-image-share` branch, and a mutation inverting that comparison is
    # killed by this test and nothing else. The KTX2 chunk will then have a
    # tested branch waiting to assert the share DROPPED.
    #
    # When KTX2 moves these numbers they must be RE-MEASURED with the reason
    # written down, the way the five vertex counts were, not nudged to pass.
    add_test(NAME app_glb_textures_are_most_of_the_file COMMAND mh_glb_stat
             ${CMAKE_CURRENT_BINARY_DIR}/glbstat/textured.glb --min-image-share 45)
    set_tests_properties(app_glb_textures_are_most_of_the_file PROPERTIES
        FIXTURES_REQUIRED app_glb_textured)
    add_test(NAME app_glb_textures_are_not_all_of_it COMMAND mh_glb_stat
             ${CMAKE_CURRENT_BINARY_DIR}/glbstat/textured.glb --max-image-share 65)
    set_tests_properties(app_glb_textures_are_not_all_of_it PROPERTIES
        FIXTURES_REQUIRED app_glb_textured)

    # The tool refuses a bound it cannot compare against.
    #
    # Found by reading its own doc comment back: strict parsing is there because
    # "a FLOOR of zero is a gate that cannot fail", and `strtod` accepts "nan",
    # for which BOTH `bound < 0.0` and `bound > 100.0` are false -- so it passed
    # validation, and `share < NaN` is false, so the gate passed vacuously. The
    # exact failure the parsing exists to prevent, inside the tool that exists
    # to prevent it. `inf` and `-inf` were already rejected; only NaN slipped.
    #
    # The fixture is REQUIRED, not decoration: the file is opened before the
    # bound is parsed, so without a real GLB this would exit non-zero for a
    # missing file and pass for the wrong reason.
    add_test(NAME app_glb_stat_refuses_a_bound_it_cannot_compare COMMAND mh_glb_stat
             ${CMAKE_CURRENT_BINARY_DIR}/glbstat/textured.glb --min-image-share nan)
    set_tests_properties(app_glb_stat_refuses_a_bound_it_cannot_compare PROPERTIES
        FIXTURES_REQUIRED app_glb_textured WILL_FAIL TRUE)

    # The other way to write a gate that cannot fail: a ceiling above 100, which
    # no share can ever exceed. Already rejected -- this pins it, so narrowing
    # the range check to just the NaN case does not pass unnoticed.
    add_test(NAME app_glb_stat_refuses_an_impossible_ceiling COMMAND mh_glb_stat
             ${CMAKE_CURRENT_BINARY_DIR}/glbstat/textured.glb --max-image-share 150)
    set_tests_properties(app_glb_stat_refuses_an_impossible_ceiling PROPERTIES
        FIXTURES_REQUIRED app_glb_textured WILL_FAIL TRUE)

    # And the third: a negative floor, which every share clears. Three tests
    # rather than one because each kills a DIFFERENT narrowing of the range
    # check -- dropping the upper half, the lower half, or the NaN-safe
    # spelling -- and a mutation that survives is a branch nothing tests.
    add_test(NAME app_glb_stat_refuses_a_negative_floor COMMAND mh_glb_stat
             ${CMAKE_CURRENT_BINARY_DIR}/glbstat/textured.glb --min-image-share -5)
    set_tests_properties(app_glb_stat_refuses_a_negative_floor PROPERTIES
        FIXTURES_REQUIRED app_glb_textured WILL_FAIL TRUE)

    # And a bound with trailing garbage, which `strtod` alone would silently
    # read as 45 and act on. Not the unfalsifiable class -- 45 can still fail --
    # but it is a typo acted on instead of reported, which is what the strict
    # parsing was for. Pinned because a mutation dropping the check survived
    # everything else.
    add_test(NAME app_glb_stat_refuses_a_bound_with_trailing_garbage COMMAND mh_glb_stat
             ${CMAKE_CURRENT_BINARY_DIR}/glbstat/textured.glb --min-image-share 45abc)
    set_tests_properties(app_glb_stat_refuses_a_bound_with_trailing_garbage PROPERTIES
        FIXTURES_REQUIRED app_glb_textured WILL_FAIL TRUE)

    add_test(NAME app_glb_keeps_blendshapes_quietly COMMAND makehuman --blendshapes
             --export ${CMAKE_CURRENT_BINARY_DIR}/morphcap/m.glb)
    set_tests_properties(app_glb_keeps_blendshapes_quietly PROPERTIES
        FAIL_REGULAR_EXPRESSION "carries no blendshapes")

    # The shipped expressions must still match what the FACS table composes.
    # `make_faceunits.py --check` got a gate and this did not, so
    # `data/expressions/*.mhpose` could drift from the EXPRESSIONS table with
    # nothing noticing. It needs the built application -- the AU-to-pose-unit
    # mapping lives there -- so it belongs inside this block, unlike the
    # faceunits check which needs only Python.
    if(Python3_Interpreter_FOUND)
        add_test(NAME expressions_are_generated COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tools/make_expressions.py --check
                 --app $<TARGET_FILE:makehuman>)
    endif()

    # The face units must actually MOVE THE FACE, not merely be accepted. They
    # are bundled as ordinary targets, so `--set faceunits/<name>` reaches them
    # with no `--custom-targets` -- which is the whole point of bundling.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/faceunits)
    add_test(NAME app_faceunit_jaw COMMAND makehuman --set faceunits/jawOpen=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/faceunits/jaw.obj)
    set_tests_properties(app_faceunit_jaw PROPERTIES
        FAIL_REGULAR_EXPRESSION "no such modifier"
        FIXTURES_SETUP app_faceunit_jaw)
    add_test(NAME app_faceunit_plain COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/faceunits/plain.obj)
    set_tests_properties(app_faceunit_plain PROPERTIES FIXTURES_SETUP app_faceunit_plain)
    # An EXACT count, not a minimum: 2,317 of 14,780 vertices, measured. The
    # script is built for exactness and a threshold would pass on a morph that
    # had quietly shrunk to nothing much.
    add_test(NAME app_faceunit_moves_the_face COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/faceunits/jaw.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/faceunits/plain.obj
             -DEXPECT_DIFFERENT=2317
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_faceunit_moves_the_face PROPERTIES
        FIXTURES_REQUIRED "app_faceunit_jaw;app_faceunit_plain")

    # ...and a name that is not one of the 52 is REFUSED, so a typo cannot read
    # as a face unit that happens to do nothing.
    add_test(NAME app_faceunit_unknown_refused COMMAND makehuman
             --set faceunits/noSuchUnit=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/faceunits/never.obj)
    set_tests_properties(app_faceunit_unknown_refused PROPERTIES
        PASS_REGULAR_EXPRESSION "no such modifier")

    # HELP -- About and Credits, the whole of the reference's help tab that this
    # port can honestly supply (`legacy/python/plugins/6_help.py`). The window
    # had no Help menu and no licence surface anywhere, which for an AGPL
    # application is nearer a compliance gap than missing chrome.
    #
    # THE POINT OF THESE IS ANTI-DRIFT. Asserting the text "contains 2.0.0"
    # would be satisfied by a hardcoded string and would rot silently at the
    # next release, so the expected version is `${MH_VERSION}` -- the value
    # CMake read from `/VERSION`, the same single source `audit_version.py`
    # holds everything else to.
    add_test(NAME app_about_states_the_version COMMAND makehuman --about)
    set_tests_properties(app_about_states_the_version PROPERTIES
        PASS_REGULAR_EXPRESSION "MakeHuman ${MH_VERSION}")

    # ...and the two licences, which are DIFFERENT and whose difference is the
    # commercially consequential fact: naming only the AGPL tells a user the
    # opposite of the truth about what they may sell.
    add_test(NAME app_about_names_both_licences COMMAND makehuman --about)
    set_tests_properties(app_about_names_both_licences PROPERTIES
        PASS_REGULAR_EXPRESSION "Affero")
    add_test(NAME app_about_names_the_asset_licence COMMAND makehuman --about)
    set_tests_properties(app_about_names_the_asset_licence PROPERTIES
        PASS_REGULAR_EXPRESSION "CC0 1.0")

    # The licence surface must be reachable when NOTHING ELSE IS. Placed after
    # the asset-tree check at first, which meant a damaged or mis-pointed data/
    # answered --about with "cannot find the asset tree" and exit 1.
    add_test(NAME app_about_survives_a_missing_asset_tree COMMAND makehuman --about)
    set_tests_properties(app_about_survives_a_missing_asset_tree PROPERTIES
        ENVIRONMENT "MH_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/no-such-assets"
        PASS_REGULAR_EXPRESSION "MakeHuman ${MH_VERSION}"
        FAIL_REGULAR_EXPRESSION "cannot find the asset tree")

    add_test(NAME app_credits_name_the_upstream_project COMMAND makehuman --credits)
    set_tests_properties(app_credits_name_the_upstream_project PROPERTIES
        PASS_REGULAR_EXPRESSION "port of MakeHuman")

    # THE VIEWPORT BACKDROP -- a reference photograph to model to, which is what
    # the reference's BackgroundChooser is for (0_modeling_background.py:104).
    # Until now `--background` reached only `--render`, compositing a backdrop
    # into a finished frame; the viewport, the thing you actually model in, had
    # none.
    #
    # The fixture image is GENERATED here rather than committed: it has to be a
    # deliberately wrong aspect ratio (2:1 into a portrait viewport) so that
    # cover-fit is exercised rather than bypassed, and a committed PNG would
    # also be a second thing to keep in step with the test that reads it.
    add_test(NAME app_backdrop_fixture COMMAND ${CMAKE_COMMAND}
             -DOUT=${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             -P ${CMAKE_CURRENT_SOURCE_DIR}/make_backdrop_fixture.cmake)
    set_tests_properties(app_backdrop_fixture PROPERTIES FIXTURES_SETUP app_backdrop_fixture)

    # A save must RECORD where the backdrop sits, or the drag is lost the moment
    # the file is closed.
    add_test(NAME app_background_saves_the_placement COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side left --background-transform 0.3,0.1,2
             --save ${CMAKE_CURRENT_BINARY_DIR}/bg_placed.mhm)
    set_tests_properties(app_background_saves_the_placement PROPERTIES
        FIXTURES_REQUIRED app_backdrop_fixture
        FIXTURES_SETUP app_bg_placed)
    add_test(NAME app_background_placement_is_in_the_file COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/bg_placed.mhm
             "-DPATTERN=^background[ ]left[ ].*[ ]0\\.3[ ]0\\.1[ ]2$"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_matches.cmake)
    set_tests_properties(app_background_placement_is_in_the_file PROPERTIES
        FIXTURES_REQUIRED app_bg_placed)

    # ...and the lines this port does NOT own survive it. `recordLine` would
    # have collapsed all of these into one; `recordBackgrounds` owns them by
    # SIDE. The `other` line's filename contains a space, so this also proves
    # the tail-parsing end to end rather than only in the unit test.
    add_test(NAME app_background_rt_fixture COMMAND ${CMAKE_COMMAND}
             -DOUT=${CMAKE_CURRENT_BINARY_DIR}/bg_rt_in.mhm
             -P ${CMAKE_CURRENT_SOURCE_DIR}/make_background_mhm_fixture.cmake)
    set_tests_properties(app_background_rt_fixture PROPERTIES FIXTURES_SETUP app_bg_rt_fixture)
    add_test(NAME app_background_round_trip COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/bg_rt_in.mhm
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side front --background-transform 0.4,0,3
             --save ${CMAKE_CURRENT_BINARY_DIR}/bg_rt_out.mhm)
    set_tests_properties(app_background_round_trip PROPERTIES
        FIXTURES_REQUIRED "app_bg_rt_fixture;app_backdrop_fixture"
        FIXTURES_SETUP app_bg_rt)
    add_test(NAME app_background_round_trip_keeps_other COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/bg_rt_out.mhm
             "-DPATTERN=^background[ ]other[ ]some[ ]photo\\.png[ ]1\\.5[ ]0\\.2[ ]0\\.3[ ]1\\.5$"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_matches.cmake)
    set_tests_properties(app_background_round_trip_keeps_other PROPERTIES
        FIXTURES_REQUIRED app_bg_rt)
    add_test(NAME app_background_round_trip_keeps_enabled COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/bg_rt_out.mhm
             "-DPATTERN=^background[ ]enabled[ ]True$"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_matches.cmake)
    set_tests_properties(app_background_round_trip_keeps_enabled PROPERTIES
        FIXTURES_REQUIRED app_bg_rt)
    # Sides this port OWNS but this run did not place must SURVIVE. They did not:
    # `recordBackgrounds` replaces the whole owned set, so handing it only the
    # placed side deleted every other one. Measured on a real file, then fixed
    # by merging what the document already carries.
    add_test(NAME app_background_round_trip_keeps_unplaced_sides COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/bg_rt_out.mhm
             "-DPATTERN=^background[ ]left[ ]leftphoto\\.png[ ]1[ ]0\\.1[ ]0\\.2[ ]1\\.5$"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_matches.cmake)
    set_tests_properties(app_background_round_trip_keeps_unplaced_sides PROPERTIES
        FIXTURES_REQUIRED app_bg_rt)
    add_test(NAME app_background_round_trip_keeps_unplaced_top COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/bg_rt_out.mhm
             "-DPATTERN=^background[ ]top[ ]topphoto\\.png[ ]1[ ]0\\.3[ ]0\\.4[ ]2$"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_matches.cmake)
    set_tests_properties(app_background_round_trip_keeps_unplaced_top PROPERTIES
        FIXTURES_REQUIRED app_bg_rt)

    # The owned side is REPLACED, not duplicated: `stale.png` must be gone.
    add_test(NAME app_background_round_trip_replaces_the_owned_side COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/bg_rt_out.mhm
             "-DPATTERN=stale\\.png"
             -DABSENT=1
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_matches.cmake)
    set_tests_properties(app_background_round_trip_replaces_the_owned_side PROPERTIES
        FIXTURES_REQUIRED app_bg_rt)

    # A backdrop placed, saved and REOPENED must come back where it was left.
    # Until this landed the port read `background` lines only to write them
    # again, so a reopened document showed no photograph at all.
    #
    # Gated on PIXELS, not on the line being in the file: the file content is
    # already covered above, and a stored value that never reaches the frame is
    # the failure this project keeps finding.
    add_test(NAME app_backdrop_reopen_save COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side front --background-transform 0.3,0.1,2
             --save ${CMAKE_CURRENT_BINARY_DIR}/reopen.mhm)
    set_tests_properties(app_backdrop_reopen_save PROPERTIES
        FIXTURES_REQUIRED app_backdrop_fixture
        FIXTURES_SETUP app_backdrop_reopen_save)
    # NOTE: no `--background` here. The image and its framing come from the
    # document or they do not come at all.
    add_test(NAME app_backdrop_reopened COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/reopen.mhm --view front
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/backdrop_reopened.png --screenshot-viewport)
    set_tests_properties(app_backdrop_reopened PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FAIL_REGULAR_EXPRESSION "covered 0 of;could not write;warning: cannot read backdrop"
        FIXTURES_REQUIRED app_backdrop_reopen_save
        FIXTURES_SETUP app_backdrop_reopened)
    # MEASURED: the reopened frame is BYTE-IDENTICAL to one rendered with the
    # same transform given explicitly -- 0 of 2,310,496 pixels differ -- and
    # differs from the UNtransformed one by 1,692,837. So `--max-differing 0`
    # is not a tight-fitting number here, it is the actual claim: the framing
    # came back exactly.
    add_test(NAME app_backdrop_reopen_restores_the_framing COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_reopened.png
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_tf.png --max-differing 0)
    set_tests_properties(app_backdrop_reopen_restores_the_framing PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_reopened;app_backdrop_transformed")
    # ...and it is NOT merely the plain cover fit: against the untransformed
    # backdrop it must differ substantially. Bound 50,000, the same reasoning
    # as the sibling gate -- 34x under the local 1,692,837 and ~13x clear of
    # CI's smaller render.
    add_test(NAME app_backdrop_reopen_is_not_the_plain_fit COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_reopened.png
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_front.png --min-differing 50000)
    set_tests_properties(app_backdrop_reopen_is_not_the_plain_fit PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_reopened;app_backdrop_front")

    add_test(NAME app_backdrop_front COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side front --view front
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/backdrop_front.png --screenshot-viewport)
    set_tests_properties(app_backdrop_front PROPERTIES
        PASS_REGULAR_EXPRESSION "backdrop: .* behind the front view"
        # The pass-regex alone would DISCARD the exit code, and that line is
        # printed long before the screenshot is taken -- so a blank frame or an
        # unwritable PNG would still set this fixture and the three gates below
        # would compare a stale file from an earlier run.
        FAIL_REGULAR_EXPRESSION "covered 0 of;could not write"
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_REQUIRED app_backdrop_fixture
        FIXTURES_SETUP app_backdrop_front)
    # --background-transform must reach the PIXELS, not merely be parsed. Without
    # this gate the viewport could drop the transform on the floor and every
    # other test stays green -- MEASURED: that mutant survives the whole suite.
    add_test(NAME app_backdrop_transformed COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side front --view front --background-transform 0.3,0.1,2
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/backdrop_tf.png --screenshot-viewport)
    set_tests_properties(app_backdrop_transformed PROPERTIES
        FAIL_REGULAR_EXPRESSION "covered 0 of;could not write"
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_REQUIRED app_backdrop_fixture
        FIXTURES_SETUP app_backdrop_transformed)

    # A MATCHED viewport-only pair, so `mh_png_compare` cannot be comparing two
    # different sizes (it hard-fails on that anyway).
    #
    # MEASURED: the transform moves 1,692,837 of 2,310,496 pixels here -- 73% of
    # the viewport. The bound is 50,000, deliberately 34x under that, BECAUSE CI
    # RENDERS AT A DIFFERENT SIZE (899,072 px there against 2,310,496 here, as
    # `1f007efe` went red proving). Scaled to CI that is roughly 659,000, so
    # 50,000 clears CI by ~13x while still killing the mutant this exists for --
    # a viewport that ignores the transform scores 0.
    add_test(NAME app_backdrop_transform_moves_pixels COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_tf.png
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_front.png --min-differing 50000)
    set_tests_properties(app_backdrop_transform_moves_pixels PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_transformed;app_backdrop_front")

    # A malformed transform is REFUSED, not quietly read as zero -- a zero scale
    # empties the source rect and `SceneResources::draw` SKIPS it, so the
    # backdrop would simply not appear.
    add_test(NAME app_backdrop_transform_bad COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-transform 0,0,x
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/never_tf.png)
    set_tests_properties(app_backdrop_transform_bad PROPERTIES
        PASS_REGULAR_EXPRESSION "--background-transform takes"
        FIXTURES_REQUIRED app_backdrop_fixture)

    add_test(NAME app_backdrop_plain_front COMMAND makehuman
             --view front --screenshot ${CMAKE_CURRENT_BINARY_DIR}/plain_front.png --screenshot-viewport)
    set_tests_properties(app_backdrop_plain_front PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_SETUP app_backdrop_plain_front)
    # Counted, not byte-compared, and in BOTH directions. MEASURED on this
    # machine: two runs of the same scene differ in 0 to 3 pixels of 4,096,000
    # (MSAA resolve order is not promised to be stable), while a real backdrop
    # moves 2,473,836. A `files_differ` check would therefore have been
    # satisfied by the noise alone -- it would pass with the feature removed --
    # and `files_identical` was genuinely FLAKY, which is how this was found.
    add_test(NAME app_backdrop_changes_the_window COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_front.png
             ${CMAKE_CURRENT_BINARY_DIR}/plain_front.png --min-differing 100000)
    set_tests_properties(app_backdrop_changes_the_window PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_front;app_backdrop_plain_front")

    # THE DISCRIMINATING ONE. A backdrop bound to the FRONT must be invisible
    # from the right, because a reference photo is orthographic and one hanging
    # behind a side view is worse than none. Without this, a `facingSide` that
    # always returned true would pass every check above -- the backdrop would be
    # on permanently and "it changed the window" would still hold.
    add_test(NAME app_backdrop_right COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side front --view right
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/backdrop_right.png --screenshot-viewport)
    set_tests_properties(app_backdrop_right PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_REQUIRED app_backdrop_fixture
        FIXTURES_SETUP app_backdrop_right)
    add_test(NAME app_backdrop_plain_right COMMAND makehuman
             --view right --screenshot ${CMAKE_CURRENT_BINARY_DIR}/plain_right.png --screenshot-viewport)
    set_tests_properties(app_backdrop_plain_right PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_SETUP app_backdrop_plain_right)
    add_test(NAME app_backdrop_hidden_from_another_side COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_right.png
             ${CMAKE_CURRENT_BINARY_DIR}/plain_right.png --max-differing 100)
    set_tests_properties(app_backdrop_hidden_from_another_side PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_right;app_backdrop_plain_right")

    # PSNR, which counting pixels cannot express.
    #
    # The KTX2 encoder work needs a QUALITY number, not a count: a lossy codec
    # changes nearly every pixel a little, so `--max-differing` is useless
    # there -- basisu's ETC1S decodes to 38.92 dB while differing almost
    # everywhere. So `--min-psnr` exists, and these two tests are what prove it
    # discriminates, using pairs the suite already produces.
    #
    # MEASURED on this machine, not guessed:
    #     backdrop_right vs plain_right   **inf dB** (0 differing pixels)
    #     backdrop_front vs plain_front   **11.28 dB**
    # The right-view pair is the same scene twice -- the backdrop is correctly
    # invisible from the side -- so it is near-identical; the front-view pair
    # differs by 2.47 million pixels. 40 dB sits far from both, which is what
    # makes it a threshold rather than a coin toss. The MSAA noise the comment
    # above documents (up to 3 pixels differing by 37) computes to about 78 dB,
    # still nowhere near the floor.
    #
    # The PAIR is the point: `--min-psnr` alone would pass if the metric were
    # stuck at infinity, and `--max-psnr` alone would pass if it were stuck at
    # zero. Only both together pin it.
    #
    # The ceiling is 20, which is nowhere near either measurement, and that is
    # deliberate after this test FAILED IN CI.
    #
    # It was first written as `--max-psnr 11.9`, a hair above the 11.28 dB
    # measured here, so that an integer parse truncating to 11 would fail it.
    # CI measured **12.72 dB over 899,072 pixels** against this machine's
    # **11.28 dB over 4,495,360**: the offscreen render is a DIFFERENT SIZE, so
    # the two frames are not the same content and their PSNR is not comparable
    # at all. A bound tight enough to detect a parsing bug was therefore tied to
    # a number that varies by machine.
    #
    # This test keeps only the job it can do honestly -- proving the metric is
    # not stuck high -- with a ceiling both machines clear by a wide margin. The
    # fractional-parse check moved to the ETC1S gate below, whose input is a
    # file in the repository and whose encoder is integer arithmetic, so its
    # number is identical everywhere.
    add_test(NAME app_psnr_sees_two_near_identical_frames COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_right.png
             ${CMAKE_CURRENT_BINARY_DIR}/plain_right.png --min-psnr 40)
    set_tests_properties(app_psnr_sees_two_near_identical_frames PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_right;app_backdrop_plain_right")
    # `--max-psnr`, NOT `WILL_FAIL` with `--min-psnr`. Written the obvious way
    # first, this test passed while the mode did not exist at all: WILL_FAIL
    # accepts ANY non-zero exit, so the tool's "unknown mode" usage error (2)
    # satisfied it just as well as a real low-PSNR verdict (1). Stated as a
    # POSITIVE assertion it cannot pass that way -- a usage error still exits 2
    # and still fails the test.
    add_test(NAME app_psnr_sees_a_changed_frame COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_front.png
             ${CMAKE_CURRENT_BINARY_DIR}/plain_front.png --max-psnr 20)
    set_tests_properties(app_psnr_sees_a_changed_frame PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_front;app_backdrop_plain_front")

    # What the ETC1S encoder actually costs, on a texture already in the repo.
    #
    # The input is `data/eyes/materials/brown_eye.png` -- the exact image the
    # KTX2 investigation measured, so the number below is comparable to the one
    # it produced. NOTHING is written into data/: the round-trip goes to the
    # build directory.
    #
    # MEASURED: our per-block encoder gives **40.20 dB** here, against basisu's
    # full pipeline at **38.92 dB** -- per-block optimal SHOULD beat it, since
    # basisu's figure includes the loss from quantising to shared codebooks.
    #
    # The bar is 39.9, NOT basisu's 38.92, and the difference is the whole point:
    # 38.92 leaves 1.28 dB of slack, and THREE real degradations fit inside it.
    # Measured, each by building the mutant and reading the number off:
    #     baseline                         40.20 dB
    #     5-bit rounding by shift          39.74
    #     L1 selector error instead of L2  39.47
    #     one refinement pass instead of 3 39.08
    # Every one clears 38.92, so that floor would have passed all of them. At
    # 39.9 they all fail, with 0.3 dB still in hand below the baseline -- and
    # 39.9 clears basisu's figure by construction, so the external claim holds
    # without a second assertion.
    #
    # The encoder is integer arithmetic throughout, so this number is not
    # platform noise. If it ever moves, RE-MEASURE and write the new figure
    # here rather than widening the bound.
    #
    # NOTE FOR THE CODEBOOK CHUNK: adding shared codebooks will push this DOWN,
    # because that is what quantising to a codebook costs. That is expected, not
    # a regression -- re-measure and state the new figure there rather than
    # loosening this one.
    add_test(NAME app_etc1s_roundtrip COMMAND mh_etc1s_roundtrip
             ${MH_DATA_DIR}/eyes/materials/brown_eye.png
             ${CMAKE_CURRENT_BINARY_DIR}/etc1s_brown_eye.png)
    set_tests_properties(app_etc1s_roundtrip PROPERTIES FIXTURES_SETUP app_etc1s_roundtrip)
    add_test(NAME app_etc1s_holds_the_quality_bar COMMAND mh_png_compare
             ${MH_DATA_DIR}/eyes/materials/brown_eye.png
             ${CMAKE_CURRENT_BINARY_DIR}/etc1s_brown_eye.png --min-psnr 39.9)
    set_tests_properties(app_etc1s_holds_the_quality_bar PROPERTIES
        FIXTURES_REQUIRED app_etc1s_roundtrip)

    # The same round trip from above, bounded from the other side.
    #
    # Two jobs. It pins the encoder's quality EXACTLY rather than from below --
    # 40.20 dB sits inside [39.9, 40.5], so a change in either direction has to
    # be re-measured and explained rather than absorbed. And the fraction is
    # load-bearing: parsed as an integer, 40.5 truncates to 40, which the
    # measured 40.20 exceeds, so the test fails. That is what now proves the
    # bound is read as a real number -- the check used to live on a pair of
    # rendered frames, and CI proved their PSNR is not the same from one machine
    # to the next. This pair is a committed PNG through an integer-only encoder,
    # so it is.
    #
    # An encoder that gets genuinely BETTER will fail this ceiling. That is the
    # same re-measure-and-say-why discipline the exact vertex counts get, not an
    # accident.
    add_test(NAME app_etc1s_quality_is_pinned_from_above COMMAND mh_png_compare
             ${MH_DATA_DIR}/eyes/materials/brown_eye.png
             ${CMAKE_CURRENT_BINARY_DIR}/etc1s_brown_eye.png --max-psnr 40.5)
    set_tests_properties(app_etc1s_quality_is_pinned_from_above PROPERTIES
        FIXTURES_REQUIRED app_etc1s_roundtrip)

    # ...and the backdrop must not EAT the model. It is drawn before the body
    # with its depth write off; if it ever wrote depth it would occlude the
    # character, and every differs-from-plain check above would still pass while
    # the model vanished. So the torso's pixels must be UNCHANGED by adding a
    # backdrop, and the empty viewport's must change.
    #
    # `MH_PIL_PYTHON`, not `Python3_EXECUTABLE`: the wheels live in `.venv-mh`
    # and not in the system interpreter. Written with the latter first, and both
    # checks then SKIPPED on the developer machine -- the one place they can run
    # -- which is the exact mistake the genitals check above already records.
    #
    # They skip in the macOS CI jobs, which do not install those wheels. Said
    # out loud because a gate that skips everywhere is no gate. What runs
    # everywhere is `app_backdrop_hidden_from_another_side` and the unit tests
    # on `facingSide` and `coverSource`.
    if(Python3_Interpreter_FOUND)
        add_test(NAME app_backdrop_does_not_eat_the_model COMMAND ${MH_PIL_PYTHON}
                 ${CMAKE_SOURCE_DIR}/tools/png_region_response.py
                 --a ${CMAKE_CURRENT_BINARY_DIR}/backdrop_front.png
                 --b ${CMAKE_CURRENT_BINARY_DIR}/plain_front.png
                 # WINDOW-LAYOUT DEPENDENT, and it has now moved TWICE. First
                 # the Material dock tabbed the right-hand side; then (2026-09-23)
                 # the preset chooser widened the Modelling dock and the viewport
                 # went 1402 -> 1192 px wide, which slid (698,586) off interior
                 # torso to a response of 2.4 against a bar of 2.0.
                 # Re-derived from the image rather than relaxed, as the note
                 # here has said to do both times: at y 843 the torso spans
                 # x 472..719, and (520,920) reads EXACTLY 0.0 while the
                 # companion check below still reads 27.3 on the empty
                 # viewport. If a dock changes again, re-derive again --
                 # scanning a grid of candidate centres for a zero response is
                 # a two-minute job and the threshold is the point of the test.
                 --cx 520 --cy 920 --radius 40 --max-response 2.0)
        set_tests_properties(app_backdrop_does_not_eat_the_model PROPERTIES
            SKIP_RETURN_CODE 77
            FIXTURES_REQUIRED "app_backdrop_front;app_backdrop_plain_front")
        add_test(NAME app_backdrop_reaches_the_pixels COMMAND ${MH_PIL_PYTHON}
                 ${CMAKE_SOURCE_DIR}/tools/png_region_response.py
                 --a ${CMAKE_CURRENT_BINARY_DIR}/backdrop_front.png
                 --b ${CMAKE_CURRENT_BINARY_DIR}/plain_front.png
                 --cx 346 --cy 246 --radius 40 --min-response 20.0)
        set_tests_properties(app_backdrop_reaches_the_pixels PROPERTIES
            SKIP_RETURN_CODE 77
            FIXTURES_REQUIRED "app_backdrop_front;app_backdrop_plain_front")
    endif()

    # Opacity 0 must be INVISIBLE, not merely faint. It is alpha against the
    # already-cleared framebuffer, so a fully transparent backdrop has to leave
    # the frame byte-for-byte as if none were set -- which also proves the
    # opacity reaches the shader at all, rather than being parsed and dropped.
    add_test(NAME app_backdrop_transparent COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side front --background-opacity 0 --view front
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/backdrop_clear.png --screenshot-viewport)
    set_tests_properties(app_backdrop_transparent PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_REQUIRED app_backdrop_fixture
        FIXTURES_SETUP app_backdrop_transparent)
    add_test(NAME app_backdrop_transparent_shows_nothing COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/backdrop_clear.png
             ${CMAKE_CURRENT_BINARY_DIR}/plain_front.png --max-differing 100)
    set_tests_properties(app_backdrop_transparent_shows_nothing PROPERTIES
        FIXTURES_REQUIRED "app_backdrop_transparent;app_backdrop_plain_front")

    # An opacity outside 0..1 is refused rather than clamped in silence.
    add_test(NAME app_backdrop_opacity_bad COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-opacity 7
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/never.png)
    set_tests_properties(app_backdrop_opacity_bad PROPERTIES
        PASS_REGULAR_EXPRESSION "--background-opacity takes a number from 0 to 1"
        FIXTURES_REQUIRED app_backdrop_fixture)

    # ...and both flags are REFUSED under --render rather than ignored. --render
    # draws its own fixed view and composites its backdrop unconditionally, so
    # accepting them there would be a flag that does nothing while its help says
    # otherwise.
    add_test(NAME app_view_refused_with_render COMMAND makehuman --view left
             --render ${CMAKE_CURRENT_BINARY_DIR}/never.png)
    set_tests_properties(app_view_refused_with_render PROPERTIES
        PASS_REGULAR_EXPRESSION "steer the viewport")

    # A name that is not one of the six is REFUSED, not defaulted to front. A
    # typo that silently means "front" is the painted no-op this codebase keeps
    # finding: the flag appears to work.
    add_test(NAME app_view_unknown COMMAND makehuman --view sideways
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/never.png)
    set_tests_properties(app_view_unknown PROPERTIES
        WILL_FAIL TRUE)
    add_test(NAME app_backdrop_side_unknown COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/backdrop_ref.ppm
             --background-side sideways
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/never.png)
    set_tests_properties(app_backdrop_side_unknown PROPERTIES
        WILL_FAIL TRUE
        FIXTURES_REQUIRED app_backdrop_fixture)

    # VIEWPORT-ONLY, and a COUNTED compare. This gate used to `files_differ`
    # two whole-window PNGs -- "is any byte different", which chrome can answer
    # without the grid. MEASURED: the grid moves 157,520 pixels and touches
    # ZERO chrome (the window diff and the viewport diff are the same 157,520),
    # so 48.6% of the old input was pixels the grid cannot reach -- a route to
    # a false pass and a false red alike. Two no-grid viewport shots differ by
    # 0, so the viewport has no noise to threshold against.
    #
    # THE BOUND IS DELIBERATELY FAR BELOW 157,520, because CI renders at a
    # different SIZE than this machine (`1f007efe` went red proving it: 899,072
    # pixels there against 4,495,360 here). Scaling the local figure to CI's
    # window gives roughly 31,500, so 5,000 clears CI by ~6x and sits 31x under
    # the local number -- while still killing the mutant this gate exists for,
    # a `--grid` that draws nothing, which scores 0.
    add_test(NAME app_grid_window COMMAND makehuman
             --grid --screenshot-viewport
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_grid.png)
    set_tests_properties(app_grid_window PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_SETUP app_grid_png)
    # Repointed as a MATCHED SET with the one above: `mh_png_compare` hard-fails
    # on a size mismatch, so these two move together or not at all.
    add_test(NAME app_grid_window_plain COMMAND makehuman
             --screenshot-viewport
             --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app_nogrid.png)
    set_tests_properties(app_grid_window_plain PROPERTIES
        SKIP_REGULAR_EXPRESSION "viewport error"
        FIXTURES_SETUP app_nogrid_png)
    add_test(NAME app_grid_changes_the_window COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/app_grid.png
             ${CMAKE_CURRENT_BINARY_DIR}/app_nogrid.png --min-differing 5000)
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

    # Which bone owns a vertex.
    #
    # The locs style was attempted twice and reverted twice, and the blocker was
    # never a parameter: a generator working on the body surface cannot tell an
    # arm vertex from a torso one, so a rope following the silhouette runs out
    # along the arm. The weights have known that all along -- this is the door
    # onto them, the same gap `--spread-roots` filled for `spreadOverSurface`.
    #
    # `compile(skel, 1)` is the whole computation, and it is gated separately in
    # test_weights_parity.cpp against an argmax taken the other way round. This
    # gates the CALL SITE, and asserts the separation rather than the format --
    # see the script for the measurements.
    add_test(NAME app_vertex_bones COMMAND ${CMAKE_COMMAND}
             -DAPP=$<TARGET_FILE:makehuman>
             -DBASE=${MH_DATA_DIR}/3dobjs/base.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_vertex_bones.cmake)

    # Dropping a record the file named and this build cannot resolve.
    #
    # A `.mhm` naming an asset that is not installed loads with a warning and
    # then, on RE-SAVE, the slot is rewritten from live state and the UUID is
    # gone for good. MEASURED 2026-09-24 on `clothes` and `hair`.
    #
    # Preserving the old line instead was considered and rejected on the
    # measurement, not on taste: the format has one line per slot and no field
    # for "wanted, but not installed", so keeping it would make the file assert
    # the character wears something it does not -- and for a slot defaulting to
    # WORN, like teeth, override the default actually in use. Nothing visual is
    # at stake either way: a file with an unresolvable teeth UUID loads as
    # default Teeth, saves as default Teeth and reloads identically, so the
    # round trip is already character-idempotent. The ONLY loss is the record.
    #
    # So the defect was that the loss was SILENT at save time. The app warned on
    # LOAD, which does not help a user who loads now and saves an hour later.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ghost_asset.mhm
         "version v1.2.0\nname Ghost\n"
         "clothes SomeJacket 11111111-2222-3333-4444-555555555555\n")
    add_test(NAME app_mhm_says_when_a_record_is_dropped COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/ghost_asset.mhm
             --save ${CMAKE_CURRENT_BINARY_DIR}/ghost_asset_out.mhm)
    set_tests_properties(app_mhm_says_when_a_record_is_dropped PROPERTIES
        PASS_REGULAR_EXPRESSION
        "clothes: the saved file will no longer record UUID 11111111-2222-3333-4444-555555555555"
        FIXTURES_SETUP app_mhm_dropped_record)
    # ...and it must NOT nag on a file whose records all resolve, or the warning
    # becomes noise a user learns to ignore. The output of the run above IS such
    # a file -- the unresolvable line is gone from it -- so this is the same
    # check from the other side rather than a second fixture.
    add_test(NAME app_mhm_is_quiet_when_nothing_is_dropped COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/ghost_asset_out.mhm
             --save ${CMAKE_CURRENT_BINARY_DIR}/ghost_asset_twice.mhm)
    set_tests_properties(app_mhm_is_quiet_when_nothing_is_dropped PROPERTIES
        FAIL_REGULAR_EXPRESSION "no longer record UUID"
        FIXTURES_REQUIRED app_mhm_dropped_record)

    # Scalp roots, walked rather than raycast.
    #
    # `mh::core::SurfaceWalk` shipped in the chunk before this one and nothing
    # could reach it -- only its own tests. The style generator does not exist
    # yet (b5437c02 shipped none of the 2026-09-11 attempt), but every asset
    # generator in `tools/` is Python and so cannot call `mh::core` directly.
    # A primitive no caller can use is not finished, so `--spread-roots` is the
    # surface that finishes it.
    #
    # The region is the cranium cap of the BASE MESH in rest -- which is the
    # mesh a proxy binds to, so generation-time is the only time these roots
    # are wanted.
    add_test(NAME app_spread_roots COMMAND ${CMAKE_COMMAND}
             -DFLAG=--spread-roots -DARG=24 -DEXPECT=24 -DAPP=$<TARGET_FILE:makehuman>
             -DBASE=${MH_DATA_DIR}/3dobjs/base.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_roots.cmake)
    # Every root is on the scalp. MEASURED: the hair-bearing region holds 237
    # body vertices at or above the hairline, so asking for more than that is
    # clamped rather than repeated. 157 would mean the region had slipped back
    # to the cranium cap, which stops 50 degrees above the nape; 303 would mean
    # it had picked up `helper-hair` (138) and `joint-head-2` (8) as well.
    add_test(NAME app_spread_roots_clamped COMMAND ${CMAKE_COMMAND}
             -DFLAG=--spread-roots -DARG=999 -DEXPECT=237 -DAPP=$<TARGET_FILE:makehuman>
             -DBASE=${MH_DATA_DIR}/3dobjs/base.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_roots.cmake)
    # Deterministic: a style that moved between runs could not be reviewed by
    # looking at it, and the generator would produce a different asset each time.
    add_test(NAME app_spread_roots_deterministic_a COMMAND ${CMAKE_COMMAND}
             -DFLAG=--spread-roots -DARG=24 -DEXPECT=24 -DOUT=${CMAKE_CURRENT_BINARY_DIR}/roots_a.txt
             -DAPP=$<TARGET_FILE:makehuman>
             -DBASE=${MH_DATA_DIR}/3dobjs/base.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_roots.cmake)
    set_tests_properties(app_spread_roots_deterministic_a PROPERTIES FIXTURES_SETUP roots_a)
    add_test(NAME app_spread_roots_deterministic_b COMMAND ${CMAKE_COMMAND}
             -DFLAG=--spread-roots -DARG=24 -DEXPECT=24 -DOUT=${CMAKE_CURRENT_BINARY_DIR}/roots_b.txt
             -DAPP=$<TARGET_FILE:makehuman>
             -DBASE=${MH_DATA_DIR}/3dobjs/base.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_roots.cmake)
    set_tests_properties(app_spread_roots_deterministic_b PROPERTIES FIXTURES_SETUP roots_b)
    add_test(NAME app_spread_roots_repeatable COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/roots_a.txt
             -DB=${CMAKE_CURRENT_BINARY_DIR}/roots_b.txt
             -P ${CMAKE_CURRENT_SOURCE_DIR}/files_identical.cmake)
    set_tests_properties(app_spread_roots_repeatable PROPERTIES
        FIXTURES_REQUIRED "roots_a;roots_b")
    # The two refusals.
    add_test(NAME app_spread_roots_zero COMMAND makehuman --spread-roots 0)
    set_tests_properties(app_spread_roots_zero PROPERTIES
        PASS_REGULAR_EXPRESSION "--spread-roots wants a count above 0")
    add_test(NAME app_spread_roots_not_a_number COMMAND makehuman --spread-roots lots)
    set_tests_properties(app_spread_roots_not_a_number PROPERTIES
        PASS_REGULAR_EXPRESSION "--spread-roots wants a count above 0")
    # ...and refusing must EXIT non-zero, not merely say so. PASS_REGULAR_EXPRESSION
    # makes ctest ignore the exit code entirely, so the two tests above pass
    # unchanged if the refusal returns 0 -- and a generator reading that exit
    # code would take an empty run for a good one. Kept as its OWN test rather
    # than paired with the message, for the reason app_rig_unknown records:
    # WILL_FAIL alongside a regex passes vacuously on the wrong failure. Here
    # the sibling above pins the message, so this one only has to pin the code.
    add_test(NAME app_spread_roots_zero_exits_nonzero COMMAND makehuman --spread-roots 0)
    set_tests_properties(app_spread_roots_zero_exits_nonzero PROPERTIES WILL_FAIL TRUE)

    # A parting, routed over the scalp rather than guessed.
    #
    # `mh::core::pathOverSurface` shipped in the chunk before this one with no
    # caller but its own tests -- the same gap `--spread-roots` filled for
    # `spreadOverSurface`. Cornrows and partings are paths, and the generator
    # that draws them is Python, so it needs this surface or it needs its own
    # Dijkstra. ORACLE: an independent Python walk over the hair-bearing region
    # makes the midline row from the front hairline (983, elevation +16.5) to
    # the nape-most vertex (941, elevation -45.2) exactly 15 vertices long,
    # 3.1145 dm over the surface -- and finds the same 237-vertex region.
    #
    # This test used to route 5192 -> 5379, a pair chosen off the cranium cap:
    # 5192 sits on the FOREHEAD, below the hairline, and 5379 on the CROWN, 57
    # degrees short of the nape. Rendering cornrows routed that way in Blender
    # is what exposed it -- six ridges over the crown with a bare occiput.
    add_test(NAME app_scalp_path COMMAND ${CMAKE_COMMAND}
             -DFLAG=--scalp-path -DARG=983,941 -DEXPECT=15
             -DFIRST=983 -DLAST=941 -DAPP=$<TARGET_FILE:makehuman>
             -DBASE=${MH_DATA_DIR}/3dobjs/base.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_roots.cmake)
    # A path to itself is one vertex, not zero and not an error.
    add_test(NAME app_scalp_path_to_itself COMMAND ${CMAKE_COMMAND}
             -DFLAG=--scalp-path -DARG=983,983 -DEXPECT=1
             -DFIRST=983 -DLAST=983 -DAPP=$<TARGET_FILE:makehuman>
             -DBASE=${MH_DATA_DIR}/3dobjs/base.obj
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_roots.cmake)
    # The refusals. An endpoint off the scalp is the one that matters: vertex 0
    # is a real mesh vertex, just not on the cap, and routing to it would leave
    # the scalp -- the exact thing the region exists to prevent.
    add_test(NAME app_scalp_path_off_the_scalp COMMAND makehuman --scalp-path 983,0)
    set_tests_properties(app_scalp_path_off_the_scalp PROPERTIES
        PASS_REGULAR_EXPRESSION "not on the scalp")
    add_test(NAME app_scalp_path_off_the_scalp_exits_nonzero
             COMMAND makehuman --scalp-path 983,0)
    set_tests_properties(app_scalp_path_off_the_scalp_exits_nonzero PROPERTIES WILL_FAIL TRUE)
    add_test(NAME app_scalp_path_malformed COMMAND makehuman --scalp-path 983)
    set_tests_properties(app_scalp_path_malformed PROPERTIES
        PASS_REGULAR_EXPRESSION "--scalp-path wants <from>,<to>")
    add_test(NAME app_scalp_path_not_a_number COMMAND makehuman --scalp-path crown,nape)
    set_tests_properties(app_scalp_path_not_a_number PROPERTIES
        PASS_REGULAR_EXPRESSION "--scalp-path wants <from>,<to>")

    # Binding authored points, so the style generator can reach `bindToSurface`.
    # It is Python and cannot call mh::core, and cornrows need authored geometry
    # because the scalp is only ten vertices across -- identity binding allows
    # five single-vertex rows, which renders as a corrugated cap.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/points.txt
         "0.0 8.60 0.50\n0.0 7.90 1.40\n0.30 8.40 0.60\n")
    add_test(NAME app_bind_points COMMAND ${CMAKE_COMMAND}
             -DPOINTS=${CMAKE_CURRENT_BINARY_DIR}/points.txt -DEXPECT=3
             -DAPP=$<TARGET_FILE:makehuman>
             -P ${CMAKE_CURRENT_SOURCE_DIR}/check_bindings.cmake)
    add_test(NAME app_bind_points_missing_file COMMAND makehuman --bind-points /nope/nope.txt)
    set_tests_properties(app_bind_points_missing_file PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot read the points")
    add_test(NAME app_bind_points_missing_file_exits_nonzero
             COMMAND makehuman --bind-points /nope/nope.txt)
    set_tests_properties(app_bind_points_missing_file_exits_nonzero PROPERTIES WILL_FAIL TRUE)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bad_points.txt "0.0 8.6\n")
    add_test(NAME app_bind_points_malformed COMMAND makehuman
             --bind-points ${CMAKE_CURRENT_BINARY_DIR}/bad_points.txt)
    set_tests_properties(app_bind_points_malformed PROPERTIES
        PASS_REGULAR_EXPRESSION "wants three numbers per line")

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/punit)

    # Setting a face pose unit DIRECTLY -- the reference's expression mixer is
    # sixty sliders, one per unit (`7_expression_mixer.py:180`), and --facs only
    # reaches the thirty Action Units that name them. A unit with no AU was
    # unreachable, and the sixty NAMES were undiscoverable besides.
    add_test(NAME app_list_pose_units COMMAND makehuman --list-pose-units)
    set_tests_properties(app_list_pose_units PROPERTIES
        PASS_REGULAR_EXPRESSION "LeftBrowDown")

    # `--rig-names auto` -- and the reason it is a FLAG and not just chooser
    # behaviour: the chooser's path runs only in the GUI, and a path no test can
    # reach is a path nothing checks. One implementation, two entry points.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/autoname)
    set(WALKBVH ${MH_DATA_DIR}/animations/walks/walk1.bvh)
    add_test(NAME app_rig_names_auto_reports_its_choice COMMAND makehuman
             --pose ${WALKBVH} --pose-frame 0 --rig-names auto
             --export ${CMAKE_CURRENT_BINARY_DIR}/autoname/a0.obj)
    set_tests_properties(app_rig_names_auto_reports_its_choice PROPERTIES
        FIXTURES_SETUP app_auto0
        PASS_REGULAR_EXPRESSION "read as makehuman1 \\(59 of 179 bones driven\\)")
    add_test(NAME app_rig_names_auto_frame7 COMMAND makehuman
             --pose ${WALKBVH} --pose-frame 7 --rig-names auto
             --export ${CMAKE_CURRENT_BINARY_DIR}/autoname/a7.obj)
    set_tests_properties(app_rig_names_auto_frame7 PROPERTIES FIXTURES_SETUP app_auto7)
    # It does not merely announce a naming -- the character moves.
    add_test(NAME app_rig_names_auto_actually_animates COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/autoname/a0.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/autoname/a7.obj
             -DEXPECT_DIFFERENT=14580
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_rig_names_auto_actually_animates PROPERTIES
        FIXTURES_REQUIRED "app_auto0;app_auto7")
    # `auto` names no table, so a RIGGED export under it must not go looking for
    # data/rigs/auto_retarget.json. It did: every .glb/.fbx/.usd export warned
    # "cannot load --rig-names auto". The earlier gates all exported .obj, which
    # carries no skeleton, so none of them reached it.
    add_test(NAME app_rig_names_auto_rigged_export_is_quiet COMMAND makehuman
             --pose ${WALKBVH} --pose-frame 0 --rig-names auto
             --export ${CMAKE_CURRENT_BINARY_DIR}/autoname/a.glb)
    set_tests_properties(app_rig_names_auto_rigged_export_is_quiet PROPERTIES
        FAIL_REGULAR_EXPRESSION "cannot load --rig-names")

    # A pose authored for THIS rig keeps its own names: `auto` ranks native
    # alongside the tables rather than treating it as a fallback, so it wins
    # when it should. Without this, `auto` would rename everything it could.
    add_test(NAME app_rig_names_auto_leaves_native_poses_alone COMMAND makehuman
             --pose tpose --rig-names auto
             --export ${CMAKE_CURRENT_BINARY_DIR}/autoname/t.obj)
    set_tests_properties(app_rig_names_auto_leaves_native_poses_alone PROPERTIES
        FAIL_REGULAR_EXPRESSION "read as")

    # The Animation chooser.
    #
    # It exists because `data/animations/` shipped for four milestones as files
    # nothing could reach: MEASURED, all three drive 0 of this rig's 179 bones
    # under their own names (59 through the MakeHuman 1.x table, 5 through
    # Mixamo's). The chooser ranks the namings per file and reads each through
    # the best one, because the GUI has no --rig-names to offer.
    # The label is title-cased by `prettyAssetName`, like every other chooser,
    # because no animation ships a .meta -- pinning it here is what caught the
    # labels coming out as raw stems.
    #
    # Each file pinned by its LABEL COLUMN, not by a bare name -- `walk1` and
    # `zombieWalk1` also appear in the path column, so pinning them alone passed
    # whether or not a label was ever produced. All three shipped files, dance1
    # included, which nothing pinned before.
    add_test(NAME app_list_animations_labels_dance COMMAND makehuman --list-animations)
    set_tests_properties(app_list_animations_labels_dance PROPERTIES
        PASS_REGULAR_EXPRESSION "dance1\\.bvh\tDance1\t1\t")
    add_test(NAME app_list_animations_labels_walk COMMAND makehuman --list-animations)
    set_tests_properties(app_list_animations_labels_walk PROPERTIES
        PASS_REGULAR_EXPRESSION "walk1\\.bvh\tWalk1\t14\t")
    # "zombieWalk1", NOT "ZombieWalk1". This assertion used to pin the second,
    # which was the title-cased STEM -- a guess. The authored name lives in
    # `data/animations/zombie/zombie.mhanim` as
    # `# anim zombieWalk1 zombieWalk1.bvh z_is_up`, and the author left that
    # first letter lower on purpose. Correcting an assertion that recorded a
    # defect, not loosening one: the expected string now comes from a file in
    # the repository rather than from what the code happened to emit.
    # Dance1 and Walk1 are unchanged -- title-casing agreed with the sidecar
    # for those two, which is exactly why the bug survived.
    add_test(NAME app_list_animations_labels_zombie COMMAND makehuman --list-animations)
    set_tests_properties(app_list_animations_labels_zombie PROPERTIES
        PASS_REGULAR_EXPRESSION "zombieWalk1\\.bvh\tzombieWalk1\t31\t")
    # The frame count is reported, which is what a scrubber will need and what
    # tells a 1-frame file from a 31-frame one.
    # THE claim: the ranking names makehuman1, not native. If this ever says
    # native, the chooser is offering files that cannot move the character.
    add_test(NAME app_list_animations_ranks_the_naming COMMAND makehuman --list-animations)
    set_tests_properties(app_list_animations_ranks_the_naming PROPERTIES
        PASS_REGULAR_EXPRESSION "makehuman1 \\(59 bones\\)")
    # ...and the ranking is measured against the rig that is loaded, so it
    # follows --rig rather than being a constant.
    add_test(NAME app_list_animations_ranks_per_rig COMMAND makehuman
             --rig default --list-animations)
    set_tests_properties(app_list_animations_ranks_per_rig PROPERTIES
        PASS_REGULAR_EXPRESSION "makehuman1 \\(56 bones\\)")

    # --rig-names on EXPORT, across every skeleton-carrying format.
    #
    # Four of the five read their bone names from one vector, SkinView::
    # jointNames, so one rename covers glTF, FBX, USD and DAE. BVH bypasses that
    # structure entirely and gets its own pass. These gates exist because that
    # asymmetry is exactly where a rename goes half-applied -- and because glTF,
    # FBX and USD each REFUSE a scene whose skinned entries name different
    # skeletons, so a partial rename does not mislabel a file, it fails the
    # export outright.
    #
    # Each case asserts the chosen skeleton's word is present AND the other two
    # are gone. Presence alone would pass on a writer that added the new name
    # beside the old one.
    #
    # The probe bone is `upperarm01`/`UpArm_L`/`LeftArm`. It was `shoulder01`
    # until 2026-09-20, when the retarget tables were corrected: `shoulder01`
    # is the scapula bone this rig interposes between clavicle and humerus, and
    # NEITHER foreign skeleton has a word for it, so after the correction it
    # exports under its own name in all three vocabularies and could no longer
    # tell them apart. `upperarm01` is renamed by both tables and is therefore
    # the bone that actually discriminates.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rignames-export)
    add_test(NAME app_export_names_bvh_native_write COMMAND makehuman
             --pose tpose --rig-names native --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.bvh)
    set_tests_properties(app_export_names_bvh_native_write PROPERTIES FIXTURES_SETUP app_export_names_bvh_native)
    add_test(NAME app_export_names_bvh_native COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.bvh "-DKEYS=upperarm01" "-DABSENT=UpArm_L;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_bvh_native PROPERTIES FIXTURES_REQUIRED app_export_names_bvh_native)
    add_test(NAME app_export_names_bvh_makehuman1_write COMMAND makehuman
             --pose tpose --rig-names makehuman1 --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.bvh)
    set_tests_properties(app_export_names_bvh_makehuman1_write PROPERTIES FIXTURES_SETUP app_export_names_bvh_makehuman1)
    add_test(NAME app_export_names_bvh_makehuman1 COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.bvh "-DKEYS=UpArm_L" "-DABSENT=upperarm01;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_bvh_makehuman1 PROPERTIES FIXTURES_REQUIRED app_export_names_bvh_makehuman1)
    add_test(NAME app_export_names_bvh_mixamo_write COMMAND makehuman
             --pose tpose --rig-names mixamo --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.bvh)
    set_tests_properties(app_export_names_bvh_mixamo_write PROPERTIES FIXTURES_SETUP app_export_names_bvh_mixamo)
    add_test(NAME app_export_names_bvh_mixamo COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.bvh "-DKEYS=LeftArm" "-DABSENT=upperarm01;UpArm_L"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_bvh_mixamo PROPERTIES FIXTURES_REQUIRED app_export_names_bvh_mixamo)
    add_test(NAME app_export_names_glb_native_write COMMAND makehuman
             --pose tpose --rig-names native --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.glb)
    set_tests_properties(app_export_names_glb_native_write PROPERTIES FIXTURES_SETUP app_export_names_glb_native)
    add_test(NAME app_export_names_glb_native COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.glb "-DKEYS=upperarm01" "-DABSENT=UpArm_L;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_glb_native PROPERTIES FIXTURES_REQUIRED app_export_names_glb_native)
    add_test(NAME app_export_names_glb_makehuman1_write COMMAND makehuman
             --pose tpose --rig-names makehuman1 --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.glb)
    set_tests_properties(app_export_names_glb_makehuman1_write PROPERTIES FIXTURES_SETUP app_export_names_glb_makehuman1)
    add_test(NAME app_export_names_glb_makehuman1 COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.glb "-DKEYS=UpArm_L" "-DABSENT=upperarm01;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_glb_makehuman1 PROPERTIES FIXTURES_REQUIRED app_export_names_glb_makehuman1)
    add_test(NAME app_export_names_glb_mixamo_write COMMAND makehuman
             --pose tpose --rig-names mixamo --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.glb)
    set_tests_properties(app_export_names_glb_mixamo_write PROPERTIES FIXTURES_SETUP app_export_names_glb_mixamo)
    add_test(NAME app_export_names_glb_mixamo COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.glb "-DKEYS=LeftArm" "-DABSENT=upperarm01;UpArm_L"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_glb_mixamo PROPERTIES FIXTURES_REQUIRED app_export_names_glb_mixamo)
    add_test(NAME app_export_names_fbx_native_write COMMAND makehuman
             --pose tpose --rig-names native --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.fbx)
    set_tests_properties(app_export_names_fbx_native_write PROPERTIES FIXTURES_SETUP app_export_names_fbx_native)
    add_test(NAME app_export_names_fbx_native COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.fbx "-DKEYS=upperarm01" "-DABSENT=UpArm_L;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_fbx_native PROPERTIES FIXTURES_REQUIRED app_export_names_fbx_native)
    add_test(NAME app_export_names_fbx_makehuman1_write COMMAND makehuman
             --pose tpose --rig-names makehuman1 --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.fbx)
    set_tests_properties(app_export_names_fbx_makehuman1_write PROPERTIES FIXTURES_SETUP app_export_names_fbx_makehuman1)
    add_test(NAME app_export_names_fbx_makehuman1 COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.fbx "-DKEYS=UpArm_L" "-DABSENT=upperarm01;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_fbx_makehuman1 PROPERTIES FIXTURES_REQUIRED app_export_names_fbx_makehuman1)
    add_test(NAME app_export_names_fbx_mixamo_write COMMAND makehuman
             --pose tpose --rig-names mixamo --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.fbx)
    set_tests_properties(app_export_names_fbx_mixamo_write PROPERTIES FIXTURES_SETUP app_export_names_fbx_mixamo)
    add_test(NAME app_export_names_fbx_mixamo COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.fbx "-DKEYS=LeftArm" "-DABSENT=upperarm01;UpArm_L"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_fbx_mixamo PROPERTIES FIXTURES_REQUIRED app_export_names_fbx_mixamo)
    add_test(NAME app_export_names_usda_native_write COMMAND makehuman
             --pose tpose --rig-names native --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.usda)
    set_tests_properties(app_export_names_usda_native_write PROPERTIES FIXTURES_SETUP app_export_names_usda_native)
    add_test(NAME app_export_names_usda_native COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.usda "-DKEYS=upperarm01_L" "-DABSENT=UpArm_L;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_usda_native PROPERTIES FIXTURES_REQUIRED app_export_names_usda_native)
    add_test(NAME app_export_names_usda_makehuman1_write COMMAND makehuman
             --pose tpose --rig-names makehuman1 --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.usda)
    set_tests_properties(app_export_names_usda_makehuman1_write PROPERTIES FIXTURES_SETUP app_export_names_usda_makehuman1)
    add_test(NAME app_export_names_usda_makehuman1 COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.usda "-DKEYS=UpArm_L" "-DABSENT=upperarm01_L;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_usda_makehuman1 PROPERTIES FIXTURES_REQUIRED app_export_names_usda_makehuman1)
    add_test(NAME app_export_names_usda_mixamo_write COMMAND makehuman
             --pose tpose --rig-names mixamo --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.usda)
    set_tests_properties(app_export_names_usda_mixamo_write PROPERTIES FIXTURES_SETUP app_export_names_usda_mixamo)
    add_test(NAME app_export_names_usda_mixamo COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.usda "-DKEYS=LeftArm" "-DABSENT=upperarm01_L;UpArm_L"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_usda_mixamo PROPERTIES FIXTURES_REQUIRED app_export_names_usda_mixamo)
    add_test(NAME app_export_names_dae_native_write COMMAND makehuman
             --pose tpose --rig-names native --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.dae)
    set_tests_properties(app_export_names_dae_native_write PROPERTIES FIXTURES_SETUP app_export_names_dae_native)
    add_test(NAME app_export_names_dae_native COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.dae "-DKEYS=upperarm01" "-DABSENT=UpArm_L;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_dae_native PROPERTIES FIXTURES_REQUIRED app_export_names_dae_native)
    add_test(NAME app_export_names_dae_makehuman1_write COMMAND makehuman
             --pose tpose --rig-names makehuman1 --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.dae)
    set_tests_properties(app_export_names_dae_makehuman1_write PROPERTIES FIXTURES_SETUP app_export_names_dae_makehuman1)
    add_test(NAME app_export_names_dae_makehuman1 COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/makehuman1.dae "-DKEYS=UpArm_L" "-DABSENT=upperarm01;LeftArm"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_dae_makehuman1 PROPERTIES FIXTURES_REQUIRED app_export_names_dae_makehuman1)
    add_test(NAME app_export_names_dae_mixamo_write COMMAND makehuman
             --pose tpose --rig-names mixamo --export ${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.dae)
    set_tests_properties(app_export_names_dae_mixamo_write PROPERTIES FIXTURES_SETUP app_export_names_dae_mixamo)
    add_test(NAME app_export_names_dae_mixamo COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/mixamo.dae "-DKEYS=LeftArm" "-DABSENT=upperarm01;UpArm_L"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_dae_mixamo PROPERTIES FIXTURES_REQUIRED app_export_names_dae_mixamo)

    # The ABSENT machinery, gated against itself. A negative assertion only
    # fires when violated, so with nothing violating it, breaking the check is
    # invisible -- measured: downgrading its FATAL_ERROR to STATUS left every
    # export-naming test green. This deliberately violates it.
    add_test(NAME app_export_names_absent_actually_fails COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/rignames-export/native.bvh
             "-DKEYS=shoulder01" "-DABSENT=shoulder01"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_export_names_absent_actually_fails PROPERTIES
        WILL_FAIL TRUE FIXTURES_REQUIRED app_export_names_bvh_native)

    # BODY pose units reach --pose-unit, not just the face ones.
    #
    # data/poseunits/body-poseunits.json shipped as 61 poses nothing could
    # apply. It was authored against a differently-named skeleton, so MEASURED
    # against both rigs: 29 resolve fully, 24 partially, 8 not at all. With
    # body-poseunits-bones.json -- three renames, the only unambiguous ones --
    # that becomes 36 / 20 / 5.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bodyunits)
    # 60 face + 61 body, and the two name sets do not overlap.
    add_test(NAME app_list_pose_units_includes_body COMMAND makehuman --list-pose-units)
    set_tests_properties(app_list_pose_units_includes_body PROPERTIES
        PASS_REGULAR_EXPRESSION "UpperArmUpLeft1")
    add_test(NAME app_list_pose_units_still_has_face COMMAND makehuman --list-pose-units)
    set_tests_properties(app_list_pose_units_still_has_face PROPERTIES
        PASS_REGULAR_EXPRESSION "LeftBrowDown")

    add_test(NAME app_body_pose_unit_rest COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/bodyunits/rest.obj)
    set_tests_properties(app_body_pose_unit_rest PROPERTIES FIXTURES_SETUP app_bpu_rest)
    # THE claim: a unit the bone table revived actually moves the character.
    # UpperLegForwardLeft names `upperleg.L` and nothing else, so without the
    # table's one rename it drives nothing at all.
    add_test(NAME app_body_pose_unit_revived COMMAND makehuman
             --pose-unit UpperLegForwardLeft=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/bodyunits/revived.obj)
    set_tests_properties(app_body_pose_unit_revived PROPERTIES FIXTURES_SETUP app_bpu_revived)
    # 1,917 before the target stack became ordered, 1,887 after -- 30 of them
    # were accumulation-order noise across the writer's fourth decimal, not leg
    # movement. Re-measured; debug and release agree exactly.
    #
    # Then 1,828, because genitals stopped being worn by default and the 59
    # cage vertices weighted to `upperleg01.L` went with them. That 59 is the
    # same number the genitals block documents, and it is still asserted
    # directly by `app_genitals_hip_deforms_the_cage`, which now asks for the
    # proxy explicitly -- so nothing was lost by letting it leave this total.
    add_test(NAME app_body_pose_unit_moves_the_leg COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/bodyunits/rest.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/bodyunits/revived.obj
             -DEXPECT_DIFFERENT=1828
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_body_pose_unit_moves_the_leg PROPERTIES
        FIXTURES_REQUIRED "app_bpu_rest;app_bpu_revived")
    # An unknown unit is still refused, so a typo does not silently do nothing.
    add_test(NAME app_body_pose_unit_unknown_refused COMMAND makehuman
             --pose-unit NoSuchUnitAtAll=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/bodyunits/never.obj)
    set_tests_properties(app_body_pose_unit_unknown_refused PROPERTIES WILL_FAIL TRUE)

    # --help must say the body units exist. It said "A face pose unit" and
    # "the expression mixer's sixty sliders" for a whole chunk after --pose-unit
    # started accepting 121 -- a stale doc ships as confidently as stale code,
    # and nothing gated it.
    #
    # WHITESPACE-TOLERANT, and that is a fix rather than a relaxation. The
    # pattern used to be the literal "sixty-one pose the BODY", which coupled
    # this gate to Qt's help LAYOUT: Qt sizes the description column from the
    # longest option name, so adding `--background-transform` re-wrapped every
    # description and split the phrase across a line -- the claim was still
    # there, verbatim, and the gate still went red. That is the same defect as
    # a pixel gate that chrome can move: sensitive to something its subject
    # does not control. The claim is what is being tested, so the pattern now
    # spans the wrap.
    add_test(NAME app_help_mentions_body_pose_units COMMAND makehuman --help)
    set_tests_properties(app_help_mentions_body_pose_units PROPERTIES
        PASS_REGULAR_EXPRESSION "sixty-one pose the[ \t\r\n]+BODY")
    # --help must name the REAL default for a proxy slot. The wording is built
    # from `ProxySlot::defaultChoice` so it cannot drift from the table, and
    # this is what proves that wiring rather than assuming it: teeth are worn
    # by default and the flag has to say so, while a garment slot still reads
    # "none (default)" exactly once -- the shared wording produced the clumsy
    # "none (default), none, or ..." until it was special-cased.
    # THE ANIMATION CHOOSER. `--list-animations`' own help text has said "the
    # animations the Animation chooser offers" while there WAS no Animation
    # chooser -- the asset groups ran [Litsphere, Pose, Eyes, ...] with no such
    # entry. A help string promising a surface that does not exist.
    #
    # Pose and Animation are two ways into ONE `.bvh` slot, so the group must
    # exist beside Pose rather than replacing it.
    # ONE test, not two: an earlier pair ran the identical command in two
    # processes, one asserting `.*Animation` and one `.*Pose.*Animation`. The
    # second subsumes the first -- it proves the group exists AND that Pose
    # survives beside it, which is the whole claim.
    add_test(NAME app_animation_group_keeps_pose COMMAND makehuman
             --save ${CMAKE_CURRENT_BINARY_DIR}/anim_g2.mhm)
    set_tests_properties(app_animation_group_keeps_pose PROPERTIES
        PASS_REGULAR_EXPRESSION "asset groups: .*Pose.*Animation")
    # "None" is the Animation chooser's no-selection entry, and every other
    # chooser in this app spells that `none`. `loadPoseRig` only recognised
    # `rest`/`apose`/`a-pose`, so picking None went looking for a FILE called
    # "none" and failed with "cannot load pose: none: file not found" -- the
    # chooser's own empty state broken by the vocabulary it shares with its
    # siblings.
    add_test(NAME app_pose_none_is_the_rest_pose COMMAND makehuman
             --pose none --export ${CMAKE_CURRENT_BINARY_DIR}/anim_none.obj)
    set_tests_properties(app_pose_none_is_the_rest_pose PROPERTIES
        FAIL_REGULAR_EXPRESSION "cannot load pose")
    # `--animation`: the command line for what the Animation chooser does.
    #
    # `--pose walk1.bvh` REFUSES a multi-frame file, deliberately -- frame 0 of
    # a walk cycle is a plausible wrong pose, so a caller who means it says so.
    # A combo box has no way to say so, and neither did the command line, which
    # is why this exists. It is `--pose-frame 0 --rig-names auto` for one file,
    # named after the thing the user is choosing.
    #
    # MEASURED before it existed: with the default `native` naming the shipped
    # walks drive 0 of 179 bones and EVERY FRAME IS IDENTICAL; with makehuman1
    # naming frames 0 and 7 differ in 14,780 of 14,780 vertices. So "the
    # animation loaded" is not "the animation animates", and these gate the
    # second claim.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/anim)
    add_test(NAME app_animation_rest COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/anim/rest.obj)
    set_tests_properties(app_animation_rest PROPERTIES FIXTURES_SETUP app_anim_rest)
    add_test(NAME app_animation_frame0 COMMAND makehuman
             --animation walk1 --export ${CMAKE_CURRENT_BINARY_DIR}/anim/f0.obj)
    set_tests_properties(app_animation_frame0 PROPERTIES
        FAIL_REGULAR_EXPRESSION "cannot load pose;drives 0 of"
        FIXTURES_SETUP app_anim_f0)
    add_test(NAME app_animation_frame7 COMMAND makehuman
             --animation walk1 --pose-frame 7
             --export ${CMAKE_CURRENT_BINARY_DIR}/anim/f7.obj)
    set_tests_properties(app_animation_frame7 PROPERTIES FIXTURES_SETUP app_anim_f7)
    # It poses at all...
    add_test(NAME app_animation_poses_the_body COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/anim/rest.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/anim/f0.obj
             -DEXPECT_DIFFERENT=14580
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_animation_poses_the_body PROPERTIES
        FIXTURES_REQUIRED "app_anim_rest;app_anim_f0")
    # ...and DIFFERENTLY per frame, which is the claim that fails when the
    # naming is not resolved: every frame then lands on the same rest pose.
    add_test(NAME app_animation_frames_differ COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/anim/f0.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/anim/f7.obj
             -DEXPECT_DIFFERENT=14580
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_animation_frames_differ PROPERTIES
        FIXTURES_REQUIRED "app_anim_f0;app_anim_f7")
    # MUTUALLY EXCLUSIVE, and said so rather than silently preferring one.
    # Both flags fill the one .bvh slot; which one won would be invisible in
    # the output, so the pair is refused. This is the command-line half of what
    # the two combos do by clearing each other.
    add_test(NAME app_animation_and_pose_refused COMMAND makehuman
             --pose tpose --animation walk1
             --export ${CMAKE_CURRENT_BINARY_DIR}/anim/both.obj)
    set_tests_properties(app_animation_and_pose_refused PROPERTIES
        PASS_REGULAR_EXPRESSION "both fill the one .bvh slot")
    # A typo names the flag that lists the real ones, rather than failing with
    # "file not found" for something the user never typed as a path.
    add_test(NAME app_animation_unknown_named COMMAND makehuman
             --animation no_such_walk
             --export ${CMAKE_CURRENT_BINARY_DIR}/anim/bogus.obj)
    set_tests_properties(app_animation_unknown_named PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --animation \"no_such_walk\"; --list-animations")
    # --pose still REFUSES a multi-frame file. The new flag is an addition, not
    # a loosening: this is the guard that stops frame 0 of a walk being served
    # as though someone had asked for it.
    add_test(NAME app_pose_still_refuses_an_animation COMMAND makehuman
             --pose ${MH_DATA_DIR}/animations/walks/walk1.bvh
             --export ${CMAKE_CURRENT_BINARY_DIR}/anim/refused.obj)
    set_tests_properties(app_pose_still_refuses_an_animation PROPERTIES
        PASS_REGULAR_EXPRESSION "is an animation, not a pose")
    # `--pose tpose --save` HUNG -- 100% CPU for fourteen minutes, found as a
    # stray process rather than by any test. `livesInAnimations` decided whether
    # a pose file sits under data/animations by walking up its parents, and
    # `path("/").parent_path()` is `"/"` with `has_parent_path()` still true, so
    # anything OUTSIDE that directory looped forever. It is a prefix comparison
    # now.
    #
    # This is a TIMEOUT test: the assertion is that the command terminates. The
    # 60 s budget is ~40x the measured runtime, so it fails on a hang and not on
    # a slow machine.
    add_test(NAME app_pose_save_terminates COMMAND makehuman
             --pose tpose --save ${CMAKE_CURRENT_BINARY_DIR}/anim/term.mhm)
    set_tests_properties(app_pose_save_terminates PROPERTIES
        TIMEOUT 60
        PASS_REGULAR_EXPRESSION "wrote .*term.mhm")
    # ...and the detection it guards still answers both ways: a file under
    # data/animations resolves its naming per file, a pose does not.
    add_test(NAME app_pose_in_animations_dir_auto_names COMMAND makehuman
             --pose ${MH_DATA_DIR}/animations/walks/dance1.bvh
             --export ${CMAKE_CURRENT_BINARY_DIR}/anim/dance.obj)
    set_tests_properties(app_pose_in_animations_dir_auto_names PROPERTIES
        TIMEOUT 60
        PASS_REGULAR_EXPRESSION "dance1.bvh: read as makehuman1")
    add_test(NAME app_plain_pose_is_not_auto_named COMMAND makehuman
             --pose tpose --export ${CMAKE_CURRENT_BINARY_DIR}/anim/plain.obj)
    set_tests_properties(app_plain_pose_is_not_auto_named PROPERTIES
        TIMEOUT 60
        FAIL_REGULAR_EXPRESSION "read as")
    # THE ROUND TRIP, gating what this chunk actually changed.
    #
    # Before it, `--animation walk1 --save` then `--load` exited 1 with
    # "is an animation, not a pose" -- the app refused to open its own save
    # file. Now the reload resolves the naming per file and poses.
    #
    # It asserts the reload POSES, not that it reproduces the direct export
    # byte for byte. MEASURED: direct vs reload differs by 832 of 14,780
    # vertices for an animation AND 759 for a plain `--pose tpose`, so the
    # gap is the DOCUMENT's, pre-existing and the same order either way. An
    # identity assertion here would fail for poses too and would be pinning
    # someone else's bug to this feature.
    # WHAT A DOCUMENT MEANS TO THE CHOOSERS.
    #
    # The window's Open restores the modifiers, the camera and Smooth, and
    # nothing else: it never read the document's `pose`, `skeleton`,
    # `skinMaterial`, `litsphere`, `eyeMaterial` or proxy lines back into the
    # choosers, so opening a posed character landed at rest with the startup rig
    # and every combo still showing the previous character's pick.
    #
    # `applyChoice` already applies a choice AND sets its combo for every group,
    # so the missing half was only the mapping from document line to chooser id.
    # That mapping is what `--print-choices` prints, and it is the half a test
    # can reach: `applyLoaded` itself is GUI-only, which is how it drifted.
    # THE EXPRESSION, WHICH NEITHER SAVE PATH RECORDED.
    #
    # A character saved with an expression reopened with a neutral face: the
    # value was live in `expressionFileRef()` and reachable from the chooser,
    # and simply never written. Stored RELATIVE to data/ like `skinMaterial`
    # and `eyeMaterial` -- not as the absolute path the `pose` key writes for an
    # animation, which cannot open on another machine.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/expr)
    add_test(NAME app_expression_saved COMMAND makehuman
             --expression ${CMAKE_SOURCE_DIR}/data/expressions/happy.mhpose
             --save ${CMAKE_CURRENT_BINARY_DIR}/expr/e.mhm)
    set_tests_properties(app_expression_saved PROPERTIES
        TIMEOUT 60 FIXTURES_SETUP app_expr_saved)
    add_test(NAME app_expression_save_records_it COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/expr/e.mhm
             "-DKEYS=expression expressions/happy.mhpose"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_expression_save_records_it PROPERTIES
        FIXTURES_REQUIRED app_expr_saved)
    # ...and means the right thing to the chooser, whose ids are full paths.
    add_test(NAME app_expression_is_a_choice COMMAND makehuman
             --print-choices ${CMAKE_CURRENT_BINARY_DIR}/expr/e.mhm)
    set_tests_properties(app_expression_is_a_choice PROPERTIES
        FIXTURES_REQUIRED app_expr_saved
        PASS_REGULAR_EXPRESSION "Expression\t.*expressions/happy.mhpose")

    # An expression from OUTSIDE data/ is kept WHOLE, not relativised.
    #
    # Without the `..` guard, `std::filesystem::relative` happily produces
    # `../../../../../../private/tmp/...` -- a path that depends on where data/
    # happens to sit and breaks the moment the file moves. The mutation dropping
    # that guard passed every other test in this file, which is why this one
    # exists.
    file(COPY ${CMAKE_SOURCE_DIR}/data/expressions/sad.mhpose
         DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/expr)
    add_test(NAME app_expression_outside_data_saved COMMAND makehuman
             --expression ${CMAKE_CURRENT_BINARY_DIR}/expr/sad.mhpose
             --save ${CMAKE_CURRENT_BINARY_DIR}/expr/outside.mhm)
    set_tests_properties(app_expression_outside_data_saved PROPERTIES
        TIMEOUT 60 FIXTURES_SETUP app_expr_outside)
    add_test(NAME app_expression_outside_data_is_whole COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/expr/outside.mhm
             "-DKEYS=expression ${CMAKE_CURRENT_BINARY_DIR}/expr/sad.mhpose"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_expression_outside_data_is_whole PROPERTIES
        FIXTURES_REQUIRED app_expr_outside)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/choices)
    add_test(NAME app_choices_saved COMMAND makehuman
             --pose tpose --rig mixamo_superset --skin-material african_deep
             --save ${CMAKE_CURRENT_BINARY_DIR}/choices/c.mhm)
    set_tests_properties(app_choices_saved PROPERTIES
        TIMEOUT 60 FIXTURES_SETUP app_choices_saved)
    # Bare, for the exit code: PASS_REGULAR_EXPRESSION discards it.
    # `--pose tpose` is a NAME, not a path, and stays one: relativising a bare
    # name would turn it into something no reader resolves.
    add_test(NAME app_pose_name_stays_a_name COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/choices/c.mhm
             "-DKEYS=pose tpose"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_pose_name_stays_a_name PROPERTIES
        FIXTURES_REQUIRED app_choices_saved)
    add_test(NAME app_print_choices COMMAND makehuman
             --print-choices ${CMAKE_CURRENT_BINARY_DIR}/choices/c.mhm)
    set_tests_properties(app_print_choices PROPERTIES
        FIXTURES_REQUIRED app_choices_saved)
    add_test(NAME app_print_choices_has_the_rig COMMAND makehuman
             --print-choices ${CMAKE_CURRENT_BINARY_DIR}/choices/c.mhm)
    set_tests_properties(app_print_choices_has_the_rig PROPERTIES
        FIXTURES_REQUIRED app_choices_saved
        PASS_REGULAR_EXPRESSION "Skeleton\tmixamo_superset")
    # The stem, not the path the file stores (`skins/african_deep.mhmat`) --
    # the chooser's id is what `applyChoice` takes.
    add_test(NAME app_print_choices_has_the_skin COMMAND makehuman
             --print-choices ${CMAKE_CURRENT_BINARY_DIR}/choices/c.mhm)
    set_tests_properties(app_print_choices_has_the_skin PROPERTIES
        FIXTURES_REQUIRED app_choices_saved
        PASS_REGULAR_EXPRESSION "Skin material\tafrican_deep")
    # Pose and animation share one slot and one `pose` key, so the mapping has
    # to decide WHICH chooser a value belongs to. A `.bvh` under data/poses is
    # a Pose; one under data/animations is an Animation.
    add_test(NAME app_print_choices_pose_is_a_pose COMMAND makehuman
             --print-choices ${CMAKE_CURRENT_BINARY_DIR}/choices/c.mhm)
    set_tests_properties(app_print_choices_pose_is_a_pose PROPERTIES
        FIXTURES_REQUIRED app_choices_saved
        PASS_REGULAR_EXPRESSION "Pose\t"
        FAIL_REGULAR_EXPRESSION "Animation\t")
    add_test(NAME app_animation_saved COMMAND makehuman
             --animation walk1 --save ${CMAKE_CURRENT_BINARY_DIR}/anim/w.mhm)
    set_tests_properties(app_animation_saved PROPERTIES
        TIMEOUT 60 FIXTURES_SETUP app_anim_saved)
    # ...RELATIVE to data/, like `skinMaterial` and `expression`. It used to be
    # the absolute path of the machine that saved it, which no other machine can
    # open -- the one remaining spelling that was not portable.
    add_test(NAME app_animation_path_is_relative COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/anim/w.mhm
             "-DKEYS=pose animations/walks/walk1.bvh"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_animation_path_is_relative PROPERTIES
        FIXTURES_REQUIRED app_anim_saved)
    # And the READ side still knows it is an animation. `livesInAnimations`
    # decides Pose vs Animation by asking whether the path is under
    # data/animations, so a relative value has to be resolved before that test
    # or every saved animation comes back as a pose.
    add_test(NAME app_animation_relative_is_still_an_animation COMMAND makehuman
             --print-choices ${CMAKE_CURRENT_BINARY_DIR}/anim/w.mhm)
    set_tests_properties(app_animation_relative_is_still_an_animation PROPERTIES
        FIXTURES_REQUIRED app_anim_saved
        PASS_REGULAR_EXPRESSION "Animation\t.*walk1.bvh")
    add_test(NAME app_animation_save_records_the_path COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/anim/w.mhm
             "-DKEYS=walk1.bvh"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_animation_save_records_the_path PROPERTIES
        FIXTURES_REQUIRED app_anim_saved)
    # Reopening its own save file must not fail, and must not land at rest.
    add_test(NAME app_animation_reload_still_poses COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/anim/w.mhm
             --export ${CMAKE_CURRENT_BINARY_DIR}/anim/reload.obj)
    set_tests_properties(app_animation_reload_still_poses PROPERTIES
        TIMEOUT 60
        PASS_REGULAR_EXPRESSION "walk1.bvh: read as makehuman1"
        FAIL_REGULAR_EXPRESSION "cannot load pose;is an animation, not a pose;drives 0 of"
        FIXTURES_REQUIRED app_anim_saved
        FIXTURES_SETUP app_anim_reload)
    add_test(NAME app_animation_reload_is_not_the_rest_pose COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/anim/rest.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/anim/reload.obj
             -DEXPECT_DIFFERENT=14580
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_animation_reload_is_not_the_rest_pose PROPERTIES
        FIXTURES_REQUIRED "app_anim_rest;app_anim_reload")
    add_test(NAME app_help_names_the_proxy_defaults COMMAND makehuman --help)
    set_tests_properties(app_help_names_the_proxy_defaults PROPERTIES
        # A SHORT run, deliberately. Qt wraps the description column, and the
        # name column is sized by the longest option string -- so asserting the
        # whole "Teeth proxy to wear: teeth (default), none" phrase would turn
        # red when some unrelated long flag pushes `none` onto the next line.
        # "teeth (default)" carries the same claim and does not depend on
        # layout; the fail-regex still catches the doubled-none wording.
        PASS_REGULAR_EXPRESSION "teeth \\(default\\)"
        FAIL_REGULAR_EXPRESSION "none \\(default\\), none")

    # The workspace presets, resolved against the registry the APPLICATION
    # builds -- not the one tests/ui/test_ui.cpp constructs. That distinction is
    # the whole point: `shippedTasks()` went on describing a two-dock window for
    # a whole chunk after main.cpp registered a third category, and no unit test
    # could notice, because the fixture IS the thing that drifted.
    # This one carries no regex ON PURPOSE: PASS_REGULAR_EXPRESSION makes ctest
    # ignore the exit code entirely, so the two below would pass on a run that
    # printed the right line and then crashed.
    add_test(NAME app_list_workspaces COMMAND makehuman --list-workspaces)
    # The preset a user picks to work on materials must show the material
    # EDITOR, not only the Assets dock that holds the skin chooser. Tab-separated
    # so a preset naming just "Materials" cannot satisfy a match for "Material".
    add_test(NAME app_list_workspaces_materials_has_the_editor
             COMMAND makehuman --list-workspaces)
    set_tests_properties(app_list_workspaces_materials_has_the_editor PROPERTIES
        PASS_REGULAR_EXPRESSION "Materials\tMaterial\tMaterials")
    # Export resolves to nothing on purpose, and is the one preset that may.
    # Without this, "every preset lists the editor" could be satisfied by
    # quietly giving Export everything.
    add_test(NAME app_list_workspaces_export_is_empty COMMAND makehuman --list-workspaces)
    set_tests_properties(app_list_workspaces_export_is_empty PROPERTIES
        PASS_REGULAR_EXPRESSION "Export\t-")

    # The Pose chooser, and what it must NOT offer.
    #
    # `data/poses/benchmark.bvh` is a developer fixture: its own .meta says
    # `tag Developement` and "Benchmark pose used to test the rigging in extreme
    # condition". MEASURED by rendering it -- the hands interpenetrate, because
    # a BVH stores rotations and carries no collision. Nothing deforms it
    # wrongly; it is authored impossible. It must not sit in a chooser beside
    # the T-pose, and must stay reachable for the developer who wants it.
    add_test(NAME app_list_poses COMMAND makehuman --list-poses)
    add_test(NAME app_list_poses_labels_come_from_meta COMMAND makehuman --list-poses)
    # "T-pose" is the `name` field of tpose.meta. The filename stem would
    # prettify to "Tpose", so this fails if the label stops coming from the
    # metadata -- which is also what the reference labels it.
    set_tests_properties(app_list_poses_labels_come_from_meta PROPERTIES
        PASS_REGULAR_EXPRESSION "T-pose")
    # FAIL_REGULAR_EXPRESSION, not PASS: it still honours the exit code, where
    # PASS_REGULAR_EXPRESSION makes ctest ignore it entirely.
    add_test(NAME app_list_poses_hides_dev_fixture COMMAND makehuman --list-poses)
    set_tests_properties(app_list_poses_hides_dev_fixture PROPERTIES
        FAIL_REGULAR_EXPRESSION "Benchmark")
    # ...but when it IS the current pose it must be listed, or the chooser
    # displays one pose's label over another's geometry.
    add_test(NAME app_list_poses_shows_the_current_dev_pose COMMAND makehuman
             --list-poses --pose benchmark)
    set_tests_properties(app_list_poses_shows_the_current_dev_pose PROPERTIES
        PASS_REGULAR_EXPRESSION "Benchmark-pose")

    # A .mhm carrying a dev-fixture pose must still show that pose in the
    # chooser. The picker used to be built from the raw --pose flag while the
    # pose actually loaded came from the document, so a loaded file's pose was
    # merely mis-highlighted -- until dev fixtures became hideable, at which
    # point the entry vanished from the list while its geometry was on screen.
    add_test(NAME app_pose_saves_dev_fixture COMMAND makehuman
             --pose benchmark --save ${CMAKE_CURRENT_BINARY_DIR}/poses/bench.mhm)
    set_tests_properties(app_pose_saves_dev_fixture PROPERTIES FIXTURES_SETUP app_pose_benchmhm)
    add_test(NAME app_pose_loaded_document_pose_is_listed COMMAND makehuman
             --load ${CMAKE_CURRENT_BINARY_DIR}/poses/bench.mhm --list-poses)
    set_tests_properties(app_pose_loaded_document_pose_is_listed PROPERTIES
        FIXTURES_REQUIRED app_pose_benchmhm
        PASS_REGULAR_EXPRESSION "Benchmark-pose")

    # Any pose is reachable by stem, not just tpose. `--pose benchmark` used to
    # fail with "file not found" for a file sitting right beside tpose.bvh: the
    # GUI could reach it (the chooser stores full paths) and the CLI could not.
    add_test(NAME app_pose_stem_resolves COMMAND makehuman
             --pose benchmark --export ${CMAKE_CURRENT_BINARY_DIR}/poses/stem.obj)
    set_tests_properties(app_pose_stem_resolves PROPERTIES
        FIXTURES_SETUP app_pose_stem FAIL_REGULAR_EXPRESSION "cannot load pose")
    add_test(NAME app_pose_path_still_resolves COMMAND makehuman
             --pose ${MH_DATA_DIR}/poses/benchmark.bvh
             --export ${CMAKE_CURRENT_BINARY_DIR}/poses/path.obj)
    set_tests_properties(app_pose_path_still_resolves PROPERTIES
        FIXTURES_SETUP app_pose_path FAIL_REGULAR_EXPRESSION "cannot load pose")
    # The stem and the path must give the SAME body, or the shortcut resolves to
    # something other than the file it names.
    add_test(NAME app_pose_stem_equals_path COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/poses/stem.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/poses/path.obj
             -DEXPECT_DIFFERENT=0
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_pose_stem_equals_path PROPERTIES
        FIXTURES_REQUIRED "app_pose_stem;app_pose_path")
    # Case and hyphens are both ignored, so a user who types what they SEE in
    # the chooser ("T-pose") reaches the file. Without this the folding in
    # `namesPose` is untested -- measured: removing it left the whole suite
    # green, which is how a behaviour nobody asked for survives.
    add_test(NAME app_pose_stem_ignores_case COMMAND makehuman
             --pose T-Pose --export ${CMAKE_CURRENT_BINARY_DIR}/poses/case.obj)
    set_tests_properties(app_pose_stem_ignores_case PROPERTIES
        FIXTURES_SETUP app_pose_case FAIL_REGULAR_EXPRESSION "cannot load pose")
    add_test(NAME app_pose_stem_case_matches_exact COMMAND makehuman
             --pose tpose --export ${CMAKE_CURRENT_BINARY_DIR}/poses/exact.obj)
    set_tests_properties(app_pose_stem_case_matches_exact PROPERTIES
        FIXTURES_SETUP app_pose_exact FAIL_REGULAR_EXPRESSION "cannot load pose")
    add_test(NAME app_pose_case_is_the_same_pose COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/poses/case.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/poses/exact.obj
             -DEXPECT_DIFFERENT=0
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_pose_case_is_the_same_pose PROPERTIES
        FIXTURES_REQUIRED "app_pose_case;app_pose_exact")

    # A stem naming nothing is still an error, not a silent fall back to rest.
    add_test(NAME app_pose_unknown_stem_refused COMMAND makehuman
             --pose nosuchpose --export ${CMAKE_CURRENT_BINARY_DIR}/poses/never.obj)
    # Two tests, because the two halves cannot be asserted in one: setting
    # PASS_REGULAR_EXPRESSION makes ctest judge by the regex and IGNORE the exit
    # code, and WILL_FAIL then inverts that verdict -- so the two together make
    # finding the message a failure. Measured the hard way.
    #
    # This one pins the exit code...
    set_tests_properties(app_pose_unknown_stem_refused PROPERTIES WILL_FAIL TRUE)
    # ...and this one pins WHY, so the suite can tell "refused the unknown pose"
    # from "crashed" or "could not write the export".
    add_test(NAME app_pose_unknown_stem_says_why COMMAND makehuman
             --pose nosuchpose --export ${CMAKE_CURRENT_BINARY_DIR}/poses/never2.obj)
    set_tests_properties(app_pose_unknown_stem_says_why PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot load pose: nosuchpose")

    # And the filename WITH its extension resolves too: the stem shortcut
    # teaches that pose names work, and the filename is the natural next guess.
    add_test(NAME app_pose_filename_with_extension COMMAND makehuman
             --pose tpose.bvh --export ${CMAKE_CURRENT_BINARY_DIR}/poses/ext.obj)
    set_tests_properties(app_pose_filename_with_extension PROPERTIES
        FAIL_REGULAR_EXPRESSION "cannot load pose")

    # --rig-names on IMPORT. The shipped data/animations/ walks are MakeHuman
    # 1.x exports: they name joints of the old MakeHuman skeleton and match 0
    # bones of this rig, so `--pose walk1.bvh` has always loaded successfully
    # and produced the rest pose. These tests pin BOTH halves -- that it used to
    # do nothing, and that with the table it does something.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rignames)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/poses)
    set(WALK ${MH_DATA_DIR}/animations/walks/walk1.bvh)
    # `--rig-names native` is now EXPLICIT here. These two fixtures exist to be
    # the UNTARGETED case, and a file under data/animations resolves its naming
    # per file by default since the Animation chooser landed -- so without the
    # flag they stopped being untargeted, the two frames started differing, and
    # both assertions below failed. Saying `native` out loud is what keeps them
    # measuring what their comments claim.
    add_test(NAME app_rig_names_native_f0 COMMAND makehuman
             --rig-names native
             --pose ${WALK} --pose-frame 0
             --export ${CMAKE_CURRENT_BINARY_DIR}/rignames/nat0.obj)
    set_tests_properties(app_rig_names_native_f0 PROPERTIES FIXTURES_SETUP app_rn_nat0)
    add_test(NAME app_rig_names_native_f7 COMMAND makehuman
             --rig-names native
             --pose ${WALK} --pose-frame 7
             --export ${CMAKE_CURRENT_BINARY_DIR}/rignames/nat7.obj)
    set_tests_properties(app_rig_names_native_f7 PROPERTIES FIXTURES_SETUP app_rn_nat7)
    add_test(NAME app_rig_names_mh1_f0 COMMAND makehuman
             --pose ${WALK} --pose-frame 0 --rig-names makehuman1
             --export ${CMAKE_CURRENT_BINARY_DIR}/rignames/mh0.obj)
    # The warning must be GONE, which is the whole claim of this chunk. Measured
    # as a mutation: forcing the table lookup to return nothing brings it back,
    # and nothing else in the suite notices.
    set_tests_properties(app_rig_names_mh1_f0 PROPERTIES
        FIXTURES_SETUP app_rn_mh0 FAIL_REGULAR_EXPRESSION "drives 0 of")
    add_test(NAME app_rig_names_mh1_f7 COMMAND makehuman
             --pose ${WALK} --pose-frame 7 --rig-names makehuman1
             --export ${CMAKE_CURRENT_BINARY_DIR}/rignames/mh7.obj)
    set_tests_properties(app_rig_names_mh1_f7 PROPERTIES
        FIXTURES_SETUP app_rn_mh7 FAIL_REGULAR_EXPRESSION "drives 0 of")

    # THE regression, and the premise for everything below it: untargeted, two
    # different frames of a 14-frame walk give the SAME mesh, because every bone
    # is left at identity. MEASURED 0 of 14,444.
    add_test(NAME app_rig_names_native_frames_are_identical COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/rignames/nat0.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/rignames/nat7.obj
             -DEXPECT_DIFFERENT=0
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_rig_names_native_frames_are_identical PROPERTIES
        FIXTURES_REQUIRED "app_rn_nat0;app_rn_nat7")
    # And retargeted, they stop being identical. MEASURED 14,580 of 14,580 --
    # a walk moves the root, so every vertex moves.
    #
    # The total is the whole character, so it moved when genitals stopped
    # being worn by default: 14,780 -> 14,580, the proxy's 200 vertices. (The
    # comment here read 14,444 while the assertion said 14,780 -- stale from
    # an earlier character, and corrected rather than carried forward.)
    add_test(NAME app_rig_names_mh1_frames_differ COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/rignames/mh0.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/rignames/mh7.obj
             -DEXPECT_DIFFERENT=14580
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_rig_names_mh1_frames_differ PROPERTIES
        FIXTURES_REQUIRED "app_rn_mh0;app_rn_mh7")
    # The flag changes the mesh at all -- without this the two tests above are
    # satisfied by any pair of files that happen to differ from each other.
    add_test(NAME app_rig_names_changes_the_pose COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/rignames/nat7.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/rignames/mh7.obj
             -DEXPECT_DIFFERENT=14580
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_rig_names_changes_the_pose PROPERTIES
        FIXTURES_REQUIRED "app_rn_nat7;app_rn_mh7")
    # A naming that does not ship must be refused by name, not fall back to
    # "native" and silently pose nothing.
    add_test(NAME app_rig_names_rejects_unknown COMMAND makehuman
             --pose ${WALK} --pose-frame 0 --rig-names nosuchrig
             --export ${CMAKE_CURRENT_BINARY_DIR}/rignames/never.obj)
    set_tests_properties(app_rig_names_rejects_unknown PROPERTIES WILL_FAIL TRUE)

    add_test(NAME app_pose_unit_neutral COMMAND makehuman
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/neutral.obj)
    set_tests_properties(app_pose_unit_neutral PROPERTIES FIXTURES_SETUP app_punit_neutral)
    add_test(NAME app_pose_unit_brow COMMAND makehuman
             --pose-unit LeftBrowDown=1.0
             --export ${CMAKE_CURRENT_BINARY_DIR}/punit/brow.obj)
    set_tests_properties(app_pose_unit_brow PROPERTIES FIXTURES_SETUP app_punit_brow)
    # The premise: a unit actually moves the face. Without this every assertion
    # below passes on a flag that parsed and did nothing.
    #
    # 425 before the target stack became ordered, 407 after. The 18 that went
    # away were never moved by the pose unit -- they differed because the two
    # exports accumulated their morphs in different orders and landed either
    # side of the writer's fourth decimal. Re-measured; debug and release agree.
    add_test(NAME app_pose_unit_moves_the_face COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/punit/neutral.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/punit/brow.obj
             -DEXPECT_DIFFERENT=407
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
    # 110 before the target stack became ordered, 72 after. The largest
    # proportional drop of the five, because this unit moves the least to begin
    # with -- which is exactly where ULP noise across the fourth decimal weighs
    # most. 72 still says "this moves the face" against the 0 it would read if
    # the unit did nothing. Re-measured; debug and release agree exactly.
    add_test(NAME app_pose_unit_au_free_unit_moves COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/punit/neutral.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/punit/tongue.obj
             -DEXPECT_DIFFERENT=72
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
    # --background-transform must reach `--render`, not only the viewport. This
    # is the trap `overBackground` was reshaped to make unwritable: it used to
    # compute its own cover-fit, so a transform applied to the viewport left the
    # production render compositing the UNtransformed image, with no error and
    # nothing on screen to notice.
    #
    # MEASURED: the transform moves 249,402 of 1,048,576 pixels (23.8%) -- less
    # than the viewport's 73% because the figure covers much of the frame and
    # only the exposed backdrop can move. The bound is 20,000, 12x under that.
    #
    # UNLIKE the viewport gates, this needs no allowance for CI's screen:
    # `--render` is a fixed 1024x1024 everywhere, which the sibling gate above
    # already asserts.
    add_test(NAME app_background_render_transformed COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/bg/with_tf.png
             --background ${CMAKE_CURRENT_BINARY_DIR}/bg/backdrop.png
             --background-transform 0.3,0.1,2)
    set_tests_properties(app_background_render_transformed PROPERTIES
        FIXTURES_SETUP app_bg_with_tf
        FIXTURES_REQUIRED app_bg_backdrop)
    add_test(NAME app_background_transform_reaches_the_render COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/bg/with_tf.png
             ${CMAKE_CURRENT_BINARY_DIR}/bg/with.png --min-differing 20000)
    set_tests_properties(app_background_transform_reaches_the_render PROPERTIES
        FIXTURES_REQUIRED "app_bg_with_tf;app_bg_with")

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
    # NARROWED 2026-09-17, not relaxed. `--background` used to require
    # `--render`; it now also feeds the VIEWPORT, so the rule became "it needs a
    # frame to sit behind". This test's premise is unchanged -- an --export has
    # no frame, and is still refused -- so only the message it expects moved.
    add_test(NAME app_background_needs_a_frame COMMAND makehuman
             --background ${CMAKE_CURRENT_BINARY_DIR}/bg/backdrop.png
             --export ${CMAKE_CURRENT_BINARY_DIR}/bg/never.obj)
    set_tests_properties(app_background_needs_a_frame PROPERTIES
        FIXTURES_REQUIRED app_bg_backdrop
        PASS_REGULAR_EXPRESSION "--background needs a frame to sit behind")

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
    # Measured: 2,333 of 14,580 vertices. Frame 40 is a wide-open jaw, which is
    # visible in a render -- the number is here so a loader that quietly
    # returned frame 0 for every index fails instead of passing.
    #
    # Was 2,265 of 14,444 before teeth became default-on. Both halves of that
    # move are explained and neither was accepted on faith: the total grew by
    # the teeth proxy's 136 vertices, and the DIFFERING count grew by exactly
    # 68 -- the lower arch, weighted to `jaw`, following the jaw down while the
    # upper arch on `head` stays put. 68 of 136 is the same split
    # app_teeth_lower_follows_jaw measures independently from a FACS jaw drop,
    # so two separately written tests agree about the rigging.
    # 2,333 before the target stack became ordered, 2,329 after: FOUR vertices
    # used to differ only because the two exports accumulated their morphs in
    # different orders, landing either side of the writer's fourth decimal.
    # Re-measured, not adjusted to taste -- debug and release now agree on
    # 2,329 exactly, which they had no reason to before.
    add_test(NAME app_pose_frame_differs COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/pframe/f0.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/pframe/f40.obj
             -DEXPECT_DIFFERENT=2329
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_verts_differ.cmake)
    set_tests_properties(app_pose_frame_differs PROPERTIES
        FIXTURES_REQUIRED "app_pframe_0;app_pframe_40")
    # A pose that drives NOTHING must say so. The shipped walks name 75 joints
    # of the old MakeHuman skeleton and match 0 of this rig's 163, so the app
    # posed nothing and reported success -- which is how the content problem
    # stayed folklore long enough to be rediscovered twice.
    # NOT a refusal: the pose still loads and the exit code is still 0, because
    # `app_pose_frame_*` and the rig tests pin that behaviour deliberately. The
    # change is that it is now VISIBLE.
    # 179, not 163: the application defaults to `mixamo_superset`, and the walks
    # match none of either rig. The first version of this test said 163 -- the
    # DEFAULT rig's count, which is what the unit test uses -- and failed.
    # `--rig-names native` is now REQUIRED to reach this warning with walk1.
    # A file under data/animations resolves its naming per file by default, so
    # the walk drives 59 of 179 rather than 0 -- the behaviour improved and this
    # fixture stopped being an example of "drives nothing". Pinning the warning
    # on the explicit native reading keeps the rule gated while the default path
    # gets its own assertion below.
    add_test(NAME app_pose_drives_nothing_says_so COMMAND makehuman
             --rig-names native
             --pose ${MH_DATA_DIR}/animations/walks/walk1.bvh --pose-frame 0
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/drives.obj)
    set_tests_properties(app_pose_drives_nothing_says_so PROPERTIES
        PASS_REGULAR_EXPRESSION "drives 0 of 179 bones")
    # ...and by DEFAULT the same file now drives the rig instead of warning.
    add_test(NAME app_pose_in_animations_drives_by_default COMMAND makehuman
             --pose ${MH_DATA_DIR}/animations/walks/walk1.bvh --pose-frame 0
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/drives_auto.obj)
    set_tests_properties(app_pose_in_animations_drives_by_default PROPERTIES
        TIMEOUT 60
        PASS_REGULAR_EXPRESSION "walk1.bvh: read as makehuman1 \\(59 of 179"
        FAIL_REGULAR_EXPRESSION "drives 0 of")
    # ...and a pose that DOES drive the rig must stay quiet, or the warning is
    # noise that gets filtered out and stops being read.
    add_test(NAME app_pose_that_drives_is_quiet COMMAND makehuman
             --pose ${MH_DATA_DIR}/poseunits/face-poseunits.bvh --pose-frame 0
             --export ${CMAKE_CURRENT_BINARY_DIR}/pframe/quiet.obj)
    set_tests_properties(app_pose_that_drives_is_quiet PROPERTIES
        FAIL_REGULAR_EXPRESSION "drives 0 of")

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
    # And the premise, measured: 1,902 of 14,444 vertices. Counting differing
    # TEXT lines says more -- midline vertices come out `0.0000` in one export
    # and `-0.0000` in the other, which is why the comparison is done in fixed
    # point.
    #
    # It read 1,949 until the target stack became ordered. Those 47 differed
    # only because the two exports accumulated their morphs in different
    # orders and landed either side of the fourth decimal; the expression
    # never moved them. Re-measured rather than adjusted: debug and release
    # now agree on 1,902 exactly.
    add_test(NAME app_save_expression_moves_the_face COMMAND ${CMAKE_COMMAND}
             -DA=${CMAKE_CURRENT_BINARY_DIR}/expr/neutral.obj
             -DB=${CMAKE_CURRENT_BINARY_DIR}/expr/from_facs.obj
             -DEXPECT_DIFFERENT=1902
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
    # undecimated. Measured per group, plus the 96 teeth worn by default and
    # decimated by neither: full = body 13,378 + eyes 1,020 + 96 = 14,494,
    # quarter = body 6,688 + eyes 1,020 + 96 = 7,804. (14,676 and 7,986 until
    # genitals stopped being default-on and took their 182 faces out of both.)
    # A worn proxy is a separate mesh
    # with its own fit and its own mask, and eyes are the last geometry anyone
    # wants to simplify, so `--decimate` reduces the BODY. The total is the
    # better gate for it either way: it moves if the body is wrong AND if the
    # eyes are ever decimated by accident.
    add_test(NAME app_decimate_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/lod_quarter/body.obj -DEXPECT=7804
             -P ${CMAKE_CURRENT_SOURCE_DIR}/obj_body_faces.cmake)
    set_tests_properties(app_decimate_obj_faces PROPERTIES
        FIXTURES_REQUIRED app_lod_quarter_obj)

    # The undecimated total, from the same helper, so the pair is a comparison
    # rather than one number in isolation.
    add_test(NAME app_decimate_full_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/lod_full/body.obj -DEXPECT=14494
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
    # was the second one that was false last time. 54,628 = 53,512 subdivided
    # body + 1,020 eyes + 96 teeth -- both proxies are separate meshes and
    # NEITHER is subdivided. (54,810 until genitals stopped being default-on.)
    add_test(NAME app_subdivide_obj_faces COMMAND ${CMAKE_COMMAND}
             -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/app_subdivide.obj -DEXPECT=54628
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
             # 3: body + worn proxy + the default teeth. One binding per
             # SKINNED mesh. Was 4 while genitals were worn by default; the
             # property under test is one-binding-per-mesh, not the number.
             --count 3)
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

        # `cmake --install` has to produce something that RUNS. There were no
        # install rules at all until 2026-09-23 -- the only way to get a
        # runnable copy out of a build tree was the `dmg` target -- and owner
        # directive 13.11 puts exactly this case first: "everyone will compile
        # on their own hardware".
        #
        # WHAT THIS PROVES, and what it leans on. It installs to a scratch
        # prefix, checks the tree holds the sentinel `resolveDataDir` searches
        # for, and then RUNS the installed binary on a command that needs the
        # modifier data. That the binary prefers its own bundle over the
        # compiled default is not re-proved here -- `test_data_dir.cpp`'s
        # "a macOS bundle's Resources are found" owns that -- so the chain is
        # those two together, which is why this one installs rather than just
        # listing files.
        add_test(NAME app_install_tree COMMAND ${CMAKE_COMMAND}
                 --install ${CMAKE_BINARY_DIR}
                 --prefix ${CMAKE_CURRENT_BINARY_DIR}/installed)
        set_tests_properties(app_install_tree PROPERTIES FIXTURES_SETUP app_installed)
        # The sentinel, by the same path `resolveDataDir` looks for it under.
        add_test(NAME app_install_carries_the_data COMMAND ${CMAKE_COMMAND}
                 -DFILE=${CMAKE_CURRENT_BINARY_DIR}/installed/makehuman.app/Contents/Resources/data/3dobjs/base.obj
                 "-DKEYS=v "
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
        set_tests_properties(app_install_carries_the_data PROPERTIES
            FIXTURES_REQUIRED app_installed)
        # ...and the licences travel with it, as section 6 requires of anything
        # distributed.
        add_test(NAME app_install_carries_the_licence COMMAND ${CMAKE_COMMAND}
                 -DFILE=${CMAKE_CURRENT_BINARY_DIR}/installed/makehuman.app/Contents/Resources/LICENSING.md
                 "-DKEYS=authoritative inventory"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
        set_tests_properties(app_install_carries_the_licence PROPERTIES
            FIXTURES_REQUIRED app_installed)
        # The installed copy RUNS. Listing files proves a copy happened; this
        # proves the result starts and reads modifier data.
        #
        # MEASURED LIMIT, because the control said so: with the data install
        # rule removed, THIS TEST STILL PASSES. The binary falls back to the
        # build machine's source tree and works perfectly on this machine and
        # nowhere else -- the exact failure `audit_runtime_paths.py` exists
        # about. So `app_install_carries_the_data` above is the load-bearing
        # gate of the pair, and this one is here to catch a tree that copies
        # everything and still cannot start.
        add_test(NAME app_installed_runs
                 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/installed/makehuman.app/Contents/MacOS/makehuman
                 --list-presets)
        set_tests_properties(app_installed_runs PROPERTIES
            PASS_REGULAR_EXPRESSION "Six-pack\tmacrodetails-universal/Muscle"
            FIXTURES_REQUIRED app_installed)

        # The licence texts must BE in the bundle, not merely referenced by it.
        # `MACOSX_BUNDLE_COPYRIGHT` says "See LICENSING.md" and LICENSING.md
        # section 3 claims the bundle "includes this notice"; both were false
        # until the bundle actually carried them, and a claim in a licence file
        # is exactly the kind that has to be enforced rather than trusted.
        # Each gate asks for text that is only in ITS file, so a build that
        # copied one licence four times, or the wrong one, still fails. A
        # shared key does not do that -- measured: "Licence" appears in
        # LICENSING.md and in none of the other three.
        add_test(NAME app_bundle_licence_overview COMMAND ${CMAKE_COMMAND}
                 -DFILE=$<TARGET_BUNDLE_CONTENT_DIR:makehuman>/Resources/LICENSE.md
                 "-DKEYS=LICENSE"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
        add_test(NAME app_bundle_licence_code COMMAND ${CMAKE_COMMAND}
                 -DFILE=$<TARGET_BUNDLE_CONTENT_DIR:makehuman>/Resources/LICENSE.CODE.md
                 "-DKEYS=GNU Affero General Public License"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
        add_test(NAME app_bundle_licence_assets COMMAND ${CMAKE_COMMAND}
                 -DFILE=$<TARGET_BUNDLE_CONTENT_DIR:makehuman>/Resources/LICENSE.ASSETS.md
                 "-DKEYS=Creative Commons CC0 1.0 Universal"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
        add_test(NAME app_bundle_licence_inventory COMMAND ${CMAKE_COMMAND}
                 -DFILE=$<TARGET_BUNDLE_CONTENT_DIR:makehuman>/Resources/LICENSING.md
                 "-DKEYS=authoritative inventory"
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

    # ...and the same for a PROXY slot, which until now was the one family that
    # did NOT say what is available.
    #
    # MEASURED 2026-09-24 across every option that rejects a value: `--rig`,
    # `--skin-material` and `--eye-colour` all printed "available: ...", while
    # all seven proxy slots -- hair, clothes, eyes, teeth, tongue, genitals,
    # eyelashes -- printed only "wearing none". That cost little when a slot
    # held one asset; `data/hair` now holds FOUR, and a typo left the user with
    # no way to find them short of listing the directory.
    #
    # `locs` specifically, because it is the newest and the one a stale message
    # would omit: the list is built from the picker's own choices, so a style
    # that ships without reaching the picker fails here.
    add_test(NAME app_proxy_unknown_lists_choices COMMAND makehuman
             --hair no_such_style
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_proxy_unknown.mhm)
    set_tests_properties(app_proxy_unknown_lists_choices PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown --hair .*available:.*locs")
    # ...and a value that IS known stays silent, or the message is noise rather
    # than a signal -- the same reason `app_mhm_is_quiet_when_nothing_is_dropped`
    # exists.
    # A mistyped pose is pointed at the list, and the message does not stutter.
    #
    # Two defects, both found by READING the output of `--pose zzbogus` rather
    # than by inspecting code:
    #  1. `--animation` and `--preset` say "--list-X prints them"; `--pose` did
    #     not, although `--list-poses` exists. MEASURED across every option
    #     that rejects a value.
    #  2. The message was "zzbogus: file not found (zzbogus: file not found)".
    #     `PoseUnits.cpp` put the BVH error's already-formatted `message()`
    #     into `detail`, and `PoseUnitsError::message()` wrapped the same shape
    #     around it again. FAIL_REGULAR_EXPRESSION pins the stutter, because a
    #     PASS on the new text alone would still pass with the old text present.
    #
    # The pointer is tied to the NotFound kind, so a malformed or unreadable
    # BVH -- which listing the poses would not help with -- keeps the bare
    # message.
    add_test(NAME app_pose_typo_points_at_the_list COMMAND makehuman
             --pose no_such_pose
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_pose_typo.mhm)
    set_tests_properties(app_pose_typo_points_at_the_list PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot load pose:.*--list-poses shows them"
        FAIL_REGULAR_EXPRESSION "file not found \\(")

    add_test(NAME app_proxy_known_is_quiet COMMAND makehuman
             --hair locs
             --save ${CMAKE_CURRENT_BINARY_DIR}/app_proxy_known.mhm)
    set_tests_properties(app_proxy_known_is_quiet PROPERTIES
        FAIL_REGULAR_EXPRESSION "unknown --hair")

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
        FAIL_REGULAR_EXPRESSION "nothing to draw;cannot write"
        FIXTURES_SETUP app_render_png)

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
    # Eleven groups: Litsphere, Pose, Eyes, Teeth, Tongue, Hair, Clothes,
    # Eyelashes, Skin material, Eye colour, Skeleton.
    add_test(NAME app_eye_colour COMMAND makehuman
             --eye-colour green
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_eye_colour.png)
    set_tests_properties(app_eye_colour PROPERTIES
        PASS_REGULAR_EXPRESSION
            # The group NAMES sit between the count and the counts now; both halves
            # of what this case cared about are still pinned.
            "asset groups: 14 \\[.*\\] \\(skin materials: 9, rigs: 2\\)"
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

    # `--genitals genitals` rather than letting the count fall to 3. This
    # check exists to catch an ANATOMY SLOT that starts blending, and genitals
    # is the slot most likely to: it is the only proxy carrying
    # `autoBlendSkin`. Dropping it when the default moved would have quietly
    # narrowed the very coverage the comment below claims. The default render
    # path is exercised by plenty of other tests.
    add_test(NAME app_eye_is_blended COMMAND makehuman --genitals genitals
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_blend.png)
    set_tests_properties(app_eye_is_blended PROPERTIES
        # 4 meshes -- body, eyes, teeth, genitals -- but still ONE
        # blended. That is the assertion that matters: the anatomy slots are
        # opaque, so a change that started blending one would push this to 2
        # and fail here.
        PASS_REGULAR_EXPRESSION "scene: 4 meshes \\(1 blended\\)"
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

    # --- the material editor (M8) -----------------------------------------
    #
    # The port could READ a .mhmat and WRITE one and had no way to CHANGE one.
    # `--set-material` is the editor's headless surface: the panel edits through
    # the same `mh::core::setMaterialProperty`, so what is gated here is what
    # the panel does.

    add_test(NAME app_set_material_writes_the_edit COMMAND makehuman
             --skin-material african_deep
             --set-material diffuseColor=0.8,0.1,0.1
             --set-material shininess=0.31
             --save-material ${CMAKE_CURRENT_BINARY_DIR}/app_edited_skin.mhmat)
    set_tests_properties(app_set_material_writes_the_edit PROPERTIES
        PASS_REGULAR_EXPRESSION "wrote .*app_edited_skin\\.mhmat \\(african_deep, 2 edits\\)"
        FAIL_REGULAR_EXPRESSION "cannot save"
        FIXTURES_SETUP app_edited_skin_mhmat)

    # BOTH edits, and the shipped values they replaced must be GONE -- a writer
    # that appended a second diffuseColor line would satisfy a presence-only
    # check and produce a file whose last line wins. african_deep ships
    # `diffuseColor 1.0 1.0 1.0` and `shininess 0.42`; 0.31 is chosen because it
    # is neither that nor the struct default (0.2), so the assertion cannot pass
    # on an edit that did nothing.
    add_test(NAME app_set_material_reaches_the_file COMMAND ${CMAKE_COMMAND}
             -DFILE=${CMAKE_CURRENT_BINARY_DIR}/app_edited_skin.mhmat
             "-DKEYS=diffuseColor 0.8 0.1 0.1;shininess 0.31"
             "-DABSENT=diffuseColor 1;shininess 0.42"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.cmake)
    set_tests_properties(app_set_material_reaches_the_file PROPERTIES
        FIXTURES_REQUIRED app_edited_skin_mhmat)

    # A typo must be REFUSED. The parser ignores an unknown key on purpose --
    # community assets carry keys this build has never seen -- and an editor
    # inheriting that would let `--set-material diffusColor=1,0,0` change
    # nothing and say nothing, which is how the Expression chooser shipped
    # doing nothing.
    add_test(NAME app_set_material_unknown_property_refused COMMAND makehuman
             --set-material diffusColor=1,0,0
             --save-material ${CMAKE_CURRENT_BINARY_DIR}/app_never_written.mhmat)
    set_tests_properties(app_set_material_unknown_property_refused PROPERTIES
        PASS_REGULAR_EXPRESSION "unknown material property 'diffusColor'"
        FAIL_REGULAR_EXPRESSION "wrote ")

    add_test(NAME app_set_material_bad_value_refused COMMAND makehuman
             --set-material shininess=abc
             --save-material ${CMAKE_CURRENT_BINARY_DIR}/app_never_written.mhmat)
    set_tests_properties(app_set_material_bad_value_refused PROPERTIES
        PASS_REGULAR_EXPRESSION "expected a number, got 'abc'"
        FAIL_REGULAR_EXPRESSION "wrote ")

    # The edit must reach the SCREEN, not just the file. Under --shading pbr
    # diffuseColor is the base colour (`ViewportMaps::baseColour`), so a red
    # diffuse renders a red body. MEASURED: 85,359 of 1,048,576 pixels differ.
    # The floor is set well under that and well over the 0-3 px two renders of
    # one scene differ by, so it fails if the edit stops reaching the viewport
    # and does not flake on MSAA resolve order.
    add_test(NAME app_render_pbr_edited COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_render_pbr_edited.png
             --shading pbr --set-material diffuseColor=0.8,0.1,0.1)
    set_tests_properties(app_render_pbr_edited PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered .*app_render_pbr_edited\\.png \\(1024x1024, pbr\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_pbr_edited_png)

    add_test(NAME app_set_material_reaches_the_viewport COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/app_render_pbr.png
             ${CMAKE_CURRENT_BINARY_DIR}/app_render_pbr_edited.png
             --min-differing 40000)
    set_tests_properties(app_set_material_reaches_the_viewport PROPERTIES
        FIXTURES_REQUIRED "app_pbr_default_png;app_pbr_edited_png")

    # And the other half of that claim, which is why the gate above says `pbr`:
    # under the DEFAULT litsphere shading the matcap shades by NORMAL and the
    # material's diffuseColor reaches nothing at all. MEASURED: 0 of 1,048,576
    # pixels differ. A render gate written against the default shading would
    # have been decorative -- it would pass with --set-material deleted.
    #
    # Pinned rather than left as a comment: if the litsphere path ever starts
    # honouring diffuseColor, this fails and somebody decides on purpose.
    add_test(NAME app_render_litsphere_edited COMMAND makehuman
             --render ${CMAKE_CURRENT_BINARY_DIR}/app_render_litsphere_edited.png --transparent
             --set-material diffuseColor=0.8,0.1,0.1)
    set_tests_properties(app_render_litsphere_edited PROPERTIES
        PASS_REGULAR_EXPRESSION
            "rendered .*app_render_litsphere_edited\\.png \\(1024x1024, transparent, litsphere\\)"
        SKIP_REGULAR_EXPRESSION "cannot render"
        FIXTURES_SETUP app_litsphere_edited_png)

    add_test(NAME app_matcap_ignores_the_diffuse_colour COMMAND mh_png_compare
             ${CMAKE_CURRENT_BINARY_DIR}/app_render.png
             ${CMAKE_CURRENT_BINARY_DIR}/app_render_litsphere_edited.png
             --max-differing 8)
    set_tests_properties(app_matcap_ignores_the_diffuse_colour PROPERTIES
        FIXTURES_REQUIRED "app_render_png;app_litsphere_edited_png")

    set_tests_properties(ui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${MH_TEST_TMPDIR}")
    set_tests_properties(ui_icons_hidpi PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2;TMPDIR=${MH_TEST_TMPDIR}")
endif()

# The Ericsson-SLA exclusion is a LICENCE boundary, and a comment cannot hold
# one. Registered only when libktx is actually built: with MH_WITH_KTX2=OFF
# there is no archive to inspect, and a test that "passes" with nothing to
# check is worse than an absent one. See third_party/licenses/README.md.
if(MH_WITH_KTX2)
    add_test(NAME ktx_excludes_ericsson_sla COMMAND ${CMAKE_COMMAND}
             "-DLIBRARY=$<TARGET_FILE:ktx>"
             "-DNM=${CMAKE_NM}"
             -P ${CMAKE_CURRENT_SOURCE_DIR}/ktx_excludes_ericsson_sla.cmake)
endif()

# ---------------------------------------------------------------------------
# KTX2 / BasisLZ through libktx, decoded by an INDEPENDENT transcoder.
#
# Registered only under MH_WITH_KTX2: with no encoder there is nothing to
# check, and a test that passes with nothing to check is worse than absent.
#
# The decode leg is `basisu -unpack`, NOT our code. Our own transcoder reading
# our own encoder would only prove we are self-consistent, and self-consistent
# is not correct -- that is the whole reason this project keeps the basisu CLI
# as an oracle. Oracle at test time, our code at runtime.
#
# Nothing is written into data/; everything lands in the build directory.
#
# `MH_HAVE_RENDER` as well as `MH_WITH_KTX2`, because `mh_ktx2_encode` links
# Qt6::Gui for QImage and is therefore built inside `if(MH_HAVE_RENDER)` at
# the top of this file. Registering these without it produced three ***Not
# Run entries in CI -- "Could not find executable mh_ktx2_encode" -- which is
# a red suite reporting a missing TOOL rather than a broken encoder. The
# licence gate above deliberately stays outside this guard: it inspects the
# built library with `nm` and needs no Qt at all, so it must still run in a
# no-Qt build. That is the half that actually enforces the ETCSLA boundary.
if(MH_WITH_KTX2 AND MH_HAVE_RENDER)
    add_test(NAME app_ktx2_encode COMMAND mh_ktx2_encode
             ${MH_DATA_DIR}/eyes/materials/brown_eye.png
             ${CMAKE_CURRENT_BINARY_DIR}/brown_eye.ktx2)
    set_tests_properties(app_ktx2_encode PROPERTIES FIXTURES_SETUP app_ktx2_encode)

    # An EXTERNAL decoder reading what we wrote. If basisu cannot unpack it,
    # the file is not conformant however happily our own code round-trips it.
    add_test(NAME app_ktx2_basisu_unpacks_it COMMAND basisu -unpack -no_ktx
             ${CMAKE_CURRENT_BINARY_DIR}/brown_eye.ktx2)
    set_tests_properties(app_ktx2_basisu_unpacks_it PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        FIXTURES_REQUIRED app_ktx2_encode
        FIXTURES_SETUP app_ktx2_unpacked)

    # ...and what it unpacked has to still look like the photograph.
    #
    # "basisu exited 0" only says the container parsed. A lossy codec needs a
    # quality number, so the image basisu decoded is compared against the
    # original. `_rgba_RGBA32_` is the full-precision decode -- the other ~30
    # PNGs `-unpack` writes are transcodes to specific GPU formats and carry
    # that format's loss on top of ETC1S's.
    #
    # THE NUMBER IS MEASURED, NOT CHOSEN: 38.83 dB, read off this exact pair.
    # The bar is 38.5, the same ~0.3 dB of headroom the 39.9 bar keeps below
    # its 40.20 baseline.
    #
    # This is BELOW our own encoder's 40.20, and that is the expected outcome
    # the note above `app_etc1s_holds_the_quality_bar` was written to predict:
    # BasisLZ quantises to shared codebooks, and that is what codebooks cost.
    # It is not a regression in our encoder, which this chunk does not touch,
    # and the 39.9 bar is deliberately left alone. For scale, basisu's own
    # figure on OUR encoder's output is 38.92 dB, measured by a different tool
    # at settings we did not match -- close to this one, but not a comparison
    # to lean on.
    #
    # No ceiling to match `app_etc1s_quality_is_pinned_from_above`. That one
    # pins an encoder we own and can explain every digit of. This is a third
    # party's, and a ceiling here would fail on a libktx upgrade that merely
    # got better. Three consecutive encodes on this machine were byte-identical
    # -- but same-machine reproducibility is not cross-platform reproducibility,
    # and nothing here claims the latter.
    add_test(NAME app_ktx2_holds_the_quality_bar COMMAND mh_png_compare
             ${MH_DATA_DIR}/eyes/materials/brown_eye.png
             ${CMAKE_CURRENT_BINARY_DIR}/brown_eye_unpacked_rgba_RGBA32_level_0_face_0_layer0000.png
             --min-psnr 38.5)
    set_tests_properties(app_ktx2_holds_the_quality_bar PROPERTIES
        FIXTURES_REQUIRED app_ktx2_unpacked)
endif()
