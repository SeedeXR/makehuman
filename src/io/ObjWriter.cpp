// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/ObjWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

namespace mh::io {
namespace {

/// Fixed-point formatting, avoiding locale-dependent iostream output. OBJ is an
/// ASCII format read by other tools; a comma decimal separator would corrupt it.
void appendFixed(std::string& out, float v, int decimals) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
    if (n > 0) out.append(buf, static_cast<size_t>(n));
}

}  // namespace

float unitScale(Unit u) noexcept {
    // apps/gui/guiexport.py:124-129
    switch (u) {
        case Unit::Decimeter: return 1.0F;
        case Unit::Meter: return 0.1F;
        case Unit::Centimeter: return 10.0F;
        case Unit::Inch: return 1.0F / 0.254F;
    }
    return 1.0F;
}

std::string_view unitName(Unit u) noexcept {
    switch (u) {
        case Unit::Decimeter: return "decimeter";
        case Unit::Meter: return "meter";
        case Unit::Centimeter: return "centimeter";
        case Unit::Inch: return "inch";
    }
    return "decimeter";
}

std::string ObjWriteError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case ObjWriteErrorKind::CannotOpen: k = "cannot open for writing"; break;
        case ObjWriteErrorKind::EmptyMesh: k = "mesh has no faces"; break;
        case ObjWriteErrorKind::MaskSizeMismatch: k = "face mask size mismatch"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<ObjWriteResult, ObjWriteError> writeObj(const std::filesystem::path& path,
                                                      const core::Mesh& mesh,
                                                      const ObjWriteOptions& options,
                                                      const core::Material* material) {
    const size_t nFaces = mesh.faceCount();
    if (nFaces == 0 || mesh.vertexCount() == 0) {
        return std::unexpected(ObjWriteError{ObjWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    if (!options.faceMask.empty() && options.faceMask.size() != nFaces) {
        return std::unexpected(ObjWriteError{
            ObjWriteErrorKind::MaskSizeMismatch, path.string(),
            std::to_string(options.faceMask.size()) + " vs " + std::to_string(nFaces)});
    }

    const float scale = unitScale(options.unit) * options.scale;

    // Offset is computed AFTER scaling, from the mesh's own minimum, so it is
    // correct whatever the orientation.
    float groundOffset = 0.0F;
    if (options.feetOnGround) {
        float lowest = std::numeric_limits<float>::infinity();
        for (const core::Vec3& v : mesh.coord())
            lowest = std::min(lowest, v.y * scale);
        if (std::isfinite(lowest)) groundOffset = -lowest;
    }

    const bool withUVs     = options.writeUVs && mesh.hasUV();
    const bool withNormals = options.writeNormals && mesh.vnorm().size() == mesh.vertexCount();

    std::ofstream out(path);
    if (!out) {
        return std::unexpected(ObjWriteError{ObjWriteErrorKind::CannotOpen, path.string(), {}});
    }

    ObjWriteResult result;
    std::string buf;
    buf.reserve(1u << 20);

    buf += "# Wavefront OBJ written by MakeHuman\n";
    buf += "# units: ";
    buf += unitName(options.unit);
    buf += '\n';

    const bool wantMtl = options.writeMaterial && material != nullptr;
    std::filesystem::path mtlPath;
    if (wantMtl) {
        mtlPath = path;
        mtlPath.replace_extension(".mtl");
        buf += "mtllib ";
        buf += mtlPath.filename().string();
        buf += '\n';
    }

    for (const core::Vec3& v : mesh.coord()) {
        buf += "v ";
        appendFixed(buf, v.x * scale, 4);
        buf += ' ';
        appendFixed(buf, v.y * scale + groundOffset, 4);
        buf += ' ';
        appendFixed(buf, v.z * scale, 4);
        buf += '\n';
        ++result.vertices;
    }

    if (withNormals) {
        for (const core::Vec3& n : mesh.vnorm()) {
            buf += "vn ";
            appendFixed(buf, n.x, 4);
            buf += ' ';
            appendFixed(buf, n.y, 4);
            buf += ' ';
            appendFixed(buf, n.z, 4);
            buf += '\n';
        }
    }

    if (withUVs) {
        for (const core::Vec2& t : mesh.texco()) {
            buf += "vt ";
            appendFixed(buf, t.x, 6);
            buf += ' ';
            appendFixed(buf, t.y, 6);
            buf += '\n';
            ++result.uvs;
        }
    }

    if (wantMtl) {
        buf += "usemtl ";
        buf += material->name;
        buf += '\n';
    }
    buf += "g ";
    buf += options.objectName;
    buf += '\n';

    // Indices are 1-based in OBJ.
    const size_t vpp     = mesh.vertsPerPrimitive();
    const size_t corners = mesh.vertsPerFaceForExport();

    for (size_t f = 0; f < nFaces; ++f) {
        if (!options.faceMask.empty() && options.faceMask[f] == 0) {
            ++result.skipped;
            continue;
        }
        buf += 'f';
        for (size_t c = 0; c < corners; ++c) {
            const uint32_t v = mesh.fvert()[f * vpp + c] + 1;
            buf += ' ';
            buf += std::to_string(v);
            if (withUVs) {
                buf += '/';
                buf += std::to_string(mesh.fuvs()[f * vpp + c] + 1);
                if (withNormals) {
                    buf += '/';
                    buf += std::to_string(v);
                }
            } else if (withNormals) {
                buf += "//";
                buf += std::to_string(v);
            }
        }
        buf += '\n';
        ++result.faces;
    }

    out << buf;
    out.close();
    if (!out) {
        return std::unexpected(
            ObjWriteError{ObjWriteErrorKind::CannotOpen, path.string(), "write failed"});
    }

    if (wantMtl) {
        std::ofstream mtl(mtlPath);
        if (mtl) {
            mtl << "# MTL written by MakeHuman\n";
            mtl << "newmtl " << material->name << '\n';
            mtl << "Ka " << material->ambient.x << ' ' << material->ambient.y << ' '
                << material->ambient.z << '\n';
            mtl << "Kd " << material->diffuse.x << ' ' << material->diffuse.y << ' '
                << material->diffuse.z << '\n';
            mtl << "Ks " << material->specular.x << ' ' << material->specular.y << ' '
                << material->specular.z << '\n';
            // OBJ's Ns is a 0..1000 exponent; the material stores 0..1.
            mtl << "Ns " << material->shininess * 1000.0F << '\n';
            mtl << "d " << material->opacity << '\n';
            mtl << "illum 2\n";

            const auto& diffuseTex = material->texture(core::TextureChannel::Diffuse);
            if (diffuseTex.present()) {
                mtl << "map_Kd " << diffuseTex.path.filename().string() << '\n';
            }
            // The reference copies a normal map into textures/ and then never
            // references it (wavefront.py:277-280, map_Disp commented out).
            // Emitting map_Bump is strictly more useful and costs nothing.
            const auto& normalTex = material->texture(core::TextureChannel::NormalMap);
            if (normalTex.present()) {
                mtl << "map_Bump " << normalTex.path.filename().string() << '\n';
            }
            result.wroteMtl = true;
        }
    }

    return result;
}

}  // namespace mh::io
