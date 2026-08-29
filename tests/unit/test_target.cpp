// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Target.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

class TempTarget {
public:
    explicit TempTarget(std::string_view contents) {
        static int counter = 0;
        path_              = std::filesystem::temp_directory_path() /
                ("mh_t_" + std::to_string(++counter) + ".target");
        std::ofstream out(path_);
        out << contents;
    }

    ~TempTarget() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempTarget(const TempTarget&)            = delete;
    TempTarget& operator=(const TempTarget&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

Mesh makeMesh(size_t n) {
    Mesh m("m", 4);
    std::vector<Vec3> c(n, Vec3{});
    for (size_t i = 0; i < n; ++i)
        c[i] = Vec3{static_cast<float>(i), 0.0F, 0.0F};
    REQUIRE(m.setCoords(std::move(c)).has_value());
    return m;
}

}  // namespace

TEST_CASE("parses index and offsets", "[core][target]") {
    const TempTarget f("# comment\n0 1 2 3\n2 -1 -2 -3\n");
    const auto t = loadTarget(f.path());
    REQUIRE(t.has_value());
    REQUIRE(t->size() == 2);
    CHECK(t->verts[0] == 0);
    CHECK(t->verts[1] == 2);
    CHECK(t->offsets[0] == Vec3{1, 2, 3});
    CHECK(t->maxVertexIndex == 2);
}

TEST_CASE("leading-dot floats parse", "[core][target]") {
    // Real files use exactly this style, e.g. "4050 -.001 0 0".
    const TempTarget f("4050 -.001 0 .004\n");
    const auto t = loadTarget(f.path());
    REQUIRE(t.has_value());
    CHECK_THAT(t->offsets[0].x, WithinAbs(-0.001, 1e-9));
    CHECK_THAT(t->offsets[0].z, WithinAbs(0.004, 1e-9));
}

TEST_CASE("comment and blank lines are ignored", "[core][target]") {
    const TempTarget f("# header\n\n# more\n1 0 0 0\n\n");
    const auto t = loadTarget(f.path());
    REQUIRE(t.has_value());
    CHECK(t->size() == 1);
}

TEST_CASE("a line with the wrong field count is skipped but counted", "[core][target]") {
    // The reference drops these silently (algos3d.py:136-138). Safe here --
    // every entry carries its own index, so nothing shifts -- but counted so a
    // caller can notice a malformed file.
    const TempTarget f("1 0 0 0\n2 0 0\n3 0 0 0\n");
    uint32_t skipped = 0;
    const auto t     = loadTarget(f.path(), &skipped);
    REQUIRE(t.has_value());
    CHECK(t->size() == 2);
    CHECK(skipped == 1);
}

TEST_CASE("a four-field line that will not parse is an error", "[core][target]") {
    const TempTarget f("1 0 0 0\n2 x y z\n");
    const auto t = loadTarget(f.path());
    REQUIRE_FALSE(t.has_value());
    CHECK(t.error().kind == TargetErrorKind::MalformedLine);
    CHECK(t.error().line == 2);
}

TEST_CASE("a missing file is reported", "[core][target]") {
    const auto t = loadTarget("/definitely/not/here.target");
    REQUIRE_FALSE(t.has_value());
    CHECK(t.error().kind == TargetErrorKind::NotFound);
    CHECK_FALSE(t.error().message().empty());
}

TEST_CASE("apply is additive and scaled by the factor", "[core][target]") {
    // coord[v] += offset * (scale * factor) -- algos3d.py:268,284
    Target t;
    t.verts          = {1};
    t.offsets        = {Vec3{2, 4, 8}};
    t.maxVertexIndex = 1;

    Mesh m = makeMesh(3);
    CHECK(applyTarget(t, m, 0.5F) == 1);
    CHECK_THAT(m.coord()[1].x, WithinAbs(1.0 + 1.0, 1e-6));  // base 1 + 2*0.5
    CHECK_THAT(m.coord()[1].y, WithinAbs(2.0, 1e-6));
    CHECK_THAT(m.coord()[1].z, WithinAbs(4.0, 1e-6));

    // Applying again accumulates: the mesh holds no notion of a current value.
    applyTarget(t, m, 0.5F);
    CHECK_THAT(m.coord()[1].y, WithinAbs(4.0, 1e-6));
}

TEST_CASE("a zero factor is a no-op", "[core][target]") {
    Target t;
    t.verts   = {0};
    t.offsets = {Vec3{9, 9, 9}};
    Mesh m    = makeMesh(2);
    CHECK(applyTarget(t, m, 0.0F) == 0);
    CHECK(m.coord()[0] == Vec3{0, 0, 0});
}

TEST_CASE("a negative factor subtracts, so deltas undo cleanly", "[core][target]") {
    // The incremental slider path applies (new - old); reversing must restore.
    Target t;
    t.verts   = {0};
    t.offsets = {Vec3{1, 2, 3}};
    Mesh m    = makeMesh(2);

    applyTarget(t, m, 0.75F);
    applyTarget(t, m, -0.75F);
    CHECK_THAT(m.coord()[0].x, WithinAbs(0.0, 1e-6));
    CHECK_THAT(m.coord()[0].y, WithinAbs(0.0, 1e-6));
    CHECK_THAT(m.coord()[0].z, WithinAbs(0.0, 1e-6));
}

TEST_CASE("per-axis scale is honoured", "[core][target]") {
    Target t;
    t.verts   = {0};
    t.offsets = {Vec3{1, 1, 1}};
    Mesh m    = makeMesh(1);
    applyTarget(t, m, 1.0F, Vec3{2, 0, -1});
    CHECK_THAT(m.coord()[0].x, WithinAbs(2.0, 1e-6));
    CHECK_THAT(m.coord()[0].y, WithinAbs(0.0, 1e-6));
    CHECK_THAT(m.coord()[0].z, WithinAbs(-1.0, 1e-6));
}

TEST_CASE("an out-of-range index is skipped, not read out of bounds", "[core][target]") {
    // The reference indexes coord[verts] unguarded (algos3d.py:284).
    Target t;
    t.verts          = {0, 99};
    t.offsets        = {Vec3{1, 0, 0}, Vec3{5, 0, 0}};
    t.maxVertexIndex = 99;

    Mesh m = makeMesh(2);
    CHECK(applyTarget(t, m, 1.0F) == 1);  // only vertex 0 moved
    CHECK_THAT(m.coord()[0].x, WithinAbs(1.0, 1e-6));
}

TEST_CASE("the library caches by relative path", "[core][target]") {
    TargetLibrary lib(MH_DATA_DIR);
    if (!std::filesystem::exists(std::filesystem::path(MH_DATA_DIR) / "targets")) {
        SKIP("target data not present");
    }

    const auto a = lib.get("targets/measure/measure-hips-circ-incr.target");
    REQUIRE(a.has_value());
    CHECK(lib.cachedCount() == 1);

    const auto b = lib.get("targets/measure/measure-hips-circ-incr.target");
    REQUIRE(b.has_value());
    CHECK(*a == *b);  // same object, not a reload
    CHECK(lib.cachedCount() == 1);

    lib.clear();
    CHECK(lib.cachedCount() == 0);
}

TEST_CASE("a missing target surfaces through the library", "[core][target]") {
    TargetLibrary lib(MH_DATA_DIR);
    const auto t = lib.get("targets/nope/does-not-exist.target");
    REQUIRE_FALSE(t.has_value());
    CHECK(t.error().kind == TargetErrorKind::NotFound);
}

TEST_CASE("every shipped target parses", "[core][target][golden]") {
    const auto dir = std::filesystem::path(MH_DATA_DIR) / "targets";
    if (!std::filesystem::exists(dir)) SKIP("target data not present");

    size_t ok = 0, failed = 0, entries = 0;
    uint32_t maxIdx = 0, skippedTotal = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.path().extension() != ".target") continue;
        uint32_t skipped = 0;
        const auto t     = loadTarget(e.path(), &skipped);
        if (!t) {
            ++failed;
            UNSCOPED_INFO("failed: " << t.error().message());
            continue;
        }
        ++ok;
        entries += t->size();
        skippedTotal += skipped;
        maxIdx = std::max(maxIdx, t->maxVertexIndex);
    }

    CHECK(failed == 0);
    CHECK(ok == 1280);          // measured: 1,280 .target files ship
    CHECK(skippedTotal == 0);   // no malformed lines in the shipped set
    CHECK(entries == 6147800);  // measured total sparse entries
    CHECK(maxIdx == 19157);     // exactly base mesh vertexCount - 1
}
