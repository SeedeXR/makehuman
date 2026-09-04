// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"
#include "makehuman/core/Types.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mh::core {

/// What a proxy replaces or adds. legacy/python/shared/proxy.py:56-57.
enum class ProxyType : uint8_t {
    Proxymeshes,  ///< an alternate body topology, replacing the base mesh
    Clothes,
    Hair,
    Eyes,
    Eyebrows,
    Eyelashes,
    Teeth,
    Tongue,
};

/// The 3x3 transform applied to a proxy vertex's design-time offset.
///
/// It rescales offsets as the body changes proportion: each axis is the ratio
/// of a measured span on the *current* body to the span the asset was authored
/// against (proxy.py:900-918). Without it, clothes would keep a fixed
/// thickness as the character grows.
struct TMatrix {
    struct Scale {
        uint32_t v1{}, v2{};
        float den{1.0F};
    };

    /// One entry per axis; absent axes stay identity.
    std::array<std::optional<Scale>, 3> scale{};

    [[nodiscard]] bool isIdentity() const noexcept { return !scale[0] && !scale[1] && !scale[2]; }

    /// Evaluates against the body's current vertex positions. Returns the
    /// diagonal, since only the scale form is used by the shipped assets.
    [[nodiscard]] Vec3 diagonal(std::span<const Vec3> humanCoords) const;
};

/// A mesh fitted to the body by barycentric reference.
///
/// Each proxy vertex is bound to a triangle of base-mesh vertices plus an
/// offset (proxy.py:719-740):
///
///     P_i = SUM_k w_ik * H[v_ik]  +  M * d_i
///
/// The single-index form binds exactly to one base vertex with weights
/// (1,0,0) and a zero offset (`fromSingle`, :710-717).
struct Proxy {
    std::string name;
    std::string uuid;
    std::string description;
    std::string basemesh{"hm08"};
    ProxyType type{ProxyType::Clothes};
    int32_t version{110};
    std::vector<std::string> tags;

    std::filesystem::path objFile;
    std::filesystem::path materialFile;

    int32_t zDepth{50};    ///< render order; -1 in file means 50 (proxy.py:535-537)
    uint32_t maxPole{16};  ///< default 8, doubled on load (proxy.py:385, :540)

    /// Per proxy vertex: the three base vertices, their weights, and the offset.
    std::vector<std::array<uint32_t, 3>> refVerts;
    std::vector<std::array<float, 3>> weights;
    std::vector<Vec3> offsets;

    /// True when every vertex uses the single-index (exact) form, in which case
    /// offsets are all zero. The compiled `.mhpxy` stores this compactly.
    bool exactFitOnly{false};

    /// Base-mesh vertices this proxy hides, sized to the base mesh.
    std::vector<uint8_t> deleteVerts;

    TMatrix tmatrix;

    [[nodiscard]] size_t vertexCount() const noexcept { return refVerts.size(); }

    /// Largest base-mesh index referenced, for validating against a body mesh.
    [[nodiscard]] uint32_t maxRefIndex() const noexcept { return maxRefIndex_; }

    uint32_t maxRefIndex_{};
};

enum class ProxyErrorKind {
    NotFound,
    Unreadable,
    MalformedLine,
    IndexOutOfRange,
    /// A key the reference honours and this parser does not implement. Refused
    /// rather than skipped: an ignored transform fits the proxy wrongly and
    /// says nothing, which is the worst of the three outcomes.
    Unsupported,
};

