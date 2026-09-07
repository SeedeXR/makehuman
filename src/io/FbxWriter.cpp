// SPDX-License-Identifier: Apache-2.0

#include "makehuman/io/FbxWriter.h"

#include "makehuman/foundation/Transform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

namespace mh::io {
namespace {

using foundation::Vec3;

/// 7500, not 7700. Both Maya (7700) and assimp (7500) write 7.x binary and the
/// record header is identical from 7500 on -- the three counts became 64-bit
/// there. 7500 is the older of the two and so the wider-read one; nothing below
/// needs anything 7700 added.
constexpr uint32_t kVersion = 7500;

constexpr int64_t kDocumentId = 1'000'000;

/// The last 16 bytes of every FBX binary. Constant: Maya and assimp write the
/// same bytes, which is how it was established rather than recalled.
constexpr uint8_t kFooterMagic[16] = {0xF8, 0x5A, 0x8C, 0x6A, 0xDE, 0xF5, 0xD9, 0x7E,
                                      0xEC, 0xE9, 0x0C, 0xE3, 0x75, 0x8F, 0x29, 0x0B};

/// THESE THREE MOVE TOGETHER. Change one and the file stops importing in Maya.
///
/// The 16-byte footer id is a function of `FileId` and `CreationTime` through
/// an obfuscation Autodesk does not publish, and **Maya's SDK checks it**:
/// given a mismatched id it reads the file, reports no warning in its own log,
/// and imports ZERO objects. Blender ignores the id entirely and imports the
/// same file correctly, so only asking both readers finds this at all.
///
/// Proved rather than reasoned: re-emitting assimp's `base.fbx` byte-for-byte
/// with our encoder, differing ONLY in these last 161 bytes, imported as 0
/// meshes; grafting the original footer back on made it 1 mesh and 21,833
/// vertices.
///
/// So we adopt a known-consistent triple. It is assimp's -- which uses a fixed
/// id and the epoch for every file it writes, verified across three of them --
/// and it is data, from a BSD-3-Clause tool's OUTPUT, not code. If the
/// obfuscation is ever worked out, compute the id and delete this note.
constexpr uint8_t kFileId[16]       = {0x28, 0xB3, 0x2A, 0xEB, 0xB6, 0x24, 0xCC, 0xC2,
                                       0xBF, 0xC8, 0xB0, 0x2A, 0xA9, 0x2B, 0xFC, 0xF1};
constexpr const char* kCreationTime = "1970-01-01 10:00:00:000";
constexpr uint8_t kFooterId[16]     = {0xFA, 0xBC, 0xAB, 0x09, 0xD0, 0xC8, 0xD4, 0x66,
                                       0xB1, 0x76, 0xFB, 0x83, 0x1C, 0xF7, 0x26, 0x7E};

/// Object names are `Name\0\x01Class` -- a NUL and a 0x01 between the two,
/// not a separator anyone would guess. Read out of Maya's file: `RefCube`
/// arrives as `RefCube\0\x01Model`.
std::string objectName(std::string_view name, std::string_view klass) {
    std::string out(name);
    out.push_back('\0');
    out.push_back('\x01');
    out.append(klass);
    return out;
}

/// A record being built. Children are appended as finished byte blocks, so a
/// node's own length is known only once it is closed -- which is why this
/// exists rather than a stream.
class Node {
public:
    explicit Node(std::string name) : name_(std::move(name)) {}

    void addI16(int16_t v) { scalar('Y', &v, sizeof(v)); }

    void addBool(bool v) {
        const uint8_t b = v ? 1U : 0U;
        scalar('C', &b, sizeof(b));
    }

    void addI32(int32_t v) { scalar('I', &v, sizeof(v)); }

    void addF64(double v) { scalar('D', &v, sizeof(v)); }

    void addI64(int64_t v) { scalar('L', &v, sizeof(v)); }

    void addString(std::string_view v) {
        ++properties_;
        props_.push_back('S');
        appendU32(props_, static_cast<uint32_t>(v.size()));
        props_.insert(props_.end(), v.begin(), v.end());
    }

    void addRaw(const uint8_t* data, size_t n) {
        ++properties_;
        props_.push_back('R');
        appendU32(props_, static_cast<uint32_t>(n));
        props_.insert(props_.end(), data, data + n);
    }

    /// An array property, stored UNCOMPRESSED (encoding 0).
    ///
    /// The format allows zlib (encoding 1) and both Maya and assimp use it. We
    /// do not yet: a deflate stream that a reader rejects fails in a way that
    /// looks like a corrupt mesh, and there is no point paying that risk before
    /// the uncompressed path is proven against both DCCs.
    template <typename T>
    void addArray(char type, const std::vector<T>& values) {
        ++properties_;
        props_.push_back(static_cast<uint8_t>(type));
        appendU32(props_, static_cast<uint32_t>(values.size()));
        appendU32(props_, 0U);  // encoding: raw
        appendU32(props_, static_cast<uint32_t>(values.size() * sizeof(T)));
        const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
        props_.insert(props_.end(), bytes, bytes + (values.size() * sizeof(T)));
    }

    void add(Node&& child) { children_.push_back(std::move(child)); }

    /// Appends this record, and everything under it, to @p out.
    void writeTo(std::vector<uint8_t>& out) const {
        const size_t headerAt = out.size();
        appendU64(out, 0);  // EndOffset, patched below
        appendU64(out, properties_);
        appendU64(out, props_.size());
        out.push_back(static_cast<uint8_t>(name_.size()));
        out.insert(out.end(), name_.begin(), name_.end());
        out.insert(out.end(), props_.begin(), props_.end());

        for (const Node& c : children_)
            c.writeTo(out);
        // The NULL record rule, MEASURED across both writers rather than
        // assumed. Surveying every record in assimp's and Maya's output:
        //
        //   children              -> 25-byte null record   (18 + 39 nodes)
        //   no children, props    -> none                  (186 + 402 nodes)
        //   no children, no props -> 25-byte null record    (1 + 1 nodes)
        //
        // That last row is the one a "only when it has children" rule gets
        // wrong, and it is not academic: `References` is exactly such a node,
        // and omitting its terminator shifted every later offset by 25 bytes.
        // Maya then read the file without a single warning and imported ZERO
        // meshes; Blender read the same file correctly.
        if (!children_.empty() || properties_ == 0) out.insert(out.end(), 25, 0U);

        const uint64_t end = out.size();
        std::memcpy(out.data() + headerAt, &end, sizeof(end));
    }

