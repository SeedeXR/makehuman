// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/UsdWriter.h"

#include "makehuman/foundation/Chars.h"

#include <algorithm>
#include <cmath>
#include <fstream>
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
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<UsdWriteResult, UsdWriteError> writeUsdaScene(const std::filesystem::path& path,
                                                            std::span<const UsdSceneEntry> entries,
                                                            const UsdWriteOptions& options) {
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

    std::ofstream out(path);
    if (!out) {
        return std::unexpected(UsdWriteError{UsdWriteErrorKind::CannotOpen, path.string(), {}});
    }

    const float s = unitScale(options.unit) * options.scale;

    out << "#usda 1.0\n(\n";
    out << "    defaultPrim = \"" << options.primName << "\"\n";
    out << "    doc = \"MakeHuman C++ USD writer\"\n";
    out << "    metersPerUnit = " << num(options.metersPerUnit()) << "\n";
    // USD stores the up axis, so a consumer never has to guess it from the
    // geometry the way a BVH reader must.
    out << "    upAxis = \"" << (options.yUp ? "Y" : "Z") << "\"\n";
    out << ")\n\n";

    out << "def Xform \"" << options.primName << "\"\n{\n";

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
            // The same Blinn-Phong to PBR conversion every other writer uses.
            out << "                float inputs:roughness = "
                << num(std::clamp(1.0F - material->shininess, 0.0F, 1.0F)) << "\n";
            out << "                float inputs:metallic = 0\n";
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
            lo.y = std::min(lo.y, v.y * s);
            lo.z = std::min(lo.z, v.z * s);
            hi.x = std::max(hi.x, v.x * s);
            hi.y = std::max(hi.y, v.y * s);
            hi.z = std::max(hi.z, v.z * s);
        }

        // A prim that binds a material must APPLY the API schema, or usdchecker
        // reports MissingMaterialBindingAPI -- the binding alone looks right
        // and is not conformant. It is prim METADATA, so it belongs in the
        // parentheses before the body, not among the properties: putting it
        // inside the braces makes the stage fail to open at all.
        out << "    def Mesh \"" << entry.name << "\"\n";
        if (entry.material != nullptr) {
            out << "    (\n        prepend apiSchemas = [\"MaterialBindingAPI\"]\n    )\n";
        }
        out << "    {\n";
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
            out << "(" << num(v.x * s) << ", " << num(v.y * s) << ", " << num(v.z * s) << ")";
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

}  // namespace mh::io
