// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The corrective compiler and the blob it bakes: stages two and three of owner
// directive 12.4.
//
//   AUTHORING  the manifest, already read (core/CorrectiveManifest.h).
//   COMPILE    solve the RBF ONCE, offline, and bake the result.
//   RUNTIME    the blob only. A DISPOSABLE CACHE, invalidated on manifest hash
//              mismatch.
//
// The compile step's central choice is what the RBF is solved FOR: the values
// are the IDENTITY matrix, so at example pose i the weight vector is the i-th
// basis vector -- pose i fully on, every other pose off -- and between poses
// they blend. Measured before it was built (scratch, this session):
//
//     at pose 0:  1.0000 -0.0000 -0.0000 -0.0000
//     midway 0-1: 0.5133  0.5133  0.0000 -0.0000     sum 1.0266
//     centre:     0.2635  0.2635  0.2635  0.2635     sum 1.0539
//
// The sums are near 1 but NOT 1: a Gaussian RBF is not a partition of unity, and
// nothing here pretends otherwise.
//
// The blob is host-endian and host-layout ON PURPOSE. It is a cache that is
// rebuilt whenever the manifest hash moves, not an interchange format, so
// paying for portability would buy nothing; the magic and version exist to
// refuse a stale or foreign one rather than to make it portable.
#include "makehuman/core/CorrectiveBlob.h"

#include "makehuman/core/CorrectiveManifest.h"
#include "makehuman/foundation/Rbf.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh::core;
using Catch::Matchers::WithinAbs;

namespace {

/// Builds a manifest plus its `.target` payloads in a scratch directory.
///
/// Real files through the real readers, because the compiler's job is exactly
/// to join those two and a fixture that skipped them would not be testing it.
struct Fixture {
    std::filesystem::path dir;
    CorrectiveManifest manifest;
};

Fixture buildFixture(const std::string& name, const std::string& manifestText,
                     const std::vector<std::pair<std::string, std::string>>& targets) {
    Fixture f;
    f.dir = std::filesystem::temp_directory_path() / "mh_blob_tests" / name;
    std::filesystem::remove_all(f.dir);
    std::filesystem::create_directories(f.dir / "deltas");
    std::ofstream(f.dir / "correctives.json") << manifestText;
    for (const auto& [file, body] : targets) {
        std::ofstream(f.dir / "deltas" / file) << body;
    }
    auto m = loadCorrectiveManifest(f.dir / "correctives.json");
    REQUIRE(m.has_value());
    f.manifest = *m;
    return f;
}

/// Two drivers -- one swing (3 numbers), one twist (1) -- so the dimension is 4
/// and a compiler that collapsed the two would be caught.
const char* kManifest = R"({
  "formatVersion": 1,
  "topologyHash": "e38c060123b5d0db",
  "kernel": "gaussian",
  "radius": 1.5,
  "drivers": [
    { "joint": "upperarm01.L", "component": "swing" },
    { "joint": "lowerarm01.L", "component": "twist" }
  ],
  "poses": [
    { "name": "rest_ish", "signal": [0.0, 0.0, 0.0, 0.0], "delta": "deltas/a.target" },
    { "name": "up",       "signal": [0.0, 0.5, 0.0, 0.0], "delta": "deltas/b.target" },
    { "name": "twisted",  "signal": [0.0, 0.0, 0.0, 0.5], "delta": "deltas/c.target" }
  ]
})";

// `.target` is `index dx dy dz` per line (docs/formats/target.md). Deliberately
// different lengths, so an off-by-one in the delta index table shows up.
const char* kTargetA = "3 0.1 0.2 0.3\n7 -0.4 0.5 -0.6\n";
const char* kTargetB = "1 1.0 0.0 0.0\n";
const char* kTargetC = "0 0.25 0.25 0.25\n4 -1.0 -1.0 -1.0\n9 0.5 0.0 -0.5\n";

Fixture goodFixture(const std::string& name = "good") {
    return buildFixture(name, kManifest,
                        {{"a.target", kTargetA}, {"b.target", kTargetB}, {"c.target", kTargetC}});
}

}  // namespace

