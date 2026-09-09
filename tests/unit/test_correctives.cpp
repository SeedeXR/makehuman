// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-frame corrective application: the third stage of the pose-space
// deformer, and the one that finally moves geometry.
//
// SwingTwist turns a joint rotation into a signal vector; Rbf turns that into a
// weight vector; this adds `sum(weight[c] * corrective[c])` to the shaped rest
// mesh. Owner directive 12.2 commits to two things about it:
//
//   PRE-SKIN, IN REST SPACE. The correctives go on before skinning, so the
//       skinning transform carries them -- which is what makes them compose
//       with the body-shape targets rather than fight them. A corrective
//       authored on one body type then degrades gracefully on another instead
//       of exploding.
//   The deltas are the SAME primitive as a morph target. `core::Target` is
//       already a sparse list of vertex indices and offsets, so a corrective
//       is a Target and nothing new is invented for it.
//
// What is new is the per-frame shape of it (directive 12.5): a dense scratch
// buffer, a DIRTY-INDEX LIST from the union of active correctives, and a reset
// that touches only those vertices. Replaying the 364-target morph stack per
// frame is what the character-static path does and it is far too slow for one;
// resetting all 19,158 vertices every frame is cheaper but still pointless when
// a corrective moves a few hundred.
//
// The determinism directive 12.5 warns about is the reason the accumulation
// order is fixed and stated: "parallel scatter-add reorders float additions, so
// the same input can give different output across thread counts."
#include "makehuman/core/Correctives.h"

#include "makehuman/core/Target.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <vector>

using namespace mh::core;
using Catch::Matchers::WithinAbs;

namespace {

std::vector<Vec3> restLine(size_t n) {
    std::vector<Vec3> v;
    for (size_t i = 0; i < n; ++i) {
        v.push_back(Vec3{static_cast<float>(i), 10.0F + static_cast<float>(i), -1.0F});
    }
    return v;
}

Target sparse(std::vector<uint32_t> verts, std::vector<Vec3> offsets) {
    Target t;
    t.name    = "synthetic";
    t.verts   = std::move(verts);
    t.offsets = std::move(offsets);
    for (const uint32_t v : t.verts)
        t.maxVertexIndex = std::max(t.maxVertexIndex, v);
    return t;
}

void checkEqual(const Vec3& got, const Vec3& want) {
    // -Wdouble-promotion is on, so the float->double widening WithinAbs needs
    // is written out rather than left implicit.
    CHECK_THAT(static_cast<double>(got.x), WithinAbs(static_cast<double>(want.x), 1e-6));
    CHECK_THAT(static_cast<double>(got.y), WithinAbs(static_cast<double>(want.y), 1e-6));
    CHECK_THAT(static_cast<double>(got.z), WithinAbs(static_cast<double>(want.z), 1e-6));
}

void checkIdentical(std::span<const Vec3> got, std::span<const Vec3> want) {
    REQUIRE(got.size() == want.size());
    for (size_t i = 0; i < got.size(); ++i) {
        // Bit-for-bit, not within a tolerance: "the rest pose is unchanged"
        // has to mean the bytes are unchanged, or a drift of one ulp per frame
        // is invisible until it is not.
        CHECK(got[i].x == want[i].x);
        CHECK(got[i].y == want[i].y);
        CHECK(got[i].z == want[i].z);
    }
}

}  // namespace

TEST_CASE("with no correctives the rest mesh comes back untouched", "[core][correctives]") {
    const auto rest = restLine(8);
    CorrectiveBuffer buffer;
    buffer.setRest(rest);
    REQUIRE(buffer.apply({}, {}));
    checkIdentical(buffer.positions(), rest);
    CHECK(buffer.touched() == 0);
}

TEST_CASE("a weight of zero is the same as no corrective at all", "[core][correctives]") {
    const auto rest = restLine(8);
    const Target t  = sparse({2, 5}, {Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 2.0F, 0.0F}});
    const Target* ts[]{&t};

    CorrectiveBuffer buffer;
    buffer.setRest(rest);
    const float zero[]{0.0F};
    REQUIRE(buffer.apply(ts, zero));
    checkIdentical(buffer.positions(), rest);

    // And the dirty list knows it: a corrective with no weight is not applied,
    // so there is nothing to undo next frame either.
    CHECK(buffer.touched() == 0);
}

