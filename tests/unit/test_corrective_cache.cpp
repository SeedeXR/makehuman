// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The blob as a CACHE, which is the half of owner directive 12.4's third layer
// that was still missing.
//
// The format and the compiler existed; nothing wrote a blob to disk, checked
// whether it was stale, or rebuilt it. The directive is explicit about what it
// should be: "the blob only. It is a DISPOSABLE CACHE, invalidated on manifest
// hash mismatch."
//
// Two words in that sentence carry the design:
//
//   DISPOSABLE  deleting it must cost nothing but time, and FAILING TO WRITE IT
//               must not fail the load. A cache that can break a character by
//               being unwritable is not a cache.
//   INVALIDATED the manifest's content hash is the key, and anything that does
//               not match it -- stale, corrupt, truncated, from another
//               manifest, or in a blob format this build no longer reads -- is
//               rebuilt rather than reported.
//
// So there is no separate compiler tool. `loadOrCompileCorrectives` IS the entry
// point: the app calls it, a batch script calls it, and both get the same
// rebuild-when-stale behaviour rather than two implementations of it.
#include "makehuman/core/CorrectiveCache.h"

#include "makehuman/core/CorrectiveBlob.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

/// A manifest and its payloads in a fresh scratch directory.
struct Fixture {
    std::filesystem::path dir;
    std::filesystem::path manifest;
    std::filesystem::path blob;
};

/// Overwrites only the manifest, leaving whatever blob is beside it.
///
/// Separate from `write` because `write` wipes the directory: an earlier
/// version of the invalidation test called it twice and deleted the very blob
/// it needed to still be there, so the case passed for the wrong reason until
/// the file-exists check caught it.
void rewriteManifest(const Fixture& f, double radius) {
    std::ofstream(f.manifest) << R"({
  "formatVersion": 1,
  "topologyHash": "e38c060123b5d0db",
  "kernel": "gaussian",
  "radius": )" << radius << R"(,
  "drivers": [ { "joint": "upperarm01.L", "component": "swing" } ],
  "poses": [
    { "name": "a", "signal": [0.0, 0.0, 0.0], "delta": "deltas/a.target" },
    { "name": "b", "signal": [0.0, 0.0, 0.5], "delta": "deltas/b.target" }
  ]
})";
}

Fixture write(const std::string& name, double radius = 1.5) {
    Fixture f;
    f.dir = std::filesystem::temp_directory_path() / "mh_cache_tests" / name;
    std::filesystem::remove_all(f.dir);
    std::filesystem::create_directories(f.dir / "deltas");
    f.manifest = f.dir / "correctives.json";
    f.blob     = f.dir / "correctives.mhcorr";
    rewriteManifest(f, radius);
    std::ofstream(f.dir / "deltas/a.target") << "3 0.1 0.2 0.3\n";
    std::ofstream(f.dir / "deltas/b.target") << "7 0.4 0.5 0.6\n";
    return f;
}

std::vector<std::byte> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) {
        bytes.push_back(static_cast<std::byte>(*it));
    }
    return bytes;
}

