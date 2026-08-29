# SPDX-License-Identifier: Apache-2.0
#
# mh_render sets up Qt RHI. That is our own code written against Qt's API, not
# a translation of anything, so it is Apache-2.0. The SHADERS it loads at
# runtime are AGPL ports of the reference's GLSL and live in resources/; loading
# a file is not derivation, so the boundary holds. See LICENSING.md 4.

add_library(mh_render STATIC OffscreenRenderer.cpp)
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
set(MH_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${MH_SHADER_OUT}")
set(_qsb_outputs)
foreach(stage litsphere.vert litsphere.frag)
    set(_src "${CMAKE_SOURCE_DIR}/resources/shaders/rhi/${stage}")
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