TEST_CASE("a compiled blob reads back as what went in", "[core][blob]") {
    const auto f     = goodFixture();
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());

    const auto blob = readCorrectiveBlob(*bytes);
    REQUIRE(blob.has_value());

    CHECK(blob->formatVersion == kCorrectiveBlobVersion);
    // The cache key. A blob whose manifest hash does not match the manifest on
    // disk is stale, and this is the whole mechanism directive 12.4 asks for.
    CHECK(blob->manifestHash == f.manifest.hash);
    CHECK(blob->topologyHash == 0xe38c060123b5d0dbULL);
    CHECK_THAT(blob->radius, WithinAbs(1.5, 1e-12));
    CHECK(blob->dimension == 4);
    CHECK(blob->poseCount == 3);

    REQUIRE(blob->drivers.size() == 2);
    CHECK(blob->drivers[0].joint == "upperarm01.L");
    CHECK(blob->drivers[0].component == DriverComponent::Swing);
    CHECK(blob->drivers[1].joint == "lowerarm01.L");
    CHECK(blob->drivers[1].component == DriverComponent::Twist);

    REQUIRE(blob->poseNames.size() == 3);
    CHECK(blob->poseNames[0] == "rest_ish");
    CHECK(blob->poseNames[2] == "twisted");
}

TEST_CASE("the deltas survive the round trip exactly", "[core][blob]") {
    // Three payloads of three different lengths, so an off-by-one in the index
    // table lands on the wrong pose rather than merely reading short.
    const auto f     = goodFixture("deltas");
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());
    const auto blob = readCorrectiveBlob(*bytes);
    REQUIRE(blob.has_value());

    REQUIRE(blob->deltas.size() == 3);
    REQUIRE(blob->deltas[0].verts.size() == 2);
    REQUIRE(blob->deltas[1].verts.size() == 1);
    REQUIRE(blob->deltas[2].verts.size() == 3);

    CHECK(blob->deltas[0].verts[0] == 3);
    CHECK(blob->deltas[0].verts[1] == 7);
    CHECK_THAT(static_cast<double>(blob->deltas[0].offsets[1].y), WithinAbs(0.5, 1e-6));

    CHECK(blob->deltas[1].verts[0] == 1);
    CHECK_THAT(static_cast<double>(blob->deltas[1].offsets[0].x), WithinAbs(1.0, 1e-6));

    CHECK(blob->deltas[2].verts[2] == 9);
    CHECK_THAT(static_cast<double>(blob->deltas[2].offsets[2].z), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(static_cast<double>(blob->deltas[2].offsets[1].x), WithinAbs(-1.0, 1e-6));
}

TEST_CASE("at an example pose the weight vector is that pose alone", "[core][blob]") {
    // What the compile step exists to arrange, and the reason the values solved
    // for are the identity matrix. If this does not hold, a sculpted pose does
    // not reproduce what the artist sculpted -- which is the one thing a
    // pose-space deformer must do.
    const auto f     = goodFixture("identity");
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());
    const auto blob = readCorrectiveBlob(*bytes);
    REQUIRE(blob.has_value());

    std::vector<double> out(blob->poseCount);
    for (size_t i = 0; i < blob->poseCount; ++i) {
        const auto signal = f.manifest.poses[i].signal;
        REQUIRE(mh::foundation::rbfEvaluate(blob->coefficients, signal, out));
        for (size_t j = 0; j < blob->poseCount; ++j) {
            CHECK_THAT(out[j], WithinAbs(i == j ? 1.0 : 0.0, 1e-9));
        }
    }
}

TEST_CASE("between example poses the weights blend", "[core][blob]") {
    // Not a partition of unity -- a Gaussian RBF does not sum to one -- so this
    // checks the property that actually matters: halfway between two poses both
    // are substantially on, and the pose that is far away is not.
    const auto f     = goodFixture("between");
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());
    const auto blob = readCorrectiveBlob(*bytes);
    REQUIRE(blob.has_value());

    const std::vector<double> midway{0.0, 0.25, 0.0, 0.0};  // between rest_ish and up
    std::vector<double> out(blob->poseCount);
    REQUIRE(mh::foundation::rbfEvaluate(blob->coefficients, midway, out));
    CHECK(out[0] > 0.3);
    CHECK(out[1] > 0.3);
    CHECK(out[2] < out[0]);
    CHECK(out[2] < out[1]);
}