    static void appendU32(std::vector<uint8_t>& b, uint32_t v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(v));
    }

    static void appendU64(std::vector<uint8_t>& b, uint64_t v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(v));
    }

private:
    void scalar(char type, const void* data, size_t n) {
        ++properties_;
        props_.push_back(static_cast<uint8_t>(type));
        const auto* p = static_cast<const uint8_t*>(data);
        props_.insert(props_.end(), p, p + n);
    }

    std::string name_;
    uint64_t properties_{0};
    std::vector<uint8_t> props_;
    std::vector<Node> children_;
};

/// A `Properties70` entry: name, type, subtype, flags, then the value.
Node property70(std::string_view name, std::string_view type, std::string_view sub,
                std::string_view flags) {
    Node p("P");
    p.addString(name);
    p.addString(type);
    p.addString(sub);
    p.addString(flags);
    return p;
}

/// The SUBTYPE is not decoration. Blender's importer asserts on it --
/// `elem_props_get_number` requires `b'Number'` in slot 2 -- and rejects the
/// whole file if it is empty. Maya writes `Integer` for `int` and `Number` for
/// `double`, which is where these came from.
Node intProperty(std::string_view name, int32_t value) {
    Node p = property70(name, "int", "Integer", "");
    p.addI32(value);
    return p;
}

Node doubleProperty(std::string_view name, double value) {
    Node p = property70(name, "double", "Number", "");
    p.addF64(value);
    return p;
}

Node headerExtension() {
    Node h("FBXHeaderExtension");
    Node v("FBXHeaderVersion");
    v.addI32(1003);
    h.add(std::move(v));
    Node fv("FBXVersion");
    fv.addI32(static_cast<int32_t>(kVersion));
    h.add(std::move(fv));
    Node enc("EncryptionType");
    enc.addI32(0);
    h.add(std::move(enc));

    // The timestamp, broken into fields. Both Maya and assimp write it and a
    // strict reader looks for it; the values match `kCreationTime`, which the
    // footer id is derived from, so the two cannot disagree.
    Node stamp("CreationTimeStamp");
    for (const auto& [name, value] : {std::pair<const char*, int32_t>{"Version", 1000},
                                      {"Year", 1970},
                                      {"Month", 1},
                                      {"Day", 1},
                                      {"Hour", 10},
                                      {"Minute", 0},
                                      {"Second", 0},
                                      {"Millisecond", 0}}) {
        Node field(name);
        field.addI32(value);
        stamp.add(std::move(field));
    }
    h.add(std::move(stamp));

    Node c("Creator");
    c.addString("MakeHuman C++ FBX writer");
    h.add(std::move(c));
    return h;
}

Node globalSettings(const FbxWriteOptions& options) {
    Node g("GlobalSettings");
    Node v("Version");
    v.addI32(1000);
    g.add(std::move(v));

    Node p("Properties70");
    // Y-up, Z-front, X-right: MakeHuman's own convention, stated rather than
    // left to the reader's default. An importer that assumes Z-up lays the
    // figure on its face.
    p.add(intProperty("UpAxis", 1));
    p.add(intProperty("UpAxisSign", 1));
    p.add(intProperty("FrontAxis", 2));
    p.add(intProperty("FrontAxisSign", 1));
    p.add(intProperty("CoordAxis", 0));
    p.add(intProperty("CoordAxisSign", 1));
    // CENTIMETRES PER FILE UNIT. The mesh is already scaled out of decimetres
    // by `unitScale`, so one file unit is `10 / unitScale` centimetres: 1 for
    // centimetres, 100 for metres, 2.54 for inches. Writing the scale factor
    // itself instead makes a metre-scale model arrive 1000x too large.
    p.add(doubleProperty("UnitScaleFactor", 10.0 / static_cast<double>(unitScale(options.unit))));
    g.add(std::move(p));
    return g;
}

/// `LayerElementNormal` plus a `Layer` that names it.
///
/// Not decoration, and not stage-2 polish: Maya imported a Geometry with no
/// Layer as **zero meshes**, silently. Blender took the same file happily, so
/// only asking both caught it. Every writer that Maya accepts -- its own and
/// assimp's -- emits these.
///
/// `ByPolygonVertex`/`Direct`: one normal per INDEX entry, in index order,
/// which is how a hard edge is expressed at all. Per-vertex normals would make
/// every edge smooth.
Node normalLayer(const foundation::RenderView& mesh) {
    Node n("LayerElementNormal");
    n.addI32(0);
    Node v("Version");
    v.addI32(102);
    n.add(std::move(v));
    Node name("Name");
    name.addString("");
    n.add(std::move(name));
    Node mapping("MappingInformationType");
    mapping.addString("ByPolygonVertex");
    n.add(std::move(mapping));
    Node reference("ReferenceInformationType");
    reference.addString("Direct");
    n.add(std::move(reference));

    std::vector<double> normals;
    normals.reserve(mesh.index.size() * 3);
    for (const uint32_t i : mesh.index) {
        const Vec3& nv = mesh.vnorm[i];
        normals.push_back(static_cast<double>(nv.x));
        normals.push_back(static_cast<double>(nv.y));
        normals.push_back(static_cast<double>(nv.z));
    }
    Node data("Normals");
    data.addArray('d', normals);
    n.add(std::move(data));
    return n;
}