TEST_CASE("one corrective moves exactly its own vertices", "[core][correctives]") {
    const auto rest = restLine(8);
    const Target t  = sparse({2, 5}, {Vec3{1.0F, 0.0F, 0.0F}, Vec3{0.0F, 2.0F, 0.0F}});
    const Target* ts[]{&t};

    CorrectiveBuffer buffer;
    buffer.setRest(rest);
    const float half[]{0.5F};
    REQUIRE(buffer.apply(ts, half));

    const auto out = buffer.positions();
    checkEqual(out[2], Vec3{rest[2].x + 0.5F, rest[2].y, rest[2].z});
    checkEqual(out[5], Vec3{rest[5].x, rest[5].y + 1.0F, rest[5].z});
    for (const size_t i : {0U, 1U, 3U, 4U, 6U, 7U}) {
        checkIdentical(out.subspan(i, 1), std::span<const Vec3>(rest).subspan(i, 1));
    }
    CHECK(buffer.touched() == 2);
}

TEST_CASE("overlapping correctives add rather than replace", "[core][correctives]") {
    // Two correctives that both move vertex 3. The whole point of a weight
    // VECTOR is that several are active at once, and an implementation that
    // wrote instead of accumulating would pass every single-corrective test.
    const auto rest = restLine(8);
    const Target a  = sparse({3, 4}, {Vec3{1.0F, 0.0F, 0.0F}, Vec3{1.0F, 0.0F, 0.0F}});
    const Target b  = sparse({3}, {Vec3{0.0F, 0.0F, 4.0F}});
    const Target* ts[]{&a, &b};

    CorrectiveBuffer buffer;
    buffer.setRest(rest);
    const float w[]{2.0F, 0.5F};
    REQUIRE(buffer.apply(ts, w));

    const auto out = buffer.positions();
    checkEqual(out[3], Vec3{rest[3].x + 2.0F, rest[3].y, rest[3].z + 2.0F});
    checkEqual(out[4], Vec3{rest[4].x + 2.0F, rest[4].y, rest[4].z});

    // The union, counted once: vertex 3 is in both correctives.
    CHECK(buffer.touched() == 2);
}

TEST_CASE("a negative weight moves the other way", "[core][correctives]") {
    // Not a curiosity: an RBF interpolant overshoots between example poses and
    // routinely produces negative weights, so a clamp to [0, 1] here would
    // quietly change what was authored.
    const auto rest = restLine(4);
    const Target t  = sparse({1}, {Vec3{0.0F, 3.0F, 0.0F}});
    const Target* ts[]{&t};

    CorrectiveBuffer buffer;
    buffer.setRest(rest);
    const float negative[]{-1.0F};
    REQUIRE(buffer.apply(ts, negative));
    checkEqual(buffer.positions()[1], Vec3{rest[1].x, rest[1].y - 3.0F, rest[1].z});
}

TEST_CASE("the previous frame is undone, and only where it was applied", "[core][correctives]") {
    // THE bug the dirty list creates if it is wrong: a corrective that stops
    // being active leaves its delta behind for ever, and the character slowly
    // inflates over a few seconds of animation. Every other test here passes
    // with that bug present, because they each apply once to a fresh buffer.
    const auto rest = restLine(8);
    const Target a  = sparse({1, 2}, {Vec3{5.0F, 0.0F, 0.0F}, Vec3{5.0F, 0.0F, 0.0F}});
    const Target b  = sparse({6}, {Vec3{0.0F, 0.0F, 7.0F}});
    const Target* ts[]{&a, &b};

    CorrectiveBuffer buffer;
    buffer.setRest(rest);

    const float onlyA[]{1.0F, 0.0F};
    REQUIRE(buffer.apply(ts, onlyA));
    CHECK(buffer.touched() == 2);

    // Now a completely disjoint corrective. Vertices 1 and 2 must be back at
    // rest EXACTLY -- not nearly, since a frame-by-frame residue accumulates.
    const float onlyB[]{0.0F, 1.0F};
    REQUIRE(buffer.apply(ts, onlyB));
    const auto out = buffer.positions();
    checkIdentical(out.subspan(1, 2), std::span<const Vec3>(rest).subspan(1, 2));
    checkEqual(out[6], Vec3{rest[6].x, rest[6].y, rest[6].z + 7.0F});
    CHECK(buffer.touched() == 1);

    // And back to nothing at all: the whole mesh is the rest mesh again.
    const float none[]{0.0F, 0.0F};
    REQUIRE(buffer.apply(ts, none));
    checkIdentical(buffer.positions(), rest);
    CHECK(buffer.touched() == 0);
}