TEST_CASE("a payload the compiler cannot read stops the compile", "[core][blob]") {
    SECTION("missing file") {
        const auto f =
            buildFixture("nopayload", kManifest, {{"a.target", kTargetA}, {"b.target", kTargetB}});
        const auto bytes = compileCorrectives(f.manifest);
        REQUIRE_FALSE(bytes.has_value());
        CHECK(bytes.error().kind == CorrectiveCompileErrorKind::PayloadUnreadable);
        // Named, or an author with forty poses has nowhere to start.
        CHECK(bytes.error().detail.find("twisted") != std::string::npos);
    }

    SECTION("a payload that parses to nothing") {
        // An empty `.target` is readable and moves no vertices. Refused: a pose
        // that sculpts nothing is an authoring mistake, and silently baking it
        // gives a corrective that does nothing with no way to notice.
        const auto f = buildFixture(
            "emptypayload", kManifest,
            {{"a.target", kTargetA}, {"b.target", "# nothing here\n"}, {"c.target", kTargetC}});
        const auto bytes = compileCorrectives(f.manifest);
        REQUIRE_FALSE(bytes.has_value());
        CHECK(bytes.error().kind == CorrectiveCompileErrorKind::EmptyPayload);
        CHECK(bytes.error().detail.find("up") != std::string::npos);
    }
}

TEST_CASE("a system that cannot be solved stops the compile", "[core][blob]") {
    // The manifest already refuses two poses at the same point, so the way this
    // is reached is a radius far larger than the spacing: every kernel value
    // rounds to 1 and the matrix collapses toward rank one. `rbfSolve` refuses
    // it rather than returning noise, and the compiler must pass that on rather
    // than baking a blob that evaluates to nonsense.
    //
    // The radius here is 1e6, and it is MEASURED rather than picked: the first
    // draft used 900 and this fixture solved it perfectly well. Bisected, three
    // poses at spacing 0.5 solve up to radius 707,115 and are refused above.
    //
    // That is worth knowing on its own. The 25-pose grid in test_rbf.cpp
    // collapses at about 12x its spacing; three poses tolerate a million times
    // theirs, because a small matrix stays well conditioned far longer. "Too
    // large a radius" is not a fixed multiple -- it depends on how many example
    // poses there are, which is why the check lives in the solver rather than
    // as a rule in the manifest reader.
    std::string text = kManifest;
    text.replace(text.find("\"radius\": 1.5"), 13, "\"radius\": 1000000.0");
    const auto f =
        buildFixture("unsolvable", text,
                     {{"a.target", kTargetA}, {"b.target", kTargetB}, {"c.target", kTargetC}});
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE_FALSE(bytes.has_value());
    CHECK(bytes.error().kind == CorrectiveCompileErrorKind::NotSolvable);
}

TEST_CASE("a blob that is not one is refused", "[core][blob]") {
    const auto f     = goodFixture("reject");
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());

    SECTION("empty") {
        const auto blob = readCorrectiveBlob({});
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().kind == CorrectiveBlobErrorKind::TooSmall);
    }

    SECTION("the wrong magic") {
        auto bad        = *bytes;
        bad[1]          = std::byte{'X'};
        const auto blob = readCorrectiveBlob(bad);
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().kind == CorrectiveBlobErrorKind::BadMagic);
    }

    SECTION("a version this build does not read") {
        // The blob format is versioned CHEAPLY and aggressively, which is the
        // opposite of the manifest: a blob can always be rebuilt, so bumping
        // costs a recompile rather than an author's afternoon.
        auto bad        = *bytes;
        bad[8]          = std::byte{99};
        const auto blob = readCorrectiveBlob(bad);
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().kind == CorrectiveBlobErrorKind::UnsupportedVersion);
    }

    SECTION("truncated ANYWHERE") {
        // Every prefix, not a couple of hand-picked lengths. A reader that
        // computes section offsets from counts has one bounds check per
        // section, and the one that is missing is the one nobody thought of.
        for (size_t n = 0; n < bytes->size(); ++n) {
            const auto blob = readCorrectiveBlob(std::span(*bytes).first(n));
            CHECK_FALSE(blob.has_value());
        }
        // ...and the whole thing still loads, so the loop is not passing by
        // refusing everything.
        CHECK(readCorrectiveBlob(*bytes).has_value());
    }

    SECTION("longer than it says it is") {
        // A blob is a cache read from a file whose length is known. Trailing
        // bytes mean it is not the file that was written.
        auto bad = *bytes;
        bad.push_back(std::byte{0});
        const auto blob = readCorrectiveBlob(bad);
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().kind == CorrectiveBlobErrorKind::SizeMismatch);
    }
}

