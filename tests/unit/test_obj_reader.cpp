// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// Writes `contents` to a uniquely-named temp .obj and removes it on scope exit.
class TempObj {
public:
    explicit TempObj(std::string_view contents) {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("mh_test_" + std::to_string(++counter) + ".obj");
        std::ofstream out(path_);
        out << contents;
    }
    ~TempObj() { std::error_code ec; std::filesystem::remove(path_, ec); }

    TempObj(const TempObj&) = delete;
    TempObj& operator=(const TempObj&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("loads a quad with UVs", "[core][obj]") {
    const TempObj f(R"(
g plane
v 0 0 0
v 1 0 0
v 1 0 1
v 0 0 1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
f 1/1 2/2 3/3 4/4
)");
    const auto m = loadObj(f.path());
    REQUIRE(m.has_value());
    REQUIRE(m->vertexCount() == 4);
    REQUIRE(m->faceCount() == 1);
    REQUIRE(m->hasUV());
    REQUIRE(m->vertsPerFaceForExport() == 4);
    REQUIRE(m->faceGroups().size() == 1);
    REQUIRE(m->faceGroups()[0].name == "plane");
}

TEST_CASE("a triangle becomes a degenerate quad", "[core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE(m.has_value());
    REQUIRE(m->faceCount() == 1);
    REQUIRE(m->fvert().size() == 4);
    REQUIRE(m->fvert()[3] == m->fvert()[0]);   // wavefront.py:105-106
    REQUIRE(m->vertsPerFaceForExport() == 3);
}

TEST_CASE("leading-dot floats parse", "[core][obj]") {
    // Real .target and .obj assets use this style, e.g. "4050 -.001 0 0".
    const TempObj f("g t\nv -.5 .25 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE(m.has_value());
    REQUIRE_THAT(m->coord()[0].x, WithinAbs(-0.5, 1e-7));
    REQUIRE_THAT(m->coord()[0].y, WithinAbs(0.25, 1e-7));
}

TEST_CASE("negative indices resolve relative to the end", "[core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf -3 -2 -1\n");
    const auto m = loadObj(f.path());
    REQUIRE(m.has_value());
    REQUIRE(m->fvert()[0] == 0);
    REQUIRE(m->fvert()[1] == 1);
    REQUIRE(m->fvert()[2] == 2);
}

TEST_CASE("vertex normals in the file are ignored", "[core][obj]") {
    // wavefront.py:50-52 -- the reference never reads vn and always recomputes.
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nvn 0 1 0\nf 1//1 2//1 3//1\n");
    const auto m = loadObj(f.path());
    REQUIRE(m.has_value());
    REQUIRE_FALSE(m->hasUV());
    REQUIRE(m->vnorm().size() == 3);
}

TEST_CASE("a loose vertex is rejected", "[core][obj][error]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nv 5 5 5\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    REQUIRE(m.error().kind == ObjErrorKind::LooseVertex);
}

TEST_CASE("an out-of-range face index is rejected, not read out of bounds",
          "[core][obj][error]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 99\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    REQUIRE(m.error().kind == ObjErrorKind::BadIndex);
    REQUIRE(m.error().line == 5);
}

TEST_CASE("index zero is rejected", "[core][obj][error]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 0 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    REQUIRE(m.error().kind == ObjErrorKind::BadIndex);
}

TEST_CASE("an n-gon is rejected rather than silently truncated",
          "[core][obj][error]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 1 0 1\nv 0 0 1\nv 2 0 2\nf 1 2 3 4 5\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    REQUIRE(m.error().kind == ObjErrorKind::MixedPrimitives);
}

TEST_CASE("a file with no faces is rejected", "[core][obj][error]") {
    const TempObj f("v 0 0 0\nv 1 0 0\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    REQUIRE(m.error().kind == ObjErrorKind::EmptyMesh);
}

TEST_CASE("a missing file is reported, not crashed on", "[core][obj][error]") {
    const auto m = loadObj("/definitely/does/not/exist.obj");
    REQUIRE_FALSE(m.has_value());
    REQUIRE(m.error().kind == ObjErrorKind::NotFound);
    REQUIRE_FALSE(m.error().message().empty());
}

TEST_CASE("faces before any g statement land in a default group", "[core][obj]") {
    const TempObj f("v 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE(m.has_value());
    REQUIRE(m->faceGroups().size() == 1);
    REQUIRE(m->faceGroups()[0].name == "default");
}
