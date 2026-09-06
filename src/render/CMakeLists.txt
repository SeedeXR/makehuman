# SPDX-License-Identifier: Apache-2.0
#
# mh_render sets up Qt RHI. That is our own code written against Qt's API, not
# a translation of anything, so it is Apache-2.0. The SHADERS it loads at
# runtime are AGPL ports of the reference's GLSL and live in resources/; loading
# a file is not derivation, so the boundary holds. See LICENSING.md 4.

add_library(mh_render STATIC SceneResources.cpp OffscreenRenderer.cpp Picking.cpp)
add_library(mh::render ALIAS mh_render)

target_include_directories(mh_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# GuiPrivate is required, not optional: rhi/qrhi.h lives behind it. Linking
# Qt6::Gui alone fails to resolve the imported target.
target_link_libraries(mh_render
    PUBLIC  mh::foundation Qt6::Gui Qt6::GuiPrivate
    PRIVATE mh::warnings mh::sanitize)

set_target_properties(mh_render PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    AUTOMOC OFF)

# Compile the RHI shaders to .qsb next to the binaries. qsb ships with Qt, so
# no separate tool is needed once Qt is found.
#
# NOT qt6_add_shaders(): that macro always packages its output into a Qt
# resource under a PREFIX (Qt6ShaderToolsMacros.cmake:5,50) and has no mode
# that leaves loose files on disk. This port loads .qsb from a RESOLVED
# directory instead -- foundation::resolveShaderDir -- which is what lets a
# relocated .app bundle carry its own shaders in Contents/Resources.
#
# GLOBbed, not listed. A literal `foreach(stage litsphere.vert ...)` here meant
# a new shader was silently not built, while tools/compile_shaders.sh (which
# globbed) did build it, so the two disagreed about what the shader set was.
# CONFIGURE_DEPENDS re-runs the glob when the directory changes, so adding a
# stage needs no CMake edit at all. Pinned by the test "every shader source has
# a compiled .qsb beside the binaries".
set(MH_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${MH_SHADER_OUT}")
file(GLOB _shader_sources CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/resources/shaders/rhi/*.vert"
     "${CMAKE_SOURCE_DIR}/resources/shaders/rhi/*.frag")
if(NOT _shader_sources)
    # An empty glob would configure and build cleanly and then fail at runtime
    # on the first pipeline, which is the worst place to find out.
    message(FATAL_ERROR "no shader sources in resources/shaders/rhi")
endif()
set(_qsb_outputs)
foreach(_src ${_shader_sources})
    get_filename_component(stage "${_src}" NAME)
    set(_dst "${MH_SHADER_OUT}/${stage}.qsb")
    add_custom_command(
        OUTPUT "${_dst}"
        COMMAND Qt6::qsb --glsl 450 --msl 12 -o "${_dst}" "${_src}"
        DEPENDS "${_src}"
        COMMENT "qsb ${stage}"
        VERBATIM)
    list(APPEND _qsb_outputs "${_dst}")
endforeach()
add_custom_target(mh_shaders DEPENDS ${_qsb_outputs})
add_dependencies(mh_render mh_shaders)
