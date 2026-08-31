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
        ::unsetenv("MH_DATA_DIR");
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

TEST_CASE("an override pointing nowhere is ignored, not obeyed", "[foundation][datadir]") {
    Sandbox sb("badenv");
    const auto compiled = makeAssetTree(sb.root / "srctree" / "data");
    const auto exe      = sb.root / "build" / "makehuman";
    fs::create_directories(exe.parent_path());

    ::setenv("MH_DATA_DIR", (sb.root / "nope").c_str(), 1);
    // Obeying it would start the app with no assets and no explanation; the
    // caller reports the miss instead.
    CHECK(foundation::resolveDataDir(exe, compiled) == compiled);
}
