// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/UsdWriter.h"

#include "makehuman/foundation/Chars.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <vector>

namespace mh::io {
namespace {

using foundation::Vec2;
using foundation::Vec3;

/// USD numbers are plain decimal text. Shortest round-trip keeps the file
/// readable without losing precision, and formatShortest is locale-independent
/// -- a decimal comma would make the array parse as twice as many elements.
std::string num(float v) {
    return foundation::formatShortest(v);
}

std::string num(double v) {
    return foundation::formatShortest(static_cast<float>(v));
}

/// A USD prim name: identifiers only, so `[A-Za-z_][A-Za-z0-9_]*`.
///
/// MakeHuman bone names are NOT valid here -- `upperarm01.L` and `finger1-1.L`
/// carry a dot and a dash, and a dot is the property separator in a USD path.
/// Emitting them raw produces a stage `usdchecker` rejects.
std::string usdIdentifier(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') ? c : '_');
    }
    if (out.empty()) out = "_";
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) out.insert(out.begin(), '_');
    return out;
}

/// A matrix4d literal in USD's convention.
///
/// **USD uses ROW vectors** (`v' = v * M`); this codebase uses COLUMN vectors
/// (`v' = M * v`). The two conventions are transposes, so writing our matrix
/// element-for-element would emit every joint transposed -- a stage that still
/// validates and poses wrongly. Translation therefore lands in the LAST ROW
/// here, not the last column.
std::string matrix4d(const foundation::Mat4& m) {
    std::string out = "( ";
    for (size_t r = 0; r < 4; ++r) {
        out += "(";
        for (size_t c = 0; c < 4; ++c) {
            // Transposed: USD row r is our column r.
            out += num(static_cast<double>(m.m[c][r]));
            if (c < 3) out += ", ";
        }
        out += ")";
        if (r < 3) out += ", ";
    }
    out += " )";
    return out;
}

}  // namespace

double UsdWriteOptions::metersPerUnit() const noexcept {
    // How many metres one exported unit represents. Points are written in
    // `unit`, so this is the inverse of the decimetre->unit scale, times the
    // decimetre->metre factor.
    switch (unit) {
        case Unit::Decimeter: return 0.1;
        case Unit::Meter: return 1.0;
        case Unit::Centimeter: return 0.01;
        case Unit::Inch: return 0.0254;
    }
    return 1.0;
}

