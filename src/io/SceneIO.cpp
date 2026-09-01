// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/SceneIO.h"

#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

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
        case SceneIoErrorKind::InvalidSkinOrMorph:
            k = "skin or morph target does not describe this mesh";
            break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

namespace {

/// Fills one aiMesh from an entry. assimp owns everything hung off the scene.
void fillMesh(aiMesh* am, const foundation::RenderView& rm, const std::string& name,
              unsigned materialIndex, const SceneExportOptions& options, float scale,
              float groundOffset) {
    const bool withNormals = options.writeNormals && rm.vnorm.size() == rm.vertexCount();
    const bool withUVs     = options.writeUVs && rm.texco.size() == rm.vertexCount();

    am->mMaterialIndex  = materialIndex;
    am->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    am->mName.Set(name);
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
}

/// Blinn-Phong properties, exactly as the single-mesh path writes them.
void fillMaterial(aiMaterial* m, const foundation::MaterialDesc* material,
                  const std::string& fallbackName) {
    const aiString name(material != nullptr && !material->name.empty() ? material->name
                                                                       : fallbackName);
    m->AddProperty(&name, AI_MATKEY_NAME);
    if (material == nullptr) return;
    const aiColor3D diffuse(material->diffuse.x, material->diffuse.y, material->diffuse.z);
    const aiColor3D specular(material->specular.x, material->specular.y, material->specular.z);
    const aiColor3D ambient(material->ambient.x, material->ambient.y, material->ambient.z);
    m->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
    m->AddProperty(&specular, 1, AI_MATKEY_COLOR_SPECULAR);
    m->AddProperty(&ambient, 1, AI_MATKEY_COLOR_AMBIENT);
    const float opacity = material->opacity;
    m->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
    // Written as an EXPONENT, which is what the key means. Omitting it entirely
    // is what we did before, and it does not read as missing: assimp's Collada
    // exporter then substitutes a fixed 10, and the FBX comes back carrying our
    // own 0.2 default -- so a 0.96 skin round-tripped as 0.2 with nothing to
    // suggest the number had been invented.
    const float shininess = foundation::specularExponentOf(material->shininess);
    m->AddProperty(&shininess, 1, AI_MATKEY_SHININESS);

    // Stated as zero, not left out. `.mhmat` has no metalness concept -- which
    // is why the glTF writer hard-codes `"metallicFactor":0` -- but unset does
    // not mean zero in an FBX: assimp's exporter fills its material template
    // with `ReflectionFactor` 1, and Blender reads that key straight into
    // Principled `metallic`. The Blender measurement is in the test.
    const float reflectivity = 0.0F;
    m->AddProperty(&reflectivity, 1, AI_MATKEY_REFLECTIVITY);

    // Texture paths. Without these a character exported to FBX or DAE arrives
    // in a DCC tool with its colours but NO skin -- the reference to the albedo
    // is simply absent from the file, which reads as "the exporter is broken"
    // rather than "the texture moved".
    //
    // Written as the material states them, so a relative path stays relative
    // and keeps working beside the exported file.
    if (!material->diffuseTexture.empty()) {
        const aiString tex(material->diffuseTexture.string());
        m->AddProperty(&tex, AI_MATKEY_TEXTURE_DIFFUSE(0));
    }
    if (!material->normalTexture.empty()) {
        const aiString tex(material->normalTexture.string());
        m->AddProperty(&tex, AI_MATKEY_TEXTURE_NORMALS(0));
    }
}

}  // namespace

/// A rest matrix placed in the exported scene: unit-scaled, and lifted by the
/// scene's ground offset.
///
/// Written once because it appeared three times -- the bone's inverse bind, the
/// joint node, and that node's parent -- and a ground offset applied to two of
/// the three would drift the rig off the body in a way that still looks like a
/// rig.
foundation::Mat4 placedRest(foundation::Mat4 g, float scale, float groundOffset) {
    g.m[0][3] *= scale;
    g.m[1][3] = g.m[1][3] * scale + groundOffset;
    g.m[2][3] *= scale;
    return g;
}

