/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm>
#include <limits>
#include <tuple>
#include <vector>

#include <glm/ext.hpp>

export module util.intersection;

/// @brief Adapted algorithm from "An Efficient and Robust Ray–Box Intersection Algorithm" by Amy Williams et al.,
/// 2004. Check if a ray intersects an axis-aligned bounding box (AABB). If the ray is inside the box, it will be
/// considered an intersection.
/// @param ray_origin ray origin.
/// @param ray_direction ray direction.
/// @param ray_t_min ray minimum distance to intersection.
/// @param ray_t_max ray maximum distance to intersection.
/// @param min_bounds AABB minimum bounds as a 3-D point.
/// @param max_bounds AABB maximum bounds as a 3-D point.
/// @return whether the ray intersected an AABB or is inside of it.
export bool ray_AABB_intersect(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_direction,
    const float ray_t_min,
    const float ray_t_max,
    const glm::vec3& min_bounds,
    const glm::vec3& max_bounds
) {
    // Check if the ray if inside of the AABB; it is considered inside if right at the surface.
    bool inside_aabb = ray_origin.x >= min_bounds.x && ray_origin.y >= min_bounds.y &&
                       ray_origin.z >= min_bounds.z && ray_origin.x <= max_bounds.x &&
                       ray_origin.y <= max_bounds.y && ray_origin.z <= max_bounds.z;
    if (inside_aabb)
        return true;

    // Otherwise, check if the ray intersects the surface of the AABB from the outside.
    // Get the distances to the yz-plane intersections.
    float t_min, t_max;
    const float x_dir_reciprocal = 1.0f / ray_direction.x;
    if (ray_direction.x >= 0) {
        t_min = (min_bounds.x - ray_origin.x) * x_dir_reciprocal;
        t_max = (max_bounds.x - ray_origin.x) * x_dir_reciprocal;
    } else {
        t_min = (max_bounds.x - ray_origin.x) * x_dir_reciprocal;
        t_max = (min_bounds.x - ray_origin.x) * x_dir_reciprocal;
    }

    // Get the distances to the xz-plane intersections.
    float ty_min, ty_max;
    const float y_dir_reciprocal = 1.0f / ray_direction.y;
    if (ray_direction.y >= 0) {
        ty_min = (min_bounds.y - ray_origin.y) * y_dir_reciprocal;
        ty_max = (max_bounds.y - ray_origin.y) * y_dir_reciprocal;
    } else {
        ty_min = (max_bounds.y - ray_origin.y) * y_dir_reciprocal;
        ty_max = (min_bounds.y - ray_origin.y) * y_dir_reciprocal;
    }

    // Check if the ray missed the box.
    // If the closest plane intersection is farther than the farthest xz-plane intersection, then the ray missed.
    // If the closest xz-plane intersection is farther than the farthest plane intersection, then the ray missed.
    if ((t_min > ty_max) || (ty_min > t_max))
        return false;

    // Get the larger of the minimums; the larger minimum is closer to the box.
    // Get the smaller of the maximums; the smaller maximum is closer to the box.
    t_min = std::max(t_min, ty_min);
    t_max = std::min(t_max, ty_max);

    // Get the distances to the xy-plane intersections.
    float tz_min, tz_max;
    const float z_dir_reciprocal = 1.0f / ray_direction.z;
    if (ray_direction.z >= 0) {
        tz_min = (min_bounds.z - ray_origin.z) * z_dir_reciprocal;
        tz_max = (max_bounds.z - ray_origin.z) * z_dir_reciprocal;
    } else {
        tz_min = (max_bounds.z - ray_origin.z) * z_dir_reciprocal;
        tz_max = (min_bounds.z - ray_origin.z) * z_dir_reciprocal;
    }

    // Check if the ray missed the box.
    // If the closest plane intersection is farther than the farthest xy-plane intersection, then the ray missed.
    // If the closest xy-plane intersection is farther than the farthest plane intersection, then the ray missed.
    if ((t_min > tz_max) || (tz_min > t_max))
        return false;

    // Get the larger of the minimums; the larger minimum is closer to the box.
    // Get the smaller of the maximums; the smaller maximum is closer to the box.
    t_min = std::max(t_min, tz_min);
    t_max = std::min(t_max, tz_max);

    // Check if the intersection is within the ray's interval.
    return ((t_min < ray_t_max) && (t_max > ray_t_min));
}

/// @brief Moller-Trumbore ray/triangle intersection algorithm. Check if a ray intersects a triangle.
/// @param ray_origin ray origin.
/// @param ray_direction ray direction.
/// @param ray_t_min ray minimum distance to intersection.
/// @param ray_t_max ray maximum distance to intersection.
/// @param vertices triangle's vertices.
/// @param cull_back_face whether to cull to back face of the triangle.
/// @param cull_front_face whether to cull the front face of the triangle.
/// @return tuple containing: (1) whether the triangle was intersected, (2) distance to intersection, (3)
/// barycentric u, (4) barycentric v, and (5) whether the ray entered the through the triangle's front face.
export std::tuple<bool, float, float, float, bool> ray_triangle_intersect(
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
    if (cull_back_face_and_entered_back || cull_front_face_and_entered_front || ray_parallel_to_triangle)
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
