// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The `.meta` sidecar reader, and the one thing it is for: telling a pose a
// user wants from a fixture that exists to break the rig.
//
// `data/poses/` ships both. `tpose.meta` is `tag Rest poses`. `benchmark.meta`
// is `tag Developement`, described by its own author as "Benchmark pose used to
// test the rigging in extreme condition" -- and MEASURED, its hands
// interpenetrate, because a BVH stores joint rotations and carries no
// collision. Nothing was deforming it wrongly; it is authored impossible on
// purpose. A chooser built from filenames could not tell the difference.
#include "makehuman/core/AssetMeta.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mh::core;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

fs::path poses() {
    return dataDir() / "poses";
}

/// A `.meta` written into a temp dir, so the parsing tests do not depend on a
/// shipped file staying exactly as it is today.
struct TempMeta {
    fs::path dir;
    fs::path asset;

    explicit TempMeta(std::string_view body, std::string_view stem = "sample") {
        dir = fs::temp_directory_path() /
              ("mh_meta_" + std::to_string(std::hash<std::string_view>{}(body)));
        fs::create_directories(dir);
        asset = dir / (std::string(stem) + ".bvh");
        std::ofstream{asset} << "HIERARCHY\n";
        std::ofstream{dir / (std::string(stem) + ".meta")} << body;
    }

    TempMeta(const TempMeta&)            = delete;
    TempMeta& operator=(const TempMeta&) = delete;

    ~TempMeta() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

// ---------------------------------------------------------------- unit

TEST_CASE("the shipped pose metadata reads back as authored", "[core][meta]") {
    const auto tpose = loadAssetMeta(poses() / "tpose.bvh");
    CHECK(tpose.name == "T-pose");
    CHECK(tpose.hasTag("rest poses"));
    CHECK_FALSE(tpose.hasTag("developement"));

    const auto bench = loadAssetMeta(poses() / "benchmark.bvh");
    CHECK(bench.name == "Benchmark-pose");
    // `Developement` is the upstream file's own spelling, typo and all. Matched
    // as the data actually is, not as it ought to be.
    CHECK(bench.hasTag("developement"));
    CHECK_FALSE(bench.hasTag("rest poses"));
}

TEST_CASE("a missing sidecar is not an error", "[core][meta]") {
    // Most assets ship without one. The reference treats absence as "no
    // metadata", so a caller can use the result unconditionally.
    const auto meta = loadAssetMeta(dataDir() / "3dobjs" / "base.obj");
    // EMPTY, not the stem. The caller needs to tell "the sidecar named it" from
    // "there was no sidecar" -- an unnamed asset is labelled by
    // `prettyAssetName` like every other chooser, and defaulting to the stem
    // here made that fallback unreachable. See the header.
    CHECK(meta.name.empty());
    CHECK(meta.tags.empty());
}

TEST_CASE("keys are case-insensitive and values keep their spaces", "[core][meta]") {
    const TempMeta t("TAG Rest Poses\nName  A Long  Name\ndescription  two words\n");
    const auto meta = loadAssetMeta(t.asset);
    // Runs of whitespace COLLAPSE to one space. That is the reference's
    // behaviour, not an accident: it splits the line on whitespace and rejoins
    // with a single space (`' '.join(l[1:])`,
    // `3_libraries_pose.py:126`). Matched deliberately -- a display name that
    // differed from the reference's by its spacing would be a silent parity
    // break in every chooser label.
    CHECK(meta.name == "A Long Name");
    // Tags are lowercased on the way in, as the reference lowercases them.
    CHECK(meta.hasTag("rest poses"));
    CHECK(meta.hasTag("REST POSES"));
}

TEST_CASE("tags are de-duplicated and blank lines ignored", "[core][meta]") {
    const TempMeta t("tag one\n\n   \ntag ONE\ntag two\n");
    const auto meta = loadAssetMeta(t.asset);
    CHECK(meta.tags.size() == 2);
    CHECK(meta.hasTag("one"));
    CHECK(meta.hasTag("two"));
}

TEST_CASE("an unknown key is ignored, not refused", "[core][meta]") {
    // `.meta` has no schema and authors add fields. Refusing would reject files
    // the reference itself writes.
    // `license` is one of the reference's own keys that this deliberately does
    // not store, so it exercises the ignore path alongside a genuinely unknown
    // key -- both must leave the rest of the file parsing normally.
    const TempMeta t("name Kept\nsomethingelse whatever\nlicense CC0\n");
    const auto meta = loadAssetMeta(t.asset);
    CHECK(meta.name == "Kept");
    CHECK(meta.tags.empty());
}

TEST_CASE("a key with no value does not become a tag", "[core][meta]") {
    // `tag` alone would otherwise add an empty tag, and an empty tag matches
    // nothing a caller would ever ask for while still inflating the count.
    const TempMeta t("tag\nname Kept\n");
    const auto meta = loadAssetMeta(t.asset);
    CHECK(meta.tags.empty());
    CHECK(meta.name == "Kept");
}

TEST_CASE("a sidecar with no name leaves the name empty", "[core][meta]") {
    // The distinction the whole field rests on: this file EXISTS and is read,
    // and still names nothing, so the caller falls back exactly as it would for
    // an asset with no sidecar at all.
    const TempMeta t("tag rest poses\nlicense CC0\n");
    const auto meta = loadAssetMeta(t.asset);
    CHECK(meta.name.empty());
    CHECK(meta.hasTag("rest poses"));
}

// ----------------------------------------------------------- regression

TEST_CASE("every shipped pose is classifiable", "[core][meta][regression]") {
    // The chooser decides what to offer from these tags. A pose that grew a
    // `.meta` with neither a rest tag nor a dev tag would silently fall into
    // whichever branch the code happens to default to, so pin that every
    // shipped pose says what it is.
    size_t seen = 0;
    for (const auto& entry : fs::directory_iterator(poses())) {
        if (entry.path().extension() != ".bvh") continue;
        ++seen;
        const auto meta = loadAssetMeta(entry.path());
        INFO(entry.path().filename().string() << " must carry at least one tag");
        CHECK_FALSE(meta.tags.empty());
        CHECK_FALSE(meta.name.empty());
    }
    // MEASURED: data/poses ships exactly two .bvh files, tpose and benchmark.
    CHECK(seen == 2);
}