TEST_CASE("a header whose counts disagree with the buffer is refused", "[core][blob]") {
    // Truncation is not the only way a blob goes wrong: the length field can
    // still match while a COUNT has been corrupted, and then every section
    // offset after it is wrong. Two guards existed for this and BOTH survived
    // mutation until this case was written -- the stored-length check caught
    // every truncation first, so nothing reached them.
    //
    // Field offsets are the layout in CorrectiveBlob.cpp. Each is read back
    // before it is corrupted, so a layout change fails this test loudly instead
    // of quietly corrupting some other byte.
    const auto f     = goodFixture("corrupt");
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());

    const auto peek = [&](size_t at) {
        uint32_t v{};
        std::memcpy(&v, bytes->data() + at, sizeof(v));
        return v;
    };
    const auto poke = [&](size_t at, uint32_t v) {
        auto bad = *bytes;
        std::memcpy(bad.data() + at, &v, sizeof(v));
        return bad;
    };

    struct Field {
        const char* name;
        size_t offset;
        uint32_t expected;
    };

    // 12 dimension, 40 poseCount, 44 driverCount, 48 nameBytes, 52 totalVerts.
    const Field fields[]{{"dimension", 12, 4},
                         {"poseCount", 40, 3},
                         {"driverCount", 44, 2},
                         {"nameBytes", 48, peek(48)},
                         {"totalVerts", 52, 6}};
    for (const Field& field : fields) {
        INFO(field.name);
        REQUIRE(peek(field.offset) == field.expected);
        for (const uint32_t wrong : {field.expected + 1, field.expected - 1, 0U}) {
            const auto blob = readCorrectiveBlob(poke(field.offset, wrong));
            CHECK_FALSE(blob.has_value());
        }
    }

    // ...and a delta span pointing outside the vertex table. The spans start
    // after the header (64), the centres (3 * 4 doubles) and the weights
    // (3 * 3 doubles): 64 + 96 + 72 = 232, two uint32 per pose.
    constexpr size_t kSpans = 232;
    REQUIRE(peek(kSpans) == 0);      // first pose starts at vertex 0
    REQUIRE(peek(kSpans + 4) == 2);  // and covers two vertices
    for (const auto [at, wrong] : std::vector<std::pair<size_t, uint32_t>>{
             {kSpans, 5},           // first, so first + count runs past the end
             {kSpans + 4, 99},      // count, likewise
             {kSpans + 4, 0},       // a pose that moves nothing
             {kSpans + 8, 999}}) {  // the second pose's start
        const auto blob = readCorrectiveBlob(poke(at, wrong));
        CHECK_FALSE(blob.has_value());
    }

    // The untouched blob still loads, so the loop is not refusing everything.
    CHECK(readCorrectiveBlob(*bytes).has_value());
}

