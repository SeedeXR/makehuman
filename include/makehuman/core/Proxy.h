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

/// What a proxy replaces or adds. legacy-python/shared/proxy.py:56-57.
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

enum class ProxyErrorKind { NotFound, Unreadable, MalformedLine, IndexOutOfRange };

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

}  // namespace mh::core
