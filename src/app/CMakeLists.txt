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
# A real .app bundle, so the Dock, Finder and a DMG all get the icon. The
# executable still lives at a predictable place inside it, and ctest addresses
# the TARGET rather than a path, so `add_test(COMMAND makehuman ...)` keeps
# working -- CMake resolves it to Contents/MacOS/makehuman.
set_target_properties(makehuman PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "MakeHuman"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.makehuman.makehuman"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "AGPL-3.0-or-later. See LICENSING.md."
    # No extension: macOS appends .icns, and "AppIcon.icns" here resolves to
    # AppIcon.icns.icns and falls back to the generic icon with no error.
    MACOSX_BUNDLE_ICON_FILE "AppIcon"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

# The icon has to BE in Contents/Resources; naming it in Info.plist is not
# enough. MACOSX_PACKAGE_LOCATION puts it there as part of the target's sources.
target_sources(makehuman PRIVATE "${CMAKE_SOURCE_DIR}/resources/branding/AppIcon.icns")
set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/branding/AppIcon.icns"
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

add_dependencies(makehuman mh_shaders)

# --- Packaging -----------------------------------------------------------
#
# `cmake --build . --target dmg` produces a distributable disk image.
#
# macdeployqt is what makes the bundle self-contained: without it the .app runs
# only on a machine that has this exact Qt in this exact prefix, which is the
# classic "works here, fails everywhere" packaging bug. It is looked up rather
# than assumed, so a Qt install without it fails loudly at configure time
# instead of producing a DMG nobody else can open.
find_program(MH_MACDEPLOYQT macdeployqt HINTS "${QT_HOST_PATH}/bin" "${Qt6_DIR}/../../../bin")

if(APPLE AND MH_MACDEPLOYQT)
    add_custom_target(dmg
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/dmg"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dmg"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_BUNDLE_DIR:makehuman>" "${CMAKE_BINARY_DIR}/dmg/MakeHuman.app"
        # The ASSETS. macdeployqt bundles Qt and nothing else, so without these
        # three the DMG shipped an application with no data, no compiled shaders
        # and no icons -- it ran only on a machine that still had the source and
        # build trees at the paths baked in at compile time.
        #
        # `foundation::resolve{Data,Shader,Resource}Dir` look here FIRST, ahead
        # of those compile-time paths, so a bundle that carries its own copies
        # uses them; one that does not still falls back for a dev build.
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MH_DATA_DIR}" "${CMAKE_BINARY_DIR}/dmg/MakeHuman.app/Contents/Resources/data"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/shaders"
                "${CMAKE_BINARY_DIR}/dmg/MakeHuman.app/Contents/Resources/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources"
                "${CMAKE_BINARY_DIR}/dmg/MakeHuman.app/Contents/Resources/resources"
        # -dmg would build the image too, but it lays out a bare window with no
        # Applications symlink; the hdiutil call below is what gives the usual
        # drag-to-install layout.
        COMMAND ${MH_MACDEPLOYQT} "${CMAKE_BINARY_DIR}/dmg/MakeHuman.app"
        COMMAND ${CMAKE_COMMAND} -E create_symlink /Applications
                "${CMAKE_BINARY_DIR}/dmg/Applications"
        COMMAND hdiutil create -volname "MakeHuman" -srcfolder "${CMAKE_BINARY_DIR}/dmg"
                -ov -format UDZO "${CMAKE_BINARY_DIR}/MakeHuman.dmg"
        DEPENDS makehuman mh_shaders
        COMMENT "Packaging MakeHuman.dmg"
        VERBATIM)
endif()