TEST_CASE("each validation guard is reachable, not just the first one", "[core][blob]") {
    // The reader's guards are layered, and single-field corruption only ever
    // reaches the FIRST one -- the computed-layout length check catches every
    // wrong count, so four guards behind it survived mutation while doing
    // nothing observable. That is what a decorative gate looks like from the
    // outside, and the question is whether they are decorative or merely
    // shadowed.
    //
    // Each case below is a CRAFTED two-field corruption that keeps the computed
    // length right, so the guard under test is the one that fires. All four
    // turn out to be real: none is redundant, they were unreachable.
    const auto f     = goodFixture("layers");
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 400);

    const auto poke = [](std::vector<std::byte> b, size_t at, uint32_t v) {
        std::memcpy(b.data() + at, &v, sizeof(v));
        return b;
    };

    SECTION("a count of zero, with another count made to compensate") {
        // poseCount 3 -> 0 removes the centres, the weights and the delta spans;
        // nameBytes 46 -> 239 puts the same number of bytes back.
        auto bad        = poke(poke(*bytes, 40, 0), 48, 239);
        const auto blob = readCorrectiveBlob(bad);
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().detail.find("zero") != std::string::npos);
    }

    SECTION("a dimension the drivers do not add up to") {
        // dimension 4 -> 8 grows the centres by 96 bytes; totalVerts 6 -> 0
        // takes the same 96 back. The drivers still say swing + twist = 4.
        auto bad        = poke(poke(*bytes, 12, 8), 52, 0);
        const auto blob = readCorrectiveBlob(bad);
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().detail.find("drivers add up to") != std::string::npos);
    }

    SECTION("a name table that does not terminate where it should") {
        // One NUL overwritten. Same length, same layout: only the scan notices,
        // and without it a name would run into the next one.
        uint32_t nameBytes{};
        std::memcpy(&nameBytes, bytes->data() + 48, sizeof(nameBytes));
        const size_t names = bytes->size() - 2 - nameBytes;
        auto bad           = *bytes;
        bool clobbered     = false;
        for (size_t i = names; i < names + nameBytes; ++i) {
            if (bad[i] == std::byte{0}) {
                bad[i]    = std::byte{'x'};
                clobbered = true;
                break;
            }
        }
        REQUIRE(clobbered);
        const auto blob = readCorrectiveBlob(bad);
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().detail.find("name table") != std::string::npos);
    }

    SECTION("a driver component that is neither swing nor twist") {
        auto bad            = *bytes;
        bad[bad.size() - 1] = std::byte{7};
        const auto blob     = readCorrectiveBlob(bad);
        REQUIRE_FALSE(blob.has_value());
        CHECK(blob.error().detail.find("component") != std::string::npos);
    }
}

TEST_CASE("the blob is laid out so it can be mapped rather than parsed", "[core][blob]") {
    // "mmap-able" (directive 12.4) means the numeric sections can be read in
    // place. That needs their offsets to be aligned, or a span of doubles over
    // them is undefined behaviour rather than merely slow.
    const auto f     = goodFixture("aligned");
    const auto bytes = compileCorrectives(f.manifest);
    REQUIRE(bytes.has_value());
    const auto blob = readCorrectiveBlob(*bytes);
    REQUIRE(blob.has_value());

    const auto* base    = reinterpret_cast<const std::byte*>(bytes->data());
    const auto offsetOf = [&](const void* p) {
        return static_cast<size_t>(reinterpret_cast<const std::byte*>(p) - base);
    };
    CHECK(offsetOf(blob->coefficients.centres.data()) % alignof(double) == 0);
    CHECK(offsetOf(blob->coefficients.weights.data()) % alignof(double) == 0);
    for (const auto& d : blob->deltas) {
        CHECK(offsetOf(d.verts.data()) % alignof(uint32_t) == 0);
        CHECK(offsetOf(d.offsets.data()) % alignof(float) == 0);
    }
}

TEST_CASE("compiling twice gives the same bytes", "[core][blob]") {
    // A cache whose contents move without its inputs moving is a cache that
    // rebuilds for ever and defeats byte-comparison in any test downstream.
    const auto f = goodFixture("determinism");
    const auto a = compileCorrectives(f.manifest);
    const auto b = compileCorrectives(f.manifest);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
}

TEST_CASE("changing the manifest changes the blob's cache key", "[core][blob]") {
    // The invalidation directive 12.4 asks for, end to end: edit the manifest,
    // and the blob compiled from the old one no longer matches.
    const auto before = goodFixture("invalidate_a");
    const auto baked  = compileCorrectives(before.manifest);
    REQUIRE(baked.has_value());
    const auto blob = readCorrectiveBlob(*baked);
    REQUIRE(blob.has_value());
    CHECK(blob->manifestHash == before.manifest.hash);

    std::string text = kManifest;
    text.replace(text.find("\"radius\": 1.5"), 13, "\"radius\": 1.6");
    const auto after =
        buildFixture("invalidate_b", text,
                     {{"a.target", kTargetA}, {"b.target", kTargetB}, {"c.target", kTargetC}});
    CHECK(blob->manifestHash != after.manifest.hash);
}
