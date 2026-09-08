// SPDX-License-Identifier: Apache-2.0
//
// Where the asset tree lives at RUNTIME.
//
// MH_DATA_DIR is a compile-time absolute path into whoever's source tree built
// the binary -- CMakeLists.txt:27 calls it "used by development builds" and it
// is exactly that. An installed or bundled app cannot use it: the directory
// does not exist on the user's machine.

#include "makehuman/foundation/DataDir.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace mh;
namespace fs = std::filesystem;

namespace {

/// A directory that looks like an asset tree, so the resolver's existence check
/// has something to find.
fs::path makeAssetTree(const fs::path& at) {
    fs::create_directories(at / "3dobjs");
    std::ofstream(at / "3dobjs" / "base.obj") << "# marker\n";
    return at;
}

struct Sandbox {
    fs::path root;

    explicit Sandbox(const char* name)
        : root(fs::temp_directory_path() / (std::string("mh_datadir_") + name)) {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }

    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(root, ec);
        // All three, not just the data one: a case that sets the shader or
        // resource override would otherwise leak it into every later test in
        // this binary, and the leak reads as a resolver bug.
        ::unsetenv("MH_DATA_DIR");
        ::unsetenv("MH_SHADER_DIR");
        ::unsetenv("MH_RESOURCE_DIR");
    }
};

}  // namespace

TEST_CASE("the environment overrides everything", "[foundation][datadir]") {
    Sandbox sb("env");
    const auto wanted = makeAssetTree(sb.root / "explicit");
    makeAssetTree(sb.root / "compiled");

    ::setenv("MH_DATA_DIR", wanted.c_str(), 1);
    // An override that exists wins even when the compiled default also exists:
    // it is how a packager or a test points the app at another tree.
    CHECK(foundation::resolveDataDir(sb.root / "bin" / "makehuman", sb.root / "compiled") ==
          wanted);
}

TEST_CASE("a macOS bundle's Resources are found", "[foundation][datadir]") {
    Sandbox sb("bundle");
    // MakeHuman.app/Contents/MacOS/makehuman -> ../Resources/data
    const auto exe = sb.root / "MakeHuman.app" / "Contents" / "MacOS" / "makehuman";
    fs::create_directories(exe.parent_path());
    const auto wanted =
        makeAssetTree(sb.root / "MakeHuman.app" / "Contents" / "Resources" / "data");

    CHECK(foundation::resolveDataDir(exe, sb.root / "does-not-exist") == wanted);
}

TEST_CASE("a unix-style install beside the binary is found", "[foundation][datadir]") {
    Sandbox sb("install");
    const auto exe = sb.root / "usr" / "bin" / "makehuman";
    fs::create_directories(exe.parent_path());
    const auto wanted = makeAssetTree(sb.root / "usr" / "share" / "makehuman" / "data");

    CHECK(foundation::resolveDataDir(exe, sb.root / "does-not-exist") == wanted);
}

TEST_CASE("the compiled default is the last resort, not the first", "[foundation][datadir]") {
    Sandbox sb("fallback");
    const auto compiled = makeAssetTree(sb.root / "srctree" / "data");
    const auto exe      = sb.root / "build" / "makehuman";
    fs::create_directories(exe.parent_path());

    // Nothing installed anywhere: a development build must still work.
    CHECK(foundation::resolveDataDir(exe, compiled) == compiled);
}

TEST_CASE("a candidate that does not exist is skipped", "[foundation][datadir]") {
    Sandbox sb("skip");
    const auto compiled = makeAssetTree(sb.root / "srctree" / "data");
    const auto exe      = sb.root / "MakeHuman.app" / "Contents" / "MacOS" / "makehuman";
    fs::create_directories(exe.parent_path());
    // A bundle layout with NO Resources/data must fall through rather than
    // returning a path nothing can be loaded from.
    CHECK(foundation::resolveDataDir(exe, compiled) == compiled);
}

// This used to fall back to the compiled default, and the fallback was the
// worse failure of the two. A packager or a script whose MH_DATA_DIR has a typo
// silently got the SOURCE TREE OF THE MACHINE THAT BUILT THE BINARY: the app
// works there and nowhere else, and nothing anywhere says which tree it read.
// The header's own rule for a candidate applies to the override too -- "a
// directory that exists but holds nothing we need is worse than no candidate at
// all, because it stops the search and the failure surfaces later, somewhere
// less obvious".
//
// Obeyed, `makehuman` reports the directory the user actually named and exits 1.
// It is also what makes that branch reachable from a test at all: every other
// candidate is derived from the executable's own path or baked in at compile
// time, so an override that is honoured is the only way to run the binary with
// no asset tree.
TEST_CASE("an explicit override is obeyed even when it holds nothing", "[foundation][datadir]") {
    Sandbox sb("badenv");
    const auto compiled = makeAssetTree(sb.root / "srctree" / "data");
    const auto exe      = sb.root / "build" / "makehuman";
    fs::create_directories(exe.parent_path());

    ::setenv("MH_DATA_DIR", (sb.root / "nope").c_str(), 1);
    CHECK(foundation::resolveDataDir(exe, compiled) == sb.root / "nope");

    // An EMPTY value is not an instruction. `MH_DATA_DIR= makehuman` and an
    // unset variable are the same wish, and obeying "" would resolve every
    // asset against the process's working directory.
    ::setenv("MH_DATA_DIR", "", 1);
    CHECK(foundation::resolveDataDir(exe, compiled) == compiled);
}