/// `LayerElementMaterial`, `AllSame`: one material for the whole mesh.
///
/// Required in practice, not in principle. Maya imported our Geometry as ZERO
/// meshes whenever no material was connected to it, and accepted the identical
/// geometry as soon as one was -- so the mesh needs a material to exist, and
/// the material needs this layer to be addressed by.
/// `LayerElementUV`, `ByPolygonVertex`/`IndexToDirect`.
///
/// The direct array is our per-vertex UVs and the index array is the polygon
/// index list, unnegated -- our RenderView already has exactly the shape this
/// form wants, so it stores each UV once and costs nothing to produce.
///
/// V is NOT flipped. FBX's UV origin is the lower-left, like OBJ's; glTF is the
/// odd one out (session 150 checked all four formats against each other in
/// Blender) and flipping here would mirror every texture vertically.
Node uvLayer(const foundation::RenderView& mesh) {
    Node u("LayerElementUV");
    u.addI32(0);
    Node v("Version");
    v.addI32(101);
    u.add(std::move(v));
    Node name("Name");
    name.addString("UVMap");
    u.add(std::move(name));
    Node mapping("MappingInformationType");
    mapping.addString("ByPolygonVertex");
    u.add(std::move(mapping));
    Node reference("ReferenceInformationType");
    reference.addString("IndexToDirect");
    u.add(std::move(reference));

    std::vector<double> uvs;
    uvs.reserve(mesh.texco.size() * 2);
    for (const foundation::Vec2& t : mesh.texco) {
        uvs.push_back(static_cast<double>(t.x));
        uvs.push_back(static_cast<double>(t.y));
    }
    Node data("UV");
    data.addArray('d', uvs);
    u.add(std::move(data));

    std::vector<int32_t> indices;
    indices.reserve(mesh.index.size());
    for (const uint32_t i : mesh.index)
        indices.push_back(static_cast<int32_t>(i));
    Node idx("UVIndex");
    idx.addArray('i', indices);
    u.add(std::move(idx));
    return u;
}

Node materialLayer() {
    Node m("LayerElementMaterial");
    m.addI32(0);
    Node v("Version");
    v.addI32(101);
    m.add(std::move(v));
    Node name("Name");
    name.addString("");
    m.add(std::move(name));
    Node mapping("MappingInformationType");
    mapping.addString("AllSame");
    m.add(std::move(mapping));
    Node reference("ReferenceInformationType");
    reference.addString("IndexToDirect");
    m.add(std::move(reference));
    Node materials("Materials");
    materials.addArray('i', std::vector<int32_t>{0});
    m.add(std::move(materials));
    return m;
}

Node layerEntry(std::string_view type) {
    Node e("LayerElement");
    Node t("Type");
    t.addString(type);
    e.add(std::move(t));
    Node index("TypedIndex");
    index.addI32(0);
    e.add(std::move(index));
    return e;
}

Node layer(bool withUVs) {
    Node l("Layer");
    l.addI32(0);
    Node v("Version");
    v.addI32(100);
    l.add(std::move(v));
    l.add(layerEntry("LayerElementNormal"));
    l.add(layerEntry("LayerElementMaterial"));
    if (withUVs) l.add(layerEntry("LayerElementUV"));
    return l;
}

/// A minimal Lambert material. Stage 2 gives it the real colours; stage 1 needs
/// it to exist at all, for the reason `materialLayer` records.
Node colourProperty(std::string_view name, const foundation::Vec3& c) {
    Node p = property70(name, "Color", "", "A");
    p.addF64(static_cast<double>(c.x));
    p.addF64(static_cast<double>(c.y));
    p.addF64(static_cast<double>(c.z));
    return p;
}

/// A Lambert material carrying the description's own colours.
///
/// `ShininessExponent`, not `Shininess`: `MaterialDesc::shininess` is 0..1 and
/// every Blinn-Phong interchange format wants the EXPONENT, which is what
/// `specularExponentOf` converts it to. Writing the 0..1 number into an
/// exponent field says "almost perfectly matte" for a value that means the
/// opposite -- the same trap `Geometry.h` documents for Collada and FBX alike.
Node materialNode(int64_t id, const std::string& name, const foundation::MaterialDesc* desc) {
    Node m("Material");
    m.addI64(id);
    m.addString(objectName(desc != nullptr && !desc->name.empty() ? desc->name : name, "Material"));
    m.addString("");
    Node v("Version");
    v.addI32(102);
    m.add(std::move(v));
    Node shading("ShadingModel");
    shading.addString("Lambert");
    m.add(std::move(shading));
    Node multi("MultiLayer");
    multi.addI32(0);
    m.add(std::move(multi));

    Node props("Properties70");
    const foundation::Vec3 diffuse  = desc != nullptr ? desc->diffuse : foundation::Vec3{1, 1, 1};
    const foundation::Vec3 specular = desc != nullptr ? desc->specular : foundation::Vec3{0, 0, 0};
    const foundation::Vec3 ambient  = desc != nullptr ? desc->ambient : foundation::Vec3{0, 0, 0};
    props.add(colourProperty("AmbientColor", ambient));
    props.add(colourProperty("DiffuseColor", diffuse));
    props.add(colourProperty("SpecularColor", specular));
    Node shine = property70("ShininessExponent", "Number", "", "A");
    shine.addF64(static_cast<double>(
        foundation::specularExponentOf(desc != nullptr ? desc->shininess : 0.2F)));
    props.add(std::move(shine));
    if (desc != nullptr && desc->opacity < 1.0F) {
        Node opacity = property70("Opacity", "Number", "", "A");
        opacity.addF64(static_cast<double>(desc->opacity));
        props.add(std::move(opacity));
    }
    m.add(std::move(props));
    return m;
}

/// The two objects a file texture takes, read out of Maya's own output: a
/// `Video` holding the path and a `Texture` naming it through `Media`.
///
/// They are separate because FBX lets several textures share one image. We
/// write one pair per texture, which is the simple case and the only one this
/// writer produces.
Node videoClip(int64_t id, const std::string& name, const std::string& path) {
    Node v("Video");
    v.addI64(id);
    v.addString(objectName(name, "Video"));
    v.addString("Clip");
    Node type("Type");
    type.addString("Clip");
    v.add(std::move(type));
    v.add(Node("Properties70"));
    Node mip("UseMipMap");
    mip.addI32(0);
    v.add(std::move(mip));
    Node file("Filename");
    file.addString(path);
    v.add(std::move(file));
    Node rel("RelativeFilename");
    rel.addString(path);
    v.add(std::move(rel));
    return v;
}

