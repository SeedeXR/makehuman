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

std::optional<DracoBuffer> dracoEncode(const foundation::RenderView&, const DracoSkin&) {
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

std::optional<DracoBuffer> dracoEncode(const foundation::RenderView& mesh, const DracoSkin& skin) {
    const size_t faces = mesh.triangleCount();
    if (faces == 0 || mesh.coord.empty()) return std::nullopt;

    // The triangle-soup builder, not Mesh::AddAttribute: it takes values per
    // CORNER, which is the shape our index list already has, and it does the
    // vertex deduplication itself. Building the indexed mesh by hand would mean
    // reimplementing that.
    draco::TriangleSoupMeshBuilder builder;
    builder.Start(static_cast<int>(faces));

    const bool withNormals  = mesh.vnorm.size() == mesh.coord.size();
    const bool withUVs      = mesh.texco.size() == mesh.coord.size();
    const bool withTangents = mesh.vtang.size() == mesh.coord.size();
    // Four per vertex, glTF's fixed influence count. A partial array is a
    // caller bug, not something to pad over: padding would silently weight
    // vertices to joint 0.
    const bool withSkin =
        skin.joints.size() == mesh.coord.size() * 4 && skin.weights.size() == mesh.coord.size() * 4;
    if (!skin.joints.empty() && !withSkin) return std::nullopt;

    const int posId =
        builder.AddAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32);
    const int normId =
        withNormals ? builder.AddAttribute(draco::GeometryAttribute::NORMAL, 3, draco::DT_FLOAT32)
                    : -1;
    const int uvId =
        withUVs ? builder.AddAttribute(draco::GeometryAttribute::TEX_COORD, 2, draco::DT_FLOAT32)
                : -1;
    // GENERIC for the rest: draco's own TANGENT/JOINTS/WEIGHTS types are behind
    // DRACO_TRANSCODER_SUPPORTED and are a bitstream change, and the glTF
    // extension does not care -- it maps glTF NAMES to draco unique ids, so a
    // generic attribute is exactly as addressable as a named one.
    const int tangId =
        withTangents ? builder.AddAttribute(draco::GeometryAttribute::GENERIC, 4, draco::DT_FLOAT32)
                     : -1;
    const int jointId =
        withSkin ? builder.AddAttribute(draco::GeometryAttribute::GENERIC, 4, draco::DT_UINT16)
                 : -1;
    const int weightId =
        withSkin ? builder.AddAttribute(draco::GeometryAttribute::GENERIC, 4, draco::DT_FLOAT32)
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
        if (tangId >= 0) {
            builder.SetAttributeValuesForFace(tangId, fi, &mesh.vtang[a], &mesh.vtang[b],
                                              &mesh.vtang[c]);
        }
        if (jointId >= 0) {
            builder.SetAttributeValuesForFace(jointId, fi, &skin.joints[a * 4], &skin.joints[b * 4],
                                              &skin.joints[c * 4]);
            builder.SetAttributeValuesForFace(weightId, fi, &skin.weights[a * 4],
                                              &skin.weights[b * 4], &skin.weights[c * 4]);
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
    // and the unique id is what survives that. For the generic ones the
    // BUILDER's id is the only handle we have -- there is no "the" generic
    // attribute to look up by type, since three of them share it.
    const auto uniqueOf = [&built](int id) {
        return static_cast<uint32_t>(built->attribute(id)->unique_id());
    };
    const auto named = [&built, &uniqueOf](draco::GeometryAttribute::Type t) {
        return uniqueOf(built->GetNamedAttributeId(t));
    };
    result.attributes.emplace_back("POSITION", named(draco::GeometryAttribute::POSITION));
    if (normId >= 0) {
        result.attributes.emplace_back("NORMAL", named(draco::GeometryAttribute::NORMAL));
    }
    if (uvId >= 0) {
        result.attributes.emplace_back("TEXCOORD_0", named(draco::GeometryAttribute::TEX_COORD));
    }
    if (tangId >= 0) result.attributes.emplace_back("TANGENT", uniqueOf(tangId));
    if (jointId >= 0) {
        result.attributes.emplace_back("JOINTS_0", uniqueOf(jointId));
        result.attributes.emplace_back("WEIGHTS_0", uniqueOf(weightId));
    }
    return result;
}

#endif  // MH_HAVE_DRACO

}  // namespace mh::io
