// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/ObjWriter.h"
#include "makehuman/foundation/Chars.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <tuple>

namespace mh::io {

using foundation::Vec2;
using foundation::Vec3;
using foundation::Vec4;

namespace {

/// Fixed-point formatting, locale-independently. OBJ is an ASCII format read
/// by other tools; a comma decimal separator corrupts it outright.
///
/// This used snprintf and claimed that avoided the problem. It does not:
/// snprintf honours LC_NUMERIC exactly as iostreams do. The shim in
/// foundation/Chars.h handles both that and the toolchains whose libc++ has no
/// floating-point std::to_chars.
void appendFixed(std::string& out, float v, int decimals) {
    out += foundation::formatFixed(v, decimals);
}

/// The .mtl block wrote floats with `operator<<`, which has the same locale
/// problem (`d 0,5`). Route it through the same helper.
std::string fixed(float v, int decimals) {
    std::string s;
    appendFixed(s, v, decimals);
    return s;
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
        case ObjWriteErrorKind::InconsistentMaterials: k = "materials are inconsistent"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

namespace {

/// Two descriptions of the same material. Compared by value because two proxies
/// loading the same `.mhmat` produce equal descriptions at different addresses;
/// comparing pointers would refuse a perfectly ordinary scene.
bool sameMaterial(const foundation::MaterialDesc& a, const foundation::MaterialDesc& b) {
    const auto scalars = [](const foundation::MaterialDesc& m) {
        return std::tie(m.name, m.diffuseTexture, m.normalTexture, m.opacity, m.shininess);
    };
    const auto rgb = [](const foundation::Vec3& v) { return std::tie(v.x, v.y, v.z); };
    return scalars(a) == scalars(b) && rgb(a.diffuse) == rgb(b.diffuse) &&
           rgb(a.ambient) == rgb(b.ambient) && rgb(a.specular) == rgb(b.specular);
}

}  // namespace

std::expected<ObjWriteResult, ObjWriteError> writeObjScene(const std::filesystem::path& path,
                                                           std::span<const ObjSceneEntry> entries,
                                                           const ObjWriteOptions& options) {
    if (entries.empty()) {
        return std::unexpected(ObjWriteError{ObjWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    // OBJ has no "no material" state: once `usemtl` is emitted it stays in
    // effect, so an entry without a material would silently inherit the
    // previous entry's. Refusing the mix beats inventing a placeholder material
    // or writing a file whose clothes are textured as skin.
    const bool anyMaterial =
        std::any_of(entries.begin(), entries.end(),
                    [](const ObjSceneEntry& e) { return e.material != nullptr; });
    const bool allMaterials =
        std::all_of(entries.begin(), entries.end(),
                    [](const ObjSceneEntry& e) { return e.material != nullptr; });
    if (options.writeMaterial && anyMaterial && !allMaterials) {
        return std::unexpected(ObjWriteError{ObjWriteErrorKind::InconsistentMaterials,
                                             path.string(),
                                             "some entries carry a material and some do not; "
                                             "OBJ cannot express \"no material\""});
    }

    for (const ObjSceneEntry& e : entries) {
        if (e.mesh.faceCount() == 0 || e.mesh.vertexCount() == 0) {
            return std::unexpected(ObjWriteError{ObjWriteErrorKind::EmptyMesh, path.string(), {}});
        }
        if (!e.faceMask.empty() && e.faceMask.size() != e.mesh.faceCount()) {
            return std::unexpected(ObjWriteError{
                ObjWriteErrorKind::MaskSizeMismatch, path.string(),
                std::to_string(e.faceMask.size()) + " vs " + std::to_string(e.mesh.faceCount())});
        }
    }

    const float scale = unitScale(options.unit) * options.scale;

    // One ground offset for the whole scene, taken from the lowest point of any
    // entry: levelling each mesh independently would drop the clothes to the
    // floor beside the body.
    float groundOffset = 0.0F;
    if (options.feetOnGround) {
        float lowest = std::numeric_limits<float>::infinity();
        for (const ObjSceneEntry& e : entries) {
            for (const Vec3& v : e.mesh.coord)
                lowest = std::min(lowest, v.y * scale);
        }
        if (std::isfinite(lowest)) groundOffset = -lowest;
    }

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

    // Deduped by name: two entries sharing a material must not write the same
    // `newmtl` block twice, and two DIFFERENT materials sharing a name would
    // silently lose one -- consumers keep the last block they read.
    std::vector<const foundation::MaterialDesc*> materials;
    if (options.writeMaterial) {
        for (const ObjSceneEntry& e : entries) {
            if (e.material == nullptr) continue;
            const auto seen = std::find_if(
                materials.begin(), materials.end(),
                [&](const foundation::MaterialDesc* m) { return m->name == e.material->name; });
            if (seen == materials.end()) {
                materials.push_back(e.material);
            } else if (!sameMaterial(**seen, *e.material)) {
                return std::unexpected(ObjWriteError{
                    ObjWriteErrorKind::EmptyMesh, path.string(),
                    "two different materials are both named \"" + e.material->name + "\""});
            }
        }
    }
    std::filesystem::path mtlPath;
    if (!materials.empty()) {
        mtlPath = path;
        mtlPath.replace_extension(".mtl");
        buf += "mtllib ";
        buf += mtlPath.filename().string();
        buf += '\n';
    }

    /// Marks a vertex or UV no surviving face names.
    constexpr uint32_t kUnused = ~0U;

    // Running totals of what has already been written, because OBJ indices
    // address the file rather than the mesh. Normals and UVs are counted
    // separately: an entry may carry one and not the other.
    size_t vBase = 0;
    size_t tBase = 0;
    size_t nBase = 0;

    for (const ObjSceneEntry& entry : entries) {
        const foundation::MeshView& mesh = entry.mesh;
        const bool withUVs               = options.writeUVs && mesh.hasUV();
        const bool withNormals = options.writeNormals && mesh.vnorm.size() == mesh.vertexCount();

        // Only what the SURVIVING faces name. The mask skips faces; writing the
        // vertex and UV lists whole left the file declaring points no `f` ever
        // referenced -- measured at **20,222 `v` lines of which 14,444 were
        // used**, 28.6% dead, on a default character.
        //
        // Normals are indexed by the vertex index here, so they share its
        // remap; UVs have their own.
        const size_t vppScan    = mesh.vertsPerPrimitive;
        const size_t cornerScan = mesh.vertsPerFaceForExport;
        std::vector<uint32_t> vRemap(mesh.vertexCount(), kUnused);
        std::vector<uint32_t> tRemap(mesh.texco.size(), kUnused);
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            if (!entry.faceMask.empty() && entry.faceMask[f] == 0) continue;
            for (size_t c = 0; c < cornerScan; ++c) {
                const uint32_t v = mesh.fvert[f * vppScan + c];
                if (v < vRemap.size()) vRemap[v] = 0U;
                if (withUVs) {
                    const uint32_t t = mesh.fuvs[f * vppScan + c];
                    if (t < tRemap.size()) tRemap[t] = 0U;
                }
            }
        }
        // Numbered in ascending order, so the file's vertex list is the input's
        // with deletions rather than a reshuffle. One lambda for both, so the
        // two cannot drift into different numbering rules.
        const auto renumber = [](std::vector<uint32_t>& r) {
            uint32_t n = 0;
            for (uint32_t& x : r)
                if (x != kUnused) x = n++;
            return n;
        };
        const uint32_t nextV = renumber(vRemap);
        const uint32_t nextT = renumber(tRemap);

        for (size_t i = 0; i < mesh.coord.size(); ++i) {
            if (vRemap[i] == kUnused) continue;
            const Vec3& v = mesh.coord[i];
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
            for (size_t i = 0; i < mesh.vnorm.size(); ++i) {
                if (vRemap[i] == kUnused) continue;
                const Vec3& n = mesh.vnorm[i];
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
            for (size_t i = 0; i < mesh.texco.size(); ++i) {
                if (tRemap[i] == kUnused) continue;
                const Vec2& t = mesh.texco[i];
                buf += "vt ";
                appendFixed(buf, t.x, 6);
                buf += ' ';
                appendFixed(buf, t.y, 6);
                buf += '\n';
                ++result.uvs;
            }
        }

        if (entry.material != nullptr && options.writeMaterial) {
            buf += "usemtl ";
            buf += entry.material->name;
            buf += '\n';
        }
        buf += "g ";
        buf += entry.name;
        buf += '\n';

        const size_t vpp     = mesh.vertsPerPrimitive;
        const size_t corners = mesh.vertsPerFaceForExport;
        const size_t nFaces  = mesh.faceCount();

        for (size_t f = 0; f < nFaces; ++f) {
            if (!entry.faceMask.empty() && entry.faceMask[f] == 0) {
                ++result.skipped;
                continue;
            }
            buf += 'f';
            for (size_t c = 0; c < corners; ++c) {
                const size_t local = vRemap[mesh.fvert[f * vpp + c]];
                buf += ' ';
                buf += std::to_string(vBase + local + 1);
                if (withUVs) {
                    buf += '/';
                    buf += std::to_string(tBase + tRemap[mesh.fuvs[f * vpp + c]] + 1);
                    if (withNormals) {
                        buf += '/';
                        buf += std::to_string(nBase + local + 1);
                    }
                } else if (withNormals) {
                    buf += "//";
                    buf += std::to_string(nBase + local + 1);
                }
            }
            buf += '\n';
            ++result.faces;
        }

        // Advanced by what was WRITTEN, not by what the mesh holds: the next
        // entry's indices address the file.
        vBase += nextV;
        if (withUVs) tBase += nextT;
        if (withNormals) nBase += nextV;
    }

    out << buf;
    out.close();
    if (!out) {
        return std::unexpected(
            ObjWriteError{ObjWriteErrorKind::CannotOpen, path.string(), "write failed"});
    }

    if (!materials.empty()) {
        // The OBJ already emitted `mtllib`, so a silently skipped .mtl leaves a
        // dangling reference in a file we reported as written successfully.
        std::ofstream mtl(mtlPath);
        if (!mtl) {
            return std::unexpected(ObjWriteError{ObjWriteErrorKind::CannotOpen, mtlPath.string(),
                                                 "material library requested but not writable"});
        }
        // A texture named in the .mtl must be beside it, or the file we just
        // reported as written references something that is not there. The
        // reference copies too (legacy/python/shared/wavefront.py:278).
        const auto copyTexture =
            [&](const std::filesystem::path& src) -> std::expected<std::string, ObjWriteError> {
            const std::filesystem::path dst = mtlPath.parent_path() / src.filename();
            std::error_code ec;
            if (std::filesystem::equivalent(src, dst, ec)) return src.filename().string();
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing,
                                       ec);
            if (ec) {
                return std::unexpected(
                    ObjWriteError{ObjWriteErrorKind::CannotOpen, src.string(),
                                  "texture named by the material: " + ec.message()});
            }
            return src.filename().string();
        };

        mtl << "# MTL written by MakeHuman\n";
        for (const foundation::MaterialDesc* material : materials) {
            mtl << "newmtl " << material->name << '\n';
            mtl << "Ka " << fixed(material->ambient.x, 6) << ' ' << fixed(material->ambient.y, 6)
                << ' ' << fixed(material->ambient.z, 6) << '\n';
            mtl << "Kd " << fixed(material->diffuse.x, 6) << ' ' << fixed(material->diffuse.y, 6)
                << ' ' << fixed(material->diffuse.z, 6) << '\n';
            mtl << "Ks " << fixed(material->specular.x, 6) << ' ' << fixed(material->specular.y, 6)
                << ' ' << fixed(material->specular.z, 6) << '\n';
            // OBJ's Ns is a 0..1000 exponent; the material stores 0..1.
            mtl << "Ns " << fixed(material->shininess * 1000.0F, 6) << '\n';
            mtl << "d " << fixed(material->opacity, 6) << '\n';
            mtl << "illum 2\n";

            const auto& diffuseTex = material->diffuseTexture;
            if (!diffuseTex.empty()) {
                const auto name = copyTexture(diffuseTex);
                if (!name) return std::unexpected(name.error());
                mtl << "map_Kd " << *name << '\n';
            }
            // The reference copies a normal map into textures/ and then never
            // references it (wavefront.py:277-280, map_Disp commented out).
            // Emitting map_Bump is strictly more useful and costs nothing.
            const auto& normalTex = material->normalTexture;
            if (!normalTex.empty()) {
                const auto name = copyTexture(normalTex);
                if (!name) return std::unexpected(name.error());
                mtl << "map_Bump " << *name << '\n';
            }
        }
        mtl.close();
        if (!mtl) {
            return std::unexpected(
                ObjWriteError{ObjWriteErrorKind::CannotOpen, mtlPath.string(), "write failed"});
        }
        result.wroteMtl = true;
    }

    return result;
}

std::expected<ObjWriteResult, ObjWriteError> writeObj(const std::filesystem::path& path,
                                                      const foundation::MeshView& mesh,
                                                      const ObjWriteOptions& options,
                                                      const foundation::MaterialDesc* material) {
    const ObjSceneEntry entry{mesh, options.objectName, material, options.faceMask};
    return writeObjScene(path, {&entry, 1}, options);
}

}  // namespace mh::io
