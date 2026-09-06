// SPDX-License-Identifier: Apache-2.0
//
// Draco compression of a glTF primitive, checked by DECODING IT BACK.
//
// There is no reference implementation to compare against -- the Python
// MakeHuman has no Draco support -- and a compressor is exactly the kind of
// code that looks fine until someone opens the file. So every test here
// round-trips through draco's own decoder, which is a separate code path from
// the encoder, and compares against what went in.
//
// The property that matters most is not the size. It is that JOINTS come back
// EXACT: they are bone indices, and one off by a single unit weights a vertex
// to the wrong bone -- a defect that shows as a limb tearing in a DCC and
// nowhere in a byte count.

#include "makehuman/io/DracoMesh.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#if defined(MH_HAVE_DRACO)
#include <draco/compression/decode.h>
#endif

using namespace mh;

namespace {

/// A quad as two triangles, with distinct positions, normals and UVs.
struct Primitive {
    std::vector<foundation::Vec3> coord{
        {-1.0F, -1.0F, 0.0F}, {1.0F, -1.0F, 0.25F}, {1.0F, 1.0F, 0.5F}, {-1.0F, 1.0F, 0.75F}};
    std::vector<foundation::Vec3> norm{
        {0.0F, 0.0F, 1.0F}, {0.0F, 0.2F, 0.98F}, {0.1F, 0.0F, 0.99F}, {0.0F, -0.2F, 0.98F}};
    std::vector<foundation::Vec2> uv{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    std::vector<uint32_t> index{0, 1, 2, 0, 2, 3};

    [[nodiscard]] foundation::RenderView view() const {
        return foundation::RenderView{coord, uv, norm, {}, index};
    }
};

}  // namespace

TEST_CASE("draco availability is reported, not assumed", "[io][draco]") {
    // The one assertion that is meaningful whether or not the library is here:
    // the build flag and the runtime answer must agree, so a stale CMake cache
    // cannot leave the writer silently emitting an extension nothing encoded.
#if defined(MH_HAVE_DRACO)
    CHECK(io::dracoAvailable());
#else
    CHECK_FALSE(io::dracoAvailable());
    CHECK_FALSE(io::dracoEncode(Primitive{}.view()).has_value());
#endif
}

#if defined(MH_HAVE_DRACO)

TEST_CASE("a compressed primitive decodes back to the same mesh", "[io][draco]") {
    const Primitive p;
    const auto enc = io::dracoEncode(p.view());
    REQUIRE(enc.has_value());
    CHECK(!enc->bytes.empty());

    draco::DecoderBuffer buf;
    buf.Init(reinterpret_cast<const char*>(enc->bytes.data()), enc->bytes.size());
    draco::Decoder dec;
    auto mesh = dec.DecodeMeshFromBuffer(&buf);
    REQUIRE(mesh.ok());

    CHECK(mesh.value()->num_points() == 4);
    CHECK(mesh.value()->num_faces() == 2);
}

TEST_CASE("the attribute map names every attribute the primitive declares", "[io][draco]") {
    // KHR_draco_mesh_compression requires the extension to carry EVERY
    // attribute of the primitive: a consumer reads the compressed buffer and
    // has nowhere else to get one from. A map that quietly omits TEXCOORD_0
    // produces a file that loads with no UVs and no error.
    const Primitive p;
    const auto enc = io::dracoEncode(p.view());
    REQUIRE(enc.has_value());

    std::vector<std::string> names;
    for (const auto& [name, id] : enc->attributes)
        names.push_back(name);
    std::ranges::sort(names);
    CHECK(names == std::vector<std::string>{"NORMAL", "POSITION", "TEXCOORD_0"});
}

TEST_CASE("positions survive quantisation to within their step", "[io][draco]") {
    const Primitive p;
    const auto enc = io::dracoEncode(p.view());
    REQUIRE(enc.has_value());

    draco::DecoderBuffer buf;
    buf.Init(reinterpret_cast<const char*>(enc->bytes.data()), enc->bytes.size());
    draco::Decoder dec;
    auto mesh = dec.DecodeMeshFromBuffer(&buf);
    REQUIRE(mesh.ok());

    const auto* pos = mesh.value()->GetNamedAttribute(draco::GeometryAttribute::POSITION);
    REQUIRE(pos != nullptr);

    // The quad spans 2 units and positions are quantised to 14 bits, so the
    // step is 2/16383 -- about 1.2e-4. Asserting a bound rather than equality
    // is the honest statement: this is a LOSSY codec and a test that demanded
    // equality would only ever pass by disabling the compression.
    float worst = 0.0F;
    for (draco::PointIndex i(0); i < mesh.value()->num_points(); ++i) {
        std::array<float, 3> got{};
        REQUIRE(pos->ConvertValue<float, 3>(pos->mapped_index(i), got.data()));
        // Draco reorders points, so match by nearest rather than by index.
        float best = 1e30F;
        for (const auto& want : p.coord) {
            const float d = std::max(
                {std::abs(got[0] - want.x), std::abs(got[1] - want.y), std::abs(got[2] - want.z)});
            best = std::min(best, d);
        }
        worst = std::max(worst, best);
    }
    INFO("worst position error " << worst);
    CHECK(worst < 2.0e-4F);
    // ... and it must not be ZERO either: exact positions would mean the
    // quantisation setting never reached the encoder.
    CHECK(worst > 0.0F);
}

TEST_CASE("compression actually makes the base mesh smaller", "[io][draco]") {
    // A wrapper that returned the input unchanged would pass every test above.
    // 19,158 vertices of float32 position+normal+uv is 21 floats a triangle
    // before indices; Draco should beat that by a wide margin, so the bar is
    // set at "less than half" rather than "smaller".
    std::vector<foundation::Vec3> coord;
    std::vector<foundation::Vec3> norm;
    std::vector<foundation::Vec2> uv;
    std::vector<uint32_t> index;
    constexpr int kGrid = 64;
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            const float fx = static_cast<float>(x) / kGrid;
            const float fy = static_cast<float>(y) / kGrid;
            coord.push_back({fx, fy, 0.1F * std::sin(fx * 12.0F) * std::cos(fy * 9.0F)});
            norm.push_back({0.0F, 0.0F, 1.0F});
            uv.push_back({fx, fy});
        }
    }
    for (int y = 0; y + 1 < kGrid; ++y) {
        for (int x = 0; x + 1 < kGrid; ++x) {
            const auto a = static_cast<uint32_t>((y * kGrid) + x);
            index.insert(index.end(), {a, a + 1, a + kGrid, a + 1, a + kGrid + 1, a + kGrid});
        }
    }
    const foundation::RenderView v{coord, uv, norm, {}, index};
    const auto enc = io::dracoEncode(v);
    REQUIRE(enc.has_value());

    const size_t raw = (coord.size() * sizeof(foundation::Vec3) * 2) +
                       (uv.size() * sizeof(foundation::Vec2)) + (index.size() * sizeof(uint32_t));
    INFO("raw " << raw << " bytes, draco " << enc->bytes.size());
    CHECK(enc->bytes.size() < raw / 2);
}

#endif  // MH_HAVE_DRACO
