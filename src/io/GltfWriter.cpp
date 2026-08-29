// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/GltfWriter.h"

#include "makehuman/core/RenderMesh.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace mh::io {
namespace {

// ---- glTF 2.0 / GLB constants, from the published specification ------------
constexpr uint32_t kGlbMagic      = 0x46546C67;  // "glTF"
constexpr uint32_t kGlbVersion    = 2;
constexpr uint32_t kChunkTypeJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kChunkTypeBin  = 0x004E4942;  // "BIN\0"

constexpr uint32_t kComponentFloat       = 5126;
constexpr uint32_t kComponentUnsignedInt = 5125;
constexpr uint32_t kTargetArrayBuffer    = 34962;
constexpr uint32_t kTargetElementArray   = 34963;
constexpr uint32_t kModeTriangles        = 4;

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    // glTF buffers are little-endian regardless of host.
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendFloat(std::vector<uint8_t>& b, float v) {
    uint32_t bits{};
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    appendU32(b, bits);
}

/// Every bufferView offset must be aligned to its component size; padding the
/// buffer to 4 satisfies all the types used here.
void padTo4(std::vector<uint8_t>& b, uint8_t fill = 0) {
    while (b.size() % 4 != 0)
        b.push_back(fill);
}

/// JSON numbers, locale-independently.
///
/// snprintf was used here and honours LC_NUMERIC just as iostreams do: under
/// de_DE.UTF-8 it writes "0,5", which turns `"max":[0.2,0.3]` into
/// `"max":[0,2,0,3]` -- still *valid* JSON, but a three-element array becomes
/// five and the accessor bounds are silently garbage. std::to_chars is defined
/// to be locale-independent.
///
/// Non-finite values are rejected before they reach here (writeGlb validates),
/// because "nan"/"inf" are not JSON at all and would make the file unparseable.
std::string fmtFloat(float v) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general, 7);
    return (r.ec == std::errc{}) ? std::string(buf, r.ptr) : std::string("0");
}

/// Minimal JSON string escaping -- enough for names, which is all we emit.
std::string jsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char esc[8];
                    std::snprintf(esc, sizeof(esc), "\\u%04x", c);
                    out += esc;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

}  // namespace

