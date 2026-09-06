# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tools, kept OUT of the library directories on purpose. mh_render_probe links
# mh::core, which is AGPL; mh_render is Apache-2.0 and links neither. Having the
# probe live in src/render/CMakeLists.txt made the licence-boundary gate fire on
# that file, and the gate was right -- an Apache module's build file should not
# mention the AGPL one at all, even for a neighbouring target.

# Renders the base mesh offscreen and reports pixel statistics. Not a test
# target: it writes an image for a human, and prints numbers that make "it
# renders" checkable without looking at one.
add_executable(mh_render_probe src/mh_render_probe.cpp)
target_link_libraries(mh_render_probe PRIVATE mh::core mh::render mh::warnings)
target_compile_definitions(mh_render_probe PRIVATE MH_DATA_DIR="${MH_DATA_DIR}")
set_target_properties(mh_render_probe PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
add_dependencies(mh_render_probe mh_shaders)

# Writes our own FBX so Maya and Blender can be asked whether it opens. Not a
# test target: the answer comes from another process, and neither DCC is in CI.
add_executable(mh_fbx_probe src/mh_fbx_probe.cpp)
target_link_libraries(mh_fbx_probe PRIVATE mh::core mh::io mh::warnings)
target_compile_definitions(mh_fbx_probe PRIVATE MH_DATA_DIR="${MH_DATA_DIR}")
set_target_properties(mh_fbx_probe PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