struct ProxyError {
    ProxyErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Parses a `.mhclo` / `.proxy` file (proxy.py:376-543).
[[nodiscard]] std::expected<Proxy, ProxyError> loadProxy(const std::filesystem::path& path);

/// Evaluates fitted positions for every proxy vertex against @p humanCoords.
///
/// @param out resized to the proxy's vertex count.
/// @return false if the proxy references a vertex the body does not have.
bool fitProxy(const Proxy& proxy, std::span<const Vec3> humanCoords, std::vector<Vec3>& out);

/// Accumulates the base-mesh vertices hidden by a set of worn proxies.
///
/// Mirrors `updateFaceMasks` (3_libraries_clothes_chooser.py:101-143): start
/// with everything visible, then clear a bit for every vertex any proxy
/// declares in its `delete_verts` block. Feed the result to
/// `Mesh::faceMaskForVisibleVertices` and then `RenderMesh::setFaceMask`.
///
/// The reference walks the proxies in reverse render order. That ordering is
/// only load-bearing for the *proxy-on-proxy* masking it also does on the same
/// pass (`transferVertexMaskToProxy`, :131-134), where a lower layer sees the
/// mask accumulated by the layers above it. For the body mask alone the result
/// is a plain union and order cannot matter, so none is imposed here. Clothes
/// hiding other clothes is a separate feature and is not implemented yet.
///
/// Note: **no shipped asset exercises this.** All four shipped
/// `.mhclo`/`.proxy` files declare zero `delete_verts`, so the code path is
/// covered by synthetic masks only (tests/golden/mask).
///
/// @param bodyVertexCount vertices in the body mesh the proxies are fitted to.
/// @return one byte per body vertex, nonzero = visible.
[[nodiscard]] std::vector<uint8_t> visibleVertexMask(std::span<const Proxy* const> proxies,
                                                     size_t bodyVertexCount);

/// Remaps a BASE-mesh vertex mask onto @p proxy through the proxy's own fit.
///
/// A port of `transferVertexMaskToProxy` (shared/proxy.py:960-983). Two rules,
/// and they are deliberately not the same rule:
///
///   * a proxy vertex fitted to ONE base vertex -- `weights[1]` and `weights[2]`
///     both zero -- copies that vertex's visibility;
///   * an interpolated one is hidden only when at least **two** of its three
///     references are hidden (`< 2` visible in the reference's arithmetic).
///
/// The second is the one that matters. The natural guess -- hide as soon as any
/// reference is hidden -- erodes a much wider band around every hole than the
/// reference produces.
///
/// A reference index past the end of @p baseVisible counts as visible: a proxy
/// fitted to a different base mesh would otherwise read out of bounds, and
/// showing a vertex that should be hidden is a cosmetic bug where reading past
/// the array is not one we get to observe.
///
/// @param baseVisible one byte per BASE vertex, nonzero = visible.
/// @return one byte per PROXY vertex, nonzero = visible.
[[nodiscard]] std::vector<uint8_t> transferVertexMaskToProxy(std::span<const uint8_t> baseVisible,
                                                             const Proxy& proxy);

/// Every worn proxy's vertex mask, plus the body's, in one ordered pass.
struct WornMasks {
    /// Parallel to the `worn` span the caller passed, whatever order that was
    /// in -- the render order is used internally and not imposed on the caller.
    std::vector<std::vector<uint8_t>> perProxy;
    /// The same union `visibleVertexMask` returns.
    std::vector<uint8_t> body;
};

/// Clothes hiding clothes, not just clothes hiding body.
///
/// `updateFaceMasks` (3_libraries_clothes_chooser.py:101-143) walks the stack
/// **outermost first** -- `reversed(sorted by z_depth)` -- handing each garment
/// the mask accumulated by the layers above it and only then folding in its own
/// `delete_verts`. So a garment is masked by what is over it and never by
/// itself, which is why the order is the feature rather than a detail.
///
/// Ties on `z_depth` fall to the uuid, as the reference's `(z_depth, uuid)`
/// sort does, so the answer does not depend on the order the caller collected
/// the proxies in.
///
/// The body half is a plain union and order cannot affect it; `body` therefore
/// equals `visibleVertexMask(worn, bodyVertexCount)` and is asserted to.
///
/// **No shipped asset exercises this**: all four shipped `.mhclo`/`.proxy`
/// files declare zero `delete_verts`, so the coverage is synthetic.
[[nodiscard]] WornMasks wornVertexMasks(std::span<const Proxy* const> worn, size_t bodyVertexCount);

/// The body's drawn-and-exported face mask: group visibility AND everything the
/// worn proxies delete.
///
/// This is the single answer to "which body faces exist", and it exists because
/// the two halves were previously answered in different places or not at all:
/// `Mesh::staticFaceMask` was applied to the render mesh only, so OBJ export
/// shipped the helper cages, and `visibleVertexMask` had no caller anywhere.
///
/// @param base  the mesh the proxies were fitted against; `delete_verts` index
///              into it, so the mask can only be computed here.
/// @param shown what is actually drawn: @p base itself, or exactly one
///              Catmull-Clark level of it. A subdivided face mask is the parent
///              mask expanded 4:1, because child face `f*4+k` comes from parent
///              face `f` and inherits its group (Subdivider.cpp:258-277).
/// @return one byte per face of @p shown, nonzero = visible.
[[nodiscard]] std::expected<std::vector<uint8_t>, MeshError> bodyFaceMask(
    const Mesh& base, const Mesh& shown, std::span<const Proxy* const> worn);

}  // namespace mh::core
