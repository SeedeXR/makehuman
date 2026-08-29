// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Regression tests for the io/asset review findings on commit 869497b2.
// Every test here FAILS on the pre-fix code. Two were memory-safety bugs
// reachable by loading an untrusted asset file.

#include "makehuman/core/AssetIndex.h"
#include "makehuman/core/Material.h"
#include "makehuman/core/Mesh.h"
#include "makehuman/core/Proxy.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"

#include <catch2/catch_test_macros.hpp>

#include <clocale>
#include <filesystem>
#include <fstream>
#include <locale>
#include <string>
#include <vector>

namespace {

/// A file that deletes itself, so a failing assertion cannot leave litter.
class TempFile {
public:
    TempFile(std::string_view name, std::string_view contents) {
        static int counter = 0;
        path_              = std::filesystem::temp_directory_path() /
                ("mh_io_reg_" + std::to_string(++counter) + "_" + std::string(name));
        std::ofstream out(path_);
        out << contents;
    }

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempFile(const TempFile&)            = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

mh::core::Mesh triangleMesh() {
    mh::core::Mesh m("tri", 4);
    (void)m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
    (void)m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    (void)m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0});
    return m;
}

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

// ---------------------------------------------------------------- proxy

// HIGH. `p.deleteVerts.resize(v + 1)` computed v+1 in uint32: at UINT32_MAX it
// wrapped to 0, the vector was resized to EMPTY, and the next line wrote to
// index 4294967295. ASan: "BUS on unknown address ... WRITE memory access" at
// loadProxy. A five-line text file was enough.
TEST_CASE("a delete_verts index cannot overflow the resize", "[regression][proxy][security]") {
    const TempFile f("oob.mhclo",
                     "name Evil\nobj_file e.obj\nverts 0\ndelete_verts\n5 4294967295\n");
    const auto p = mh::core::loadProxy(f.path());
    REQUIRE_FALSE(p.has_value());
    CHECK(p.error().kind == mh::core::ProxyErrorKind::IndexOutOfRange);
}

// HIGH. `for (uint32_t i = last + 1; i <= v; ++i)` never terminated when
// v == UINT32_MAX -- i wrapped to 0 and the condition held again, pushing
// forever. Without the wrap, `delete_verts 4294967290` still allocated 4.29 GB
// from a two-line file. If this test hangs or OOMs, the guard is gone.
TEST_CASE("a delete_verts range cannot allocate without bound", "[regression][proxy][security]") {
    const TempFile loop("loop.mhclo",
                        "name Evil\nobj_file e.obj\nverts 0\ndelete_verts\n0 - 4294967295\n");
    const auto a = mh::core::loadProxy(loop.path());
    REQUIRE_FALSE(a.has_value());
    CHECK(a.error().kind == mh::core::ProxyErrorKind::IndexOutOfRange);

    const TempFile big("big.mhclo",
                       "name Evil\nobj_file e.obj\nverts 0\ndelete_verts\n4294967290\n");
    const auto b = mh::core::loadProxy(big.path());
    REQUIRE_FALSE(b.has_value());
    CHECK(b.error().kind == mh::core::ProxyErrorKind::IndexOutOfRange);
}

// A '-' range carries across lines in the oracle, because v0 is a
// function-level local there (proxy.py:516-529). Confirmed by running that
// loop on this exact input: it deletes 10,11,12,13,14.
TEST_CASE("a delete_verts range spans a line break", "[regression][proxy][parity]") {
    const TempFile f("cross.mhclo", "name X\nobj_file e.obj\nverts 0\ndelete_verts\n10\n- 14\n");
    const auto p = mh::core::loadProxy(f.path());
    REQUIRE(p.has_value());
    REQUIRE(p->deleteVerts.size() == 15);
    for (size_t v = 10; v <= 14; ++v) {
        CAPTURE(v);
        CHECK(p->deleteVerts[v] == 1);
    }
    CHECK(p->deleteVerts[9] == 0);
}

// A verts line with 2-5 fields fell through silently. Proxy vertex n binds to
// vertex n of the sibling .obj, so a dropped line shifts every later vertex
// onto the wrong reference triangle: plausible geometry, silently wrong.
TEST_CASE("a truncated verts line is rejected, not skipped", "[regression][proxy]") {
    const TempFile f("trunc.mhclo",
                     "name X\nobj_file e.obj\nverts 3\n"
                     "0 1 2 0.5 0.25 0.25\n"
                     "5 6 7 0.5\n"
                     "1 2 3 0.5 0.25 0.25\n");
    const auto p = mh::core::loadProxy(f.path());
    REQUIRE_FALSE(p.has_value());
    CHECK(p.error().kind == mh::core::ProxyErrorKind::MalformedLine);
}

