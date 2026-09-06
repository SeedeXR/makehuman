// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

namespace mh::foundation {

/// Finds the asset tree at runtime.
///
/// `MH_DATA_DIR` is baked in at compile time as an absolute path into whichever
/// source tree built the binary -- `CMakeLists.txt` calls it "used by
/// development builds", and that is all it can be. On any machine that did not
/// build it, that directory does not exist, so an installed or bundled app has
/// no assets at all.
///
/// Candidates, first existing one wins:
///
///   1. `$MH_DATA_DIR` -- an explicit override. A packager or a test pointing
///      the app at another tree must beat every built-in guess.
///   2. `<exe>/../Resources/data` -- inside a macOS `.app` bundle.
///   3. `<exe>/../share/makehuman/data` -- a Unix-style install prefix.
///   4. `~/Library/Application Support/MakeHuman/data` -- assets the user
///      installed separately from the application.
///   5. @p compiledDefault -- the development build's source tree.
///
/// A candidate counts only if it **exists and looks like an asset tree**
/// (`3dobjs/base.obj` is present). Returning a path that merely has the right
/// shape would trade "no assets" for "no assets, reported later and less
/// clearly" -- including for an override that points nowhere, which is ignored
/// rather than obeyed.
///
/// @param executable   the running binary, normally `argv[0]` resolved.
/// @param compiledDefault the compile-time `MH_DATA_DIR`.
/// @return the first candidate that exists; @p compiledDefault if none do, so
///         the caller always has something to report a failure against.
[[nodiscard]] std::filesystem::path resolveDataDir(const std::filesystem::path& executable,
                                                   const std::filesystem::path& compiledDefault);

/// The compiled shaders (`*.qsb`), found the same way.
///
/// `MH_SHADER_DIR` is `${CMAKE_BINARY_DIR}/shaders` — a path inside the BUILD
/// tree, so it is even less portable than the data one: it does not survive
/// copying the `.app` off the machine that built it, and it does not survive
/// deleting the build directory on the machine that did.
///
/// Sentinel: `litsphere.vert.qsb`, which every render path needs.
[[nodiscard]] std::filesystem::path resolveShaderDir(const std::filesystem::path& executable,
                                                     const std::filesystem::path& compiledDefault);

/// The shipped `resources/` tree — icons, fonts, shader sources.
///
/// `MH_RESOURCE_DIR` points into the SOURCE tree. Sentinel:
/// `icons/lucide/save.svg`, because a resources directory with no icons in it
/// would leave every toolbar button blank, which is the failure this project
/// has already shipped once.
[[nodiscard]] std::filesystem::path resolveResourceDir(
    const std::filesystem::path& executable, const std::filesystem::path& compiledDefault);

}  // namespace mh::foundation
