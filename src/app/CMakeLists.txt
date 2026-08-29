# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The application is AGPL: it links mh::core. Keeping the load here is what lets
# mh_ui and mh_render stay Apache-2.0 -- they only ever see a plain RenderView.

add_executable(makehuman main.cpp)
# mh::rig for posing, mh::io for the writers. Both are the legal direction:
# this executable is AGPL, mh::io is Apache-2.0.
target_link_libraries(makehuman PRIVATE mh::core mh::rig mh::ui mh::warnings)
target_compile_definitions(makehuman PRIVATE
    MH_DATA_DIR="${MH_DATA_DIR}"
    MH_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    MH_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")
set_target_properties(makehuman PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
add_dependencies(makehuman mh_shaders)