Node textureNode(int64_t id, const std::string& name, const std::string& path) {
    Node t("Texture");
    t.addI64(id);
    t.addString(objectName(name, "Texture"));
    t.addString("");
    Node type("Type");
    type.addString("TextureVideoClip");
    t.add(std::move(type));
    Node v("Version");
    v.addI32(202);
    t.add(std::move(v));
    Node tname("TextureName");
    tname.addString(objectName(name, "Texture"));
    t.add(std::move(tname));
    t.add(Node("Properties70"));
    Node media("Media");
    media.addString(objectName(name, "Video"));
    t.add(std::move(media));
    Node file("FileName");
    file.addString(path);
    t.add(std::move(file));
    Node rel("RelativeFilename");
    rel.addString(path);
    t.add(std::move(rel));
    Node uvt("ModelUVTranslation");
    uvt.addF64(0.0);
    uvt.addF64(0.0);
    t.add(std::move(uvt));
    Node uvs("ModelUVScaling");
    uvs.addF64(1.0);
    uvs.addF64(1.0);
    t.add(std::move(uvs));
    Node alpha("Texture_Alpha_Source");
    alpha.addString("None");
    t.add(std::move(alpha));
    Node crop("Cropping");
    crop.addI32(0);
    crop.addI32(0);
    crop.addI32(0);
    crop.addI32(0);
    t.add(std::move(crop));
    return t;
}

/// FBX stores a 4x4 COLUMN-major, the same convention glTF uses. Ours is
/// row-major with column vectors, so this transposes on the way out. Writing it
/// straight through produces a file that loads, poses, and is wrong in a way
/// that looks like bad weights.
std::vector<double> matrixValues(const foundation::Mat4& m) {
    std::vector<double> out;
    out.reserve(16);
    for (size_t c = 0; c < 4; ++c) {
        for (size_t r = 0; r < 4; ++r)
            out.push_back(static_cast<double>(m.m[r][c]));
    }
    return out;
}

/// One joint's `NodeAttribute`. `TypeFlags: Skeleton` is what makes it a bone
/// rather than a transform: without it Maya imports a hierarchy of empties and
/// the skin has nothing to bind to.
Node limbAttribute(int64_t id) {
    Node n("NodeAttribute");
    n.addI64(id);
    n.addString(objectName("", "NodeAttribute"));
    n.addString("LimbNode");
    n.add(Node("Properties70"));
    Node flags("TypeFlags");
    flags.addString("Skeleton");
    n.add(std::move(flags));
    return n;
}

/// One joint's `Model`, carrying its LOCAL transform.
///
/// This is the POSED transform, and the cluster's `TransformLink` is the BIND
/// one. That difference is the live rig: a consumer computes
/// `pose * inverse(bind)` and reproduces the deformation itself, instead of
/// receiving geometry with the pose already baked into it.
Node limbModel(int64_t id, const std::string& name, const foundation::Mat4& local) {
    Node m("Model");
    m.addI64(id);
    m.addString(objectName(name, "Model"));
    m.addString("LimbNode");
    Node v("Version");
    v.addI32(232);
    m.add(std::move(v));

    Node props("Properties70");
    Node rotationActive = property70("RotationActive", "bool", "", "");
    rotationActive.addI32(1);
    props.add(std::move(rotationActive));
    Node inherit = property70("InheritType", "enum", "", "");
    inherit.addI32(1);
    props.add(std::move(inherit));
    props.add(intProperty("DefaultAttributeIndex", 0));

    Node translation = property70("Lcl Translation", "Lcl Translation", "", "A");
    translation.addF64(static_cast<double>(local.m[0][3]));
    translation.addF64(static_cast<double>(local.m[1][3]));
    translation.addF64(static_cast<double>(local.m[2][3]));
    props.add(std::move(translation));

    // Degrees, in FBX's default XYZ rotation order, taken from the same matrix
    // the translation came from so the two cannot describe different placements.
    //
    // The angles are written by AXIS IDENTITY -- `sxyz` returns [x, y, z] and
    // that is the order `Lcl Rotation` wants. Popping them positionally into
    // some other order is the mistake session 076 made in the BVH writer, where
    // it moved a matrix element by 1.69 and looked like a precision problem.
    // Checked against Maya below rather than argued.
    const auto euler = foundation::eulerFromMatrix(local, foundation::EulerOrder{0, 0, 0, 0});
    Node rotation    = property70("Lcl Rotation", "Lcl Rotation", "", "A");
    constexpr double kRadiansToDegrees = 57.295779513082320876798154814105;
    for (const double angle : euler)
        rotation.addF64(angle * kRadiansToDegrees);
    props.add(std::move(rotation));
    m.add(std::move(props));

    Node shading("Shading");
    shading.addBool(true);
    m.add(std::move(shading));
    Node culling("Culling");
    culling.addString("CullingOff");
    m.add(std::move(culling));
    return m;
}

/// The skin deformer itself. One per mesh; the clusters hang off it.
Node skinDeformer(int64_t id) {
    Node s("Deformer");
    s.addI64(id);
    s.addString(objectName("", "Deformer"));
    s.addString("Skin");
    Node v("Version");
    v.addI32(101);
    s.add(std::move(v));
    Node accuracy("Link_DeformAcuracy");  // spelt this way in the format
    accuracy.addF64(50.0);
    s.add(std::move(accuracy));
    Node type("SkinningType");
    type.addString("Linear");
    s.add(std::move(type));
    return s;
}