std::string UsdWriteError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case UsdWriteErrorKind::CannotOpen: k = "cannot open for writing"; break;
        case UsdWriteErrorKind::EmptyMesh: k = "mesh has no geometry"; break;
        case UsdWriteErrorKind::NonFiniteValue: k = "non-finite value"; break;
        case UsdWriteErrorKind::InvalidMorphTarget: k = "invalid blend shape"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<UsdWriteResult, UsdWriteError> writeUsdaScene(const std::filesystem::path& path,
                                                            std::span<const UsdSceneEntry> entries,
                                                            const UsdWriteOptions& options,
                                                            const foundation::SkinView* skin) {
    if (entries.empty()) {
        return std::unexpected(UsdWriteError{UsdWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    for (const UsdSceneEntry& entry : entries) {
        if (entry.mesh.vertexCount() == 0 || entry.mesh.indexCount() == 0) {
            return std::unexpected(UsdWriteError{UsdWriteErrorKind::EmptyMesh, path.string(), {}});
        }
        for (const Vec3& v : entry.mesh.coord) {
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                return std::unexpected(UsdWriteError{UsdWriteErrorKind::NonFiniteValue,
                                                     path.string(), "vertex position"});
            }
        }
    }

    // Blend shapes, under the same one-entry rule as the skin.
    size_t morphed = entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].morphTargets.empty()) continue;
        if (morphed != entries.size()) {
            return std::unexpected(UsdWriteError{UsdWriteErrorKind::InvalidMorphTarget,
                                                 path.string(),
                                                 "more than one entry carries blend shapes"});
        }
        for (const auto& t : entries[i].morphTargets) {
            if (t.deltas.size() != entries[i].mesh.vertexCount()) {
                return std::unexpected(
                    UsdWriteError{UsdWriteErrorKind::InvalidMorphTarget, path.string(),
                                  t.name + ": wrong delta count in " + entries[i].name});
            }
        }
        morphed = i;
    }

    std::ofstream out(path);
    if (!out) {
        return std::unexpected(UsdWriteError{UsdWriteErrorKind::CannotOpen, path.string(), {}});
    }

    const Transform xf =
        sceneTransform(unitScale(options.unit) * options.scale, options.feetOnGround, entries);
    const float s = xf.scale;
    /// A point placed in the stage: unit-scaled, then lifted onto the ground.
    const auto placedY = [&xf](float y) { return xf.placedY(y); };

    out << "#usda 1.0\n(\n";
    out << "    defaultPrim = \"" << options.primName << "\"\n";
    out << "    doc = \"MakeHuman C++ USD writer\"\n";
    out << "    metersPerUnit = " << num(options.metersPerUnit()) << "\n";
    // USD stores the up axis, so a consumer never has to guess it from the
    // geometry the way a BVH reader must.
    out << "    upAxis = \"" << (options.yUp ? "Y" : "Z") << "\"\n";
    out << ")\n\n";

    // A stage that binds ANYTHING through UsdSkel -- a skeleton or only blend
    // shapes -- MUST be rooted at a SkelRoot. usdchecker rejects SkelBindingAPI
    // on a prim not rooted at one, "as required by the UsdSkel schema", even
    // with no skeleton present. Verified on a hand-written three-vertex stage
    // before this was generated -- and Blender imports the invalid Xform
    // version happily, so it is not the tool that tells you.
    const bool needsSkelRoot = skin != nullptr || morphed != entries.size();
    out << "def " << (needsSkelRoot ? "SkelRoot" : "Xform") << " \"" << options.primName
        << "\"\n{\n";

    // ---- skeleton --------------------------------------------------------
    std::vector<std::string> jointPaths;
    if (skin != nullptr && skin->jointCount() > 0) {
        // UsdSkel joint tokens are PATHS, so each joint's token is its parent's
        // token plus its own name -- that is how the hierarchy is expressed;
        // there is no separate parent array.
        jointPaths.reserve(skin->jointCount());
        for (size_t j = 0; j < skin->jointCount(); ++j) {
            const std::string leaf = usdIdentifier(
                j < skin->jointNames.size() ? skin->jointNames[j] : ("joint" + std::to_string(j)));
            const int32_t parent = j < skin->jointParents.size() ? skin->jointParents[j] : -1;
            jointPaths.push_back(parent >= 0 && static_cast<size_t>(parent) < jointPaths.size()
                                     ? jointPaths[static_cast<size_t>(parent)] + "/" + leaf
                                     : leaf);
        }

        out << "    def Skeleton \"Skel\"\n    {\n";
        out << "        uniform token[] joints = [";
        for (size_t j = 0; j < jointPaths.size(); ++j) {
            out << (j != 0 ? ", " : "") << "\"" << jointPaths[j] << "\"";
        }
        out << "]\n";

        // Scaled like the points. `points` are multiplied by `s` while these
        // used to emit globalRest verbatim, so at the default unit -- metre,
        // s = 0.1 -- the mesh came out in metres and the rig in decimetres: a
        // skeleton TEN TIMES the size of the body. usdchecker passes either
        // way; it validates the stage's structure, not whether the rig fits.
        const auto placed = [s, &placedY](foundation::Mat4 m) {
            m.m[0][3] *= s;
            m.m[1][3] = placedY(m.m[1][3]);
            m.m[2][3] *= s;
            return m;
        };

        out << "        uniform matrix4d[] bindTransforms = [";
        for (size_t j = 0; j < skin->globalRest.size(); ++j) {
            out << (j != 0 ? ", " : "") << matrix4d(placed(skin->globalRest[j]));
        }
        out << "]\n";

        // restTransforms are LOCAL, unlike bindTransforms which are world.
        // Emitting the world matrices for both leaves every joint's rest pose
        // compounded by its ancestors -- a stage that validates and poses
        // wrongly, which is the whole class of bug this milestone keeps hitting.
        //
        // They also carry the POSE when there is one: bindTransforms above stay
        // the bind pose, so UsdSkel deforms rest geometry by
        // `rest * inverse(bind)` and arrives at what we would have baked. With
        // no pose the two arrays coincide and this is the plain rest rig.
        const auto nodeGlobal = [&skin](size_t j) {
            return skin->globalPose.empty() ? skin->globalRest[j] : skin->globalPose[j];
        };
        out << "        uniform matrix4d[] restTransforms = [";
        for (size_t j = 0; j < skin->globalRest.size(); ++j) {
            const int32_t parent = j < skin->jointParents.size() ? skin->jointParents[j] : -1;
            // Both sides placed before the local is taken: scaling the local
            // afterwards would be the same number here, but only because the
            // rotation is rigid -- doing it on the globals is what the identity
            // actually says.
            const foundation::Mat4 local =
                (parent >= 0 && static_cast<size_t>(parent) < skin->globalRest.size())
                    ? foundation::rigidInverse(placed(nodeGlobal(static_cast<size_t>(parent)))) *
                          placed(nodeGlobal(j))
                    : placed(nodeGlobal(j));
            out << (j != 0 ? ", " : "") << matrix4d(local);
        }
        out << "]\n";
        out << "    }\n\n";
    }

    // ---- materials -------------------------------------------------------
    // UsdPreviewSurface under one Looks scope, bound per mesh. A scene with no
    // materials writes no scope at all, so its output is unchanged from before
    // materials existed -- a test pins that.
    std::vector<const foundation::MaterialDesc*> materials;
    for (const UsdSceneEntry& entry : entries) {
        if (entry.material == nullptr) continue;
        const auto seen = std::find_if(
            materials.begin(), materials.end(),
            [&](const foundation::MaterialDesc* m) { return m->name == entry.material->name; });
        if (seen == materials.end()) materials.push_back(entry.material);
    }

    if (!materials.empty()) {
        out << "    def Scope \"Looks\"\n    {\n";
        for (const foundation::MaterialDesc* material : materials) {
            const std::string base = "/" + options.primName + "/Looks/" + material->name;
            out << "        def Material \"" << material->name << "\"\n        {\n";
            out << "            token outputs:surface.connect = <" << base
                << "/Shader.outputs:surface>\n";
            out << "            def Shader \"Shader\"\n            {\n";
            out << "                uniform token info:id = \"UsdPreviewSurface\"\n";

            if (!material->diffuseTexture.empty()) {
                // Referenced by asset path, so the file must be beside the
                // stage. Copied for the same reason the OBJ writer copies a
                // map_Kd: naming a texture that is not there is a broken file.
                const std::filesystem::path dst =
                    path.parent_path() / material->diffuseTexture.filename();
                std::error_code ec;
                if (!std::filesystem::equivalent(material->diffuseTexture, dst, ec)) {
                    std::filesystem::copy_file(material->diffuseTexture, dst,
                                               std::filesystem::copy_options::overwrite_existing,
                                               ec);
                    if (ec) {
                        return std::unexpected(UsdWriteError{
                            UsdWriteErrorKind::CannotOpen, material->diffuseTexture.string(),
                            "texture named by the material: " + ec.message()});
                    }
                }
                out << "                color3f inputs:diffuseColor.connect = <" << base
                    << "/DiffuseTexture.outputs:rgb>\n";
            } else {
                out << "                color3f inputs:diffuseColor = (" << num(material->diffuse.x)
                    << ", " << num(material->diffuse.y) << ", " << num(material->diffuse.z)
                    << ")\n";
            }
            // The one shared Blinn-Phong to metallic-roughness conversion; see
            // foundation::metallicRoughnessOf for why metallic is always 0.
            const auto mr = foundation::metallicRoughnessOf(*material);
            out << "                float inputs:roughness = " << num(mr.roughness) << "\n";
            out << "                float inputs:metallic = " << num(mr.metallic) << "\n";
            out << "                float inputs:opacity = " << num(material->opacity) << "\n";
            out << "                token outputs:surface\n";
            out << "            }\n";

            if (!material->diffuseTexture.empty()) {
                out << "            def Shader \"DiffuseTexture\"\n            {\n";
                out << "                uniform token info:id = \"UsdUVTexture\"\n";
                out << "                asset inputs:file = @"
                    << material->diffuseTexture.filename().string() << "@\n";
                out << "                float2 inputs:st.connect = <" << base
                    << "/Reader.outputs:result>\n";
                out << "                color3f outputs:rgb\n";
                out << "            }\n";
                out << "            def Shader \"Reader\"\n            {\n";
                out << "                uniform token info:id = \"UsdPrimvarReader_float2\"\n";
                // `string`, not `token`: usdchecker rejects a token here with
                // ShaderSdrCompliance.MismatchedPropertyType.
                out << "                string inputs:varname = \"st\"\n";
                out << "                float2 outputs:result\n";
                out << "            }\n";
            }
            out << "        }\n";
        }
        out << "    }\n";
    }

    size_t vertices  = 0;
    size_t triangles = 0;
    for (const UsdSceneEntry& entry : entries) {
        const foundation::RenderView& mesh = entry.mesh;
        const bool withNormals = options.writeNormals && mesh.vnorm.size() == mesh.vertexCount();
        const bool withUVs     = options.writeUVs && mesh.texco.size() == mesh.vertexCount();
        const size_t nTris     = mesh.indexCount() / 3;

        // `extent` is per prim, not per stage: USD expects each Mesh to declare
        // its own bounds.
        Vec3 lo{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity()};
        Vec3 hi{-lo.x, -lo.y, -lo.z};
        for (const Vec3& v : mesh.coord) {
            lo.x = std::min(lo.x, v.x * s);
            lo.y = std::min(lo.y, placedY(v.y));
            lo.z = std::min(lo.z, v.z * s);
            hi.x = std::max(hi.x, v.x * s);
            hi.y = std::max(hi.y, placedY(v.y));
            hi.z = std::max(hi.z, v.z * s);
        }

        // A prim that binds a material must APPLY the API schema, or usdchecker
        // reports MissingMaterialBindingAPI -- the binding alone looks right
        // and is not conformant. It is prim METADATA, so it belongs in the
        // parentheses before the body, not among the properties: putting it
        // inside the braces makes the stage fail to open at all.
        // Only the FIRST entry is skinned -- the body. Clothing follows the
        // body through its own fit, not through the skeleton.
        const bool skinned = skin != nullptr && !jointPaths.empty() && &entry == entries.data();
        const bool morphs  = !entry.morphTargets.empty();
        // One API schema entry covers both: SkelBindingAPI carries the skeleton
        // binding AND the blend shapes, so listing it twice would be invalid.
        const bool skelBound = skinned || morphs;

        out << "    def Mesh \"" << entry.name << "\"\n";
        if (entry.material != nullptr || skelBound) {
            out << "    (\n        prepend apiSchemas = [";
            if (entry.material != nullptr) out << "\"MaterialBindingAPI\"";
            if (entry.material != nullptr && skelBound) out << ", ";
            if (skelBound) out << "\"SkelBindingAPI\"";
            out << "]\n    )\n";
        }
        out << "    {\n";
        if (morphs) {
            // The names a DCC shows, and the prims holding the offsets. Both
            // are required and must agree: a name with no prim, or a prim not
            // listed, makes the stage inconsistent.
            out << "        uniform token[] skel:blendShapes = [";
            for (size_t t = 0; t < entry.morphTargets.size(); ++t) {
                out << (t != 0 ? ", " : "") << '"' << usdIdentifier(entry.morphTargets[t].name)
                    << '"';
            }
            out << "]\n";
            out << "        rel skel:blendShapeTargets = [";
            for (size_t t = 0; t < entry.morphTargets.size(); ++t) {
                out << (t != 0 ? ", " : "") << "</" << options.primName << "/" << entry.name << "/"
                    << usdIdentifier(entry.morphTargets[t].name) << ">";
            }
            out << "]\n";
        }
        if (skinned) {
            out << "        rel skel:skeleton = </" << options.primName << "/Skel>\n";
            const size_t infl = skin->influences != 0 ? skin->influences : 1;
            out << "        int[] primvars:skel:jointIndices = [";
            for (size_t i = 0; i < skin->joints.size(); ++i) {
                out << (i != 0 ? ", " : "") << skin->joints[i];
            }
            out << "] (\n            elementSize = " << infl
                << "\n            interpolation = \"vertex\"\n        )\n";
            out << "        float[] primvars:skel:jointWeights = [";
            for (size_t i = 0; i < skin->weights.size(); ++i) {
                out << (i != 0 ? ", " : "") << num(skin->weights[i]);
            }
            out << "] (\n            elementSize = " << infl
                << "\n            interpolation = \"vertex\"\n        )\n";
        }
        if (entry.material != nullptr) {
            out << "        rel material:binding = </" << options.primName << "/Looks/"
                << entry.material->name << ">\n";
        }
        out << "        float3[] extent = [(" << num(lo.x) << ", " << num(lo.y) << ", " << num(lo.z)
            << "), (" << num(hi.x) << ", " << num(hi.y) << ", " << num(hi.z) << ")]\n";

        // Every face is a triangle: RenderView is already fan-triangulated.
        out << "        int[] faceVertexCounts = [";
        for (size_t f = 0; f < nTris; ++f)
            out << (f != 0 ? ", 3" : "3");
        out << "]\n";

        out << "        int[] faceVertexIndices = [";
        for (size_t i = 0; i < mesh.indexCount(); ++i) {
            if (i != 0) out << ", ";
            out << mesh.index[i];
        }
        out << "]\n";

        out << "        point3f[] points = [";
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            const Vec3& v = mesh.coord[i];
            if (i != 0) out << ", ";
            out << "(" << num(v.x * s) << ", " << num(placedY(v.y)) << ", " << num(v.z * s) << ")";
        }
        out << "]\n";

        if (withNormals) {
            out << "        normal3f[] normals = [";
            for (size_t i = 0; i < mesh.vertexCount(); ++i) {
                const Vec3& n = mesh.vnorm[i];
                if (i != 0) out << ", ";
                out << "(" << num(n.x) << ", " << num(n.y) << ", " << num(n.z) << ")";
            }
            out << "] (\n            interpolation = \"vertex\"\n        )\n";
        }

        if (withUVs) {
            out << "        texCoord2f[] primvars:st = [";
            for (size_t i = 0; i < mesh.vertexCount(); ++i) {
                const Vec2& t = mesh.texco[i];
                if (i != 0) out << ", ";
                out << "(" << num(t.x) << ", " << num(t.y) << ")";
            }
            out << "] (\n            interpolation = \"vertex\"\n        )\n";
        }

        // Without this a consumer may treat the mesh as a subdivision cage and
        // render a smoothed, shrunken body.
        out << "        uniform token subdivisionScheme = \"none\"\n";

        // ---- blend shapes, as CHILD prims of the mesh --------------------
        // Sparse via `pointIndices`: a dense `offsets` array would be valid and
        // would animate identically, and would make a 34-target body 34 copies
        // of the vertex buffer. The offsets take the unit scale but NOT the
        // ground offset -- that is already in the points they are added to.
        for (const auto& target : entry.morphTargets) {
            out << "\n        def BlendShape \"" << usdIdentifier(target.name) << "\"\n";
            out << "        {\n";
            out << "            uniform vector3f[] offsets = [";
            bool first = true;
            for (size_t v = 0; v < target.deltas.size(); ++v) {
                const Vec3& d = target.deltas[v];
                if (d.x == 0.0F && d.y == 0.0F && d.z == 0.0F) continue;
                out << (first ? "" : ", ") << "(" << num(d.x * s) << ", " << num(d.y * s) << ", "
                    << num(d.z * s) << ")";
                first = false;
            }
            out << "]\n";
            out << "            uniform int[] pointIndices = [";
            first = true;
            for (size_t v = 0; v < target.deltas.size(); ++v) {
                const Vec3& d = target.deltas[v];
                if (d.x == 0.0F && d.y == 0.0F && d.z == 0.0F) continue;
                out << (first ? "" : ", ") << v;
                first = false;
            }
            out << "]\n";
            out << "        }\n";
        }

        out << "    }\n";

        vertices += mesh.vertexCount();
        triangles += nTris;
    }
    out << "}\n";

    out.close();
    if (!out) {
        return std::unexpected(
            UsdWriteError{UsdWriteErrorKind::CannotOpen, path.string(), "write failed"});
    }

    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    return UsdWriteResult{vertices, triangles, ec ? 0U : static_cast<size_t>(sz)};
}

