// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/SceneIO.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>

namespace mh::io {

using foundation::Vec2;
using foundation::Vec3;
using foundation::Vec4;

namespace {

/// assimp takes ownership of everything hung off an aiScene and frees it with
/// delete[], so every array here must be allocated the same way.
template <typename T>
T* allocArray(size_t n) {
    return new T[n]{};
}

}  // namespace

std::string_view formatId(SceneFormat f) noexcept {
    switch (f) {
        case SceneFormat::FbxBinary: return "fbx";
        case SceneFormat::FbxAscii: return "fbxa";
        case SceneFormat::Collada: return "collada";
        case SceneFormat::StlBinary: return "stlb";
        case SceneFormat::StlAscii: return "stl";
        case SceneFormat::ThreeMf: return "3mf";
    }
    return "fbx";
}

std::string_view formatExtension(SceneFormat f) noexcept {
    switch (f) {
        case SceneFormat::FbxBinary:
        case SceneFormat::FbxAscii: return ".fbx";
        case SceneFormat::Collada: return ".dae";
        case SceneFormat::StlBinary:
        case SceneFormat::StlAscii: return ".stl";
        case SceneFormat::ThreeMf: return ".3mf";
    }
    return ".fbx";
}

std::string SceneIoError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case SceneIoErrorKind::EmptyMesh: k = "mesh has no geometry"; break;
        case SceneIoErrorKind::UnsupportedFormat: k = "unsupported format"; break;
        case SceneIoErrorKind::ExportFailed: k = "export failed"; break;
        case SceneIoErrorKind::ImportFailed: k = "import failed"; break;
        case SceneIoErrorKind::NotFound: k = "file not found"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<SceneExportResult, SceneIoError> exportScene(
    const std::filesystem::path& path, const foundation::RenderView& mesh, SceneFormat format,
    const SceneExportOptions& options, const foundation::MaterialDesc* material) {
    // The caller supplies render-ready geometry: the unweld is a port of
    // module3d.py and lives in the AGPL core, so running it here would drag
    // this Apache-2.0 module back across the licence boundary.
    const foundation::RenderView& rm = mesh;
    if (rm.vertexCount() == 0 || rm.indexCount() == 0) {
        return std::unexpected(SceneIoError{SceneIoErrorKind::EmptyMesh, path.string(), {}});
    }

    const float scale = unitScale(options.unit) * options.scale;

    float groundOffset = 0.0F;
    if (options.feetOnGround) {
        float lowest = std::numeric_limits<float>::infinity();
        for (const Vec3& v : rm.coord)
            lowest = std::min(lowest, v.y * scale);
        if (std::isfinite(lowest)) groundOffset = -lowest;
    }

    const bool withNormals = options.writeNormals && rm.vnorm.size() == rm.vertexCount();
    const bool withUVs     = options.writeUVs && rm.texco.size() == rm.vertexCount();

    // aiScene owns everything below; it is released by the unique_ptr on any
    // early return and handed to the exporter otherwise.
    auto scene = std::make_unique<aiScene>();

    scene->mRootNode = new aiNode();
    scene->mRootNode->mName.Set(options.meshName);

    scene->mNumMaterials = 1;
    scene->mMaterials    = allocArray<aiMaterial*>(1);
    scene->mMaterials[0] = new aiMaterial();
    {
        aiMaterial* m = scene->mMaterials[0];
        const aiString name(material != nullptr && !material->name.empty() ? material->name
                                                                           : options.materialName);
        m->AddProperty(&name, AI_MATKEY_NAME);
        if (material != nullptr) {
            const aiColor3D diffuse(material->diffuse.x, material->diffuse.y, material->diffuse.z);
            const aiColor3D specular(material->specular.x, material->specular.y,
                                     material->specular.z);
            const aiColor3D ambient(material->ambient.x, material->ambient.y, material->ambient.z);
            m->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
            m->AddProperty(&specular, 1, AI_MATKEY_COLOR_SPECULAR);
            m->AddProperty(&ambient, 1, AI_MATKEY_COLOR_AMBIENT);
            const float opacity = material->opacity;
            m->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
        }
    }

    scene->mNumMeshes = 1;
    scene->mMeshes    = allocArray<aiMesh*>(1);
    scene->mMeshes[0] = new aiMesh();

    aiMesh* am          = scene->mMeshes[0];
    am->mMaterialIndex  = 0;
    am->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    am->mName.Set(options.meshName);
    am->mNumVertices = static_cast<unsigned>(rm.vertexCount());
    am->mVertices    = allocArray<aiVector3D>(rm.vertexCount());

    for (size_t i = 0; i < rm.vertexCount(); ++i) {
        const Vec3& v    = rm.coord[i];
        am->mVertices[i] = aiVector3D(v.x * scale, v.y * scale + groundOffset, v.z * scale);
    }

    if (withNormals) {
        am->mNormals = allocArray<aiVector3D>(rm.vertexCount());
        for (size_t i = 0; i < rm.vertexCount(); ++i) {
            const Vec3& n   = rm.vnorm[i];
            am->mNormals[i] = aiVector3D(n.x, n.y, n.z);
        }
    }

    if (withUVs) {
        am->mNumUVComponents[0] = 2;
        am->mTextureCoords[0]   = allocArray<aiVector3D>(rm.vertexCount());
        for (size_t i = 0; i < rm.vertexCount(); ++i) {
            const Vec2& t            = rm.texco[i];
            am->mTextureCoords[0][i] = aiVector3D(t.x, t.y, 0.0F);
        }
    }

    const size_t nTris = rm.indexCount() / 3;
    am->mNumFaces      = static_cast<unsigned>(nTris);
    am->mFaces         = allocArray<aiFace>(nTris);
    for (size_t f = 0; f < nTris; ++f) {
        aiFace& face     = am->mFaces[f];
        face.mNumIndices = 3;
        face.mIndices    = allocArray<unsigned>(3);
        face.mIndices[0] = rm.index[f * 3 + 0];
        face.mIndices[1] = rm.index[f * 3 + 1];
        face.mIndices[2] = rm.index[f * 3 + 2];
    }

    scene->mRootNode->mNumMeshes = 1;
    scene->mRootNode->mMeshes    = allocArray<unsigned>(1);
    scene->mRootNode->mMeshes[0] = 0;

    Assimp::Exporter exporter;
    const aiReturn rc = exporter.Export(scene.get(), std::string(formatId(format)), path.string());
    if (rc != AI_SUCCESS) {
        return std::unexpected(
            SceneIoError{SceneIoErrorKind::ExportFailed, path.string(), exporter.GetErrorString()});
    }

    SceneExportResult result;
    result.vertices  = rm.vertexCount();
    result.triangles = nTris;
    std::error_code ec;
    // On failure file_size returns static_cast<uintmax_t>(-1); reporting
    // SIZE_MAX bytes written is worse than reporting nothing.
    const auto sz    = std::filesystem::file_size(path, ec);
    result.fileBytes = ec ? 0U : static_cast<size_t>(sz);
    return result;
}

std::expected<ImportedMesh, SceneIoError> importMesh(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(SceneIoError{SceneIoErrorKind::NotFound, path.string(), {}});
    }

