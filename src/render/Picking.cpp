// SPDX-License-Identifier: Apache-2.0

#include "makehuman/render/Picking.h"

#include <QMatrix4x4>

#include <cmath>
#include <limits>

namespace mh::render {
namespace {

constexpr float kPi = 3.14159265358979323846F;

foundation::Vec3 normalized(const foundation::Vec3& v) {
    const float n = std::sqrt(foundation::dot(v, v));
    return n > 0.0F ? v * (1.0F / n) : v;
}

foundation::Vec3 toVec3(const QVector3D& v) {
    return {v.x(), v.y(), v.z()};
}

}  // namespace

Ray rayThroughPixel(const Camera& camera, int px, int py, int width, int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    // Qt's y grows down; the world's grows up.
    const float ndcX = (2.0F * static_cast<float>(px) / w) - 1.0F;
    const float ndcY = 1.0F - (2.0F * static_cast<float>(py) / h);

    const float tanHalf = std::tan(camera.fovY * 0.5F * kPi / 180.0F);
    const float aspect  = w / h;

    // Eye space: the camera sits at the origin looking down -Z.
    const QVector3D dirEye(ndcX * aspect * tanHalf, ndcY * tanHalf, -1.0F);
    // ... and the view translation puts the model `distance` in front of it,
    // offset by the pan. Inverting that places the eye in model space.
    QMatrix4x4 view;
    view.translate(camera.panX, camera.panY, -camera.distance);
    const QMatrix4x4 inverse = (view * modelMatrixOf(camera)).inverted();

    return Ray{toVec3(inverse.map(QVector3D(0.0F, 0.0F, 0.0F))),
               normalized(toVec3(inverse.mapVector(dirEye)))};
}

std::optional<foundation::Vec3> intersect(const foundation::RenderView& mesh, const Ray& ray) {
    // Below this a triangle is edge-on to the ray and the barycentric division
    // is meaningless, not merely imprecise.
    constexpr float kParallel = 1e-8F;

    float nearest = std::numeric_limits<float>::max();
    bool found    = false;

    for (size_t i = 0; i + 2 < mesh.index.size(); i += 3) {
        const foundation::Vec3& a = mesh.coord[mesh.index[i]];
        const foundation::Vec3& b = mesh.coord[mesh.index[i + 1]];
        const foundation::Vec3& c = mesh.coord[mesh.index[i + 2]];

        const foundation::Vec3 ab = b - a;
        const foundation::Vec3 ac = c - a;
        const foundation::Vec3 p  = cross(ray.direction, ac);
        const float det           = foundation::dot(ab, p);
        if (std::abs(det) < kParallel) continue;  // parallel, either facing

        const float invDet       = 1.0F / det;
        const foundation::Vec3 t = ray.origin - a;
        const float u            = foundation::dot(t, p) * invDet;
        if (u < 0.0F || u > 1.0F) continue;

        const foundation::Vec3 q = cross(t, ab);
        const float v            = foundation::dot(ray.direction, q) * invDet;
        if (v < 0.0F || u + v > 1.0F) continue;

        const float distance = foundation::dot(ac, q) * invDet;
        // Behind the eye is not in front of it.
        if (distance <= 0.0F || distance >= nearest) continue;
        nearest = distance;
        found   = true;
    }

    if (!found) return std::nullopt;
    return ray.origin + (ray.direction * nearest);
}

void focusOn(Camera& camera, const foundation::Vec3& pointInMesh) {
    // Pan is applied AFTER the model rotation, so it cancels the rotated
    // point's screen-plane offset -- not the unrotated one.
    const QVector3D rotated =
        modelMatrixOf(camera).map(QVector3D(pointInMesh.x, pointInMesh.y, pointInMesh.z));
    camera.panX = -rotated.x();
    camera.panY = -rotated.y();
}

}  // namespace mh::render
