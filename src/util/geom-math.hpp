/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef UTIL_GEOMMATH_HPP
#define UTIL_GEOMMATH_HPP

#include <tuple>
#include <vector>

#include "glm/ext.hpp"

namespace GeomMath {

/// @brief Check if a ray intersects or is within an axis-aligned bounding box (AABB)
/// @param ray_origin ray origin.
/// @param ray_direction ray direction.
/// @param ray_t_min ray minimum distance to intersection.
/// @param ray_t_max ray maximum distance to intersection.
/// @param min_bounds AABB minimum bounds as a 3-D point.
/// @param max_bounds AABB maximum bounds as a 3-D point.
/// @return the minimum intersection time if any part of the ray is within the AABB. Infinity otherwise.
float ray_AABB_intersect(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_direction,
    const float ray_t_min,
    const float ray_t_max,
    const glm::vec3& min_bounds,
    const glm::vec3& max_bounds
);

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
std::tuple<bool, float, float, float, bool> ray_triangle_intersect(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_direction,
    const float ray_t_min,
    const float ray_t_max,
    const std::vector<glm::vec3>& vertices,
    const bool cull_back_face,
    const bool cull_front_face
);

};  // namespace GeomMath
#endif
