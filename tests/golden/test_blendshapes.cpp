// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The expression units as glTF blendshapes.
//
// The shipped data has **102** files under `data/targets/expression/units/`,
// and exporting 102 shape keys would be wrong: they are **34 units x 3
// ethnicities** (african/asian/caucasian). A character is a blend of the three,
// so a correct blendshape set is 34 targets, each the ethnic blend of its three
// files, weighted by `MacroFactors`. `tests/golden/target_groups.txt` records
// the grouping independently: 34 `expression-units-*` groups, 3 components each.
#include "makehuman/core/Blendshape.h"
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"
#include "makehuman/io/Compact.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace mh;
using Catch::Matchers::WithinAbs;

namespace {

const core::TargetIndex& sharedIndex() {
    static const core::TargetIndex index =
        core::TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    return index;
}

/// The base mesh and its render mesh, built once: 19,158 vertices is not
/// something to re-parse per assertion.
struct Fixture {
    core::Mesh mesh;
    core::RenderMesh rm;
};

const Fixture& fixture() {
    static const Fixture f = [] {
        auto m = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
        REQUIRE(m.has_value());
        auto rm = core::RenderMesh::build(*m);
        return Fixture{std::move(*m), std::move(rm)};
    }();
    return f;
}

/// One ethnic variant's deltas, expanded to render vertices. The oracle the
/// blend is checked against.
std::vector<foundation::Vec3> variantDeltas(const std::string& unit, const std::string& race) {
    const auto& f = fixture();
    const auto p  = std::filesystem::path(MH_DATA_DIR) / "targets" / "expression" / "units" / race /
                   (unit + ".target");
    const auto t = core::loadTarget(p);
    REQUIRE(t.has_value());
    std::vector<foundation::Vec3> out;
    REQUIRE(core::expandTargetToRenderVertices(*t, f.rm.vmap(), f.mesh.vertexCount(), out));
    return out;
}

}  // namespace

TEST_CASE("34 blendshapes, not the 102 files on disk", "[blendshape]") {
    const auto& f = fixture();
    core::MacroFactors factors;
    const auto shapes =
        core::buildExpressionBlendshapes(sharedIndex(), factors, f.rm.vmap(), f.mesh.vertexCount());

    // 102 files / 3 ethnicities. If this ever reads 102 the export is wrong,
    // not merely large: a DCC would show three near-duplicate keys per unit.
    CHECK(shapes.size() == 34);

    for (const auto& s : shapes) {
        INFO(s.name);
        // A DCC labels its shape keys with these, so the group prefix must be
        // gone -- "expression-units-eye-left-closure" is not a usable key name.
        CHECK(s.name.find("expression") == std::string::npos);
        CHECK_FALSE(s.name.empty());
        CHECK(s.deltas.size() == f.rm.vmap().size());
    }

    // Names are the units, and every one is distinct.
    std::vector<std::string> names;
    for (const auto& s : shapes)
        names.push_back(s.name);
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(std::ranges::find(names, "eye-left-closure") != names.end());
}

TEST_CASE("a blendshape IS the ethnic blend, not one file of three", "[blendshape]") {
    const auto& f      = fixture();
    const auto african = variantDeltas("eye-left-closure", "african");
    const auto asian   = variantDeltas("eye-left-closure", "asian");
    const auto cauc    = variantDeltas("eye-left-closure", "caucasian");

    const auto shapeFor = [&](const core::MacroFactors& mf) {
        const auto shapes =
            core::buildExpressionBlendshapes(sharedIndex(), mf, f.rm.vmap(), f.mesh.vertexCount());
        const auto it = std::ranges::find_if(
            shapes, [](const core::Blendshape& s) { return s.name == "eye-left-closure"; });
        REQUIRE(it != shapes.end());
        return it->deltas;
    };

    // The three files genuinely differ, or nothing below can distinguish a
    // blend from a copy.
    size_t differ = 0;
    for (size_t i = 0; i < african.size(); ++i) {
        if (african[i].x != cauc[i].x || african[i].y != cauc[i].y || african[i].z != cauc[i].z) {
            ++differ;
        }
    }
    INFO("african vs caucasian differ at " << differ << " render vertices");
    REQUIRE(differ > 0);

    SECTION("default factors give the mean of the three") {
        const core::MacroFactors mf;  // 1/3 each
        REQUIRE_THAT(mf.african() + mf.asian() + mf.caucasian(), WithinAbs(1.0, 1e-6));
        const auto got = shapeFor(mf);
        REQUIRE(got.size() == african.size());
        for (size_t i = 0; i < got.size(); ++i) {
            const float wantX =
                mf.african() * african[i].x + mf.asian() * asian[i].x + mf.caucasian() * cauc[i].x;
            REQUIRE_THAT(static_cast<double>(got[i].x),
                         WithinAbs(static_cast<double>(wantX), 1e-5));
        }
    }

    SECTION("a pure ethnicity gives that file back") {
        core::MacroFactors mf;
        mf.setAfrican(1.0F);
        REQUIRE_THAT(mf.african(), WithinAbs(1.0, 1e-6));
        const auto got = shapeFor(mf);
        for (size_t i = 0; i < got.size(); ++i) {
            REQUIRE_THAT(static_cast<double>(got[i].x),
                         WithinAbs(static_cast<double>(african[i].x), 1e-5));
            REQUIRE_THAT(static_cast<double>(got[i].y),
                         WithinAbs(static_cast<double>(african[i].y), 1e-5));
            REQUIRE_THAT(static_cast<double>(got[i].z),
                         WithinAbs(static_cast<double>(african[i].z), 1e-5));
        }
    }

    SECTION("changing the ethnicity changes the blendshape") {
        core::MacroFactors afr;
        afr.setAfrican(1.0F);
        core::MacroFactors cau;
        cau.setCaucasian(1.0F);
        const auto a = shapeFor(afr);
        const auto c = shapeFor(cau);
        size_t moved = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].x != c[i].x || a[i].y != c[i].y || a[i].z != c[i].z) ++moved;
        }
        // Ignoring `factors` entirely -- summing, averaging, or taking the
        // first component -- makes these two identical.
        INFO("differing render vertices: " << moved);
        CHECK(moved == differ);
    }
}

