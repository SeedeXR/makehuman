// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"

#include <expected>
#include <string>

namespace mh::core {

/// Mesh simplification by quadric-error-metric edge collapse.
///
/// **Written from the technique, not from anyone's code.** The quadric error
/// metric is Garland & Heckbert, "Surface Simplification Using Quadric Error
/// Metrics" (SIGGRAPH 1997): each vertex accumulates the sum of the squared-
/// distance quadrics of its incident triangle planes, an edge's cost is the
/// quadric of the pair evaluated at the position that minimises it, and edges
/// are collapsed cheapest-first. The reference has none of this -- there is no
/// simplification, LOD or corrective code anywhere in `legacy/python/` -- so
/// this is the one subsystem here with no parity fixture to answer to, and its
/// tests are invariants and measurements on meshes whose answer is known by
/// construction.
///
/// **Attributes are preserved by REFUSING collapses, not by interpolating
/// them.** A collapse is rejected when its endpoints sit on a UV seam, or when
/// they disagree about which face groups they belong to. Interpolating instead
/// would smear a texture across every seam, for the sake of a few percent more
/// reduction.
///
/// The UV refusal is load-bearing and measured: without it the base mesh's
/// texture smears, and the test that says so fails. The GROUP refusal is not
/// reachable on the base mesh at all -- **0 of its 19,158 vertices belong to
/// more than one of the 139 groups**, so every group is a separate vertex
/// island and no edge can cross one. It is kept for the meshes where groups DO
/// share vertices (an imported mesh, a garment), which is what the two-group
/// case in the test file constructs; its mutation survives, and the note there
/// records why the obvious assertion cannot see it.
///
/// **The result is a triangle mesh**, stored the way the rest of the codebase
/// stores triangles: degenerate quads with corner 0 repeated
/// (`wavefront.py:105-106`), so `vertsPerFaceForExport()` is 3 and every
/// existing writer handles it unchanged.
///
/// Skin weights are NOT carried over -- they are held outside `Mesh`, in
/// `rig::VertexWeights`, and transferring them is its own step. A decimated
/// mesh is geometry for now.
struct DecimateOptions {
    /// Fraction of the *triangulated* input's triangles to keep, in (0, 1].
    /// The base mesh's 18,486 quads triangulate to 36,972, so 0.25 asks for
    /// about 9,243.
    float ratio{0.5F};
};

enum class DecimateErrorKind {
    /// Not a fraction in (0, 1], or not a number at all.
    BadRatio,
    /// Nothing to decimate.
    NoFaces,
};

struct DecimateError {
    DecimateErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Simplifies @p src to about `ratio` of its triangles.
///
/// Deterministic: ties in the cost queue are broken by edge index, so the same
/// input always gives the same output. That is not a nicety -- an LOD chain
/// that differs between runs makes every byte-golden export test useless.
///
/// The count lands on the target or one collapse short of it, since a collapse
/// removes exactly two triangles. It can stop early: a collapse that would
/// flip a triangle, break the surface's topology, or cross an attribute
/// boundary is refused, and a mesh made mostly of such edges cannot reach an
/// arbitrary target.
[[nodiscard]] std::expected<Mesh, DecimateError> decimate(const Mesh& src, DecimateOptions options);

}  // namespace mh::core