std::expected<UsdWriteResult, UsdWriteError> writeUsda(const std::filesystem::path& path,
                                                       const foundation::RenderView& mesh,
                                                       const UsdWriteOptions& options) {
    const UsdSceneEntry entry{mesh, "mesh"};
    return writeUsdaScene(path, {&entry, 1}, options);
}

namespace {

/// CRC-32 (IEEE), which every zip entry carries. Table built once.
uint32_t crc32Of(std::span<const char> bytes) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFU;
    for (const char b : bytes) {
        c = table[(c ^ static_cast<unsigned char>(b)) & 0xFFU] ^ (c >> 8U);
    }
    return c ^ 0xFFFFFFFFU;
}

void put16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFFU));
    out.push_back(static_cast<char>((v >> 8U) & 0xFFU));
}

void put32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFU));
}

/// USDZ alignment: an entry's DATA must start on a 64-byte boundary, and the
/// padding goes in the extra field under header id 0x1986 -- verified against
/// `usdzip`, which emits id 0x1986 / size 22 / zero payload so that
/// 30 + 8 + 26 = 64.
///
/// A TLV extra field cannot be shorter than its own 4-byte header, so when the
/// gap is 1..3 bytes another whole 64 is taken rather than emitting a malformed
/// field.
constexpr size_t kUsdzAlign   = 64;
constexpr uint16_t kPaddingId = 0x1986;