// Where the amplitude goes on the way out, measured rather than assumed.
//
// Blender reads `eye-left-closure` in the exported GLB as **0.0102** at 1
// unit = 1 m, i.e. **0.102 dm** -- 64% of the 0.15915 dm the builder produces
// and the `.target` files state. That gap is NOT a bug: the largest-moving
// eyelid vertices sit on helper geometry the body face mask hides, so
// compaction drops them along with the faces that referenced them. The visible
// mesh legitimately moves less than the full cage does.
//
// Asserted here so the day it becomes a real loss -- a scale factor, a wrong
// remap -- this fails instead of quietly shipping 34 keys that barely move.
TEST_CASE("the visible mesh moves less than the full cage, and by how much",
          "[blendshape][compact]") {
    const auto& f       = fixture();
    core::RenderMesh rm = core::RenderMesh::build(f.mesh);
    REQUIRE(rm.setFaceMask(f.mesh, f.mesh.staticFaceMask()));
    const auto compact = mh::io::compactUnusedVertices(rm.view());

    const core::MacroFactors factors;
    const auto shapes =
        core::buildExpressionBlendshapes(sharedIndex(), factors, rm.vmap(), f.mesh.vertexCount());
    const auto it = std::ranges::find_if(
        shapes, [](const core::Blendshape& s) { return s.name == "eye-left-closure"; });
    REQUIRE(it != shapes.end());

    const auto maxMag = [](std::span<const foundation::Vec3> ds) {
        double mx = 0.0;
        for (const auto& d : ds) {
            const auto x = static_cast<double>(d.x);
            const auto y = static_cast<double>(d.y);
            const auto z = static_cast<double>(d.z);
            mx           = std::max(mx, std::sqrt(x * x + y * y + z * z));
        }
        return mx;
    };

    const double full = maxMag(it->deltas);
    const auto moved  = mh::io::compactDeltas(it->deltas, compact.remap, compact.coord.size());
    const double vis  = maxMag(moved);
    INFO("full cage " << full << " dm, visible mesh " << vis << " dm");

    CHECK_THAT(full, WithinAbs(0.15915, 1e-4));
    // The number Blender independently reads back out of the exported GLB,
    // times 10 for the dm -> m export scale.
    CHECK_THAT(vis, WithinAbs(0.102, 2e-3));
    // Still a real deformation, not a rounding of it to nothing.
    CHECK(vis > 0.5 * full);
}

// The subdivided guard, exercised rather than merely documented. A subdivided
// render mesh's vmap names vertices past the base mesh's count, and
// `expandTargetToRenderVertices` rejects that (Target.cpp:203) -- so the result
// is empty rather than 34 shapes built on the wrong vertices. `main.cpp` also
// refuses `--blendshapes --subdivided` up front and says so, but the function
// must not depend on its caller for that.
TEST_CASE("a vmap that outruns the base mesh yields nothing", "[blendshape]") {
    const auto& f = fixture();
    std::vector<uint32_t> tooBig(f.rm.vmap().begin(), f.rm.vmap().end());
    tooBig.push_back(static_cast<uint32_t>(f.mesh.vertexCount()));  // one past the end

    const core::MacroFactors factors;
    const auto shapes =
        core::buildExpressionBlendshapes(sharedIndex(), factors, tooBig, f.mesh.vertexCount());
    CHECK(shapes.empty());
}
