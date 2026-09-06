// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/GltfWriter.h"

#include "makehuman/foundation/Chars.h"
#include "makehuman/io/DracoMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace mh::io {

using foundation::Vec2;
using foundation::Vec3;
using foundation::Vec4;

namespace {

// ---- glTF 2.0 / GLB constants, from the published specification ------------
constexpr uint32_t kGlbMagic      = 0x46546C67;  // "glTF"
constexpr uint32_t kGlbVersion    = 2;
constexpr uint32_t kChunkTypeJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kChunkTypeBin  = 0x004E4942;  // "BIN\0"

constexpr uint32_t kComponentFloat         = 5126;
constexpr uint32_t kComponentUnsignedInt   = 5125;
constexpr uint32_t kComponentUnsignedShort = 5123;  // JOINTS_0 for < 65536 joints
constexpr uint32_t kTargetArrayBuffer      = 34962;
constexpr uint32_t kTargetElementArray     = 34963;
constexpr uint32_t kModeTriangles          = 4;

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    // glTF buffers are little-endian regardless of host.
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFU));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFU));
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
/// five and the accessor bounds are silently garbage.
///
/// Non-finite values are rejected before they reach here (writeGlb validates),
/// because "nan"/"inf" are not JSON at all and would make the file unparseable.
std::string fmtFloat(float v) {
    return foundation::formatGeneral(v, 7);
}