TEST_CASE("repeated frames of the same corrective do not drift", "[core][correctives]") {
    // Applying and releasing repeatedly must land on exactly the rest mesh
    // every time, and exactly the deformed mesh every time. The bug in view is
    // an undo that SUBTRACTS its own delta instead of restoring from rest,
    // because `x + d - d` is not `x` in float and the residue accumulates.
    //
    // Three frames, not a hundred. The first draft ran a hundred with a comment
    // claiming one round trip would not catch a subtract-undo. Measured: it
    // does -- that mutation is caught with the loop cut to a single frame,
    // because `checkIdentical` compares bits rather than a tolerance. The extra
    // 97 iterations contributed 38,400 assertions and no coverage. Three keeps
    // a cheap guard against state that is only wrong on a later pass.
    const auto rest = restLine(64);
    Target t;
    t.name = "wide";
    for (uint32_t v = 0; v < 64; v += 3) {
        t.verts.push_back(v);
        t.offsets.push_back(Vec3{0.1F, 0.3F, -0.7F});
        t.maxVertexIndex = v;
    }
    const Target* ts[]{&t};

    CorrectiveBuffer buffer;
    buffer.setRest(rest);
    std::vector<Vec3> deformed;
    for (int frame = 0; frame < 3; ++frame) {
        const float on[]{0.35F};
        REQUIRE(buffer.apply(ts, on));
        if (frame == 0) {
            deformed.assign(buffer.positions().begin(), buffer.positions().end());
        } else {
            checkIdentical(buffer.positions(), deformed);
        }
        const float off[]{0.0F};
        REQUIRE(buffer.apply(ts, off));
        checkIdentical(buffer.positions(), rest);
    }
}

TEST_CASE("the same input gives the same bits", "[core][correctives]") {
    // Directive 12.5: "parallel scatter-add reorders float additions, so the
    // same input can give different output across thread counts. Fix the
    // accumulation order per vertex." The order here is the stored corrective
    // order, which is what the baked blob pins.
    const auto rest = restLine(32);
    const Target a  = sparse({4, 9}, {Vec3{0.1F, 0.2F, 0.3F}, Vec3{0.4F, 0.5F, 0.6F}});
    const Target b  = sparse({9, 4}, {Vec3{0.7F, 0.8F, 0.9F}, Vec3{1.1F, 1.2F, 1.3F}});
    const Target* ts[]{&a, &b};
    const float w[]{0.37F, 0.61F};

    CorrectiveBuffer one;
    one.setRest(rest);
    REQUIRE(one.apply(ts, w));
    const std::vector<Vec3> first(one.positions().begin(), one.positions().end());

    CorrectiveBuffer two;
    two.setRest(rest);
    REQUIRE(two.apply(ts, w));
    checkIdentical(two.positions(), first);

    // And a buffer that has been used for something else first lands on the
    // same bits, so history does not leak into the result.
    CorrectiveBuffer reused;
    reused.setRest(rest);
    const float other[]{1.7F, -0.4F};
    REQUIRE(reused.apply(ts, other));
    REQUIRE(reused.apply(ts, w));
    checkIdentical(reused.positions(), first);
}

