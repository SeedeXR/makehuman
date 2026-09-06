// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every reader we own, against the same corpus of hostile bytes.
//
// The assertion is deliberately NOT "it returns an error". Some mutants are
// still perfectly valid files -- truncating an OBJ at a line boundary gives a
// smaller mesh, not a broken one -- and demanding failure would only teach the
// suite to accept whatever the parsers happen to do today.
//
// What is asserted is that the reader RETURNS: no crash, no hang, no read past
// the end of a buffer. Under ASan that last one is checked for real, which is
// where this test earns most of its keep. A reader that succeeds must also hand
// back something self-consistent -- an index that fits the array it indexes --
// because "parsed fine" followed by an out-of-bounds read downstream is the
// failure this class of bug actually produces.
//
// Mutants are derived from REAL shipped files rather than invented junk: junk
// bounces off the first `if` in a parser, while a file that is valid for a
// hundred thousand lines and then is not gets deep into the state machine
// before it breaks.

#include "makehuman/core/Material.h"
#include "makehuman/core/Mhm.h"
#include "makehuman/core/Modifier.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Proxy.h"
#include "makehuman/core/SliderLayout.h"
#include "makehuman/core/Target.h"
#include "makehuman/io/BvhReader.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/VertexWeights.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// Enough of a file to reach the interesting parser states without spending the
/// suite's wall-clock on a megabyte of weights. Only `.mhw` exceeds it, and a
/// prefix of a flat record list is still a list of every record type.
///
/// A prefix is NOT automatically representative, which is the trap this cap
/// walked into once already: an OBJ is SECTIONED -- every `v`, then every `vt`,
/// then every `f` -- so 256 KB of the 1.7 MB base.obj contained **no face lines
/// at all** and the whole face parser went unfuzzed. Removing both of the
/// codebase's vertex-index bounds checks did not fail this test. `mustContain`
/// below exists so that cannot happen again silently.
constexpr size_t kMaxSample = 512U * 1024U;

std::string readSample(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (s.size() > kMaxSample) s.resize(kMaxSample);
    return s;
}

/// A named mutant of @p base. Deterministic: a failure here must be
/// reproducible from the seed alone, or it cannot be debugged.
struct Mutant {
    std::string name;
    std::string bytes;
};

