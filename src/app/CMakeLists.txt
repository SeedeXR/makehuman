# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The application is AGPL: it links mh::core. Keeping the load here is what lets
# mh_ui and mh_render stay Apache-2.0 -- they only ever see a plain RenderView.

add_executable(makehuman main.cpp)
# mh::rig for posing, mh::io for the writers. Both are the legal direction:
# this executable is AGPL, mh::io is Apache-2.0.
# mh::core is NOT listed: mh::rig PUBLIC-links it since PosedMesh, so naming it
# here too makes the linker see libmh_core.a twice and warn -- the same trap the
# comment above records for libmh_io.a.
target_link_libraries(makehuman PRIVATE mh::rig mh::ui mh::warnings)
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

# The licence texts go in the bundle for the same reason the icon does: naming
# them is not shipping them. LICENSING.md section 6 requires all four inside the
# bundle, and MACOSX_BUNDLE_COPYRIGHT above already tells the user to "See
# LICENSING.md" -- in a bundle that, until this, did not contain it. Section 3
# of that file also ASSERTED the bundle "includes this notice", which was false.
#
# Attached to the TARGET rather than copied by the `dmg` step, so every build
# produces a compliant bundle and the gate can read one without packaging.
foreach(_licence LICENSE.md LICENSE.CODE.md LICENSE.ASSETS.md LICENSING.md)
    target_sources(makehuman PRIVATE "${CMAKE_SOURCE_DIR}/${_licence}")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/${_licence}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endforeach()

add_dependencies(makehuman mh_shaders)

# --- Installing ----------------------------------------------------------
#
# `cmake --install build --prefix <dir>` puts a runnable MakeHuman there. This
# is the case owner directive 13.11 puts FIRST -- "everyone will compile on
# their own hardware" -- and until now there were no install rules at all, so
# the only way to get a runnable copy out of a build tree was the `dmg` target.
#
# The layout is not invented here: `foundation::resolveDir` already looks for
# `<bundle>/Contents/Resources/<name>` and then `<prefix>/share/makehuman/<name>`
# (src/foundation/DataDir.cpp). These rules fill exactly those two, so the
# installed binary finds its own assets by the search it already performs.
#
# NOT a redistributable bundle: `macdeployqt` is NOT run here, so the installed
# app still links the Qt it was built against. That is correct for "build it
# yourself" -- the Qt is right there -- and the `dmg` target below is what
# produces a copy for a machine that has none. Said out loud because an
# install rule that looked self-contained and was not is the packaging bug
# `audit_runtime_paths.py` exists about.
if(APPLE)
    install(TARGETS makehuman BUNDLE DESTINATION ".")
    # Trailing slashes: copy the CONTENTS of each directory, not the directory
    # into itself (`.../data/data`).
    install(DIRECTORY "${MH_DATA_DIR}/"
            DESTINATION "makehuman.app/Contents/Resources/data")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/"
            DESTINATION "makehuman.app/Contents/Resources/shaders")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/"
            DESTINATION "makehuman.app/Contents/Resources/resources")
else()
    install(TARGETS makehuman RUNTIME DESTINATION "bin")
    install(DIRECTORY "${MH_DATA_DIR}/" DESTINATION "share/makehuman/data")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION "share/makehuman/shaders")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/" DESTINATION "share/makehuman/resources")
    # The licences ride inside the bundle on macOS (MACOSX_PACKAGE_LOCATION
    # above); off it they need a home of their own.
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.md" "${CMAKE_SOURCE_DIR}/LICENSE.CODE.md"
                  "${CMAKE_SOURCE_DIR}/LICENSE.ASSETS.md" "${CMAKE_SOURCE_DIR}/LICENSING.md"
            DESTINATION "share/doc/makehuman")
endif()

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