/// Hangs @p skin's bones on @p am.
///
/// assimp stores weights per BONE (a list of affected vertices) while we hold
/// them per VERTEX. Verified empirically that assimp's FBX writer carries
/// bones and that Blender reads them back -- a writer that silently dropped
/// them would be worse than not offering it.
void attachSkin(aiMesh* am, const foundation::SkinView& skin, float scale, float groundOffset) {
    const size_t infl = skin.influences;

    // Invert per-vertex -> per-bone, counting first so each list is
    // allocated exactly once.
    std::vector<unsigned> perBoneCount(skin.jointCount(), 0);
    for (size_t v = 0; v < skin.vertexCount(); ++v) {
        for (size_t i = 0; i < infl; ++i) {
            if (skin.weights[v * infl + i] > 0.0F) {
                ++perBoneCount[skin.joints[v * infl + i]];
            }
        }
    }

    am->mNumBones = static_cast<unsigned>(skin.jointCount());
    am->mBones    = allocArray<aiBone*>(skin.jointCount());

    std::vector<unsigned> filled(skin.jointCount(), 0);
    for (size_t b = 0; b < skin.jointCount(); ++b) {
        auto* bone = new aiBone();
        bone->mName.Set(skin.jointNames[b]);
        bone->mNumWeights = perBoneCount[b];
        bone->mWeights =
            perBoneCount[b] != 0 ? allocArray<aiVertexWeight>(perBoneCount[b]) : nullptr;

        // The offset matrix is the inverse bind: it takes a vertex from
        // model space into the bone's space. Scaled with the mesh, then
        // transposed -- aiMatrix4x4 is row-major with translation in the
        // last COLUMN, same as ours, but assimp indexes it a[row][col] via
        // named members, so this is written out explicitly rather than
        // memcpy'd.
        const foundation::Mat4 inv =
            foundation::rigidInverse(placedRest(skin.globalRest[b], scale, groundOffset));

        bone->mOffsetMatrix = aiMatrix4x4(inv.m[0][0], inv.m[0][1], inv.m[0][2], inv.m[0][3],
                                          inv.m[1][0], inv.m[1][1], inv.m[1][2], inv.m[1][3],
                                          inv.m[2][0], inv.m[2][1], inv.m[2][2], inv.m[2][3],
                                          inv.m[3][0], inv.m[3][1], inv.m[3][2], inv.m[3][3]);
        am->mBones[b]       = bone;
    }

    for (size_t v = 0; v < skin.vertexCount(); ++v) {
        for (size_t i = 0; i < infl; ++i) {
            const float w = skin.weights[v * infl + i];
            if (w <= 0.0F) continue;
            const uint32_t b   = skin.joints[v * infl + i];
            aiVertexWeight& vw = am->mBones[b]->mWeights[filled[b]];
            vw.mVertexId       = static_cast<unsigned>(v);
            vw.mWeight         = w;
            ++filled[b];
        }
    }
}

/// Adds @p skin's joint hierarchy under @p root, KEEPING the children it
/// already has.
///
/// Every aiBone needs a node of the same name in the scene graph or the FBX
/// writer fails outright: "Failed to find node for bone: root". The bone array
/// alone is not a skeleton -- the hierarchy lives in the nodes, and aiBone
/// only references it by name.
///
/// The multi-mesh scene already gives the root one child per mesh, so this
/// grows the array rather than replacing it. Replacing it is what the
/// single-mesh version could get away with, having no children to lose.
void addJointNodes(aiNode* root, const foundation::SkinView& skin, float scale,
                   float groundOffset) {
    std::vector<aiNode*> jointNodes(skin.jointCount(), nullptr);
    std::vector<unsigned> childCount(skin.jointCount(), 0);
    unsigned rootJoints = 0;
    for (size_t b = 0; b < skin.jointCount(); ++b) {
        const int32_t parent = skin.jointParents[b];
        if (parent < 0) {
            ++rootJoints;
        } else {
            ++childCount[static_cast<size_t>(parent)];
        }
    }

    for (size_t b = 0; b < skin.jointCount(); ++b) {
        auto* node = new aiNode();
        node->mName.Set(skin.jointNames[b]);

        // Node transforms are LOCAL to the parent, and scaled with the mesh
        // so the rig cannot drift from the body.
        const foundation::Mat4 g = placedRest(skin.globalRest[b], scale, groundOffset);
        foundation::Mat4 local   = g;
        if (skin.jointParents[b] >= 0) {
            const foundation::Mat4 pg = placedRest(
                skin.globalRest[static_cast<size_t>(skin.jointParents[b])], scale, groundOffset);
            local = foundation::rigidInverse(pg) * g;
        }
        node->mTransformation =
            aiMatrix4x4(local.m[0][0], local.m[0][1], local.m[0][2], local.m[0][3], local.m[1][0],
                        local.m[1][1], local.m[1][2], local.m[1][3], local.m[2][0], local.m[2][1],
                        local.m[2][2], local.m[2][3], local.m[3][0], local.m[3][1], local.m[3][2],
                        local.m[3][3]);

        if (childCount[b] != 0) {
            node->mChildren = allocArray<aiNode*>(childCount[b]);
        }
        jointNodes[b] = node;
    }

    // Link children. Parents precede children (validated above), so every
    // parent node already exists.
    for (size_t b = 0; b < skin.jointCount(); ++b) {
        const int32_t parent = skin.jointParents[b];
        if (parent < 0) continue;
        aiNode* pn                        = jointNodes[static_cast<size_t>(parent)];
        pn->mChildren[pn->mNumChildren++] = jointNodes[b];
        jointNodes[b]->mParent            = pn;
    }

    // Grow, do not replace: the multi-mesh scene has already given the root one
    // child per mesh, and overwriting the array leaves mNumChildren counting
    // entries that are no longer there -- which crashes rather than dropping
    // meshes quietly.
    const unsigned kept = root->mNumChildren;
    aiNode** grown      = allocArray<aiNode*>(kept + rootJoints);
    for (unsigned c = 0; c < kept; ++c)
        grown[c] = root->mChildren[c];
    delete[] root->mChildren;
    root->mChildren = grown;

    for (size_t b = 0; b < skin.jointCount(); ++b) {
        if (skin.jointParents[b] >= 0) continue;
        jointNodes[b]->mParent                = root;
        root->mChildren[root->mNumChildren++] = jointNodes[b];
    }
}