uint32_t nextRandom(uint32_t& state) {
    // xorshift32 rather than <random>: the ENGINES there are reproducible but the
    // DISTRIBUTIONS are not specified, so libc++ and libstdc++ give different
    // sequences from the same seed. A corpus that differs per standard library is
    // not a fixture, and a failure here has to be reproducible from the seed alone.
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

std::vector<Mutant> mutantsOf(const std::string& base) {
    std::vector<Mutant> out;
    // The control. Without it "nothing crashed" is satisfied just as well by a
    // reader that rejects every input, and the corpus would prove nothing.
    out.push_back({"unmodified", base});
    out.push_back({"empty", ""});

    for (const int pct : {1, 10, 50, 99}) {
        const size_t n = base.size() * static_cast<size_t>(pct) / 100U;
        out.push_back({"truncated_" + std::to_string(pct) + "pct", base.substr(0, n)});
    }

    // Valid prefix, then bytes no text format can mean anything by.
    const size_t half = base.size() / 2U;
    out.push_back({"half_then_0xFF", base.substr(0, half) + std::string(4096, '\xFF')});
    out.push_back({"half_then_NUL", base.substr(0, half) + std::string(4096, '\0')});

    // Every digit becomes a 9, so every count, index and length in the file is
    // enormous. This is the mutation that finds unchecked indices -- a parser
    // that trusts a vertex index reads far past the end of its array.
    std::string huge = base;
    for (char& c : huge)
        if (c >= '0' && c <= '9') c = '9';
    out.push_back({"all_digits_9", std::move(huge)});

    // Scattered single-byte flips, biased to nothing in particular.
    uint32_t seed = 0x5EED1234U;
    for (int i = 0; i < 8 && !base.empty(); ++i) {
        std::string flipped = base;
        for (int k = 0; k < 32; ++k) {
            const size_t at = nextRandom(seed) % flipped.size();
            flipped[at]     = static_cast<char>(nextRandom(seed) & 0xFFU);
        }
        out.push_back({"byteflip_" + std::to_string(i), std::move(flipped)});
    }
    return out;
}

fs::path writeTemp(const std::string& bytes, const char* stem) {
    const fs::path p = fs::temp_directory_path() / (std::string("mh_fuzz_") + stem);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

/// A reader under test. `run` must not throw and must not care what it is
/// handed; returning an error is the expected outcome, not a failure.
struct Reader {
    const char* name;
    fs::path sample;
    /// A record the capped sample must still contain, or the corpus is
    /// vacuous: it would fuzz the header and nothing else.
    std::string_view mustContain;
    std::function<void(const fs::path&)> run;
};

}  // namespace

TEST_CASE("every reader survives malformed input", "[malformed][robustness]") {
    using namespace mh;

    const std::vector<Reader> readers{
        // axis.obj, not base.obj: 6 KB against 1.7 MB, and it fits under the cap
        // whole, so the face section is actually reached.
        {"obj", dataDir() / "3dobjs" / "axis.obj", "\nf ",
         [](const fs::path& p) {
             const auto m = core::loadObj(p);
             if (!m) return;
             // A mesh that "loaded" must be indexable. This is the assertion
             // that matters: a parser can accept a corrupt index and only crash
             // later, in code that trusted it.
             for (const uint32_t v : m->fvert())
                 REQUIRE(v < m->vertexCount());
         }},
        {"mhclo", dataDir() / "eyes" / "high-poly" / "high-poly.mhclo", "verts",
         [](const fs::path& p) {
             const auto proxy = core::loadProxy(p);
             if (!proxy) return;
             for (const auto& ref : proxy->refVerts)
                 for (const uint32_t v : ref)
                     REQUIRE(v <= proxy->maxRefIndex());
         }},
        {"mhmat", dataDir() / "skins" / "default.mhmat", "shininess",
         [](const fs::path& p) { (void)core::loadMaterial(p); }},
        {"mhm", fs::path(MH_GOLDEN_DIR) / "mhm" / "reference_save.mhm", "modifier",
         [](const fs::path& p) { (void)core::loadMhm(p); }},
        // A real delta line, not just "\n": the header is 15 lines of licence
        // comment and a guard that only reaches it would be no guard at all.
        {"target", dataDir() / "targets" / "macrodetails" / "african-female-baby.target",
         "\n0 .119 -10.205 -.775", [](const fs::path& p) { (void)core::loadTarget(p); }},
        {"mhskel", dataDir() / "rigs" / "default.mhskel", "parent",
         [](const fs::path& p) {
             const auto s = rig::loadSkeleton(p);
             if (!s) return;
             // Parents must precede children and stay in range, which is the
             // invariant every traversal in the rig relies on.
             for (size_t i = 0; i < s->bones.size(); ++i)
                 REQUIRE(s->bones[i].parent < static_cast<int>(i));
         }},
        {"mhw", dataDir() / "rigs" / "default_weights.mhw", "weights",
         [](const fs::path& p) { (void)rig::loadWeights(p, 19158); }},
        {"bvh", dataDir() / "poses" / "tpose.bvh", "CHANNELS",
         [](const fs::path& p) { (void)io::readBvh(p); }},
        {"sliders.json", dataDir() / "modifiers" / "bodyshapes_sliders.json", "modifier",
         [](const fs::path& p) { (void)core::loadSliderLayout(p, {}); }},
        {"poseunits.json", dataDir() / "poseunits" / "face-poseunits.json", "framemapping",
         [](const fs::path& p) { (void)rig::loadPoseUnitNames(p); }},
        {"glb", fs::path(MH_GOLDEN_DIR) / "scene" / "two_cubes.glb", "glTF",
         [](const fs::path& p) { (void)io::importScene(p); }},
    };

    for (const Reader& r : readers) {
        DYNAMIC_SECTION(r.name) {
            const std::string base = readSample(r.sample);
            INFO("sample: " << r.sample.string());
            REQUIRE_FALSE(base.empty());  // a missing sample would pass vacuously
            // ...and so would a sample capped before its interesting records.
            INFO("must contain: " << r.mustContain);
            REQUIRE(base.find(r.mustContain) != std::string::npos);

            for (const Mutant& mut : mutantsOf(base)) {
                INFO("mutant: " << mut.name);
                const fs::path f = writeTemp(mut.bytes, r.name);
                r.run(f);
                std::error_code ec;
                fs::remove(f, ec);
            }

            // Not a file at all. Both are ordinary user mistakes -- a path that
            // moved, a directory picked in a file dialog -- and both must be
            // errors rather than undefined behaviour.
            r.run(fs::temp_directory_path() / "mh_fuzz_does_not_exist");
            r.run(fs::temp_directory_path());
        }
    }
}

// ---------------------------------------------------------------------------
// A DIRECTORY IS NOT A FILE, and every reader here treated it as one.
//
// `std::filesystem::exists()` is true for a directory and `std::ifstream`
// OPENS one, so the universal `if (!in) return NotFound;` guard never fires.
// What happens next splits by how the parser reads:
//
//   * a parser that calls `sbumpc()` (nlohmann's) THROWS on the first read;
//   * a parser that calls `in.get()` or `std::getline` merely sets badbit and
//     sees end-of-input, so it happily parses a directory into a valid, EMPTY
//     asset and returns success.
//
// The second is ours on every standard library, and it is the worse half: an
// exception is at least loud. `data/` is full of directories sitting next to
// the files these readers want, so this is one mistyped path away.
//
// This file has handed readers a directory since it was written. It could not
// see any of it, because it discarded the result. These assert the error KIND.
// ---------------------------------------------------------------------------

TEST_CASE("every reader refuses a directory", "[malformed][directory]") {
    // A real directory that certainly exists, with real assets beside it --
    // which is the point: `data/` is full of these, so this is one mistyped
    // path away.
    const std::filesystem::path dir = std::filesystem::path(MH_DATA_DIR) / "targets";
    REQUIRE(std::filesystem::is_directory(dir));

    // ONE flat case with CHECK, not SECTIONs with REQUIRE. The first version of
    // this test used both, and Catch2 abandons a section at its first failed
    // REQUIRE -- so `loadMaterial` failing hid `loadMhm` and `loadProxy`
    // entirely, and they were doing the same thing. Every reader is asserted
    // independently now.
    //
    // The kind matters as much as the failure. `Unreadable` says "I could not
    // read this"; `Malformed` -- what four of these returned -- says the
    // CONTENTS are wrong, which sends whoever is debugging it to inspect a file
    // that is not a file.
    const auto mat = mh::core::loadMaterial(dir);
    CHECK_FALSE(mat.has_value());
    if (!mat) CHECK(mat.error().kind == mh::core::MaterialErrorKind::Unreadable);

    const auto mhm = mh::core::loadMhm(dir);
    CHECK_FALSE(mhm.has_value());
    if (!mhm) CHECK(mhm.error().kind == mh::core::MhmErrorKind::Unreadable);

    const auto proxy = mh::core::loadProxy(dir);
    CHECK_FALSE(proxy.has_value());
    if (!proxy) CHECK(proxy.error().kind == mh::core::ProxyErrorKind::Unreadable);

    const auto mods = mh::core::loadModifiers(dir);
    CHECK_FALSE(mods.has_value());
    if (!mods) CHECK(mods.error().kind == mh::core::ModifierErrorKind::Unreadable);

    const auto layout = mh::core::loadSliderLayout(dir, {});
    CHECK_FALSE(layout.has_value());
    if (!layout) CHECK(layout.error().kind == mh::core::SliderLayoutErrorKind::Unreadable);

    const auto obj = mh::core::loadObj(dir);
    CHECK_FALSE(obj.has_value());
    if (!obj) CHECK(obj.error().kind == mh::core::ObjErrorKind::Unreadable);

    const auto skel = mh::rig::loadSkeleton(dir);
    CHECK_FALSE(skel.has_value());
    if (!skel) CHECK(skel.error().kind == mh::rig::SkeletonErrorKind::Unreadable);

    const auto units = mh::rig::loadPoseUnitNames(dir);
    CHECK_FALSE(units.has_value());
    if (!units) CHECK(units.error().kind == mh::rig::PoseUnitsErrorKind::Unreadable);
}

// A missing path must still say NotFound. NotAFile mapping to Unreadable is
// deliberate -- something IS there -- but collapsing both into one answer would
// lose the distinction that tells a user whether to check the spelling or the
// permissions.
TEST_CASE("a missing path is still NotFound, not Unreadable", "[malformed][directory]") {
    const std::filesystem::path missing =
        std::filesystem::path(MH_DATA_DIR) / "no-such-thing-anywhere.mhmat";
    REQUIRE_FALSE(std::filesystem::exists(missing));

    const auto mat = mh::core::loadMaterial(missing);
    CHECK_FALSE(mat.has_value());
    if (!mat) CHECK(mat.error().kind == mh::core::MaterialErrorKind::NotFound);

    const auto skel = mh::rig::loadSkeleton(missing);
    CHECK_FALSE(skel.has_value());
    if (!skel) CHECK(skel.error().kind == mh::rig::SkeletonErrorKind::NotFound);
}
