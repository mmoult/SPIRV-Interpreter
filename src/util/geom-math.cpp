/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "geom-math.hpp"

#include <algorithm>
#include <limits>

namespace GeomMath {

float ray_AABB_intersect(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_direction,
    const float ray_t_min,
    const float ray_t_max,
    const glm::vec3& min_bounds,
    const glm::vec3& max_bounds
) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    assert(ray_t_min <= ray_t_max);

    float t_min = ray_t_min;
    float t_max = ray_t_max;
    // Generate values of t for which the ray is within each axis of the box
    for (unsigned i = 0; i < 3; ++i) {
        // If this component is 0, the reciprocal will be infinite
        float dir_recip = 1.0 / ray_direction[i];

        float lo_plane_t = (min_bounds[i] - ray_origin[i]) * dir_recip;
        float hi_plane_t = (max_bounds[i] - ray_origin[i]) * dir_recip;

        bool pos_dir = ray_direction[i] >= 0.0;
        t_min = std::max(t_min, pos_dir ? lo_plane_t : hi_plane_t);
        t_max = std::min(t_max, pos_dir ? hi_plane_t : lo_plane_t);

        if (t_min > t_max || std::isnan(lo_plane_t) || std::isnan(hi_plane_t))
            return inf;
    }

    return t_min;
}

std::tuple<bool, float, float, float, bool> ray_triangle_intersect(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_direction,
    const float ray_t_min,
    const float ray_t_max,
    const std::vector<glm::vec3>& vertices,
    const bool cull_back_face,
    const bool cull_front_face
) {
    // Immediately return if culling both faces
    if (cull_back_face && cull_front_face)
        return {false, 0.0f, 0.0f, 0.0f, false};

    constexpr float epsilon = std::numeric_limits<float>::epsilon();

    // Find vectors for 2 edges that share a vertex.
    // Vertex at index 0 will be the shared vertex.
    glm::vec3 edge_1 = vertices[1] - vertices[0];
    glm::vec3 edge_2 = vertices[2] - vertices[0];

    glm::vec3 pvec = glm::cross(ray_direction, edge_2);

    // If positive determinant, then the ray hit the front face.
    // If negative determinant, then the ray hit the back face.
    // If determinant is close to zero, then the ray missed the triangle.
    float determinant = glm::dot(edge_1, pvec);
    const bool intersect_front = determinant >= epsilon;

    const bool cull_back_face_and_entered_back = cull_back_face && determinant <= -epsilon;
    const bool cull_front_face_and_entered_front = cull_front_face && intersect_front;
    const bool ray_parallel_to_triangle = std::fabs(determinant) < epsilon;
    if (std::isnan(determinant) || cull_back_face_and_entered_back || cull_front_face_and_entered_front ||
        ray_parallel_to_triangle)
        return {false, 0.0f, 0.0f, 0.0f, intersect_front};

    float inverse_determinant = 1.0f / determinant;

    glm::vec3 tvec = ray_origin - vertices[0];
    float u = glm::dot(tvec, pvec) * inverse_determinant;
    if (u < 0 || u > 1)
        return {false, 0.0f, u, 0.0f, intersect_front};

    glm::vec3 qvec = glm::cross(tvec, edge_1);
    float v = glm::dot(ray_direction, qvec) * inverse_determinant;
    if (v < 0 || u + v > 1)
        return {false, 0.0f, u, v, intersect_front};

    float t = glm::dot(edge_2, qvec) * inverse_determinant;
    if (t < ray_t_min || t > ray_t_max)
        return {false, t, u, v, intersect_front};

    return {true, t, u, v, intersect_front};
}

};  // namespace GeomMath