// The oracle substitutes 50 for exactly -1, not for any negative value.
TEST_CASE("a negative z_depth other than -1 is preserved", "[regression][proxy][parity]") {
    const TempFile neg("z.mhclo", "name X\nobj_file e.obj\nz_depth -5\nverts 0\n");
    const auto a = mh::core::loadProxy(neg.path());
    REQUIRE(a.has_value());
    CHECK(a->zDepth == -5);

    const TempFile sentinel("z1.mhclo", "name X\nobj_file e.obj\nz_depth -1\nverts 0\n");
    const auto b = mh::core::loadProxy(sentinel.path());
    REQUIRE(b.has_value());
    CHECK(b->zDepth == 50);
}

// ------------------------------------------------------------- material

// A known key with an unparseable value is documented as an error, but
// readColor's result was discarded: both of these loaded as pure white.
TEST_CASE("a malformed colour is an error, not a silent default", "[regression][material]") {
    const TempFile few("c1.mhmat", "name X\ndiffuseColor 0.5 0.5\n");
    CHECK_FALSE(mh::core::loadMaterial(few.path()).has_value());

    const TempFile junk("c2.mhmat", "name X\ndiffuseColor 0.5 zzz 0.5\n");
    CHECK_FALSE(mh::core::loadMaterial(junk.path()).has_value());
}

// material.py:396 clamps to 0..1; this clamped to +/-1e30.
TEST_CASE("translucency is clamped to 0..1", "[regression][material][parity]") {
    const TempFile hi("t1.mhmat", "name X\ntranslucency 5\n");
    const auto a = mh::core::loadMaterial(hi.path());
    REQUIRE(a.has_value());
    CHECK(a->translucency == 1.0F);

    const TempFile lo("t2.mhmat", "name X\ntranslucency -1\n");
    const auto b = mh::core::loadMaterial(lo.path());
    REQUIRE(b.has_value());
    CHECK(b->translucency == 0.0F);
}

// from_chars rejects a leading '+' where the oracle's float() accepts it, so a
// legal asset failed to load outright.
TEST_CASE("a leading + is accepted and nan/inf are not", "[regression][material][parity]") {
    const TempFile plus("p.mhmat", "name X\nopacity +0.5\n");
    const auto a = mh::core::loadMaterial(plus.path());
    REQUIRE(a.has_value());
    CHECK(a->opacity == 0.5F);

    const TempFile nan("n.mhmat", "name X\nopacity nan\n");
    CHECK_FALSE(mh::core::loadMaterial(nan.path()).has_value());
}

// material.py:392 sets _hasViewPortColor on viewPortAlpha too.
TEST_CASE("viewPortAlpha sets hasViewPortColor", "[regression][material][parity]") {
    const TempFile f("v.mhmat", "name X\nviewPortAlpha 0.25\n");
    const auto m = mh::core::loadMaterial(f.path());
    REQUIRE(m.has_value());
    CHECK(m->hasViewPortColor);
    CHECK(m->viewPortAlpha == 0.25F);
}

// ---------------------------------------------------------- asset index

// A duplicate UUID skipped entries_.push_back entirely, so the asset vanished
// from entries(), findByTag() and allTags() -- not just from UUID resolution.
TEST_CASE("a duplicate-UUID asset is still indexed", "[regression][assetindex]") {
    const auto root = std::filesystem::temp_directory_path() / "mh_dupe_uuid_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    {
        std::ofstream(root / "a.mhmat") << "name A\nuuid same\ntag hat\n";
        std::ofstream(root / "b.mhmat") << "name B\nuuid same\ntag hat\n";
        std::ofstream(root / "c.mhmat") << "name C\nuuid uniq\ntag hat\n";
    }
    const std::filesystem::path roots[] = {root};
    const auto idx                      = mh::core::AssetIndex::build(roots);

    CHECK(idx.entries().size() == 3);
    CHECK(idx.findByTag("hat").size() == 3);
    CHECK(idx.duplicateUuids().size() == 1);

    std::filesystem::remove_all(root, ec);
}

// recursive_directory_iterator's operator++ THROWS; the error_code overload
// only covers construction. One unreadable subdirectory under a search path
// aborted the process: "terminating due to uncaught exception".
TEST_CASE("an unreadable subdirectory does not abort the scan", "[regression][assetindex]") {
    const auto root = std::filesystem::temp_directory_path() / "mh_perm_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "secret", ec);
    std::filesystem::create_directories(root / "ok", ec);
    std::ofstream(root / "ok" / "a.mhmat") << "name A\nuuid u1\n";

    std::filesystem::permissions(root / "secret", std::filesystem::perms::none, ec);
    if (ec) {  // some filesystems refuse; then there is nothing to test here
        std::filesystem::remove_all(root, ec);
        SUCCEED("filesystem does not support removing directory permissions");
        return;
    }

    const std::filesystem::path roots[] = {root};
    const auto idx                      = mh::core::AssetIndex::build(roots);  // used to abort
    CHECK(idx.entries().size() == 1);

    std::filesystem::permissions(root / "secret", std::filesystem::perms::owner_all, ec);
    std::filesystem::remove_all(root, ec);
}

