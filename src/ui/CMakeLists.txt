# SPDX-License-Identifier: Apache-2.0
#
# mh_ui is Apache-2.0. It takes geometry as a non-owning foundation::RenderView
# and never includes a core header, so the licence boundary holds -- the AGPL
# side is the app, which loads the mesh and hands down a view.

set(CMAKE_AUTOMOC ON)

# The headers are listed explicitly: AUTOMOC pairs a header with a .cpp of the
# same name in the SAME directory, and ours live under include/. Without this
# the Q_OBJECT classes compile but their vtables never do, and the link fails
# with "undefined vtable" rather than anything pointing at moc.
add_library(mh_ui STATIC
    ViewportWidget.cpp
    MainWindow.cpp
    Theme.cpp
    PanelTitleBar.cpp
    ModifierPanel.cpp
    AssetPanel.cpp
    Workspace.cpp
    UndoCommands.cpp
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/ViewportWidget.h
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/MainWindow.h
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/Theme.h
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/PanelTitleBar.h
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/ModifierPanel.h
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/AssetPanel.h
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/Workspace.h
    ${CMAKE_SOURCE_DIR}/include/makehuman/ui/UndoCommands.h)
add_library(mh::ui ALIAS mh_ui)

target_include_directories(mh_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(mh_ui
    PUBLIC  mh::render Qt6::Widgets Qt6::Svg
    PRIVATE mh::warnings mh::sanitize)

target_compile_definitions(mh_ui PRIVATE MH_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")

set_target_properties(mh_ui PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)