std::expected<SceneExportResult, SceneIoError> exportScene(const std::filesystem::path& path,
                                                           std::span<const SceneEntry> entries,
                                                           SceneFormat format,
                                                           const SceneExportOptions& options) {
    if (entries.empty()) {
        return std::unexpected(SceneIoError{SceneIoErrorKind::EmptyMesh, path.string(), {}});
    }
    size_t skinned = entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        const SceneEntry& e = entries[i];
        if (e.mesh.vertexCount() == 0 || e.mesh.indexCount() == 0) {
            return std::unexpected(SceneIoError{SceneIoErrorKind::EmptyMesh, path.string(), {}});
        }
        if (e.skin == nullptr) continue;
        if (skinned != entries.size()) {
            return std::unexpected(SceneIoError{SceneIoErrorKind::InvalidSkinOrMorph, path.string(),
                                                "more than one entry carries a skin"});
        }
        // Same checks the single-mesh path makes: a skin that does not describe
        // its mesh writes a file whose weights address the wrong vertices.
        if (!e.skin->valid() || e.skin->vertexCount() != e.mesh.vertexCount() ||
            e.skin->influences == 0) {
            return std::unexpected(SceneIoError{SceneIoErrorKind::InvalidSkinOrMorph, path.string(),
                                                "skin does not describe " + e.name});
        }
        for (const uint32_t jIdx : e.skin->joints) {
            if (jIdx >= e.skin->jointCount()) {
                return std::unexpected(SceneIoError{SceneIoErrorKind::InvalidSkinOrMorph,
                                                    path.string(),
                                                    "joint index out of range in " + e.name});
            }
        }
        skinned = i;
    }

    const float scale = unitScale(options.unit) * options.scale;

    // One offset for the whole scene: levelling each mesh alone would drop the
    // clothes to the floor beside the body.
    float groundOffset = 0.0F;
    if (options.feetOnGround) {
        float lowest = std::numeric_limits<float>::infinity();
        for (const SceneEntry& e : entries) {
            for (const Vec3& v : e.mesh.coord)
                lowest = std::min(lowest, v.y * scale);
        }
        if (std::isfinite(lowest)) groundOffset = -lowest;
    }

    auto scene       = std::make_unique<aiScene>();
    scene->mRootNode = new aiNode();
    scene->mRootNode->mName.Set(options.meshName);

    // One material per entry. Not deduped: assimp is happy with duplicates and
    // a shared material is not expressible in every target format anyway.
    scene->mNumMaterials = static_cast<unsigned>(entries.size());
    scene->mMaterials    = allocArray<aiMaterial*>(entries.size());
    scene->mNumMeshes    = static_cast<unsigned>(entries.size());
    scene->mMeshes       = allocArray<aiMesh*>(entries.size());

    size_t vertices  = 0;
    size_t triangles = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        scene->mMaterials[i] = new aiMaterial();
        fillMaterial(scene->mMaterials[i], entries[i].material, options.materialName);
        scene->mMeshes[i] = new aiMesh();
        fillMesh(scene->mMeshes[i], entries[i].mesh, entries[i].name, static_cast<unsigned>(i),
                 options, scale, groundOffset);
        if (entries[i].skin != nullptr) {
            attachSkin(scene->mMeshes[i], *entries[i].skin, scale, groundOffset);
        }
        vertices += entries[i].mesh.vertexCount();
        triangles += entries[i].mesh.indexCount() / 3;
    }

    // One CHILD NODE per mesh, rather than hanging them all off the root.
    //
    // Not cosmetic: assimp's FBX exporter names a mesh after the node that owns
    // it, so meshes sharing the root all came back named "body" -- the geometry
    // and materials survived but the identity did not, and Blender merged them
    // into a single object. Collada happened to keep the names, which is what
    // made the difference visible.
    scene->mRootNode->mNumChildren = static_cast<unsigned>(entries.size());
    scene->mRootNode->mChildren    = allocArray<aiNode*>(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        auto* node = new aiNode();
        node->mName.Set(entries[i].name);
        node->mParent                  = scene->mRootNode;
        node->mNumMeshes               = 1;
        node->mMeshes                  = allocArray<unsigned>(1);
        node->mMeshes[0]               = static_cast<unsigned>(i);
        scene->mRootNode->mChildren[i] = node;
    }

    // After the mesh nodes, because addJointNodes grows the array rather than
    // replacing it: the joint roots become further children of the same root.
    if (skinned != entries.size()) {
        addJointNodes(scene->mRootNode, *entries[skinned].skin, scale, groundOffset);
    }

    Assimp::Exporter exporter;
    const aiReturn rc = exporter.Export(scene.get(), std::string(formatId(format)), path.string());
    if (rc != AI_SUCCESS) {
        return std::unexpected(
            SceneIoError{SceneIoErrorKind::ExportFailed, path.string(), exporter.GetErrorString()});
    }

    SceneExportResult result;
    result.vertices  = vertices;
    result.triangles = triangles;
    std::error_code ec;
    const auto sz    = std::filesystem::file_size(path, ec);
    result.fileBytes = ec ? 0U : static_cast<size_t>(sz);
    return result;
}