    Assimp::Importer importer;
    // Importing is a trust boundary: the caller may hand us anything.
    //
    // ValidateDataStructure is REQUIRED, not optional, and its ORDER in
    // assimp's pipeline is what makes the rest safe. Verified against a file
    // with out-of-range indices (one assimp itself wrote):
    //   JoinIdenticalVertices alone            -> SIGSEGV, reproducibly
    //   ValidateDataStructure + Join           -> clean error, 5/5 runs
    //   Triangulate + Validate + Join          -> clean error, 5/5 runs
    // Validation runs first and rejects the scene before any step dereferences
    // the bad indices, so the join step is safe behind it and we keep the
    // compact welded import it gives.
    const aiScene* scene =
        importer.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_ValidateDataStructure |
                                             aiProcess_JoinIdenticalVertices);

    if (scene == nullptr || scene->mNumMeshes == 0 || scene->mMeshes == nullptr) {
        return std::unexpected(
            SceneIoError{SceneIoErrorKind::ImportFailed, path.string(), importer.GetErrorString()});
    }

    const aiMesh* am = scene->mMeshes[0];
    if (am->mNumVertices == 0 || am->mNumFaces == 0) {
        return std::unexpected(
            SceneIoError{SceneIoErrorKind::ImportFailed, path.string(), "first mesh is empty"});
    }

    ImportedMesh out;
    out.meshCount              = scene->mNumMeshes;
    out.mesh.name              = path.stem().string();
    out.mesh.vertsPerPrimitive = 3;  // triangulated on import

    // Import is a trust boundary. NaN and infinity survive assimp's validator
    // (a NaN ASCII STL imports "successfully"), and once inside a mesh they
    // poison bounding boxes, normals and every exporter downstream.
    out.mesh.coord.resize(am->mNumVertices);
    for (unsigned i = 0; i < am->mNumVertices; ++i) {
        const auto& v = am->mVertices[i];
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
            return std::unexpected(SceneIoError{SceneIoErrorKind::ImportFailed, path.string(),
                                                "file contains a non-finite vertex coordinate"});
        }
        out.mesh.coord[i] = Vec3{v.x, v.y, v.z};
    }

    const bool hasUV = am->HasTextureCoords(0);
    if (hasUV) {
        out.mesh.texco.resize(am->mNumVertices);
        for (unsigned i = 0; i < am->mNumVertices; ++i) {
            out.mesh.texco[i] = Vec2{am->mTextureCoords[0][i].x, am->mTextureCoords[0][i].y};
        }
    }

    out.mesh.fvert.reserve(static_cast<size_t>(am->mNumFaces) * 3);
    for (unsigned f = 0; f < am->mNumFaces; ++f) {
        const aiFace& face = am->mFaces[f];
        if (face.mNumIndices != 3) continue;  // Triangulate should prevent this
        for (unsigned c = 0; c < 3; ++c) {
            // MeshData is unvalidated by contract, but an index past the end is
            // a corrupt file, not a caller error -- report it here rather than
            // hand back something whose only safe consumer is Mesh::fromData.
            if (face.mIndices[c] >= am->mNumVertices) {
                return std::unexpected(SceneIoError{SceneIoErrorKind::ImportFailed, path.string(),
                                                    "face index out of range"});
            }
            out.mesh.fvert.push_back(face.mIndices[c]);
            if (hasUV) out.mesh.fuvs.push_back(face.mIndices[c]);
        }
    }
    if (out.mesh.fvert.empty()) {
        return std::unexpected(
            SceneIoError{SceneIoErrorKind::ImportFailed, path.string(), "no triangular faces"});
    }

    return out;
}

}  // namespace mh::io
