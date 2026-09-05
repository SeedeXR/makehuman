// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/AssetIndex.h"

#include "makehuman/core/Proxy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

using namespace mh::core;

namespace {

/// A throwaway asset tree.
class TempTree {
public:
    TempTree() {
        static int counter = 0;
        root_ = std::filesystem::temp_directory_path() / ("mh_ai_" + std::to_string(++counter));
        std::filesystem::create_directories(root_);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    TempTree(const TempTree&)            = delete;
    TempTree& operator=(const TempTree&) = delete;

    void write(const std::string& rel, std::string_view contents) const {
        const auto p = root_ / rel;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p);
        out << contents;
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

}  // namespace

TEST_CASE("the shipped assets index by uuid", "[core][assetindex][golden]") {
    const std::array<std::filesystem::path, 1> paths{std::filesystem::path(MH_DATA_DIR)};
    const auto idx = AssetIndex::build(paths);
    if (idx.size() == 0) SKIP("asset data not present");

    // Counted from disk: 4 proxies (base.mhclo, a7_converter.proxy, and the two
    // eye meshes) plus 11 materials -- the 3 originals (xray, default skin,
    // brown eye) and the 8 generated skin tones (tools/make_skins.py).
    CHECK(idx.size() == 15);
    CHECK(idx.duplicateUuids().empty());

    // The eye proxy's UUID, as it appears in data/eyes/high-poly/high-poly.mhclo.
    const AssetEntry* eye = idx.findByUuid("2c12f43b-1303-432c-b7ce-d78346baf2e6");
    REQUIRE(eye != nullptr);
    CHECK(eye->kind == AssetKind::Proxy);
    CHECK(eye->path.filename() == "high-poly.mhclo");
    CHECK(eye->name == "HighPolyEyes");
}

TEST_CASE("a uuid from a .mhm resolves to a loadable proxy", "[core][assetindex][golden]") {
    // This is the whole point of the index: .mhm stores only the UUID
    // (proxychooser.py:550-552 refuses filenames), so resolution must go
    // through here and the result must actually load.
    const std::array<std::filesystem::path, 1> paths{std::filesystem::path(MH_DATA_DIR)};
    const auto idx = AssetIndex::build(paths);
    if (idx.size() == 0) SKIP("asset data not present");

    const AssetEntry* e = idx.findByUuid("2c12f43b-1303-432c-b7ce-d78346baf2e6");
    REQUIRE(e != nullptr);

    const auto p = loadProxy(e->path);
    REQUIRE(p.has_value());
    CHECK(p->vertexCount() == 1064);
    CHECK(p->uuid == e->uuid);
}

TEST_CASE("an unknown uuid resolves to nothing", "[core][assetindex]") {
    const std::array<std::filesystem::path, 1> paths{std::filesystem::path(MH_DATA_DIR)};
    const auto idx = AssetIndex::build(paths);
    CHECK(idx.findByUuid("00000000-0000-0000-0000-000000000000") == nullptr);
    CHECK(idx.findByUuid("") == nullptr);
}

TEST_CASE("metadata is peeked without reading the vertex data", "[core][assetindex]") {
    // proxy.py:1035-1036 stops at the `verts` line. Anything after it must not
    // influence the metadata -- here a bogus uuid that would win if we kept
    // scanning.
    const TempTree t;
    t.write("a.mhclo",
            "name RealName\nuuid good-uuid\ntag Hat\nverts 3\n"
            "uuid bogus-uuid-after-verts\ntag ShouldNotAppear\n0\n1\n2\n");

    const AssetEntry e = peekAsset(t.root() / "a.mhclo");
    CHECK(e.uuid == "good-uuid");
    CHECK(e.name == "RealName");
    CHECK(e.tags.size() == 1);
    CHECK(e.tags.contains("hat"));
}

TEST_CASE("tags are lowercased and multi-word tags are kept whole", "[core][assetindex]") {
    const TempTree t;
    t.write("a.mhclo", "uuid u1\ntag Summer Dress\ntag WINTER\nverts 0\n");

    const AssetEntry e = peekAsset(t.root() / "a.mhclo");
    CHECK(e.tags.contains("summer dress"));
    CHECK(e.tags.contains("winter"));
}

TEST_CASE("search paths are ordered, earlier wins a uuid collision", "[core][assetindex]") {
    // User data must take precedence over system data (getpath.py:289-308).
    const TempTree userDir;
    const TempTree sysDir;
    userDir.write("mine.mhclo", "name UserVersion\nuuid shared-uuid\nverts 0\n");
    sysDir.write("theirs.mhclo", "name SystemVersion\nuuid shared-uuid\nverts 0\n");

    const std::array<std::filesystem::path, 2> paths{userDir.root(), sysDir.root()};
    const auto idx = AssetIndex::build(paths);

    const AssetEntry* e = idx.findByUuid("shared-uuid");
    REQUIRE(e != nullptr);
    CHECK(e->name == "UserVersion");

    // The collision is reported rather than silently resolved.
    REQUIRE(idx.duplicateUuids().size() == 1);
    CHECK(idx.duplicateUuids()[0] == "shared-uuid");
}

TEST_CASE("an asset without a uuid is still indexed", "[core][assetindex]") {
    // Materials frequently have no uuid; they are found by path or tag.
    const TempTree t;
    t.write("m.mhmat", "name NoUuid\ntag skin\n");

    const std::array<std::filesystem::path, 1> paths{t.root()};
    const auto idx = AssetIndex::build(paths);

    CHECK(idx.size() == 1);
    CHECK(idx.entries()[0].kind == AssetKind::Material);
    CHECK(idx.findByTag("skin").size() == 1);
}

TEST_CASE("tag lookup is case-insensitive", "[core][assetindex]") {
    const TempTree t;
    t.write("a.mhclo", "uuid u\ntag Fancy\nverts 0\n");

    const std::array<std::filesystem::path, 1> paths{t.root()};
    const auto idx = AssetIndex::build(paths);

    CHECK(idx.findByTag("fancy").size() == 1);
    CHECK(idx.findByTag("FANCY").size() == 1);
    CHECK(idx.findByTag("nope").empty());
}

TEST_CASE("proxy and material extensions are distinguished", "[core][assetindex]") {
    const TempTree t;
    t.write("a.mhclo", "uuid u1\nverts 0\n");
    t.write("b.proxy", "uuid u2\nverts 0\n");
    t.write("c.mhmat", "name mat\n");
    t.write("ignored.txt", "not an asset\n");

    const std::array<std::filesystem::path, 1> paths{t.root()};
    const auto idx = AssetIndex::build(paths);

    REQUIRE(idx.size() == 3);
    size_t proxies = 0, materials = 0;
    for (const AssetEntry& e : idx.entries()) {
        if (e.kind == AssetKind::Proxy) ++proxies;
        if (e.kind == AssetKind::Material) ++materials;
    }
    CHECK(proxies == 2);
    CHECK(materials == 1);
}

TEST_CASE("a missing search path is skipped, not fatal", "[core][assetindex]") {
    const std::array<std::filesystem::path, 2> paths{"/definitely/not/a/dir",
                                                     std::filesystem::path(MH_DATA_DIR)};
    const auto idx = AssetIndex::build(paths);
    CHECK(idx.size() > 0);  // the real path still indexed
}

TEST_CASE("allTags collects every distinct tag", "[core][assetindex]") {
    const TempTree t;
    t.write("a.mhclo", "uuid u1\ntag one\ntag two\nverts 0\n");
    t.write("b.mhclo", "uuid u2\ntag two\ntag three\nverts 0\n");

    const std::array<std::filesystem::path, 1> paths{t.root()};
    const auto idx  = AssetIndex::build(paths);
    const auto tags = idx.allTags();

    CHECK(tags.size() == 3);
    CHECK(tags.contains("one"));
    CHECK(tags.contains("two"));
    CHECK(tags.contains("three"));
}