void overwrite(const std::filesystem::path& p, std::span<const std::byte> bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST_CASE("the first call compiles and leaves a blob behind", "[core][cache]") {
    const Fixture f = write("fresh");
    REQUIRE_FALSE(std::filesystem::exists(f.blob));

    const auto cache = loadOrCompileCorrectives(f.manifest);
    REQUIRE(cache.has_value());
    CHECK(cache->status == CorrectiveCacheStatus::Rebuilt);
    CHECK(cache->blobPath == f.blob);
    CHECK(std::filesystem::exists(f.blob));

    // What came back is a real blob, not just bytes.
    const auto blob = readCorrectiveBlob(cache->bytes);
    REQUIRE(blob.has_value());
    CHECK(blob->poseCount == 2);
}

TEST_CASE("the second call reuses what the first left", "[core][cache]") {
    // The point of the cache. Reuse is asserted through the STATUS and through
    // the file being untouched, because "it returned the right bytes" is true
    // whether or not it recompiled.
    const Fixture f  = write("reuse");
    const auto first = loadOrCompileCorrectives(f.manifest);
    REQUIRE(first.has_value());
    REQUIRE(first->status == CorrectiveCacheStatus::Rebuilt);
    const auto written = std::filesystem::last_write_time(f.blob);

    const auto second = loadOrCompileCorrectives(f.manifest);
    REQUIRE(second.has_value());
    CHECK(second->status == CorrectiveCacheStatus::Reused);
    CHECK(second->bytes == first->bytes);
    // Compared outside the macro: Catch2 cannot stringify a file_time_type
    // (its duration is __int128), so decomposing the expression fails to build.
    const bool untouched = std::filesystem::last_write_time(f.blob) == written;
    CHECK(untouched);
}

TEST_CASE("editing the manifest invalidates the blob", "[core][cache]") {
    // Directive 12.4's "invalidated on manifest hash mismatch", end to end.
    const Fixture f   = write("invalidate");
    const auto before = loadOrCompileCorrectives(f.manifest);
    REQUIRE(before.has_value());

    // Only the manifest changes; the blob from the OLD one stays where it is,
    // which is the situation the hash check exists for.
    rewriteManifest(f, 1.6);
    REQUIRE(std::filesystem::exists(f.blob));

    const auto after = loadOrCompileCorrectives(f.manifest);
    REQUIRE(after.has_value());
    CHECK(after->status == CorrectiveCacheStatus::Rebuilt);
    CHECK(after->bytes != before->bytes);

    // ...and the rebuilt one is now the current one, so a third call reuses.
    const auto third = loadOrCompileCorrectives(f.manifest);
    REQUIRE(third.has_value());
    CHECK(third->status == CorrectiveCacheStatus::Reused);
}

TEST_CASE("the load hands back the manifest it read", "[core][cache]") {
    // The manifest is parsed on EVERY call already -- that is this function's
    // whole design, so that a manifest which no longer loads is an error rather
    // than a silently-still-working character. Returning it costs nothing and
    // is what lets a caller reach the fields the blob deliberately does not
    // bake: the per-pose wrinkle paths (see docs/formats/corrective-manifest.md,
    // "Not baked into the blob").
    //
    // BOTH paths, and that is the assertion worth having. Filling this only
    // where the blob is compiled is the obvious way to write it, and it would
    // leave every second load -- every load, in practice, since the blob
    // persists -- with an empty manifest and a character with no wrinkles.
    const Fixture f = write("manifest_back");

    const auto first = loadOrCompileCorrectives(f.manifest);
    REQUIRE(first.has_value());
    REQUIRE(first->status == CorrectiveCacheStatus::Rebuilt);
    CHECK(first->manifest.poses.size() == 2);
    CHECK(first->manifest.poses[0].name == "a");
    CHECK(first->manifest.radius == 1.5);

    const auto second = loadOrCompileCorrectives(f.manifest);
    REQUIRE(second.has_value());
    REQUIRE(second->status == CorrectiveCacheStatus::Reused);
    CHECK(second->manifest.poses.size() == 2);
    CHECK(second->manifest.poses[0].name == "a");
    // The same manifest, so the same hash -- which is also what made the blob
    // reusable, so this is the two answers agreeing rather than one of them.
    CHECK(second->manifest.hash == first->manifest.hash);
}

TEST_CASE("a blob that cannot be trusted is rebuilt, not reported", "[core][cache]") {
    // Every one of these is a cache miss rather than an error. A disposable
    // cache that can fail a load is not disposable.
    SECTION("truncated") {
        const Fixture f = write("truncated");
        REQUIRE(loadOrCompileCorrectives(f.manifest).has_value());
        auto bytes = readFile(f.blob);
        bytes.resize(bytes.size() / 2);
        overwrite(f.blob, bytes);

        const auto cache = loadOrCompileCorrectives(f.manifest);
        REQUIRE(cache.has_value());
        CHECK(cache->status == CorrectiveCacheStatus::Rebuilt);
        CHECK(readCorrectiveBlob(cache->bytes).has_value());
    }

    SECTION("empty") {
        const Fixture f = write("empty_blob");
        REQUIRE(loadOrCompileCorrectives(f.manifest).has_value());
        std::ofstream(f.blob, std::ios::binary | std::ios::trunc);

        const auto cache = loadOrCompileCorrectives(f.manifest);
        REQUIRE(cache.has_value());
        CHECK(cache->status == CorrectiveCacheStatus::Rebuilt);
    }

    SECTION("not a blob at all") {
        const Fixture f = write("not_a_blob");
        std::ofstream(f.blob, std::ios::binary) << "this is not a corrective blob";
        const auto cache = loadOrCompileCorrectives(f.manifest);
        REQUIRE(cache.has_value());
        CHECK(cache->status == CorrectiveCacheStatus::Rebuilt);
    }

    SECTION("a blob format this build no longer reads") {
        // Why the directive says to version the blob CHEAPLY and aggressively:
        // bumping it costs a recompile, and this is that recompile happening
        // without anyone being told.
        const Fixture f = write("old_version");
        REQUIRE(loadOrCompileCorrectives(f.manifest).has_value());
        auto bytes = readFile(f.blob);
        REQUIRE(bytes.size() > 12);
        bytes[8] = std::byte{99};  // formatVersion, per the blob layout
        overwrite(f.blob, bytes);

        const auto cache = loadOrCompileCorrectives(f.manifest);
        REQUIRE(cache.has_value());
        CHECK(cache->status == CorrectiveCacheStatus::Rebuilt);
        const auto blob = readCorrectiveBlob(cache->bytes);
        REQUIRE(blob.has_value());
        CHECK(blob->formatVersion == kCorrectiveBlobVersion);
    }

    SECTION("a valid blob compiled from a DIFFERENT manifest") {
        // The realistic version of staleness: the file parses, its version is
        // right, and it simply belongs to something else. Only the hash says so.
        const Fixture mine  = write("hash_mine");
        const Fixture other = write("hash_other", 2.1);
        REQUIRE(loadOrCompileCorrectives(other.manifest).has_value());
        std::filesystem::copy_file(other.blob, mine.blob,
                                   std::filesystem::copy_options::overwrite_existing);

        const auto cache = loadOrCompileCorrectives(mine.manifest);
        REQUIRE(cache.has_value());
        CHECK(cache->status == CorrectiveCacheStatus::Rebuilt);
    }
}

TEST_CASE("a cache that cannot be written is still a successful load", "[core][cache]") {
    // DISPOSABLE, taken literally. A read-only asset directory, a full disk or
    // a sandbox must cost the rebuild time, not the character.
    const Fixture f = write("readonly");
    std::filesystem::permissions(
        f.dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);

    const auto cache = loadOrCompileCorrectives(f.manifest);
    // Restore before asserting, or a failure leaves an undeletable directory
    // behind for every later run.
    std::filesystem::permissions(f.dir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);

    REQUIRE(cache.has_value());
    CHECK(cache->status == CorrectiveCacheStatus::Rebuilt);
    CHECK(readCorrectiveBlob(cache->bytes).has_value());
    CHECK_FALSE(std::filesystem::exists(f.blob));
}

TEST_CASE("what is genuinely wrong is reported, not cached around", "[core][cache]") {
    SECTION("no manifest") {
        const auto cache = loadOrCompileCorrectives(std::filesystem::temp_directory_path() /
                                                    "mh_cache_tests" / "nope.json");
        REQUIRE_FALSE(cache.has_value());
        CHECK(cache.error().kind == CorrectiveCacheErrorKind::Manifest);
    }

    SECTION("a manifest that does not validate") {
        const Fixture f = write("bad_manifest");
        std::ofstream(f.manifest) << R"({"formatVersion": 99})";
        const auto cache = loadOrCompileCorrectives(f.manifest);
        REQUIRE_FALSE(cache.has_value());
        CHECK(cache.error().kind == CorrectiveCacheErrorKind::Manifest);
    }

    SECTION("a payload the compiler cannot read") {
        const Fixture f = write("bad_payload");
        std::filesystem::remove(f.dir / "deltas/b.target");
        const auto cache = loadOrCompileCorrectives(f.manifest);
        REQUIRE_FALSE(cache.has_value());
        CHECK(cache.error().kind == CorrectiveCacheErrorKind::Compile);
        CHECK(cache.error().detail.find("b") != std::string::npos);
    }

    SECTION("a stale blob does NOT rescue a broken manifest") {
        // The trap a cache invites: a good blob on disk next to a manifest that
        // no longer loads. Returning the blob would keep a character working
        // that nobody can rebuild, and hide the breakage until the cache is
        // cleared -- on someone else's machine.
        const Fixture f = write("stale_rescue");
        REQUIRE(loadOrCompileCorrectives(f.manifest).has_value());
        REQUIRE(std::filesystem::exists(f.blob));
        std::ofstream(f.manifest) << R"({"formatVersion": 99})";

        const auto cache = loadOrCompileCorrectives(f.manifest);
        REQUIRE_FALSE(cache.has_value());
        CHECK(cache.error().kind == CorrectiveCacheErrorKind::Manifest);
    }
}