/// One joint's cluster: which vertices it moves, how much, and from where.
///
/// `Transform` is the MESH's global at bind time (identity here -- the geometry
/// is written in model space) and `TransformLink` is the JOINT's. Swapping them
/// produces a rig that deforms by the inverse of what it should.
Node clusterDeformer(int64_t id, const std::string& name, std::vector<int32_t> indices,
                     std::vector<double> weights, const foundation::Mat4& bind) {
    Node c("Deformer");
    c.addI64(id);
    c.addString(objectName(name, "SubDeformer"));
    c.addString("Cluster");
    Node v("Version");
    v.addI32(100);
    c.add(std::move(v));
    Node user("UserData");
    user.addString("");
    user.addString("");
    c.add(std::move(user));

    // A joint that moves nothing still needs its cluster -- the bind pose is
    // per joint, and a missing one shifts every later index.
    if (!indices.empty()) {
        Node idx("Indexes");
        idx.addArray('i', indices);
        c.add(std::move(idx));
        Node w("Weights");
        w.addArray('d', weights);
        c.add(std::move(w));
    }

    Node transform("Transform");
    transform.addArray('d', matrixValues(foundation::Mat4::identity()));
    c.add(std::move(transform));
    Node link("TransformLink");
    link.addArray('d', matrixValues(bind));
    c.add(std::move(link));
    return c;
}

/// The bind pose: every node's global transform at the moment of binding.
///
/// **This is the record assimp omits**, and its own SDK log says what happens
/// then: "The imported scene has no initial binding position (Bind Pose) for
/// the skin. The plug-in will compute one automatically." Maya's automatic one
/// is taken from the CURRENT pose, so rest and posed coincide and the rig
/// arrives baked -- measured at 168.63 x 166.30 x 30.09 cm for both.
Node bindPose(int64_t id, int64_t meshModelId, std::span<const int64_t> jointModelIds,
              std::span<const foundation::Mat4> jointGlobals) {
    Node p("Pose");
    p.addI64(id);
    p.addString(objectName("", "Pose"));
    p.addString("BindPose");
    Node type("Type");
    type.addString("BindPose");
    p.add(std::move(type));
    Node v("Version");
    v.addI32(100);
    p.add(std::move(v));
    Node count("NbPoseNodes");
    count.addI32(static_cast<int32_t>(jointModelIds.size() + 1));
    p.add(std::move(count));

    // The MESH's node is in the pose too, not just the joints: a bind pose
    // records where everything was, and Maya's own files include it.
    const auto poseNode = [](int64_t node, const foundation::Mat4& m) {
        Node n("PoseNode");
        Node id2("Node");
        id2.addI64(node);
        n.add(std::move(id2));
        Node matrix("Matrix");
        matrix.addArray('d', matrixValues(m));
        n.add(std::move(matrix));
        return n;
    };
    p.add(poseNode(meshModelId, foundation::Mat4::identity()));
    for (size_t j = 0; j < jointModelIds.size(); ++j) {
        p.add(poseNode(jointModelIds[j], jointGlobals[j]));
    }
    return p;
}

/// One blend shape target's geometry: which vertices move, and by how much.
///
/// SPARSE, like Maya's own. The shipped expression targets move a few hundred
/// vertices of 21,833, so a dense shape would be two orders of magnitude of
/// zeros, thirty-four times over.
Node shapeGeometry(int64_t id, const std::string& name, const foundation::MorphTarget& target,
                   const Transform& xf) {
    Node g("Geometry");
    g.addI64(id);
    g.addString(objectName(name, "Geometry"));
    g.addString("Shape");
    Node v("Version");
    v.addI32(100);
    g.add(std::move(v));

    std::vector<int32_t> indices;
    std::vector<double> deltas;
    for (size_t i = 0; i < target.deltas.size(); ++i) {
        const Vec3& d = target.deltas[i];
        if (d.x == 0.0F && d.y == 0.0F && d.z == 0.0F) continue;
        indices.push_back(static_cast<int32_t>(i));
        // Scaled like a position but WITHOUT the ground offset: a delta is a
        // displacement, not a point, and adding the offset would shift the body
        // once per active target.
        deltas.push_back(static_cast<double>(d.x * xf.scale));
        deltas.push_back(static_cast<double>(d.y * xf.scale));
        deltas.push_back(static_cast<double>(d.z * xf.scale));
    }
    Node idx("Indexes");
    idx.addArray('i', indices);
    g.add(std::move(idx));
    Node verts("Vertices");
    verts.addArray('d', deltas);
    g.add(std::move(verts));
    // Normals, all zero: a shape may carry normal deltas and ours does not --
    // the consumer recomputes, which is what the viewport does too and is more
    // accurate than blending rest normals.
    Node normals("Normals");
    normals.addArray('d', std::vector<double>(deltas.size(), 0.0));
    g.add(std::move(normals));
    return g;
}

/// The channel that drives one target. `DeformPercent` 0 is the unmorphed
/// state, which is what a file should open in; `FullWeights` is the 0..100
/// scale at which the shape is fully applied.
Node blendShapeChannel(int64_t id, const std::string& name) {
    Node c("Deformer");
    c.addI64(id);
    c.addString(objectName(name, "SubDeformer"));
    c.addString("BlendShapeChannel");
    Node v("Version");
    v.addI32(100);
    c.add(std::move(v));
    Node percent("DeformPercent");
    percent.addF64(0.0);
    c.add(std::move(percent));
    Node weights("FullWeights");
    weights.addArray('d', std::vector<double>{100.0});
    c.add(std::move(weights));
    return c;
}

Node blendShapeDeformer(int64_t id, const std::string& name) {
    Node b("Deformer");
    b.addI64(id);
    b.addString(objectName(name, "Deformer"));
    b.addString("BlendShape");
    Node v("Version");
    v.addI32(100);
    b.add(std::move(v));
    b.add(Node("Properties70"));
    return b;
}