/// An accessor bound, which must contain the data it bounds.
///
/// The spec requires min/max to be the true minimum and maximum. Two ways to
/// get this wrong, and this code has now had both:
///
///  * `fmtFloat`'s 7 significant digits cannot round-trip a binary32
///    (FLT_DECIMAL_DIG is 9), so the printed bound lands INSIDE the real range
///    and the Khronos validator reports ACCESSOR_ELEMENT_OUT_OF_MAX_BOUND.
///  * The shortest string that round-trips as a *float* is still not enough,
///    because a validator parses JSON numbers as doubles and compares against
///    the data widened to double. `0.84894335` round-trips to the right float
///    yet is strictly less than that float's double value
///    0.8489433526992798, so the bound is still too small.
///
/// Widening first and formatting as a double gives the shortest decimal that
/// parses back to exactly the same value the comparison uses. Only the bounds
/// need this: vertex data is binary, and everything else here is cosmetic.
std::string fmtBound(float v) {
    return foundation::formatShortest(static_cast<double>(v));
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
        case GltfWriteErrorKind::InvalidSkin: k = "invalid skin"; break;
        case GltfWriteErrorKind::TextureUnsupported: k = "texture cannot be embedded"; break;
        case GltfWriteErrorKind::InvalidMorphTarget: k = "invalid morph target"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

namespace {

/// Where one entry's data landed in the shared binary buffer, and which
/// accessors describe it. Filled in two passes: packing, then index assignment.
struct Packed {
    bool withNormals{}, withUVs{}, withTangents{};
    size_t posOffset{}, posBytes{};
    size_t normOffset{}, normBytes{};
    size_t uvOffset{}, uvBytes{};
    size_t tangOffset{}, tangBytes{};
    size_t jointOffset{}, jointBytes{}, weightOffset{}, weightBytes{}, ibmOffset{}, ibmBytes{};
    size_t idxOffset{}, idxBytes{};

    /// One morph target's blocks in the binary buffer. Grouped rather than
    /// kept as another five parallel vectors: sparse doubled the per-target
    /// state, and three loops walk it by index.
    struct MorphBlock {
        size_t valOffset{}, valBytes{};
        /// Zero means dense: `val` then describes every vertex and there is no
        /// index block.
        size_t sparseCount{}, idxOffset{}, idxBytes{};
        Vec3 lo{}, hi{};
    };

    std::vector<MorphBlock> morphs;
    Vec3 lo{}, hi{};
    std::vector<foundation::Mat4> localRest;

    /// The Draco block, when `GltfWriteOptions::draco` is on and this build has
    /// the codec. `dracoBytes == 0` means this entry is written plainly, so the
    /// two paths cannot be half-applied to one primitive.
    size_t dracoOffset{}, dracoBytes{};
    /// Filled in when the view is emitted, not computed from the entry shape:
    /// a second formula for "which view did that entry get" is one that can
    /// disagree with the loop.
    int dracoView{-1};
    std::vector<std::pair<std::string, uint32_t>> dracoAttributes;

    [[nodiscard]] bool compressed() const { return dracoBytes != 0; }

    int posAcc{-1}, normAcc{-1}, uvAcc{-1}, tangAcc{-1}, jointAcc{-1}, weightAcc{-1}, ibmAcc{-1},
        idxAcc{-1};
    std::vector<int> morphAcc;
    int materialIndex{};
};

/// Everything writeGlbScene rejects, in one place so a bad entry cannot be
/// half-written. Mirrors the single-mesh checks exactly.
std::expected<void, GltfWriteError> validateEntry(const std::filesystem::path& path,
                                                  const GltfSceneEntry& entry) {
    const foundation::RenderView& rm = entry.mesh;
    if (rm.vertexCount() == 0 || rm.indexCount() == 0) {
        return std::unexpected(GltfWriteError{GltfWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    if (rm.vertexCount() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(
            GltfWriteError{GltfWriteErrorKind::TooManyVertices, path.string(), {}});
    }

    // NaN and infinity have no JSON representation, so fmtFloat would emit a
    // bare `nan`/`inf` token and the whole file would fail to parse -- while
    // the writer still reported success. Reject at the boundary instead. Note
    // std::clamp does not filter NaN (both `nan < lo` and `hi < nan` are
    // false), so clamping the material later is not a substitute.
    for (const auto& c : rm.coord) {
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) {
            return std::unexpected(GltfWriteError{GltfWriteErrorKind::NonFiniteValue, path.string(),
                                                  "vertex position"});
        }
    }
    if (entry.material != nullptr) {
        const float mv[] = {entry.material->diffuse.x, entry.material->diffuse.y,
                            entry.material->diffuse.z, entry.material->opacity,
                            entry.material->shininess};
        for (const float v : mv) {
            if (!std::isfinite(v)) {
                return std::unexpected(GltfWriteError{GltfWriteErrorKind::NonFiniteValue,
                                                      path.string(), "material value"});
            }
        }
    }

    if (entry.skin != nullptr) {
        const foundation::SkinView* skin = entry.skin;
        if (!skin->valid() || skin->vertexCount() != rm.vertexCount() ||
            skin->influences != kGltfInfluences) {
            return std::unexpected(GltfWriteError{GltfWriteErrorKind::InvalidSkin, path.string(),
                                                  "skin does not describe this mesh"});
        }
        for (const uint32_t j : skin->joints) {
            if (j >= skin->jointCount()) {
                return std::unexpected(GltfWriteError{GltfWriteErrorKind::InvalidSkin,
                                                      path.string(),
                                                      "joint index " + std::to_string(j) + " of " +
                                                          std::to_string(skin->jointCount())});
            }
        }
        for (size_t i = 0; i < skin->jointCount(); ++i) {
            if (skin->jointParents[i] >= static_cast<int32_t>(i)) {
                return std::unexpected(GltfWriteError{
                    GltfWriteErrorKind::InvalidSkin, path.string(),
                    "joint " + std::to_string(i) + " has a parent that does not precede it"});
            }
        }
    }

    for (const auto& t : entry.morphTargets) {
        if (t.deltas.size() != rm.vertexCount()) {
            return std::unexpected(
                GltfWriteError{GltfWriteErrorKind::InvalidMorphTarget, path.string(),
                               t.name + " has " + std::to_string(t.deltas.size()) + " deltas for " +
                                   std::to_string(rm.vertexCount()) + " vertices"});
        }
        for (const auto& d : t.deltas) {
            if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) {
                return std::unexpected(GltfWriteError{GltfWriteErrorKind::NonFiniteValue,
                                                      path.string(), "morph target " + t.name});
            }
        }
    }
    return {};
}

/// Appends one entry's attributes to @p bin, recording where each block landed.
Packed packEntry(std::vector<uint8_t>& bin, const GltfSceneEntry& entry,
                 const GltfWriteOptions& options, float scale, float groundOffset) {
    const foundation::RenderView& rm = entry.mesh;
    Packed pk;
    pk.withNormals = options.writeNormals && rm.vnorm.size() == rm.vertexCount();
    pk.withUVs     = options.writeUVs && rm.texco.size() == rm.vertexCount();
    // Tangents ride with normals: a TANGENT without a NORMAL is meaningless,
    // and glTF's normal-map path needs both. Written when the mesh has them --
    // Mesh::calcVertexTangents leaves the array empty for a UV-less mesh.
    pk.withTangents = pk.withNormals && rm.vtang.size() == rm.vertexCount();

    pk.lo = Vec3{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity()};
    pk.hi = Vec3{-pk.lo.x, -pk.lo.y, -pk.lo.z};

    // Built once, then either written to the buffer or handed to Draco. The
    // transforms are not cosmetic -- the scale, the ground offset and the V flip
    // are what makes the file correct -- so compressing `rm` directly would
    // produce a compressed mesh that disagrees with the uncompressed one about
    // its size, its height and which way up its textures go.
    std::vector<Vec3> pos;
    pos.reserve(rm.coord.size());
    for (const Vec3& v : rm.coord) {
        pos.push_back({v.x * scale, (v.y * scale) + groundOffset, v.z * scale});
        pk.lo.x = std::min(pk.lo.x, pos.back().x);
        pk.lo.y = std::min(pk.lo.y, pos.back().y);
        pk.lo.z = std::min(pk.lo.z, pos.back().z);
        pk.hi.x = std::max(pk.hi.x, pos.back().x);
        pk.hi.y = std::max(pk.hi.y, pos.back().y);
        pk.hi.z = std::max(pk.hi.z, pos.back().z);
    }

    std::vector<Vec2> uv;
    if (pk.withUVs) {
        uv.reserve(rm.texco.size());
        // glTF's UV origin is top-left; OBJ/MakeHuman's is bottom-left, so V is
        // flipped. Getting this wrong mirrors every texture vertically.
        for (const Vec2& t : rm.texco)
            uv.push_back({t.x, 1.0F - t.y});
    }

    std::vector<Vec4> tang;
    if (pk.withTangents) {
        tang.reserve(rm.vtang.size());
        // The handedness, and the reason this is VEC4 rather than VEC3. The
        // reference computes its binormal as `cross(normal, tangent)` and so
        // discards Lengyel's sign, which inverts normal-map lighting on the
        // mirrored half of a symmetric body (project_context.md 8). Ours
        // carries it, and glTF's spec says exactly +1 or -1.
        for (const Vec4& t : rm.vtang)
            tang.push_back({t.x, t.y, t.z, t.w < 0.0F ? -1.0F : 1.0F});
    }

    std::vector<uint16_t> jointIdx;
    if (entry.skin != nullptr) {
        jointIdx.reserve(entry.skin->joints.size());
        for (const uint32_t jIdx : entry.skin->joints)
            jointIdx.push_back(static_cast<uint16_t>(jIdx));
    }

    if (options.draco && dracoAvailable()) {
        const foundation::RenderView compressible{pos, uv, rm.vnorm, tang, rm.index};
        const DracoSkin skin{
            jointIdx, entry.skin != nullptr ? entry.skin->weights : std::span<const float>{}};
        if (auto enc = dracoEncode(compressible, skin)) {
            padTo4(bin);
            pk.dracoOffset = bin.size();
            bin.insert(bin.end(), enc->bytes.begin(), enc->bytes.end());
            pk.dracoBytes      = bin.size() - pk.dracoOffset;
            pk.dracoAttributes = std::move(enc->attributes);
        }
    }

    // Everything below is the UNCOMPRESSED layout. A compressed entry writes
    // none of it: the geometry lives in the Draco block and its accessors carry
    // no bufferView, so leaving these blocks in would be dead bytes a consumer
    // might read instead.
    if (!pk.compressed()) {
        pk.posOffset = bin.size();
        for (const Vec3& v : pos) {
            appendFloat(bin, v.x);
            appendFloat(bin, v.y);
            appendFloat(bin, v.z);
        }
        pk.posBytes = bin.size() - pk.posOffset;
    }

    if (pk.withNormals && !pk.compressed()) {
        padTo4(bin);
        pk.normOffset = bin.size();
        for (const Vec3& n : rm.vnorm) {
            appendFloat(bin, n.x);
            appendFloat(bin, n.y);
            appendFloat(bin, n.z);
        }
        pk.normBytes = bin.size() - pk.normOffset;
    }

    if (pk.withUVs && !pk.compressed()) {
        padTo4(bin);
        pk.uvOffset = bin.size();
        for (const Vec2& t : uv) {
            appendFloat(bin, t.x);
            appendFloat(bin, t.y);
        }
        pk.uvBytes = bin.size() - pk.uvOffset;
    }

    if (pk.withTangents && !pk.compressed()) {
        padTo4(bin);
        pk.tangOffset = bin.size();
        for (const Vec4& t : tang) {
            appendFloat(bin, t.x);
            appendFloat(bin, t.y);
            appendFloat(bin, t.z);
            appendFloat(bin, t.w);
        }
        pk.tangBytes = bin.size() - pk.tangOffset;
    }

    if (entry.skin != nullptr) {
        const foundation::SkinView* skin = entry.skin;
        // Scale the joints exactly as the mesh was scaled: rotation unchanged,
        // translation through the same scale and ground offset. Doing this here
        // rather than trusting the caller is what keeps the rig and the mesh in
        // the same space.
        const auto place = [scale, groundOffset](std::span<const foundation::Mat4> src) {
            std::vector<foundation::Mat4> out(src.begin(), src.end());
            for (auto& m : out) {
                m.m[0][3] *= scale;
                m.m[1][3] = m.m[1][3] * scale + groundOffset;
                m.m[2][3] *= scale;
            }
            return out;
        };

        // The BIND pose. Inverse-bind matrices come from here and nowhere else.
        std::vector<foundation::Mat4> scaledGlobal = place(skin->globalRest);

        // Where the joints actually SIT in the file.
        //
        // With a live rig these are the POSED globals, so the consumer computes
        // `pose * inverse(bind)` and reproduces our skinning from rest
        // geometry. With no pose the two arrays are the same and the skinning
        // matrices come out identity, which is the unposed export.
        //
        // The two must be allowed to differ here -- that is the whole feature.
        // They were deliberately derived from ONE array before, so they could
        // not disagree; the guarantee that replaces it is that the IBMs are
        // always `scaledGlobal` and the nodes are always `scaledNode`.
        const std::vector<foundation::Mat4> scaledNode =
            skin->globalPose.empty() ? scaledGlobal : place(skin->globalPose);

        // Node transforms are LOCAL to the parent; inverse-bind matrices are
        // global.
        pk.localRest.resize(scaledNode.size());
        for (size_t i = 0; i < scaledNode.size(); ++i) {
            const int32_t par = skin->jointParents[i];
            pk.localRest[i]   = (par < 0)
                                    ? scaledNode[i]
                                    : foundation::rigidInverse(scaledNode[static_cast<size_t>(par)]) *
                                        scaledNode[i];
        }

        if (!pk.compressed()) {
            padTo4(bin);
            pk.jointOffset = bin.size();
            for (const uint16_t jIdx : jointIdx)
                appendU16(bin, jIdx);
            pk.jointBytes = bin.size() - pk.jointOffset;

            padTo4(bin);
            pk.weightOffset = bin.size();
            for (const float w : skin->weights)
                appendFloat(bin, w);
            pk.weightBytes = bin.size() - pk.weightOffset;
        }

        padTo4(bin);
        pk.ibmOffset = bin.size();
        for (const auto& g : scaledGlobal) {
            const foundation::Mat4 inv = foundation::rigidInverse(g);
            // glTF stores matrices COLUMN-major. Ours are row-major, so this
            // transposes on the way out. Writing them row-major produces a file
            // that loads, poses, and is wrong in a way that looks like bad
            // weights.
            for (size_t c = 0; c < 4; ++c) {
                for (size_t r = 0; r < 4; ++r)
                    appendFloat(bin, inv.m[r][c]);
            }
        }
        pk.ibmBytes = bin.size() - pk.ibmOffset;
    }

    // Morph targets, one block per target. Scaled like positions, but WITHOUT
    // the ground offset -- a delta is a displacement, not a point, so adding the
    // offset would shift the body once per active target.
    //
    // Written SPARSE when that is smaller. A target is a delta per render
    // vertex and almost all of them are zero: nose-base-up moves 305 of 19,158
    // mesh vertices, yet dense costs the same 12 bytes per vertex as one that
    // moves everything. glTF's sparse accessor stores an index list plus a
    // value list and takes the base as zero, which is exactly what an unmoved
    // vertex means.
    for (const auto& t : entry.morphTargets) {
        std::vector<uint32_t> nonZero;
        for (uint32_t v = 0; v < t.deltas.size(); ++v) {
            const Vec3& d = t.deltas[v];
            if (d.x != 0.0F || d.y != 0.0F || d.z != 0.0F) nonZero.push_back(v);
        }

        // 16 bytes a vertex sparse (a uint32 index and a vec3) against 12
        // dense, plus roughly 120 bytes for the second bufferView's JSON. A
        // target that moves most of the mesh is genuinely cheaper dense, so the
        // choice is made per target rather than once for the file.
        Packed::MorphBlock mb;
        const bool sparse = nonZero.size() * 16U + 120U < t.deltas.size() * 12U;

        Vec3 tlo{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::infinity()};
        Vec3 thi{-tlo.x, -tlo.y, -tlo.z};
        const auto extend = [&](const Vec3& v) {
            tlo.x = std::min(tlo.x, v.x);
            tlo.y = std::min(tlo.y, v.y);
            tlo.z = std::min(tlo.z, v.z);
            thi.x = std::max(thi.x, v.x);
            thi.y = std::max(thi.y, v.y);
            thi.z = std::max(thi.z, v.z);
        };

        if (sparse) {
            // Indices first, ascending -- the spec requires strictly
            // increasing, which walking the deltas in order gives for free.
            padTo4(bin);
            mb.sparseCount = nonZero.size();
            mb.idxOffset   = bin.size();
            for (const uint32_t v : nonZero)
                appendU32(bin, v);
            mb.idxBytes = bin.size() - mb.idxOffset;

            // The vertices left out read as zero, and min/max describe the
            // accessor's EFFECTIVE values. Sparse always leaves some out -- it
            // would not be smaller otherwise -- so the zero is unconditional.
            extend(Vec3{});
        }

        padTo4(bin);
        mb.valOffset   = bin.size();
        const size_t n = sparse ? nonZero.size() : t.deltas.size();
        for (size_t k = 0; k < n; ++k) {
            const Vec3& d = t.deltas[sparse ? nonZero[k] : k];
            const Vec3 v{d.x * scale, d.y * scale, d.z * scale};
            appendFloat(bin, v.x);
            appendFloat(bin, v.y);
            appendFloat(bin, v.z);
            extend(v);
        }
        mb.valBytes = bin.size() - mb.valOffset;
        mb.lo       = tlo;
        mb.hi       = thi;
        pk.morphs.push_back(mb);
    }

    if (!pk.compressed()) {
        padTo4(bin);
        pk.idxOffset = bin.size();
        for (const uint32_t i : rm.index)
            appendU32(bin, i);
        pk.idxBytes = bin.size() - pk.idxOffset;
    }

    padTo4(bin);
    return pk;
}

/// glTF 2.0 permits exactly two image formats, and the declared mimeType must
/// match the actual bytes -- a PNG named `.jpg` is IMAGE_MIME_TYPE_INVALID to a
/// validator. So this reads the magic bytes rather than trusting the extension,
/// which is a claim and not evidence. Anything else is refused: dropping it
/// would export an untextured model while reporting success, and there is no
/// converter here to fall back on.
std::string_view mimeTypeOf(std::span<const char> bytes) {
    static constexpr std::array<unsigned char, 8> kPng{0x89, 0x50, 0x4E, 0x47,
                                                       0x0D, 0x0A, 0x1A, 0x0A};
    static constexpr std::array<unsigned char, 3> kJpeg{0xFF, 0xD8, 0xFF};
    const auto matches = [&](const auto& magic) {
        return bytes.size() >= magic.size() &&
               std::equal(magic.begin(), magic.end(), bytes.begin(), [](unsigned char a, char b) {
                   return a == static_cast<unsigned char>(b);
               });
    };
    if (matches(kPng)) return "image/png";
    if (matches(kJpeg)) return "image/jpeg";
    return {};
}

/// One image embedded in the BIN chunk. GLB carries its textures inside the
/// file, which is what makes a single .glb self-contained -- the alternative is
/// a URI and a sidecar the user has to keep next to it.
struct EmbeddedImage {
    std::filesystem::path source;
    std::string mimeType;
    size_t offset{};
    size_t bytes{};
    int view{-1};
};

}  // namespace

std::expected<GltfWriteResult, GltfWriteError> writeGlbScene(
    const std::filesystem::path& path, std::span<const GltfSceneEntry> entries,
    const GltfWriteOptions& options) {
    if (entries.empty()) {
        return std::unexpected(GltfWriteError{GltfWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    for (const GltfSceneEntry& e : entries) {
        if (const auto ok = validateEntry(path, e); !ok) return std::unexpected(ok.error());
    }

    // Joint nodes follow the mesh nodes, so one skeleton's node block is easy
    // to address and two would collide. Refused rather than silently dropped.
    const size_t skinned = static_cast<size_t>(std::count_if(
        entries.begin(), entries.end(), [](const GltfSceneEntry& e) { return e.skin != nullptr; }));
    if (skinned > 1) {
        return std::unexpected(GltfWriteError{GltfWriteErrorKind::InvalidSkin, path.string(),
                                              "only one entry may carry a skin"});
    }

    // The same two material hazards the OBJ writer refuses (ObjWriter.cpp), and
    // for the same reasons -- the two writers must not disagree about what is
    // legal. Merging on name alone would drop the second material's colour and
    // its alphaMode, so a transparent item exports opaque.
    const bool anyMaterial =
        std::any_of(entries.begin(), entries.end(),
                    [](const GltfSceneEntry& e) { return e.material != nullptr; });
    const bool allMaterials =
        std::all_of(entries.begin(), entries.end(),
                    [](const GltfSceneEntry& e) { return e.material != nullptr; });
    if (anyMaterial && !allMaterials) {
        return std::unexpected(GltfWriteError{
            GltfWriteErrorKind::NonFiniteValue, path.string(),
            "some entries carry a material and some do not; the material-less ones would take "
            "the default and could collide with a real one"});
    }

    const Transform xf =
        sceneTransform(unitScale(options.unit) * options.scale, options.feetOnGround, entries);
    const float scale        = xf.scale;
    const float groundOffset = xf.groundOffset;

    // ---- binary buffer ----------------------------------------------------
    std::vector<uint8_t> bin;
    std::vector<Packed> packs;
    packs.reserve(entries.size());
    for (const GltfSceneEntry& e : entries)
        packs.push_back(packEntry(bin, e, options, scale, groundOffset));

    // ---- materials, deduped by name in first-use order ---------------------
    struct MatSlot {
        std::string name;
        const foundation::MaterialDesc* desc;
    };

    std::vector<MatSlot> mats;
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string name = options.materialName;
        if (entries[i].material != nullptr && !entries[i].material->name.empty()) {
            name = entries[i].material->name;
        }
        const auto seen = std::find_if(mats.begin(), mats.end(),
                                       [&](const MatSlot& m) { return m.name == name; });
        if (seen == mats.end()) {
            mats.push_back({name, entries[i].material});
            packs[i].materialIndex = static_cast<int>(mats.size()) - 1;
        } else if (seen->desc != entries[i].material) {
            return std::unexpected(
                GltfWriteError{GltfWriteErrorKind::NonFiniteValue, path.string(),
                               "two different materials are both named \"" + name + "\""});
        } else {
            packs[i].materialIndex = static_cast<int>(seen - mats.begin());
        }
    }

    // ---- textures ---------------------------------------------------------
    // Deduped by path: a body and a proxy sharing one map must embed it once.
    std::vector<EmbeddedImage> images;
    std::vector<int> baseColorImage(mats.size(), -1);
    std::vector<int> normalImage(mats.size(), -1);
    for (size_t i = 0; i < mats.size(); ++i) {
        if (mats[i].desc == nullptr) continue;
        const std::pair<const std::filesystem::path&, std::vector<int>&> slots[] = {
            {mats[i].desc->diffuseTexture, baseColorImage},
            {mats[i].desc->normalTexture, normalImage},
        };
        for (const auto& [texture, into] : slots) {
            if (texture.empty()) continue;

            // A texture needs UVs to sample it. Without them the primitive is
            // MESH_PRIMITIVE_TOO_FEW_TEXCOORDS -- a hard validator error, not a
            // cosmetic one.
            for (size_t e = 0; e < entries.size(); ++e) {
                if (packs[e].materialIndex == static_cast<int>(i) && !packs[e].withUVs) {
                    return std::unexpected(GltfWriteError{
                        GltfWriteErrorKind::TextureUnsupported, path.string(),
                        entries[e].name + " has a textured material but no UVs to sample it"});
                }
            }

            const auto seen =
                std::find_if(images.begin(), images.end(),
                             [&](const EmbeddedImage& e) { return e.source == texture; });
            if (seen != images.end()) {
                into[i] = static_cast<int>(seen - images.begin());
                continue;
            }
            std::ifstream file(texture, std::ios::binary);
            if (!file) {
                return std::unexpected(GltfWriteError{GltfWriteErrorKind::TextureUnsupported,
                                                      path.string(),
                                                      "cannot read " + texture.string()});
            }
            const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
            if (bytes.empty()) {
                // glTF requires bufferView.byteLength >= 1, so a zero-byte file
                // (or a directory, which reads as empty) makes an invalid GLB.
                return std::unexpected(GltfWriteError{GltfWriteErrorKind::TextureUnsupported,
                                                      path.string(),
                                                      "empty texture " + texture.string()});
            }
            const std::string_view mime = mimeTypeOf(bytes);
            if (mime.empty()) {
                return std::unexpected(
                    GltfWriteError{GltfWriteErrorKind::TextureUnsupported, path.string(),
                                   "glTF allows only PNG and JPEG; " + texture.string() +
                                       " is neither, whatever its extension (" +
                                       texture.extension().string() + ") says"});
            }
            padTo4(bin);
            EmbeddedImage img;
            img.source   = texture;
            img.mimeType = std::string(mime);
            img.offset   = bin.size();
            img.bytes    = bytes.size();
            bin.insert(bin.end(), bytes.begin(), bytes.end());
            into[i] = static_cast<int>(images.size());
            images.push_back(std::move(img));
        }
    }

    // ---- JSON -------------------------------------------------------------
    std::string j;
    j.reserve(2048);
    j += R"({"asset":{"version":"2.0","generator":"MakeHuman C++ glTF writer"},)";
    // REQUIRED, not merely used. The geometry exists in no other form in the
    // file, so a consumer without a Draco decoder has to refuse it rather than
    // open an empty scene -- which is what `extensionsUsed` alone would invite.
    if (std::ranges::any_of(packs, [](const Packed& p) { return p.compressed(); })) {
        j += R"("extensionsUsed":["KHR_draco_mesh_compression"],)"
             R"("extensionsRequired":["KHR_draco_mesh_compression"],)";
    }

    // Mesh nodes come first, so joints occupy nodes[entries.size() ..] and a
    // joint's node index is its skin index plus that base.
    const size_t jointBase                = entries.size();
    const foundation::SkinView* sceneSkin = nullptr;
    size_t skinnedEntry                   = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].skin != nullptr) {
            sceneSkin    = entries[i].skin;
            skinnedEntry = i;
        }
    }

    j += R"("scene":0,"scenes":[{"nodes":[)";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) j += ",";
        j += std::to_string(i);
    }
    if (sceneSkin != nullptr) {
        // glTF requires every joint to be a node reachable in the scene graph,
        // not a loose array.
        for (size_t i = 0; i < sceneSkin->jointCount(); ++i) {
            if (sceneSkin->jointParents[i] < 0) j += "," + std::to_string(jointBase + i);
        }
    }
    j += R"(]}],)";

    j += R"("nodes":[)";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) j += ",";
        j += R"({"mesh":)" + std::to_string(i);
        if (entries[i].skin != nullptr) j += R"(,"skin":0)";
        j += R"(,"name":")" + jsonEscape(entries[i].name) + R"("})";
    }
    if (sceneSkin != nullptr) {
        for (size_t i = 0; i < sceneSkin->jointCount(); ++i) {
            j += R"(,{"name":")" + jsonEscape(sceneSkin->jointNames[i]) + R"(","matrix":[)";
            // Column-major, as glTF requires. Ours are row-major.
            for (size_t c = 0; c < 4; ++c) {
                for (size_t r = 0; r < 4; ++r) {
                    if (c != 0 || r != 0) j += ",";
                    j += fmtFloat(packs[skinnedEntry].localRest[i].m[r][c]);
                }
            }
            j += "]";

            bool firstChild = true;
            for (size_t k = 0; k < sceneSkin->jointCount(); ++k) {
                if (sceneSkin->jointParents[k] != static_cast<int32_t>(i)) continue;
                j += firstChild ? R"(,"children":[)" : ",";
                j += std::to_string(jointBase + k);
                firstChild = false;
            }
            if (!firstChild) j += "]";
            j += "}";
        }
    }
    j += "],";

    // buffers / bufferViews
    j += R"("buffers":[{"byteLength":)" + std::to_string(bin.size()) + "}],";

    j += R"("bufferViews":[)";
    bool firstView = true;
    // Counted as they are emitted, not re-derived from the entry shape: a
    // second formula for "how many views did that entry write" is one that can
    // disagree with the loop, and the images would then point at mesh data.
    int viewCount   = 0;
    const auto view = [&](size_t offset, size_t bytes, int target) {
        ++viewCount;
        if (!firstView) j += ",";
        firstView = false;
        j += R"({"buffer":0,"byteOffset":)" + std::to_string(offset) + R"(,"byteLength":)" +
             std::to_string(bytes);
        if (target >= 0) j += R"(,"target":)" + std::to_string(target);
        j += "}";
    };
    for (size_t i = 0; i < entries.size(); ++i) {
        Packed& pk = packs[i];
        if (pk.compressed()) {
            pk.dracoView = viewCount;
            // ONE view for all of the geometry, and no "target": the block is a
            // Draco bitstream, not an array of vertices, and a validator flags
            // an ARRAY_BUFFER target on it. It is emitted FIRST so its index is
            // the entry's first, which is what the accessor loop below relies on
            // when it skips a slot.
            view(pk.dracoOffset, pk.dracoBytes, -1);
        } else {
            view(pk.posOffset, pk.posBytes, kTargetArrayBuffer);
            if (pk.withNormals) view(pk.normOffset, pk.normBytes, kTargetArrayBuffer);
            if (pk.withUVs) view(pk.uvOffset, pk.uvBytes, kTargetArrayBuffer);
            if (pk.withTangents) view(pk.tangOffset, pk.tangBytes, kTargetArrayBuffer);
        }
        if (entries[i].skin != nullptr) {
            if (!pk.compressed()) {
                view(pk.jointOffset, pk.jointBytes, kTargetArrayBuffer);
                view(pk.weightOffset, pk.weightBytes, kTargetArrayBuffer);
            }
            // No "target" on the inverse-bind view: it is not vertex data, and a
            // validator flags an ARRAY_BUFFER target on a MAT4 accessor.
            view(pk.ibmOffset, pk.ibmBytes, -1);
        }
        for (const Packed::MorphBlock& mb : pk.morphs) {
            // A sparse accessor's own bufferViews must NOT declare a target
            // (glTF 2.0 5.1.1); its index block is not vertex data at all.
            if (mb.sparseCount != 0) {
                view(mb.idxOffset, mb.idxBytes, -1);
                view(mb.valOffset, mb.valBytes, -1);
            } else {
                view(mb.valOffset, mb.valBytes, kTargetArrayBuffer);
            }
        }
        if (!pk.compressed()) view(pk.idxOffset, pk.idxBytes, kTargetElementArray);
    }
    // Image views come last so the accessor indices assigned above stay valid.
    // No "target": image bytes are neither vertex nor index data.
    for (EmbeddedImage& img : images) {
        img.view = viewCount;
        view(img.offset, img.bytes, -1);
    }
    j += "],";

    // accessors -- POSITION is required to carry min and max.
    j += R"("accessors":[)";
    int nextView   = 0;
    int nextAcc    = 0;
    bool firstAcc  = true;
    const auto acc = [&](int componentType, size_t count, const char* type) {
        if (!firstAcc) j += ",";
        firstAcc = false;
        j += R"({"bufferView":)" + std::to_string(nextView++) + R"(,"componentType":)" +
             std::to_string(componentType) + R"(,"count":)" + std::to_string(count) +
             R"(,"type":")" + type + "\"";
    };
    // The compressed twin: same description, NO bufferView, and no view index
    // consumed. KHR_draco_mesh_compression requires exactly this -- the
    // accessor still says how much data a decoder will produce, and points at
    // nothing, because the bytes only exist inside the Draco block.
    const auto accNoView = [&](int componentType, size_t count, const char* type) {
        if (!firstAcc) j += ",";
        firstAcc = false;
        j += R"({"componentType":)" + std::to_string(componentType) + R"(,"count":)" +
             std::to_string(count) + R"(,"type":")" + type + "\"";
    };
    for (size_t i = 0; i < entries.size(); ++i) {
        Packed& pk   = packs[i];
        const auto n = entries[i].mesh.vertexCount();
        // The geometry accessor helper for THIS entry.
        const auto geo = [&](int componentType, size_t count, const char* type) {
            if (pk.compressed()) {
                accNoView(componentType, count, type);
            } else {
                acc(componentType, count, type);
            }
        };
        // The Draco block is this entry's first view and no accessor points at
        // it, so the counter has to step over it or every later accessor here
        // reads the wrong block.
        if (pk.compressed()) ++nextView;
        pk.posAcc = nextAcc++;
        geo(kComponentFloat, n, "VEC3");
        j += R"(,"min":[)" + fmtBound(pk.lo.x) + "," + fmtBound(pk.lo.y) + "," + fmtBound(pk.lo.z) +
             R"(],"max":[)" + fmtBound(pk.hi.x) + "," + fmtBound(pk.hi.y) + "," +
             fmtBound(pk.hi.z) + "]}";
        if (pk.withNormals) {
            pk.normAcc = nextAcc++;
            geo(kComponentFloat, n, "VEC3");
            j += "}";
        }
        if (pk.withUVs) {
            pk.uvAcc = nextAcc++;
            geo(kComponentFloat, n, "VEC2");
            j += "}";
        }
        if (pk.withTangents) {
            pk.tangAcc = nextAcc++;
            geo(kComponentFloat, n, "VEC4");
            j += "}";
        }
        if (entries[i].skin != nullptr) {
            pk.jointAcc = nextAcc++;
            geo(kComponentUnsignedShort, n, "VEC4");
            j += "}";
            pk.weightAcc = nextAcc++;
            geo(kComponentFloat, n, "VEC4");
            j += "}";
            pk.ibmAcc = nextAcc++;
            acc(kComponentFloat, entries[i].skin->jointCount(), "MAT4");
            j += "}";
        }
        for (const Packed::MorphBlock& mb : pk.morphs) {
            pk.morphAcc.push_back(nextAcc++);
            int idxView = -1;
            int valView = -1;
            if (mb.sparseCount != 0) {
                // No bufferView of its own: with `sparse` and no view the base
                // values are all zero, which is what an unmoved vertex is. The
                // two views written above are consumed here, in that order.
                idxView = nextView++;
                valView = nextView++;
                if (!firstAcc) j += ",";
                firstAcc = false;
                j += R"({"componentType":)" + std::to_string(kComponentFloat) + R"(,"count":)" +
                     std::to_string(n) + R"(,"type":"VEC3")";
            } else {
                acc(kComponentFloat, n, "VEC3");
            }
            // min/max is required on every POSITION accessor, and a morph
            // target's deltas are one. Omitting it is the most common way a
            // hand-written morph export fails validation while still loading in
            // some engines. For a sparse target these describe the EFFECTIVE
            // values, zeros included.
            j += R"(,"min":[)" + fmtBound(mb.lo.x) + "," + fmtBound(mb.lo.y) + "," +
                 fmtBound(mb.lo.z) + R"(],"max":[)" + fmtBound(mb.hi.x) + "," + fmtBound(mb.hi.y) +
                 "," + fmtBound(mb.hi.z) + "]";
            if (mb.sparseCount != 0) {
                j += R"(,"sparse":{"count":)" + std::to_string(mb.sparseCount) +
                     R"(,"indices":{"bufferView":)" + std::to_string(idxView) +
                     R"(,"byteOffset":0,"componentType":)" + std::to_string(kComponentUnsignedInt) +
                     R"(},"values":{"bufferView":)" + std::to_string(valView) +
                     R"(,"byteOffset":0}})";
            }
            j += "}";
        }
        pk.idxAcc = nextAcc++;
        geo(kComponentUnsignedInt, entries[i].mesh.indexCount(), "SCALAR");
        j += "}";
    }
    j += "],";

    // meshes -- one per entry.
    j += R"("meshes":[)";
    for (size_t i = 0; i < entries.size(); ++i) {
        const Packed& pk = packs[i];
        if (i != 0) j += ",";
        j += R"({"name":")" + jsonEscape(entries[i].name) +
             R"(","primitives":[{"attributes":{"POSITION":)" + std::to_string(pk.posAcc);
        if (pk.normAcc >= 0) j += R"(,"NORMAL":)" + std::to_string(pk.normAcc);
        if (pk.uvAcc >= 0) j += R"(,"TEXCOORD_0":)" + std::to_string(pk.uvAcc);
        if (pk.tangAcc >= 0) j += R"(,"TANGENT":)" + std::to_string(pk.tangAcc);
        if (pk.jointAcc >= 0) j += R"(,"JOINTS_0":)" + std::to_string(pk.jointAcc);
        if (pk.weightAcc >= 0) j += R"(,"WEIGHTS_0":)" + std::to_string(pk.weightAcc);
        j += R"(},"indices":)" + std::to_string(pk.idxAcc) + R"(,"material":)" +
             std::to_string(pk.materialIndex) + R"(,"mode":)" + std::to_string(kModeTriangles);
        if (pk.compressed()) {
            // The map is draco's own attribute UNIQUE ids, keyed by the glTF
            // names above. The `bufferView` is the whole compressed block: a
            // decoder reads it once and gets every attribute out.
            j += R"(,"extensions":{"KHR_draco_mesh_compression":{"bufferView":)" +
                 std::to_string(pk.dracoView) + R"(,"attributes":{)";
            bool firstDracoAttr = true;
            for (const auto& [name, id] : pk.dracoAttributes) {
                if (!firstDracoAttr) j += ",";
                firstDracoAttr = false;
                j += "\"" + name + "\":" + std::to_string(id);
            }
            j += "}}}";
        }
        if (!pk.morphAcc.empty()) {
            j += R"(,"targets":[)";
            for (size_t t = 0; t < pk.morphAcc.size(); ++t) {
                if (t != 0) j += ",";
                j += R"({"POSITION":)" + std::to_string(pk.morphAcc[t]) + "}";
            }
            j += "]";
        }
        j += "}]";
        if (!pk.morphAcc.empty()) {
            // Default weights: all zero, i.e. the base mesh. A viewer that
            // ignores them still shows the unmorphed body rather than every
            // target at once.
            j += R"(,"weights":[)";
            for (size_t t = 0; t < pk.morphAcc.size(); ++t)
                j += (t != 0 ? ",0" : "0");
            j += "]";

            // targetNames is an extras convention rather than core glTF, but it
            // is what Blender and every DCC read to label the shape keys.
            // Without it the targets import as "Key 1", "Key 2", ... and become
            // unusable.
            j += R"(,"extras":{"targetNames":[)";
            for (size_t t = 0; t < pk.morphAcc.size(); ++t) {
                if (t != 0) j += ",";
                j += "\"" + jsonEscape(entries[i].morphTargets[t].name) + "\"";
            }
            j += "]}";
        }
        j += "}";
    }
    j += "],";

    if (!images.empty()) {
        j += R"("images":[)";
        for (size_t i = 0; i < images.size(); ++i) {
            if (i != 0) j += ",";
            j += R"({"bufferView":)" + std::to_string(images[i].view) + R"(,"mimeType":")" +
                 images[i].mimeType + R"("})";
        }
        j += "],";
        // One texture per image, with the default sampler. Omitting "sampler"
        // is legal and means repeat/auto, which is what these maps want.
        j += R"("textures":[)";
        for (size_t i = 0; i < images.size(); ++i) {
            if (i != 0) j += ",";
            j += R"({"source":)" + std::to_string(i) + "}";
        }
        j += "],";
    }

    // materials -- Blinn-Phong converted to metallic-roughness, which is the
    // only PBR model core glTF defines. The reference has no PBR data at all,
    // so roughness is derived from shininess and metallic is 0 (skin, cloth and
    // hair are all dielectric).
    j += R"("materials":[)";
    for (size_t i = 0; i < mats.size(); ++i) {
        float baseR = 0.8F, baseG = 0.8F, baseB = 0.8F, alpha = 1.0F, roughness = 0.7F;
        // Zero for a described material too -- see foundation::metallicRoughnessOf
        // for why -- but taken from there rather than written twice.
        float metallic = 0.0F;
        if (mats[i].desc != nullptr) {
            baseR         = mats[i].desc->diffuse.x;
            baseG         = mats[i].desc->diffuse.y;
            baseB         = mats[i].desc->diffuse.z;
            alpha         = mats[i].desc->opacity;
            const auto mr = foundation::metallicRoughnessOf(*mats[i].desc);
            roughness     = mr.roughness;
            metallic      = mr.metallic;
        }
        if (i != 0) j += ",";
        j += R"({"name":")" + jsonEscape(mats[i].name) +
             R"(","pbrMetallicRoughness":{"baseColorFactor":[)" + fmtFloat(baseR) + "," +
             fmtFloat(baseG) + "," + fmtFloat(baseB) + "," + fmtFloat(alpha) +
             R"(],"metallicFactor":)" + fmtFloat(metallic) + R"(,"roughnessFactor":)" +
             fmtFloat(roughness);
        if (baseColorImage[i] >= 0) {
            j += R"(,"baseColorTexture":{"index":)" + std::to_string(baseColorImage[i]) + "}";
        }
        j += "}";
        if (normalImage[i] >= 0) {
            j += R"(,"normalTexture":{"index":)" + std::to_string(normalImage[i]) + "}";
        }
        // Without this the alpha channel of a baseColorTexture is ignored:
        // glTF's default alphaMode is OPAQUE and the spec says so explicitly.
        // The scalar opacity is not enough -- the shipped eye material is
        // `opacity 1.0` with `transparent True` over an RGBA map.
        const bool blended = alpha < 1.0F || (mats[i].desc != nullptr && mats[i].desc->transparent);
        if (blended) j += R"(,"alphaMode":"BLEND")";
        j += "}";
    }
    j += "]";

    if (sceneSkin != nullptr) {
        j += R"(,"skins":[{"inverseBindMatrices":)" + std::to_string(packs[skinnedEntry].ibmAcc) +
             R"(,"joints":[)";
        for (size_t i = 0; i < sceneSkin->jointCount(); ++i) {
            if (i != 0) j += ",";
            j += std::to_string(jointBase + i);
        }
        j += "]}]";
    }
    j += "}";

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
        return std::unexpected(
            GltfWriteError{images.empty() ? GltfWriteErrorKind::TooManyVertices
                                          : GltfWriteErrorKind::TextureUnsupported,
                           path.string(), "GLB exceeds the 4 GiB container limit"});
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

    size_t verts = 0, tris = 0;
    for (const GltfSceneEntry& e : entries) {
        verts += e.mesh.vertexCount();
        tris += e.mesh.indexCount() / 3;
    }
    return GltfWriteResult{verts, tris, glb.size()};
}

std::expected<GltfWriteResult, GltfWriteError> writeGlb(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const GltfWriteOptions& options, const foundation::MaterialDesc* material,
    const foundation::SkinView* skin, std::span<const foundation::MorphTarget> morphTargets) {
    const GltfSceneEntry entry{mesh, options.meshName, material, skin, morphTargets};
    return writeGlbScene(path, {&entry, 1}, options);
}

}  // namespace mh::io