// ------------------------------------------------------------- writers

// snprintf honours LC_NUMERIC exactly as iostreams do, and locale::global also
// sets the C locale. Under de_DE.UTF-8 the OBJ came out as "v 0,5000 ..." and
// the GLB's "max":[0.2,0.3] became a five-element array -- valid JSON with
// garbage accessor bounds, which no validator would flag as a parse error.
TEST_CASE("writers do not honour the decimal-comma locale", "[regression][io][locale]") {
    const std::locale saved = std::locale();
    try {
        std::locale::global(std::locale("de_DE.UTF-8"));
    } catch (const std::runtime_error&) {
        SUCCEED("de_DE.UTF-8 not installed on this machine");
        return;
    }

    const auto mesh = triangleMesh();
    const auto dir  = std::filesystem::temp_directory_path();
    const auto obj  = dir / "mh_locale_test.obj";
    const auto glb  = dir / "mh_locale_test.glb";

    const auto o = mh::io::writeObj(obj, mesh.view(), {});
    const auto g = mh::io::writeGlb(glb, mh::core::RenderMesh::build(mesh).view(), {});

    std::locale::global(saved);  // restore before asserting, so a failure cannot leak it

    REQUIRE(o.has_value());
    REQUIRE(g.has_value());

    const std::string objText = readAll(obj);
    CHECK(objText.find(',') == std::string::npos);

    const std::string glbBytes = readAll(glb);
    const auto jsonStart       = glbBytes.find('{');
    REQUIRE(jsonStart != std::string::npos);
    // "min":[a,b,c] must have exactly three elements: two commas, not four.
    const auto minAt = glbBytes.find("\"min\":[", jsonStart);
    REQUIRE(minAt != std::string::npos);
    const auto minEnd = glbBytes.find(']', minAt);
    REQUIRE(minEnd != std::string::npos);
    const std::string minArr = glbBytes.substr(minAt, minEnd - minAt);
    CHECK(std::count(minArr.begin(), minArr.end(), ',') == 2);

    std::error_code ec;
    std::filesystem::remove(obj, ec);
    std::filesystem::remove(glb, ec);
}

// The OBJ emits `mtllib` before the .mtl is attempted, so a skipped .mtl left
// a dangling reference in a file reported as successfully written.
TEST_CASE("an unwritable .mtl is reported", "[regression][io]") {
    const auto dir = std::filesystem::temp_directory_path() / "mh_mtl_block_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A directory where the .mtl must go: the ofstream cannot open it.
    std::filesystem::create_directories(dir / "blocked.mtl", ec);

    const auto mesh = triangleMesh();
    mh::core::Material mat;
    mat.name           = "m";
    const auto matDesc = mat.desc();
    const auto r       = mh::io::writeObj(dir / "blocked.obj", mesh.view(), {}, &matDesc);

    CHECK_FALSE(r.has_value());  // used to return success with wroteMtl == false
    std::filesystem::remove_all(dir, ec);
}

// JSON has no nan/inf literal: fmtFloat emitted a bare `nan` token and writeGlb
// reported success, producing a file json.loads rejects outright.
TEST_CASE("non-finite geometry is refused rather than written", "[regression][io][gltf]") {
    mh::core::Mesh m("bad", 4);
    const float inf = std::numeric_limits<float>::infinity();
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, inf, 0}, {0, 1, 0}}).has_value());
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());

    const auto glb = std::filesystem::temp_directory_path() / "mh_nonfinite.glb";
    const auto r   = mh::io::writeGlb(glb, mh::core::RenderMesh::build(m).view(), {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == mh::io::GltfWriteErrorKind::NonFiniteValue);

    std::error_code ec;
    std::filesystem::remove(glb, ec);
}

// A NaN ASCII STL imported "successfully"; the NaN then poisons bounding
// boxes, normals and every exporter downstream.
TEST_CASE("a non-finite coordinate is refused on import", "[regression][io][sceneio]") {
    const TempFile stl("nan.stl",
                       "solid s\n"
                       "facet normal 0 0 1\n"
                       " outer loop\n"
                       "  vertex 0 0 0\n"
                       "  vertex nan 0 0\n"
                       "  vertex 0 1 0\n"
                       " endloop\n"
                       "endfacet\n"
                       "endsolid s\n");
    const auto r = mh::io::importMesh(stl.path());
    if (r.has_value()) {
        // assimp may reject it first; either way no NaN may reach the Mesh.
        for (const auto& c : r->mesh.coord) {
            CHECK(std::isfinite(c.x));
            CHECK(std::isfinite(c.y));
            CHECK(std::isfinite(c.z));
        }
    }
}
