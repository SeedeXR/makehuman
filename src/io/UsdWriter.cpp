// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/UsdWriter.h"

#include "makehuman/foundation/Chars.h"

#include <cmath>
#include <fstream>
#include <limits>

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

std::expected<UsdWriteResult, UsdWriteError> writeUsda(const std::filesystem::path& path,
                                                       const foundation::RenderView& mesh,
                                                       const UsdWriteOptions& options) {
    if (mesh.vertexCount() == 0 || mesh.indexCount() == 0) {
        return std::unexpected(UsdWriteError{UsdWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    for (const Vec3& v : mesh.coord) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
            return std::unexpected(
                UsdWriteError{UsdWriteErrorKind::NonFiniteValue, path.string(), "vertex position"});
        }
    }

    std::ofstream out(path);
    if (!out) {
        return std::unexpected(UsdWriteError{UsdWriteErrorKind::CannotOpen, path.string(), {}});
    }

    const float s          = unitScale(options.unit) * options.scale;
    const bool withNormals = options.writeNormals && mesh.vnorm.size() == mesh.vertexCount();
    const bool withUVs     = options.writeUVs && mesh.texco.size() == mesh.vertexCount();
    const size_t nTris     = mesh.indexCount() / 3;

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

    out << "#usda 1.0\n(\n";
    out << "    defaultPrim = \"" << options.primName << "\"\n";
    out << "    doc = \"MakeHuman C++ USD writer\"\n";
    out << "    metersPerUnit = " << num(options.metersPerUnit()) << "\n";
    // USD stores the up axis, so a consumer never has to guess it from the
    // geometry the way a BVH reader must.
    out << "    upAxis = \"" << (options.yUp ? "Y" : "Z") << "\"\n";
    out << ")\n\n";

    out << "def Xform \"" << options.primName << "\"\n{\n";
    out << "    def Mesh \"mesh\"\n    {\n";

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

    if (withNormals) {
        out << "        normal3f[] normals = [";
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            const Vec3& n = mesh.vnorm[i];
            if (i != 0) out << ", ";
            out << "(" << num(n.x) << ", " << num(n.y) << ", " << num(n.z) << ")";
        }
        // "vertex", not "faceVarying": the mesh is already unwelded, so there
        // is exactly one normal per point and no per-corner variation left.
        out << "] (\n            interpolation = \"vertex\"\n        )\n";
    }

    out << "        point3f[] points = [";
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        const Vec3& v = mesh.coord[i];
        if (i != 0) out << ", ";
        out << "(" << num(v.x * s) << ", " << num(v.y * s) << ", " << num(v.z * s) << ")";
    }
    out << "]\n";

    if (withUVs) {
        out << "        texCoord2f[] primvars:st = [";
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            const Vec2& t = mesh.texco[i];
            if (i != 0) out << ", ";
            // USD's UV origin is bottom-left, the same as OBJ and MakeHuman, so
            // V is NOT flipped here -- unlike glTF, whose origin is top-left.
            out << "(" << num(t.x) << ", " << num(t.y) << ")";
        }
        out << "] (\n            interpolation = \"vertex\"\n        )\n";
    }

    // Without this a consumer may treat the mesh as a subdivision cage and
    // render a smoothed, shrunken body.
    out << "        uniform token subdivisionScheme = \"none\"\n";
    out << "    }\n}\n";

    out.close();
    if (!out) {
        return std::unexpected(
            UsdWriteError{UsdWriteErrorKind::CannotOpen, path.string(), "write failed"});
    }

    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    return UsdWriteResult{mesh.vertexCount(), nTris, ec ? 0U : static_cast<size_t>(sz)};
}

}  // namespace mh::io