/// The scene document, and the `RootNode` that says where its root is.
///
/// The `RootNode` child is what was missing when Maya imported this file as
/// ZERO meshes, with no warning in its own SDK log -- the objects were read and
/// then attached to nothing. Blender does not need it and took the same file
/// happily, which is why only asking both found it. The name property is EMPTY
/// and the class is "Scene", matching what assimp writes.
Node documents() {
    Node d("Documents");
    Node c("Count");
    c.addI32(1);
    d.add(std::move(c));
    Node doc("Document");
    doc.addI64(kDocumentId);
    doc.addString("");
    doc.addString("Scene");
    // Two entries, because an EMPTY Properties70 is what made Maya discard the
    // Model -- the same trap, one node up.
    Node props("Properties70");
    props.add(property70("SourceObject", "object", "", ""));
    Node active = property70("ActiveAnimStackName", "KString", "", "");
    active.addString("");
    props.add(std::move(active));
    doc.add(std::move(props));

    // `L`, an int64, NOT an int32. Both Maya and assimp write it that way, and
    // a reader that takes the property's declared type at its word finds a
    // 4-byte value where it expects 8 -- which is a scene whose root resolves
    // to nothing, and objects that are read and then attached to nothing.
    Node root("RootNode");
    root.addI64(0);
    doc.add(std::move(root));
    d.add(std::move(doc));
    return d;
}

Node definitions() {
    Node d("Definitions");
    Node v("Version");
    v.addI32(100);
    d.add(std::move(v));
    Node c("Count");
    c.addI32(8);
    d.add(std::move(c));
    for (const char* type : {"Geometry", "Material", "Model", "Texture", "Video", "NodeAttribute",
                             "Deformer", "Pose"}) {
        Node o("ObjectType");
        o.addString(type);
        Node oc("Count");
        oc.addI32(1);
        o.add(std::move(oc));
        d.add(std::move(o));
    }
    return d;
}

/// The geometry record: positions and the polygon index list.
///
/// `PolygonVertexIndex` marks the END of each polygon by storing that index as
/// `~i`. Without it a reader has no way to know where one face stops, and every
/// face after the first lands on the wrong vertices.
Node geometry(int64_t id, const foundation::RenderView& mesh, const Transform& xf,
              size_t vertsPerPolygon) {
    Node g("Geometry");
    g.addI64(id);
    g.addString(objectName("", "Geometry"));
    g.addString("Mesh");

    std::vector<double> coords;
    coords.reserve(mesh.coord.size() * 3);
    for (const Vec3& v : mesh.coord) {
        const Vec3 p = xf.place(v);
        coords.push_back(static_cast<double>(p.x));
        coords.push_back(static_cast<double>(p.y));
        coords.push_back(static_cast<double>(p.z));
    }
    Node verts("Vertices");
    verts.addArray('d', coords);
    g.add(std::move(verts));

    std::vector<int32_t> indices;
    indices.reserve(mesh.index.size());
    for (size_t i = 0; i < mesh.index.size(); ++i) {
        const auto idx  = static_cast<int32_t>(mesh.index[i]);
        const bool last = (i % vertsPerPolygon) == (vertsPerPolygon - 1);
        indices.push_back(last ? ~idx : idx);
    }
    Node poly("PolygonVertexIndex");
    poly.addArray('i', indices);
    g.add(std::move(poly));

    Node gv("GeometryVersion");
    gv.addI32(124);
    g.add(std::move(gv));

    if (mesh.vnorm.size() == mesh.coord.size()) g.add(normalLayer(mesh));
    const bool withUVs = mesh.texco.size() == mesh.coord.size();
    if (withUVs) g.add(uvLayer(mesh));
    g.add(materialLayer());
    g.add(layer(withUVs));
    return g;
}

Node model(int64_t id, const std::string& name) {
    Node m("Model");
    m.addI64(id);
    m.addString(objectName(name, "Model"));
    m.addString("Mesh");
    Node v("Version");
    v.addI32(232);
    m.add(std::move(v));

    // An EMPTY Properties70 makes Maya discard the model, and with it the whole
    // mesh -- silently, with nothing in its own SDK log. Proved both ways:
    // taking assimp's working file and emptying just this node broke it (0
    // meshes), and putting these three entries onto our own model fixed it
    // (1 mesh, 21,833 vertices). Blender does not care either way.
    Node props("Properties70");
    Node rotationActive = property70("RotationActive", "bool", "", "");
    rotationActive.addI32(1);
    props.add(std::move(rotationActive));
    // `DefaultAttributeIndex`, in full. I wrote `DefaultAttributeIn` for a
    // while -- 18 characters -- because I read the name out of my own debug
    // dump, which truncated strings at 18. Maya then dropped the Model, and
    // with it the entire mesh, silently. A print that elides its data is not a
    // record of that data.
    props.add(intProperty("DefaultAttributeIndex", 0));
    Node inherit = property70("InheritType", "enum", "", "");
    inherit.addI32(1);
    props.add(std::move(inherit));
    m.add(std::move(props));
    Node shading("Shading");
    shading.addBool(true);
    m.add(std::move(shading));
    Node culling("Culling");
    culling.addString("CullingOff");
    m.add(std::move(culling));
    return m;
}

/// `C` records wire objects together. Object-to-object here: the geometry hangs
/// off the model, and the model off the scene root (id 0).
Node connections(int64_t geometryId, int64_t modelId, int64_t materialId, int64_t textureId,
                 int64_t videoId) {
    Node c("Connections");
    // Model to the scene root FIRST, then the geometry onto the model, which is
    // the order both assimp and Maya write. A reader that builds the graph as
    // it goes has the parent already when the child arrives.
    Node m("C");
    m.addString("OO");
    m.addI64(modelId);
    m.addI64(0);
    c.add(std::move(m));
    Node g("C");
    g.addString("OO");
    g.addI64(geometryId);
    g.addI64(modelId);
    c.add(std::move(g));
    Node mat("C");
    mat.addString("OO");
    mat.addI64(materialId);
    mat.addI64(modelId);
    c.add(std::move(mat));

    if (textureId != 0) {
        // `OP`, object-to-PROPERTY: this is what makes the image the material's
        // DIFFUSE map rather than an unattached picture. The property name is
        // the fourth field, and Maya writes exactly this.
        Node tex("C");
        tex.addString("OP");
        tex.addI64(textureId);
        tex.addI64(materialId);
        tex.addString("DiffuseColor");
        c.add(std::move(tex));

        Node vid("C");
        vid.addString("OO");
        vid.addI64(videoId);
        vid.addI64(textureId);
        c.add(std::move(vid));
    }
    return c;
}

