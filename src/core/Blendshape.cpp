// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Blendshape.h"

#include "makehuman/core/Target.h"

#include <string_view>

namespace mh::core {

namespace {

constexpr std::string_view kUnitPrefix = "expression-units-";

}  // namespace

std::vector<Blendshape> buildExpressionBlendshapes(const TargetIndex& index,
                                                   const MacroFactors& factors,
                                                   std::span<const uint32_t> vmap,
                                                   size_t meshVertexCount) {
    std::vector<Blendshape> shapes;

    // groupNames() sorts (TargetIndex.cpp:121), so the export order is stable
    // without sorting again here -- which matters because a glTF's targetNames
    // array is positional and two exports of one character should be one file.
    //
    // `scratch` is safe to reuse: expandTargetToRenderVertices assigns over it
    // rather than merging into it (Target.cpp:200).
    std::vector<foundation::Vec3> scratch;
    for (const std::string& group : index.groupNames()) {
        if (!group.starts_with(kUnitPrefix)) continue;

        Blendshape shape;
        shape.name = group.substr(kUnitPrefix.size());
        shape.deltas.assign(vmap.size(), foundation::Vec3{});

        bool any = false;
        for (const TargetComponent& c : index.group(group)) {
            const float w = targetWeight(c, factors);
            if (w == 0.0F) continue;  // an ethnicity dialled to zero costs no file read

            const auto t = loadTarget(c.path);
            if (!t) continue;  // a malformed unit drops out; the rest still export
            if (!expandTargetToRenderVertices(*t, vmap, meshVertexCount, scratch)) continue;

            for (size_t i = 0; i < shape.deltas.size(); ++i) {
                shape.deltas[i].x += w * scratch[i].x;
                shape.deltas[i].y += w * scratch[i].y;
                shape.deltas[i].z += w * scratch[i].z;
            }
            any = true;
        }
        if (any) shapes.push_back(std::move(shape));
    }
    return shapes;
}

}  // namespace mh::core