std::string alignmentExtra(size_t headerEnd) {
    size_t pad = (kUsdzAlign - (headerEnd % kUsdzAlign)) % kUsdzAlign;
    if (pad != 0 && pad < 4) pad += kUsdzAlign;
    std::string extra;
    if (pad == 0) return extra;
    put16(extra, kPaddingId);
    put16(extra, static_cast<uint16_t>(pad - 4));
    extra.append(pad - 4, '\0');
    return extra;
}

}  // namespace

std::expected<UsdWriteResult, UsdWriteError> writeUsdzScene(const std::filesystem::path& path,
                                                            std::span<const UsdSceneEntry> entries,
                                                            const UsdWriteOptions& options,
                                                            const foundation::SkinView* skin) {
    // Build the stage in a scratch directory. writeUsdaScene copies each
    // texture beside the stage and references it by filename, so whatever ends
    // up in this directory IS the archive's contents -- no second code path
    // deciding what a self-contained stage needs.
    std::error_code ec;
    const auto scratch = std::filesystem::temp_directory_path() /
                         ("mh_usdz_" + std::to_string(std::hash<std::string>{}(path.string())));
    std::filesystem::remove_all(scratch, ec);
    if (!std::filesystem::create_directories(scratch, ec)) {
        return std::unexpected(UsdWriteError{UsdWriteErrorKind::CannotOpen, scratch.string(),
                                             "cannot stage the usdz"});
    }

    struct Cleanup {
        std::filesystem::path dir;

        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(dir, e);
        }
    } cleanup{scratch};

    const std::string stem = path.stem().string();
    const auto stagePath   = scratch / (stem + ".usda");
    auto wrote             = writeUsdaScene(stagePath, entries, options, skin);
    if (!wrote) return std::unexpected(wrote.error());

    // The stage FIRST -- that is how a reader finds it -- then everything else
    // in a stable order so the archive is reproducible.
    std::vector<std::filesystem::path> files{stagePath};
    std::vector<std::filesystem::path> rest;
    for (const auto& e : std::filesystem::directory_iterator(scratch, ec)) {
        if (e.is_regular_file() && e.path() != stagePath) rest.push_back(e.path());
    }
    std::ranges::sort(rest);
    files.insert(files.end(), rest.begin(), rest.end());

    std::string archive;

    struct Central {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };

    std::vector<Central> central;
    central.reserve(files.size());

    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return std::unexpected(
                UsdWriteError{UsdWriteErrorKind::CannotOpen, file.string(), "cannot read to pack"});
        }
        const std::string data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        const std::string name  = file.filename().string();
        const uint32_t crc      = crc32Of(data);
        const auto offset       = static_cast<uint32_t>(archive.size());
        const std::string extra = alignmentExtra(archive.size() + 30 + name.size());

        put32(archive, 0x04034B50U);  // local file header
        put16(archive, 20);           // version needed
        put16(archive, 0);            // flags
        put16(archive, 0);            // STORED, never deflated
        put16(archive, 0);            // mod time
        put16(archive, 0x21);         // mod date (1980-01-01)
        put32(archive, crc);
        put32(archive, static_cast<uint32_t>(data.size()));  // compressed == uncompressed
        put32(archive, static_cast<uint32_t>(data.size()));
        put16(archive, static_cast<uint16_t>(name.size()));
        put16(archive, static_cast<uint16_t>(extra.size()));
        archive += name;
        archive += extra;
        archive += data;

        central.push_back({name, crc, static_cast<uint32_t>(data.size()), offset});
    }

    const auto centralStart = static_cast<uint32_t>(archive.size());
    for (const Central& c : central) {
        put32(archive, 0x02014B50U);  // central directory header
        put16(archive, 20);           // version made by
        put16(archive, 20);           // version needed
        put16(archive, 0);
        put16(archive, 0);  // STORED
        put16(archive, 0);
        put16(archive, 0x21);
        put32(archive, c.crc);
        put32(archive, c.size);
        put32(archive, c.size);
        put16(archive, static_cast<uint16_t>(c.name.size()));
        put16(archive, 0);  // no extra in the central record
        put16(archive, 0);  // comment
        put16(archive, 0);  // disk
        put16(archive, 0);  // internal attrs
        put32(archive, 0);  // external attrs
        put32(archive, c.offset);
        archive += c.name;
    }
    const auto centralSize = static_cast<uint32_t>(archive.size()) - centralStart;

    put32(archive, 0x06054B50U);  // end of central directory
    put16(archive, 0);
    put16(archive, 0);
    put16(archive, static_cast<uint16_t>(central.size()));
    put16(archive, static_cast<uint16_t>(central.size()));
    put32(archive, centralSize);
    put32(archive, centralStart);
    put16(archive, 0);  // comment length

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return std::unexpected(
            UsdWriteError{UsdWriteErrorKind::CannotOpen, path.string(), "cannot write the usdz"});
    }
    out.write(archive.data(), static_cast<std::streamsize>(archive.size()));
    out.flush();
    if (!out) {
        return std::unexpected(
            UsdWriteError{UsdWriteErrorKind::CannotOpen, path.string(), "write failed"});
    }
    return *wrote;
}

}  // namespace mh::io
