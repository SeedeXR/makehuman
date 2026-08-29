// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"
#include "makehuman/core/Types.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mh::core {

/// A sparse morph: a list of vertex indices and the offset each one moves by.
///
/// This is MakeHuman's entire modelling primitive. Every slider in the
/// application resolves to a weighted sum of these
/// (legacy/python/core/algos3d.py:67).
///
/// Indices are uint32 here. The reference's compiled form quantises them to
/// **uint16** (`algos3d.py:220`), capping any mesh at 65,536 vertices — a
/// needless ceiling that is not reproduced.
struct Target {
    std::string name;
    std::vector<uint32_t> verts;
    std::vector<Vec3> offsets;

    [[nodiscard]] size_t size() const noexcept { return verts.size(); }

    [[nodiscard]] bool empty() const noexcept { return verts.empty(); }

    /// Largest vertex index referenced, or 0 when empty. Lets a caller check a
    /// target against a mesh once rather than per application.
    uint32_t maxVertexIndex{};
};

enum class TargetErrorKind {
    NotFound,
    Unreadable,
    MalformedLine,  ///< four fields present but not parseable as index + xyz
};

struct TargetError {
    TargetErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Parses a `.target` file.
///
/// Format (legacy/python/core/algos3d.py:126-147): one vertex per line,
/// `index dx dy dz`, with `#` comments. Values use forms like `-.001`.
///
/// The reference silently skips any line that does not split into exactly four
/// fields (`:136-138`). That is safe here — unlike an OBJ vertex line, each
/// entry carries its own explicit index, so dropping one shifts nothing — and
/// real files rely on it. Skipped lines are counted in @p skippedLines rather
/// than discarded silently, so a caller can notice a malformed file.
[[nodiscard]] std::expected<Target, TargetError> loadTarget(const std::filesystem::path& path,
                                                            uint32_t* skippedLines = nullptr);

/// Applies @p target to @p mesh:
///
///     coord[verts[i]] += offsets[i] * (scale * factor)
///
/// Additive and incremental, exactly as the reference (`algos3d.py:268,284`).
/// The mesh holds no notion of a target's "current" value, so a caller either
/// replays the whole stack from the morph base or applies the delta between an
/// old and new weight.
///
/// Indices at or beyond the mesh's vertex count are skipped; the reference
/// indexes unguarded and would read out of bounds.
///
/// @return the number of vertices actually moved.
uint32_t applyTarget(const Target& target, Mesh& mesh, float factor,
                     Vec3 scale = Vec3{1.0F, 1.0F, 1.0F});

/// Loads targets on demand and keeps them for the session.
///
/// The reference caches in an unbounded module-global dict keyed by canonical
/// path (`algos3d.py:64`, `:296-336`) and never evicts. Same policy — 1,280
/// targets is a bounded, known set — but owned rather than global.
class TargetLibrary {
public:
    explicit TargetLibrary(std::filesystem::path root) : root_(std::move(root)) {}

    /// Loads @p relativePath (relative to the root) or returns the cached copy.
    [[nodiscard]] std::expected<const Target*, TargetError> get(const std::string& relativePath);

    [[nodiscard]] size_t cachedCount() const noexcept { return cache_.size(); }

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    void clear() { cache_.clear(); }

private:
    std::filesystem::path root_;
    std::unordered_map<std::string, Target> cache_;
};

}  // namespace mh::core