// One search, one policy: the override wins for the other two directories on
// the same terms. Asserted because the shared `resolveDir` is the only reason
// that is true, and a future special case for one of them would be silent.
TEST_CASE("the shader and resource overrides are obeyed the same way", "[datadir]") {
    Sandbox sb("badenv_others");
    const auto exe = sb.root / "build" / "makehuman";
    fs::create_directories(exe.parent_path());

    ::setenv("MH_SHADER_DIR", (sb.root / "no-shaders").c_str(), 1);
    ::setenv("MH_RESOURCE_DIR", (sb.root / "no-resources").c_str(), 1);
    CHECK(foundation::resolveShaderDir(exe, sb.root / "compiled") == sb.root / "no-shaders");
    CHECK(foundation::resolveResourceDir(exe, sb.root / "compiled") == sb.root / "no-resources");
}

// ---------------------------------------------------------------------------
// The SAME question for the other two runtime directories, and they were worse
// off than the data one.
//
// `MH_SHADER_DIR` is `${CMAKE_BINARY_DIR}/shaders` -- inside the BUILD tree, so
// it does not survive copying the `.app` off the machine that built it and does
// not survive deleting the build directory on the machine that did.
// `MH_RESOURCE_DIR` points into the SOURCE tree.
//
// Measured before this was written: a release `.app` copied to /tmp still ran,
// but only because it fell back to those absolute paths, which happened to
// still exist. `Contents/Resources/` held one file -- AppIcon.icns.
// ---------------------------------------------------------------------------

namespace {

/// A bundle layout: <root>/MakeHuman.app/Contents/{MacOS,Resources/<name>}.
fs::path makeBundle(const fs::path& root, const std::string& name, const fs::path& sentinel) {
    const fs::path app = root / "MakeHuman.app";
    fs::create_directories(app / "Contents" / "MacOS");
    const fs::path dir = app / "Contents" / "Resources" / name;
    fs::create_directories((dir / sentinel).parent_path());
    std::ofstream(dir / sentinel) << "# marker\n";
    return app / "Contents" / "MacOS" / "makehuman";
}

}  // namespace

TEST_CASE("the shader directory is found inside a bundle", "[datadir]") {
    Sandbox box("shaders");
    const fs::path exe = makeBundle(box.root, "shaders", "litsphere.vert.qsb");

    const fs::path found = foundation::resolveShaderDir(exe, box.root / "nowhere");
    CHECK(found == exe.parent_path().parent_path() / "Resources" / "shaders");

    // The sentinel is load-bearing: a shaders directory with no compiled
    // shaders in it must NOT win, or the app reports "shader missing" from a
    // path that looks right.
    fs::remove(found / "litsphere.vert.qsb");
    CHECK(foundation::resolveShaderDir(exe, box.root / "nowhere") == box.root / "nowhere");
}

TEST_CASE("the resource directory is found inside a bundle", "[datadir]") {
    Sandbox box("resources");
    const fs::path exe =
        makeBundle(box.root, "resources", fs::path("icons") / "lucide" / "save.svg");

    const fs::path found = foundation::resolveResourceDir(exe, box.root / "nowhere");
    CHECK(found == exe.parent_path().parent_path() / "Resources" / "resources");

    // An icons-less resources tree loses. That failure has shipped here once
    // already -- every toolbar button blank, and nothing logged.
    fs::remove(found / "icons" / "lucide" / "save.svg");
    CHECK(foundation::resolveResourceDir(exe, box.root / "nowhere") == box.root / "nowhere");
}

// All three share one search, so the candidate ORDER is one policy rather than
// three that can drift. A bundle beats the compiled default for every one.
TEST_CASE("all three directories use the same precedence", "[datadir]") {
    Sandbox box("precedence");
    const fs::path app = box.root / "MakeHuman.app";
    fs::create_directories(app / "Contents" / "MacOS");
    const fs::path exe = app / "Contents" / "MacOS" / "makehuman";
    const fs::path res = app / "Contents" / "Resources";

    makeAssetTree(res / "data");
    fs::create_directories(res / "shaders");
    std::ofstream(res / "shaders" / "litsphere.vert.qsb") << "x\n";
    fs::create_directories(res / "resources" / "icons" / "lucide");
    std::ofstream(res / "resources" / "icons" / "lucide" / "save.svg") << "x\n";

    // A compiled default that also exists, so "bundle wins" is a real choice
    // rather than the only option.
    makeAssetTree(box.root / "src" / "data");

    CHECK(foundation::resolveDataDir(exe, box.root / "src" / "data") == res / "data");
    CHECK(foundation::resolveShaderDir(exe, box.root / "src") == res / "shaders");
    CHECK(foundation::resolveResourceDir(exe, box.root / "src") == res / "resources");
}