TEST_CASE("a corrective is refused when it does not fit the mesh", "[core][correctives]") {
    const auto rest = restLine(8);

    SECTION("one weight per corrective, or the pairing is guesswork") {
        const Target t = sparse({1}, {Vec3{1.0F, 0.0F, 0.0F}});
        const Target* ts[]{&t};
        const float two[]{1.0F, 1.0F};
        CorrectiveBuffer buffer;
        buffer.setRest(rest);
        CHECK_FALSE(buffer.apply(ts, two));
    }

    SECTION("a vertex index past the end of the rest mesh") {
        // Refused for the whole call rather than skipped per vertex. A
        // corrective sculpted against a different topology is an authoring
        // error, and applying the part of it that happens to fit produces a
        // character that is subtly wrong in a way nothing reports.
        const Target t = sparse({1, 99}, {Vec3{1.0F, 0.0F, 0.0F}, Vec3{1.0F, 0.0F, 0.0F}});
        const Target* ts[]{&t};
        const float one[]{1.0F};
        CorrectiveBuffer buffer;
        buffer.setRest(rest);
        CHECK_FALSE(buffer.apply(ts, one));
        checkIdentical(buffer.positions(), rest);
    }

    SECTION("a refusal leaves the LAST GOOD FRAME, not the rest mesh") {
        // Checked from a deformed buffer, deliberately. On a fresh one the undo
        // pass has nothing to undo, so validating after it instead of before
        // looks identical -- and a mutation moving the validation after the
        // undo passed all 11 cases when this only ran on a fresh buffer.
        //
        // Mid-animation the difference is a visible pop: one bad frame would
        // snap the character to its rest pose and back.
        const Target good = sparse({1}, {Vec3{4.0F, 0.0F, 0.0F}});
        const Target bad  = sparse({99}, {Vec3{1.0F, 0.0F, 0.0F}});

        CorrectiveBuffer buffer;
        buffer.setRest(rest);
        const Target* first[]{&good};
        const float one[]{1.0F};
        REQUIRE(buffer.apply(first, one));
        const std::vector<Vec3> deformed(buffer.positions().begin(), buffer.positions().end());
        REQUIRE(deformed[1].x != rest[1].x);

        const Target* second[]{&good, &bad};
        const float two[]{1.0F, 1.0F};
        CHECK_FALSE(buffer.apply(second, two));
        checkIdentical(buffer.positions(), deformed);
        CHECK(buffer.touched() == 1);
    }

    SECTION("an out-of-range corrective is still refused when its weight is zero") {
        // Otherwise the error appears only once an animation happens to
        // activate it, which is the worst time to find out.
        const Target t = sparse({99}, {Vec3{1.0F, 0.0F, 0.0F}});
        const Target* ts[]{&t};
        const float zero[]{0.0F};
        CorrectiveBuffer buffer;
        buffer.setRest(rest);
        CHECK_FALSE(buffer.apply(ts, zero));
    }

    SECTION("a null corrective") {
        const Target* ts[]{nullptr};
        const float one[]{1.0F};
        CorrectiveBuffer buffer;
        buffer.setRest(rest);
        CHECK_FALSE(buffer.apply(ts, one));
    }

    SECTION("a target whose index and offset arrays disagree") {
        Target t;
        t.name  = "ragged";
        t.verts = {1, 2};
        t.offsets.push_back(Vec3{1.0F, 0.0F, 0.0F});
        t.maxVertexIndex = 2;
        const Target* ts[]{&t};
        const float one[]{1.0F};
        CorrectiveBuffer buffer;
        buffer.setRest(rest);
        CHECK_FALSE(buffer.apply(ts, one));
    }

    SECTION("and the good case still works, so the guards refuse the right things") {
        const Target t = sparse({1}, {Vec3{1.0F, 0.0F, 0.0F}});
        const Target* ts[]{&t};
        const float one[]{1.0F};
        CorrectiveBuffer buffer;
        buffer.setRest(rest);
        CHECK(buffer.apply(ts, one));
    }
}

TEST_CASE("a buffer with no rest mesh has nothing to deform", "[core][correctives]") {
    CorrectiveBuffer buffer;

    SECTION("no correctives either") {
        // This is the case that exercises the guard rather than another one.
        // With a corrective that HAS vertices, the index check catches it first
        // -- every index is past the end of a mesh with no vertices -- and
        // removing the rest-mesh guard passed all 11 cases. With nothing to
        // validate, the call would otherwise report success having produced no
        // positions, and the caller would skin an empty span.
        CHECK_FALSE(buffer.apply({}, {}));
    }

    SECTION("a corrective that moves nothing") {
        const Target empty;
        const Target* ts[]{&empty};
        const float one[]{1.0F};
        CHECK_FALSE(buffer.apply(ts, one));
    }

    SECTION("a corrective that moves something") {
        const Target t = sparse({0}, {Vec3{1.0F, 0.0F, 0.0F}});
        const Target* ts[]{&t};
        const float one[]{1.0F};
        CHECK_FALSE(buffer.apply(ts, one));
    }

    CHECK(buffer.positions().empty());
}

TEST_CASE("changing the rest mesh clears what the last frame did", "[core][correctives]") {
    // The character-static path runs when a slider moves: the shaped rest mesh
    // is rebuilt and handed here. Carrying the previous frame's dirty list
    // across that would undo deltas against vertices that no longer hold them.
    const auto rest = restLine(8);
    const Target t  = sparse({1}, {Vec3{9.0F, 0.0F, 0.0F}});
    const Target* ts[]{&t};

    CorrectiveBuffer buffer;
    buffer.setRest(rest);
    const float one[]{1.0F};
    REQUIRE(buffer.apply(ts, one));
    CHECK(buffer.touched() == 1);

    const auto reshaped = restLine(8);
    buffer.setRest(reshaped);
    CHECK(buffer.touched() == 0);
    checkIdentical(buffer.positions(), reshaped);

    // ...and the next apply is against the new rest mesh, not the old one.
    REQUIRE(buffer.apply(ts, one));
    checkEqual(buffer.positions()[1], Vec3{reshaped[1].x + 9.0F, reshaped[1].y, reshaped[1].z});
}