std::string GltfWriteError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case GltfWriteErrorKind::CannotOpen: k = "cannot open for writing"; break;
        case GltfWriteErrorKind::EmptyMesh: k = "mesh has no geometry"; break;
        case GltfWriteErrorKind::TooManyVertices: k = "too many vertices"; break;
        case GltfWriteErrorKind::NonFiniteValue: k = "non-finite value"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<GltfWriteResult, GltfWriteError> writeGlb(const std::filesystem::path& path,
                                                        const core::Mesh& mesh,
                                                        const GltfWriteOptions& options,
                                                        const core::Material* material) {
    if (mesh.faceCount() == 0 || mesh.vertexCount() == 0) {
        return std::unexpected(GltfWriteError{GltfWriteErrorKind::EmptyMesh, path.string(), {}});
    }

    // glTF wants one attribute per index and has no quad primitive; RenderMesh
    // already produces exactly that (unweld + fan triangulation).
    const core::RenderMesh rm = core::RenderMesh::build(mesh);
    if (rm.vertexCount() == 0 || rm.indexCount() == 0) {
        return std::unexpected(GltfWriteError{GltfWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    if (rm.vertexCount() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(
            GltfWriteError{GltfWriteErrorKind::TooManyVertices, path.string(), {}});
    }

    // NaN and infinity have no JSON representation, so fmtFloat would emit a
    // bare `nan`/`inf` token and the whole file would fail to parse -- while
    // writeGlb still reported success. Reject at the boundary instead. Note
    // std::clamp does not filter NaN (both `nan < lo` and `hi < nan` are
    // false), so clamping the material later is not a substitute.
    for (const auto& c : rm.coord()) {
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) {
            return std::unexpected(GltfWriteError{GltfWriteErrorKind::NonFiniteValue, path.string(),
                                                  "vertex position"});
        }
    }
    if (material != nullptr) {
        const float mv[] = {material->diffuse.x, material->diffuse.y, material->diffuse.z,
                            material->opacity, material->shininess};
        for (const float v : mv) {
            if (!std::isfinite(v)) {
                return std::unexpected(GltfWriteError{GltfWriteErrorKind::NonFiniteValue,
                                                      path.string(), "material value"});
            }
        }
    }

    const float scale = unitScale(options.unit) * options.scale;

    float groundOffset = 0.0F;
    if (options.feetOnGround) {
        float lowest = std::numeric_limits<float>::infinity();
        for (const core::Vec3& v : rm.coord())
            lowest = std::min(lowest, v.y * scale);
        if (std::isfinite(lowest)) groundOffset = -lowest;
    }

    const bool withNormals = options.writeNormals && rm.vnorm().size() == rm.vertexCount();
    const bool withUVs     = options.writeUVs && rm.texco().size() == rm.vertexCount();

    // ---- binary buffer ----------------------------------------------------
    std::vector<uint8_t> bin;
    bin.reserve(rm.vertexCount() * 32 + rm.indexCount() * 4);

    core::Vec3 lo{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                  std::numeric_limits<float>::infinity()};
    core::Vec3 hi{-lo.x, -lo.y, -lo.z};

    const size_t posOffset = bin.size();
    for (const core::Vec3& v : rm.coord()) {
        const float x = v.x * scale;
        const float y = v.y * scale + groundOffset;
        const float z = v.z * scale;
        appendFloat(bin, x);
        appendFloat(bin, y);
        appendFloat(bin, z);
        lo.x = std::min(lo.x, x);
        lo.y = std::min(lo.y, y);
        lo.z = std::min(lo.z, z);
        hi.x = std::max(hi.x, x);
        hi.y = std::max(hi.y, y);
        hi.z = std::max(hi.z, z);
    }
    const size_t posBytes = bin.size() - posOffset;

    size_t normOffset = 0, normBytes = 0;
    if (withNormals) {
        padTo4(bin);
        normOffset = bin.size();
        for (const core::Vec3& n : rm.vnorm()) {
            appendFloat(bin, n.x);
            appendFloat(bin, n.y);
            appendFloat(bin, n.z);
        }
        normBytes = bin.size() - normOffset;
    }

    size_t uvOffset = 0, uvBytes = 0;
    if (withUVs) {
        padTo4(bin);
        uvOffset = bin.size();
        for (const core::Vec2& t : rm.texco()) {
            // glTF's UV origin is top-left; OBJ/MakeHuman's is bottom-left, so V
            // is flipped. Getting this wrong mirrors every texture vertically.
            appendFloat(bin, t.x);
            appendFloat(bin, 1.0F - t.y);
        }
        uvBytes = bin.size() - uvOffset;
    }

    padTo4(bin);
    const size_t idxOffset = bin.size();
    for (const uint32_t i : rm.index())
        appendU32(bin, i);
    const size_t idxBytes = bin.size() - idxOffset;

    padTo4(bin);

    // ---- JSON -------------------------------------------------------------
    const auto n = rm.vertexCount();
    std::string j;
    j.reserve(2048);

    j += R"({"asset":{"version":"2.0","generator":"MakeHuman C++ glTF writer"},)";
    j += R"("scene":0,"scenes":[{"nodes":[0]}],)";
    j += R"("nodes":[{"mesh":0,"name":")" + jsonEscape(options.meshName) + R"("}],)";

    // buffers / bufferViews
    j += R"("buffers":[{"byteLength":)" + std::to_string(bin.size()) + "}],";
    j += R"("bufferViews":[)";
    j += R"({"buffer":0,"byteOffset":)" + std::to_string(posOffset) + R"(,"byteLength":)" +
         std::to_string(posBytes) + R"(,"target":)" + std::to_string(kTargetArrayBuffer) + "}";
    if (withNormals) {
        j += R"(,{"buffer":0,"byteOffset":)" + std::to_string(normOffset) + R"(,"byteLength":)" +
             std::to_string(normBytes) + R"(,"target":)" + std::to_string(kTargetArrayBuffer) + "}";
    }
    if (withUVs) {
        j += R"(,{"buffer":0,"byteOffset":)" + std::to_string(uvOffset) + R"(,"byteLength":)" +
             std::to_string(uvBytes) + R"(,"target":)" + std::to_string(kTargetArrayBuffer) + "}";
    }
    j += R"(,{"buffer":0,"byteOffset":)" + std::to_string(idxOffset) + R"(,"byteLength":)" +
         std::to_string(idxBytes) + R"(,"target":)" + std::to_string(kTargetElementArray) + "}";
    j += "],";

    // accessors -- POSITION is required to carry min and max.
    int view = 0;
    j += R"("accessors":[)";
    j += R"({"bufferView":)" + std::to_string(view++) + R"(,"componentType":)" +
         std::to_string(kComponentFloat) + R"(,"count":)" + std::to_string(n) +
         R"(,"type":"VEC3","min":[)" + fmtFloat(lo.x) + "," + fmtFloat(lo.y) + "," +
         fmtFloat(lo.z) + R"(],"max":[)" + fmtFloat(hi.x) + "," + fmtFloat(hi.y) + "," +
         fmtFloat(hi.z) + "]}";
    int normAccessor = -1;
    if (withNormals) {
        normAccessor = view;
        j += R"(,{"bufferView":)" + std::to_string(view++) + R"(,"componentType":)" +
             std::to_string(kComponentFloat) + R"(,"count":)" + std::to_string(n) +
             R"(,"type":"VEC3"})";
    }
    int uvAccessor = -1;
    if (withUVs) {
        uvAccessor = view;
        j += R"(,{"bufferView":)" + std::to_string(view++) + R"(,"componentType":)" +
             std::to_string(kComponentFloat) + R"(,"count":)" + std::to_string(n) +
             R"(,"type":"VEC2"})";
    }
    const int idxAccessor = view;
    j += R"(,{"bufferView":)" + std::to_string(view) + R"(,"componentType":)" +
         std::to_string(kComponentUnsignedInt) + R"(,"count":)" + std::to_string(rm.indexCount()) +
         R"(,"type":"SCALAR"})";
    j += "],";

    // mesh
    j += R"("meshes":[{"name":")" + jsonEscape(options.meshName) +
         R"(","primitives":[{"attributes":{"POSITION":0)";
    if (normAccessor >= 0) j += R"(,"NORMAL":)" + std::to_string(normAccessor);
    if (uvAccessor >= 0) j += R"(,"TEXCOORD_0":)" + std::to_string(uvAccessor);
    j += R"(},"indices":)" + std::to_string(idxAccessor) + R"(,"material":0,"mode":)" +
         std::to_string(kModeTriangles) + "}]}],";

    // material -- Blinn-Phong converted to metallic-roughness, which is the
    // only PBR model core glTF defines. The reference has no PBR data at all,
    // so roughness is derived from shininess and metallic is 0 (skin, cloth and
    // hair are all dielectric).
    float baseR = 0.8F, baseG = 0.8F, baseB = 0.8F, alpha = 1.0F, roughness = 0.7F;
    std::string matName = options.materialName;
    if (material != nullptr) {
        baseR     = material->diffuse.x;
        baseG     = material->diffuse.y;
        baseB     = material->diffuse.z;
        alpha     = material->opacity;
        roughness = std::clamp(1.0F - material->shininess, 0.0F, 1.0F);
        if (!material->name.empty()) matName = material->name;
    }
    j += R"("materials":[{"name":")" + jsonEscape(matName) +
         R"(","pbrMetallicRoughness":{"baseColorFactor":[)" + fmtFloat(baseR) + "," +
         fmtFloat(baseG) + "," + fmtFloat(baseB) + "," + fmtFloat(alpha) +
         R"(],"metallicFactor":0,"roughnessFactor":)" + fmtFloat(roughness) + "}";
    if (alpha < 1.0F) j += R"(,"alphaMode":"BLEND")";
    j += "}]}";

    // ---- GLB container ----------------------------------------------------
    std::vector<uint8_t> jsonChunk(j.begin(), j.end());
    padTo4(jsonChunk, 0x20);  // JSON chunk pads with spaces
    padTo4(bin, 0x00);        // BIN chunk pads with zeros

    // Every GLB length field is uint32. The TooManyVertices guard above bounds
    // the vertex COUNT, not the byte size, so a large enough buffer would wrap
    // `total` and write a truncated chunk header into a file we called valid.
    constexpr size_t kGlbHeader = 12, kChunkHeader = 8;
    const size_t totalBytes =
        kGlbHeader + kChunkHeader + jsonChunk.size() + kChunkHeader + bin.size();
    if (totalBytes > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(GltfWriteError{GltfWriteErrorKind::TooManyVertices, path.string(),
                                              "GLB exceeds the 4 GiB container limit"});
    }

    const uint32_t total =
        12 + 8 + static_cast<uint32_t>(jsonChunk.size()) + 8 + static_cast<uint32_t>(bin.size());

    std::vector<uint8_t> glb;
    glb.reserve(total);
    appendU32(glb, kGlbMagic);
    appendU32(glb, kGlbVersion);
    appendU32(glb, total);
    appendU32(glb, static_cast<uint32_t>(jsonChunk.size()));
    appendU32(glb, kChunkTypeJson);
    glb.insert(glb.end(), jsonChunk.begin(), jsonChunk.end());
    appendU32(glb, static_cast<uint32_t>(bin.size()));
    appendU32(glb, kChunkTypeBin);
    glb.insert(glb.end(), bin.begin(), bin.end());

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return std::unexpected(GltfWriteError{GltfWriteErrorKind::CannotOpen, path.string(), {}});
    }
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    out.close();
    if (!out) {
        return std::unexpected(
            GltfWriteError{GltfWriteErrorKind::CannotOpen, path.string(), "write failed"});
    }

    return GltfWriteResult{rm.vertexCount(), rm.indexCount() / 3, glb.size()};
}

}  // namespace mh::io