std::expected<SceneExportResult, SceneIoError> exportScene(
    const std::filesystem::path& path, const foundation::RenderView& mesh, SceneFormat format,
    const SceneExportOptions& options, const foundation::MaterialDesc* material,
    const foundation::SkinView* skin, std::span<const foundation::MorphTarget> morphTargets) {
    // The caller supplies render-ready geometry: the unweld is a port of
    // module3d.py and lives in the AGPL core, so running it here would drag
    // this Apache-2.0 module back across the licence boundary.
    const foundation::RenderView& rm = mesh;
    if (rm.vertexCount() == 0 || rm.indexCount() == 0) {
        return std::unexpected(SceneIoError{SceneIoErrorKind::EmptyMesh, path.string(), {}});
    }

    if (skin != nullptr &&
        (!skin->valid() || skin->vertexCount() != rm.vertexCount() || skin->influences == 0)) {
        return std::unexpected(SceneIoError{SceneIoErrorKind::InvalidSkinOrMorph, path.string(),
                                            "skin does not describe this mesh"});
    }
    if (skin != nullptr) {
        for (const uint32_t jIdx : skin->joints) {
            if (jIdx >= skin->jointCount()) {
                return std::unexpected(SceneIoError{SceneIoErrorKind::InvalidSkinOrMorph,
                                                    path.string(), "joint index out of range"});
            }
        }
    }
    for (const auto& t : morphTargets) {
        if (t.deltas.size() != rm.vertexCount()) {
            return std::unexpected(SceneIoError{SceneIoErrorKind::InvalidSkinOrMorph, path.string(),
                                                t.name + ": wrong delta count"});
        }
    }

    const float scale = unitScale(options.unit) * options.scale;

    float groundOffset = 0.0F;
    if (options.feetOnGround) {
        float lowest = std::numeric_limits<float>::infinity();
        for (const Vec3& v : rm.coord)
            lowest = std::min(lowest, v.y * scale);
        if (std::isfinite(lowest)) groundOffset = -lowest;
    }

    // aiScene owns everything below; it is released by the unique_ptr on any
    // early return and handed to the exporter otherwise.
    auto scene = std::make_unique<aiScene>();

    scene->mRootNode = new aiNode();
    scene->mRootNode->mName.Set(options.meshName);

    scene->mNumMaterials = 1;
    scene->mMaterials    = allocArray<aiMaterial*>(1);
    scene->mMaterials[0] = new aiMaterial();
    fillMaterial(scene->mMaterials[0], material, options.materialName);

    scene->mNumMeshes = 1;
    scene->mMeshes    = allocArray<aiMesh*>(1);
    scene->mMeshes[0] = new aiMesh();

    aiMesh* am = scene->mMeshes[0];
    fillMesh(am, rm, options.meshName, 0, options, scale, groundOffset);
    const size_t nTris = rm.indexCount() / 3;

    if (skin != nullptr) attachSkin(am, *skin, scale, groundOffset);

    // ---- morph targets -----------------------------------------------------
    // aiAnimMesh holds ABSOLUTE positions, not deltas, so the base is added
    // back. Sending deltas produces a shape key that collapses the model to
    // near the origin when enabled.
    if (!morphTargets.empty()) {
        am->mNumAnimMeshes = static_cast<unsigned>(morphTargets.size());
        am->mAnimMeshes    = allocArray<aiAnimMesh*>(morphTargets.size());
        am->mMethod        = aiMorphingMethod_MORPH_NORMALIZED;

        for (size_t t = 0; t < morphTargets.size(); ++t) {
            auto* anim = new aiAnimMesh();
            anim->mName.Set(morphTargets[t].name);
            anim->mWeight      = 0.0F;
            anim->mNumVertices = static_cast<unsigned>(rm.vertexCount());
            anim->mVertices    = allocArray<aiVector3D>(rm.vertexCount());
            for (size_t v = 0; v < rm.vertexCount(); ++v) {
                const Vec3& base = rm.coord[v];
                const Vec3& d    = morphTargets[t].deltas[v];
                // The delta takes the unit scale but not the ground offset --
                // that is already in the base position.
                anim->mVertices[v] =
                    aiVector3D((base.x + d.x) * scale, (base.y + d.y) * scale + groundOffset,
                               (base.z + d.z) * scale);
            }
            am->mAnimMeshes[t] = anim;
        }
    }

    scene->mRootNode->mNumMeshes = 1;
    scene->mRootNode->mMeshes    = allocArray<unsigned>(1);
    scene->mRootNode->mMeshes[0] = 0;

    if (skin != nullptr) addJointNodes(scene->mRootNode, *skin, scale, groundOffset);
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

std::expected<ImportedScene, SceneIoError> importScene(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(SceneIoError{SceneIoErrorKind::NotFound, path.string(), {}});
    }

    // Same flags, and the same reason for them: ValidateDataStructure must run
    // before JoinIdenticalVertices or a file with out-of-range indices segfaults
    // inside the join. See importMesh above for the measurements.
    Assimp::Importer importer;
    const aiScene* scene =
        importer.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_ValidateDataStructure |
                                             aiProcess_JoinIdenticalVertices);
    if (scene == nullptr || scene->mNumMeshes == 0 || scene->mMeshes == nullptr) {
        return std::unexpected(
            SceneIoError{SceneIoErrorKind::ImportFailed, path.string(), importer.GetErrorString()});
    }

    // Walk the NODE GRAPH, not the mesh array. A scene places its meshes with
    // node transforms -- the mesh data is local and the node carries where it
    // goes -- so reading mMeshes directly returns every object stacked at the
    // origin, silently. Verified with a two-cube fixture: both came back at
    // x in [-1,1] when Blender had placed them at -5 and +5.
    //
    // Walking nodes also handles INSTANCING for free: one mesh referenced by
    // two nodes is two placed objects, which a mesh-array loop cannot express.
    struct Placement {
        unsigned meshIndex;
        aiMatrix4x4 world;
        std::string nodeName;
    };

    std::vector<Placement> placements;
    const auto walk = [&placements](const aiNode* node, const aiMatrix4x4& parent,
                                    auto&& self) -> void {
        if (node == nullptr) return;
        const aiMatrix4x4 world = parent * node->mTransformation;
        for (unsigned i = 0; i < node->mNumMeshes; ++i) {
            placements.push_back({node->mMeshes[i], world, std::string(node->mName.C_Str())});
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c) {
            self(node->mChildren[c], world, self);
        }
    };
    walk(scene->mRootNode, aiMatrix4x4{}, walk);

    // A file with no node graph at all still has meshes worth reading.
    if (placements.empty()) {
        for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
            placements.push_back({mi, aiMatrix4x4{}, {}});
        }
    }

    ImportedScene out;
    out.meshes.reserve(placements.size());

    // What one file unit is worth in metres, where the file says. Coordinates
    // themselves are NOT converted -- see ImportedScene::metersPerUnit.
    {
        const std::string ext = [&path] {
            std::string e = path.extension().string();
            std::ranges::transform(
                e, e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return e;
        }();
        if (ext == ".glb" || ext == ".gltf") {
            // glTF defines metres and carries no unit metadata, so the spec is
            // the only source there is.
            out.metersPerUnit = 1.0;
        } else if (scene->mMetaData != nullptr) {
            // FBX's UnitScaleFactor is CENTIMETRES per unit.
            float cmPerUnit = 0.0F;
            double dPerUnit = 0.0;
            if (scene->mMetaData->Get("UnitScaleFactor", cmPerUnit) && cmPerUnit > 0.0F) {
                out.metersPerUnit = static_cast<double>(cmPerUnit) / 100.0;
            } else if (scene->mMetaData->Get("UnitScaleFactor", dPerUnit) && dPerUnit > 0.0) {
                out.metersPerUnit = dPerUnit / 100.0;
            }
        }
    }

    for (const Placement& placed : placements) {
        if (placed.meshIndex >= scene->mNumMeshes) continue;
        const aiMesh* am = scene->mMeshes[placed.meshIndex];
        if (am == nullptr || am->mNumVertices == 0 || am->mNumFaces == 0) continue;
        const unsigned mi = placed.meshIndex;

        ImportedSceneMesh entry;
        entry.name                   = (am->mName.length > 0)     ? std::string(am->mName.C_Str())
                                       : !placed.nodeName.empty() ? placed.nodeName
                                                                  : ("mesh" + std::to_string(mi));
        entry.mesh.name              = entry.name;
        entry.mesh.vertsPerPrimitive = 3;  // triangulated on import

        // Non-finite coordinates survive assimp's validator and poison every
        // consumer downstream, so they are refused here rather than carried.
        entry.mesh.coord.resize(am->mNumVertices);
        bool finite = true;
        for (unsigned i = 0; i < am->mNumVertices && finite; ++i) {
            const auto& v = am->mVertices[i];
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                finite = false;
                break;
            }
            const aiVector3D w  = placed.world * v;
            entry.mesh.coord[i] = Vec3{w.x, w.y, w.z};
        }
        if (!finite) {
            return std::unexpected(SceneIoError{SceneIoErrorKind::ImportFailed, path.string(),
                                                "file contains a non-finite vertex coordinate"});
        }

        const bool hasUV = am->HasTextureCoords(0);
        if (hasUV) {
            entry.mesh.texco.resize(am->mNumVertices);
            for (unsigned i = 0; i < am->mNumVertices; ++i) {
                entry.mesh.texco[i] = Vec2{am->mTextureCoords[0][i].x, am->mTextureCoords[0][i].y};
            }
        }

        entry.mesh.fvert.reserve(static_cast<size_t>(am->mNumFaces) * 3);
        bool indicesOk = true;
        for (unsigned f = 0; f < am->mNumFaces && indicesOk; ++f) {
            const aiFace& face = am->mFaces[f];
            if (face.mNumIndices != 3) continue;  // Triangulate should prevent this
            for (unsigned c = 0; c < 3; ++c) {
                if (face.mIndices[c] >= am->mNumVertices) {
                    indicesOk = false;
                    break;
                }
                entry.mesh.fvert.push_back(face.mIndices[c]);
                if (hasUV) entry.mesh.fuvs.push_back(face.mIndices[c]);
            }
        }
        if (!indicesOk) {
            return std::unexpected(SceneIoError{SceneIoErrorKind::ImportFailed, path.string(),
                                                "face index out of range"});
        }

        // A mesh with no triangles is skipped, not fatal: real scenes carry
        // empty or non-triangular helper meshes, and failing the whole file for
        // one of them would make many usable assets unopenable.
        if (entry.mesh.fvert.empty()) continue;

        // The material, when the file carried one. Absent means the file had
        // none -- not that it had a default -- so a caller can tell those apart
        // before substituting its own.
        if (am->mMaterialIndex < scene->mNumMaterials && scene->mMaterials != nullptr) {
            const aiMaterial* mat = scene->mMaterials[am->mMaterialIndex];
            if (mat != nullptr) {
                foundation::MaterialDesc desc;
                aiString str;
                if (mat->Get(AI_MATKEY_NAME, str) == AI_SUCCESS && str.length > 0) {
                    desc.name = str.C_Str();
                }
                const auto colour = [&mat](const char* key, unsigned t, unsigned i, Vec3& into) {
                    aiColor3D c;
                    if (mat->Get(key, t, i, c) == AI_SUCCESS) into = Vec3{c.r, c.g, c.b};
                };
                colour(AI_MATKEY_COLOR_DIFFUSE, desc.diffuse);
                colour(AI_MATKEY_COLOR_AMBIENT, desc.ambient);
                colour(AI_MATKEY_COLOR_SPECULAR, desc.specular);
                float f = 0.0F;
                // Back from the exponent, and clamped: `.mhmat` guarantees
                // 0..1 for this field (Material.cpp:242) and every consumer
                // relies on it -- glTF and USD roughness is `1 - shininess`, so
                // an unscaled exponent of 10 would ask for roughness -9.
                if (mat->Get(AI_MATKEY_SHININESS, f) == AI_SUCCESS) {
                    desc.shininess = foundation::shininessFromExponent(f);
                }
                if (mat->Get(AI_MATKEY_OPACITY, f) == AI_SUCCESS) {
                    desc.opacity = f;
                    // Only opacity below 1 implies transparency. Deriving it
                    // from the presence of an alpha channel instead would mark
                    // every RGBA-textured material transparent.
                    desc.transparent = f < 1.0F;
                }
                // Paths come back as the file states them -- usually relative.
                // Resolving is the caller's job; only it knows the origin.
                if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &str) == AI_SUCCESS) {
                    desc.diffuseTexture = str.C_Str();
                }
                if (mat->GetTexture(aiTextureType_NORMALS, 0, &str) == AI_SUCCESS) {
                    desc.normalTexture = str.C_Str();
                }
                entry.material = std::move(desc);
            }
        }

        // The skin, when the file carried one. Bone vertex ids are post-join:
        // assimp remaps them through JoinIdenticalVertices, so they index the
        // vertices we just read rather than the file's originals.
        if (am->mNumBones > 0 && am->mBones != nullptr) {
            ImportedSkin skin;
            skin.bones.reserve(am->mNumBones);
            for (unsigned b = 0; b < am->mNumBones; ++b) {
                const aiBone* ab = am->mBones[b];
                if (ab == nullptr) continue;
                ImportedBone bone;
                bone.name = ab->mName.C_Str();
                // assimp's aiMatrix4x4 is row-major and so is ours, so this is
                // an element-wise copy rather than a transpose.
                for (unsigned r = 0; r < 4; ++r) {
                    for (unsigned c = 0; c < 4; ++c) {
                        bone.offset.m[r][c] = ab->mOffsetMatrix[r][c];
                    }
                }
                bone.verts.reserve(ab->mNumWeights);
                bone.weights.reserve(ab->mNumWeights);
                for (unsigned w = 0; w < ab->mNumWeights; ++w) {
                    const aiVertexWeight& vw = ab->mWeights[w];
                    // A weight naming a vertex the mesh does not have is a
                    // corrupt file; dropping it silently would leave a body
                    // part unbound and moving with the wrong bone.
                    if (vw.mVertexId >= am->mNumVertices) {
                        return std::unexpected(SceneIoError{SceneIoErrorKind::ImportFailed,
                                                            path.string(),
                                                            "bone weight names a vertex that "
                                                            "does not exist"});
                    }
                    bone.verts.push_back(vw.mVertexId);
                    bone.weights.push_back(vw.mWeight);
                }
                skin.bones.push_back(std::move(bone));
            }
            if (!skin.bones.empty()) entry.skin = std::move(skin);
        }

        out.meshes.push_back(std::move(entry));
    }

    if (out.meshes.empty()) {
        return std::unexpected(SceneIoError{SceneIoErrorKind::ImportFailed, path.string(),
                                            "no mesh in the file had triangular faces"});
    }
    return out;
}

}  // namespace mh::io