void appendFooter(std::vector<uint8_t>& out) {
    out.insert(out.end(), kFooterId, kFooterId + sizeof(kFooterId));
    // Pad so the trailing block -- four zeros, the version, 120 zeros, the
    // magic -- BEGINS on a 16-byte boundary. Measured from Maya's own file: its
    // four zeros sit at 38304 and 38304 % 16 == 0. Aligning the version itself
    // instead puts the whole tail four bytes out.
    while ((out.size() % 16) != 0)
        out.push_back(0U);
    Node::appendU32(out, 0U);
    Node::appendU32(out, kVersion);
    out.insert(out.end(), 120, 0U);
    out.insert(out.end(), kFooterMagic, kFooterMagic + sizeof(kFooterMagic));
}

}  // namespace

std::string FbxWriteError::message() const {
    switch (kind) {
        case FbxWriteErrorKind::CannotOpen: return "cannot open " + file;
        case FbxWriteErrorKind::EmptyMesh: return file + ": mesh has no vertices or no faces";
        case FbxWriteErrorKind::NonFiniteValue: return file + ": non-finite " + detail;
    }
    return file + ": unknown error";
}

std::expected<FbxWriteResult, FbxWriteError> writeFbx(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const FbxWriteOptions& options, const foundation::MaterialDesc* material,
    const foundation::SkinView* skin, std::span<const foundation::MorphTarget> morphTargets) {
    if (mesh.coord.empty() || mesh.index.empty()) {
        return std::unexpected(FbxWriteError{FbxWriteErrorKind::EmptyMesh, path.string(), {}});
    }
    for (const Vec3& v : mesh.coord) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
            return std::unexpected(
                FbxWriteError{FbxWriteErrorKind::NonFiniteValue, path.string(), "vertex position"});
        }
    }

    // A RenderView is a triangle list by construction, so a polygon is three
    // corners. Quads survive to the FILE only if the caller kept them, and this
    // writer does not invent them back.
    constexpr size_t kVertsPerPolygon = 3;
    if ((mesh.index.size() % kVertsPerPolygon) != 0) {
        return std::unexpected(FbxWriteError{FbxWriteErrorKind::EmptyMesh, path.string(),
                                             "index count is not a whole number of triangles"});
    }

    const Transform xf =
        meshTransform(unitScale(options.unit) * options.scale, options.feetOnGround, mesh);

    std::vector<uint8_t> out;
    out.insert(out.end(), {'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F', 'B', 'X',
                           ' ', 'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', '\0'});
    out.push_back(0x1A);
    out.push_back(0x00);
    Node::appendU32(out, kVersion);

    constexpr int64_t kGeometryId = 2'000'000;
    constexpr int64_t kMaterialId = 3'000'000;
    constexpr int64_t kModelId    = 4'000'000;
    constexpr int64_t kTextureId  = 5'000'000;
    constexpr int64_t kVideoId    = 6'000'000;

    const bool withTexture = material != nullptr && !material->diffuseTexture.empty();
    // The path as the material names it. An absolute path from the build
    // machine would be a broken link everywhere else, and FBX's two filename
    // fields exist precisely so a consumer can fall back to the relative one.
    const std::string texturePath =
        withTexture ? material->diffuseTexture.generic_string() : std::string{};
    const std::string textureName =
        withTexture ? material->diffuseTexture.stem().string() : std::string{};

    // --- the rig -------------------------------------------------------
    //
    // Ids are laid out in blocks so a joint's attribute, model and cluster are
    // derivable from its index. Blocks rather than a running counter because
    // the connection list is written after the objects and has to name the same
    // ids without carrying a second table that could disagree.
    const bool withSkin             = skin != nullptr && skin->valid();
    const size_t jointCount         = withSkin ? skin->jointCount() : 0;
    constexpr int64_t kSkinId       = 7'000'000;
    constexpr int64_t kPoseId       = 8'000'000;
    constexpr int64_t kJointModel   = 10'000'000;
    constexpr int64_t kJointAttr    = 20'000'000;
    constexpr int64_t kJointCluster = 30'000'000;
    const auto jointModelId   = [](size_t j) { return kJointModel + static_cast<int64_t>(j); };
    const auto jointAttrId    = [](size_t j) { return kJointAttr + static_cast<int64_t>(j); };
    const auto jointClusterId = [](size_t j) { return kJointCluster + static_cast<int64_t>(j); };

    // Blend shapes. A target that moves NOTHING is dropped: it would be a shape
    // a user can drag with no effect, which is worse than its absence.
    constexpr int64_t kBlendId        = 9'000'000;
    constexpr int64_t kBlendChannel   = 40'000'000;
    constexpr int64_t kBlendShapeGeom = 50'000'000;
    std::vector<size_t> movingTargets;
    for (size_t t = 0; t < morphTargets.size(); ++t) {
        const auto& deltas = morphTargets[t].deltas;
        const bool moves   = std::ranges::any_of(
            deltas, [](const Vec3& d) { return d.x != 0.0F || d.y != 0.0F || d.z != 0.0F; });
        if (moves) movingTargets.push_back(t);
    }
    const bool withMorphs = !movingTargets.empty();

    // Placed exactly as the mesh was: rotation untouched, translation through
    // the same scale and ground offset. Doing it here rather than trusting the
    // caller is what keeps the rig and the body in one space.
    const auto place = [&xf](const foundation::Mat4& m) {
        foundation::Mat4 out = m;
        out.m[0][3] *= xf.scale;
        out.m[1][3] = xf.placedY(out.m[1][3]);
        out.m[2][3] *= xf.scale;
        return out;
    };
    std::vector<foundation::Mat4> bindGlobal;
    std::vector<foundation::Mat4> nodeGlobal;
    std::vector<foundation::Mat4> nodeLocal;
    std::vector<int64_t> jointModelIds;
    if (withSkin) {
        bindGlobal.reserve(jointCount);
        nodeGlobal.reserve(jointCount);
        for (size_t j = 0; j < jointCount; ++j) {
            bindGlobal.push_back(place(skin->globalRest[j]));
            // Where the joint SITS. With a pose these are the posed globals, so
            // the consumer computes pose * inverse(bind) and reproduces our
            // skinning; with none the two coincide and the deformation is the
            // identity, which is the unposed export.
            nodeGlobal.push_back(
                place(skin->globalPose.empty() ? skin->globalRest[j] : skin->globalPose[j]));
        }
        nodeLocal.resize(jointCount);
        for (size_t j = 0; j < jointCount; ++j) {
            const int32_t parent = skin->jointParents[j];
            nodeLocal[j]         = parent < 0
                                       ? nodeGlobal[j]
                                       : foundation::rigidInverse(nodeGlobal[static_cast<size_t>(parent)]) *
                                     nodeGlobal[j];
            jointModelIds.push_back(jointModelId(j));
        }
    }

    headerExtension().writeTo(out);
    // Maya and assimp both write these three between the header extension and
    // the settings. Cheap, and the kind of thing a strict reader looks for.
    Node fileId("FileId");
    fileId.addRaw(kFileId, sizeof(kFileId));
    fileId.writeTo(out);
    Node created("CreationTime");
    created.addString(kCreationTime);
    created.writeTo(out);
    Node creator("Creator");
    creator.addString("MakeHuman C++ FBX writer");
    creator.writeTo(out);

    globalSettings(options).writeTo(out);
    documents().writeTo(out);
    Node("References").writeTo(out);
    definitions().writeTo(out);

    Node objects("Objects");
    objects.add(geometry(kGeometryId, mesh, xf, kVertsPerPolygon));
    objects.add(materialNode(kMaterialId, options.materialName, material));
    objects.add(model(kModelId, options.meshName));
    if (withTexture) {
        objects.add(textureNode(kTextureId, textureName, texturePath));
        objects.add(videoClip(kVideoId, textureName, texturePath));
    }
    if (withSkin) {
        for (size_t j = 0; j < jointCount; ++j) {
            objects.add(limbAttribute(jointAttrId(j)));
            objects.add(limbModel(jointModelId(j), skin->jointNames[j], nodeLocal[j]));
        }
        objects.add(skinDeformer(kSkinId));
        for (size_t j = 0; j < jointCount; ++j) {
            // Which vertices this joint moves, and how much. Zero weights are
            // dropped: a cluster listing every vertex at 0 is the same rig and
            // several times the file.
            std::vector<int32_t> indices;
            std::vector<double> weights;
            for (size_t v = 0; v < mesh.coord.size(); ++v) {
                for (uint8_t i = 0; i < skin->influences; ++i) {
                    const size_t at = (v * skin->influences) + i;
                    if (skin->joints[at] != j || skin->weights[at] == 0.0F) continue;
                    indices.push_back(static_cast<int32_t>(v));
                    weights.push_back(static_cast<double>(skin->weights[at]));
                }
            }
            objects.add(clusterDeformer(jointClusterId(j), skin->jointNames[j], std::move(indices),
                                        std::move(weights), bindGlobal[j]));
        }
        objects.add(bindPose(kPoseId, kModelId, jointModelIds, bindGlobal));
    }
    if (withMorphs) {
        objects.add(blendShapeDeformer(kBlendId, options.meshName));
        for (const size_t t : movingTargets) {
            objects.add(
                blendShapeChannel(kBlendChannel + static_cast<int64_t>(t), morphTargets[t].name));
            objects.add(shapeGeometry(kBlendShapeGeom + static_cast<int64_t>(t),
                                      morphTargets[t].name, morphTargets[t], xf));
        }
    }
    objects.writeTo(out);

    Node conn = connections(kGeometryId, kModelId, kMaterialId, withTexture ? kTextureId : 0,
                            withTexture ? kVideoId : 0);
    if (withSkin) {
        const auto link = [&conn](const char* kind, int64_t from, int64_t to) {
            Node c("C");
            c.addString(kind);
            c.addI64(from);
            c.addI64(to);
            conn.add(std::move(c));
        };
        // The deformer hangs off the GEOMETRY, not the model: it deforms
        // vertices, and the model is only where they are drawn.
        link("OO", kSkinId, kGeometryId);
        for (size_t j = 0; j < jointCount; ++j) {
            link("OO", jointAttrId(j), jointModelId(j));
            const int32_t parent = skin->jointParents[j];
            // A root joint parents to the SCENE, not to the mesh: parenting it
            // under the mesh model makes the whole rig inherit the mesh's
            // transform and deform twice.
            link("OO", jointModelId(j),
                 parent < 0 ? int64_t{0} : jointModelId(static_cast<size_t>(parent)));
            link("OO", jointClusterId(j), kSkinId);
            link("OO", jointModelId(j), jointClusterId(j));
        }
    }
    if (withMorphs) {
        const auto link = [&conn](int64_t from, int64_t to) {
            Node c("C");
            c.addString("OO");
            c.addI64(from);
            c.addI64(to);
            conn.add(std::move(c));
        };
        // The deformer hangs off the GEOMETRY, the channels off the deformer,
        // and each shape off its channel. Any other order and a DCC lists the
        // blend shapes with nothing behind them.
        link(kBlendId, kGeometryId);
        for (const size_t t : movingTargets) {
            const auto channel = kBlendChannel + static_cast<int64_t>(t);
            link(channel, kBlendId);
            link(kBlendShapeGeom + static_cast<int64_t>(t), channel);
        }
    }
    conn.writeTo(out);

    // The top-level list is terminated like any other child list.
    out.insert(out.end(), 25, 0U);
    appendFooter(out);

    std::ofstream f(path, std::ios::binary);
    if (!f) return std::unexpected(FbxWriteError{FbxWriteErrorKind::CannotOpen, path.string(), {}});
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) return std::unexpected(FbxWriteError{FbxWriteErrorKind::CannotOpen, path.string(), {}});

    return FbxWriteResult{.vertices = mesh.coord.size(),
                          .polygons = mesh.index.size() / kVertsPerPolygon,
                          .bytes    = out.size()};
}

}  // namespace mh::io
