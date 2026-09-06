// SPDX-License-Identifier: Apache-2.0

#include "makehuman/io/DracoMesh.h"

#if defined(MH_HAVE_DRACO)
#include <draco/compression/encode.h>
#include <draco/mesh/mesh.h>
#include <draco/mesh/triangle_soup_mesh_builder.h>
#endif

namespace mh::io {

#if !defined(MH_HAVE_DRACO)

bool dracoAvailable() noexcept {
    return false;
}

std::optional<DracoBuffer> dracoEncode(const foundation::RenderView&) {
    return std::nullopt;
}

#else

namespace {

/// Quantisation, in bits per component.
///
/// The values glTF tooling settled on: 14 for position is about a tenth of a
/// millimetre over a two-metre figure, 10 for a unit normal, 12 for a UV. Going
/// lower shows as faceting on a shoulder before it shows in a byte count.
constexpr int kPositionBits = 14;
constexpr int kNormalBits   = 10;
constexpr int kTexCoordBits = 12;

}  // namespace

bool dracoAvailable() noexcept {
    return true;
}

std::optional<DracoBuffer> dracoEncode(const foundation::RenderView& mesh) {
    const size_t faces = mesh.triangleCount();
    if (faces == 0 || mesh.coord.empty()) return std::nullopt;

    // The triangle-soup builder, not Mesh::AddAttribute: it takes values per
    // CORNER, which is the shape our index list already has, and it does the
    // vertex deduplication itself. Building the indexed mesh by hand would mean
    // reimplementing that.
    draco::TriangleSoupMeshBuilder builder;
    builder.Start(static_cast<int>(faces));

    const bool withNormals = mesh.vnorm.size() == mesh.coord.size();
    const bool withUVs     = mesh.texco.size() == mesh.coord.size();

    const int posId =
        builder.AddAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32);
    const int normId =
        withNormals ? builder.AddAttribute(draco::GeometryAttribute::NORMAL, 3, draco::DT_FLOAT32)
                    : -1;
    const int uvId =
        withUVs ? builder.AddAttribute(draco::GeometryAttribute::TEX_COORD, 2, draco::DT_FLOAT32)
                : -1;

    for (size_t f = 0; f < faces; ++f) {
        const draco::FaceIndex fi(static_cast<uint32_t>(f));
        const uint32_t a = mesh.index[f * 3];
        const uint32_t b = mesh.index[(f * 3) + 1];
        const uint32_t c = mesh.index[(f * 3) + 2];
        builder.SetAttributeValuesForFace(posId, fi, &mesh.coord[a], &mesh.coord[b],
                                          &mesh.coord[c]);
        if (normId >= 0) {
            builder.SetAttributeValuesForFace(normId, fi, &mesh.vnorm[a], &mesh.vnorm[b],
                                              &mesh.vnorm[c]);
        }
        if (uvId >= 0) {
            builder.SetAttributeValuesForFace(uvId, fi, &mesh.texco[a], &mesh.texco[b],
                                              &mesh.texco[c]);
        }
    }

    std::unique_ptr<draco::Mesh> built = builder.Finalize();
    if (!built) return std::nullopt;

    draco::Encoder encoder;
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, kPositionBits);
    if (normId >= 0) {
        encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, kNormalBits);
    }
    if (uvId >= 0) {
        encoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD, kTexCoordBits);
    }

    draco::EncoderBuffer out;
    if (!encoder.EncodeMeshToBuffer(*built, &out).ok()) return std::nullopt;

    DracoBuffer result;
    result.bytes.assign(out.data(), out.data() + out.size());

    // The extension maps glTF names to draco UNIQUE ids, not to the attribute
    // indices used while building: the encoder is free to reorder attributes,
    // and the unique id is what survives that.
    const auto uniqueId = [&built](draco::GeometryAttribute::Type t) {
        const int id = built->GetNamedAttributeId(t);
        return static_cast<uint32_t>(built->attribute(id)->unique_id());
    };
    result.attributes.emplace_back("POSITION", uniqueId(draco::GeometryAttribute::POSITION));
    if (normId >= 0) {
        result.attributes.emplace_back("NORMAL", uniqueId(draco::GeometryAttribute::NORMAL));
    }
    if (uvId >= 0) {
        result.attributes.emplace_back("TEXCOORD_0", uniqueId(draco::GeometryAttribute::TEX_COORD));
    }
    return result;
}

#endif  // MH_HAVE_DRACO

}  // namespace mh::io
