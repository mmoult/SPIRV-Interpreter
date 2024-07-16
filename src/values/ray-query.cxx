/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <vector>

#include <glm/mat4x3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "type.hpp"
#include "value.hpp"

export module value.rayQuery;
import value.accelerationStructure;

export class RayQuery : public Value {
private:
    bool active;  // Whether the ray query can be traced (stepped through)

    AccelerationStructureManager tlas;
    unsigned rayFlags;
    unsigned cullMask;
    glm::vec4 rayOrigin;  // World-space
    glm::vec4 rayDirection;  // World-space
    float rayTMin;
    float rayTMax;

public:
    RayQuery(Type t): Value(t), tlas(t), active(false) {}

    /// @brief Must be called before tracing a ray in a ray query.
    /// Initializes the ray query with an acceleration structure and ray information.
    /// @param as Acceleration structure to query.
    /// @param ray_flags Ray flags.
    /// @param cull_mask Ray cull mask.
    /// @param origin World-space ray origin.
    /// @param direction World-space ray direction.
    /// @param t_min Ray interval lower-bound.
    /// @param t_max Ray interval upper-bound.
    void initialize(const AccelerationStructureManager& as,
            const unsigned ray_flags,
            const unsigned cull_mask,
            const std::vector<float>& origin,
            const std::vector<float>& direction,
            const float t_min,
            const float t_max) {

        assert(origin.size() == 3);
        assert(direction.size() == 3);

        tlas = as;
        this->rayFlags = ray_flags;
        this->cullMask = cull_mask;
        rayOrigin = glm::vec4(origin[0], origin[1], origin[2], 1.0f);
        rayDirection = glm::vec4(direction[0], direction[1], direction[2], 0.0f);
        rayTMin = t_min;
        rayTMax = t_max;

        active = true;
        tlas.initStepTraceRay(rayFlags, cullMask, origin, direction, rayTMin, rayTMax, false);
    }

    /// @brief Take one step in tracing the ray where each step reaches the next geometry.
    /// @return True if there is more to trace, otherwise false.
    bool proceed() {
        if (active) {
            active = tlas.stepTraceRay();
        }
        return active;
    }

    /// @brief Make this ray query inactive.
    void terminate() {
        active = false;
    }

    /// @brief Generate and commit an intersection at <hit_t>. Only works if the candidate
    /// intersection type is AABB.
    /// @param hit_t Distance from ray to intersection to consider in determining the closest hit.
    void generateIntersection(float hit_t) {
        tlas.generateIntersection(hit_t);
    }

    /// @brief Commit current candidate triangle intersection to be considered in determining
    /// the closest hit. Only works if the candidate intersection type is triangle.
    void confirmIntersection() {
        tlas.confirmIntersection();
    }

    /// @brief Get the ray's t-min; ray's minimum interval.
    /// @return Ray's t-min.
    float getRayTMin() const {
        return rayTMin;
    }

    /// @brief Get the ray's flags.
    /// @return Ray flags.
    unsigned getRayFlags() const {
        return rayFlags;
    }

    /// @brief Get the ray's world-space origin.
    /// @return World-space ray origin.
    std::vector<float> getWorldRayOrigin() const {
        return std::vector<float> {rayOrigin.x, rayOrigin.y, rayOrigin.z};
    }

    /// @brief Get the ray's world-space origin.
    /// @return World-space ray origin.
    glm::vec3 getWorldRayOriginGLM() const {
        return glm::vec3(rayOrigin);
    }

    /// @brief Get the ray's world-space direction.
    /// @return World-space ray direction.
    std::vector<float> getWorldRayDirection() const {
        return std::vector<float> {rayDirection.x, rayDirection.y, rayDirection.z};
    }

    /// @brief Get the ray's world-space direction.
    /// @return World-space ray direction.
    glm::vec3 getWorldRayDirectionGLM() const {
        return glm::vec3(rayDirection);
    }

    /// @brief Get the committed or candidate intersection type.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Intersection type.
    unsigned getIntersectionType(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionType(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate ray to intersection distance.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Distance to the intersection.
    float getIntersectionT(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionT(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection instance custom index.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Instance custom index.
    int getIntersectionInstanceCustomIndex(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionInstanceCustomIndex(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection instance id.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Instance id.
    int getIntersectionInstanceId(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionInstanceId(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection instance shader binding table (SBT) record offset.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Instance SBT record offset.
    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionInstanceShaderBindingTableRecordOffset(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection geometry index.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Geometry index.
    int getIntersectionGeometryIndex(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionGeometryIndex(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection primitive index.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Primitive index.
    int getIntersectionPrimitiveIndex(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionPrimitiveIndex(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection barycentrics.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Barycentric coordinates.
    glm::vec2 getIntersectionBarycentricsGLM(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionBarycentrics(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection barycentrics.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Barycentric coordinates.
    std::vector<float> getIntersectionBarycentrics(const unsigned intersection) const {
        glm::vec2 barycentrics = getIntersectionBarycentricsGLM(intersection);
        return std::vector<float> {barycentrics.x, barycentrics.y};
    }

    /// @brief Get the committed or candidate intersection front face status.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Whether the intersection went through the front face of a primitive.
    bool getIntersectionFrontFace(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionFrontFace(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection opaque AABB status.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Whether the intersection went through an opaque AABB/procedural primitive.
    bool getIntersectionCandidateAABBOpaque() const {
        return tlas.getIntersectionCandidateAABBOpaque();
    }

    /// @brief Get the committed or candidate intersection object-space ray direction.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Object-space ray direction.
    glm::vec3 getIntersectionObjectRayDirectionGLM(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionObjectRayDirection(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection object-space ray direction.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Object-space ray direction.
    std::vector<float> getIntersectionObjectRayDirection(const unsigned intersection) const {
        glm::vec3 object_ray_dir = getIntersectionObjectRayDirectionGLM(intersection);
        return std::vector<float> {object_ray_dir.x, object_ray_dir.y, object_ray_dir.z};
    }

    /// @brief Get the committed or candidate intersection object-space ray origin.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Object-space ray origin.
    glm::vec3 getIntersectionObjectRayOriginGLM(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionObjectRayOrigin(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection object-space ray origin.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return Object-space ray origin.
    std::vector<float> getIntersectionObjectRayOrigin(const unsigned intersection) const {
        glm::vec3 object_ray_origin = getIntersectionObjectRayOriginGLM(intersection);
        return std::vector<float> {object_ray_origin.x, object_ray_origin.y, object_ray_origin.z};
    }

    /// @brief Get the committed or candidate intersection 3x4 object-to-world matrix.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return 4x3 world-to-object column-major order matrix.
    glm::mat4x3 getIntersectionObjectToWorldGLM(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionObjectToWorld(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection 3x4 object-to-world matrix.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return 4x3 world-to-object column-major order matrix.
    std::vector<std::vector<float>> getIntersectionObjectToWorld(const unsigned intersection) const {
        glm::mat4x3 object_to_world = getIntersectionObjectToWorldGLM(intersection);

        std::vector<std::vector<float>> result;
        for (unsigned col = 0; col < 4; ++col) {
            result.push_back(
                    std::vector<float> {object_to_world[col].x, object_to_world[col].y, object_to_world[col].z});
        }

        return result;
    }

    /// @brief Get the committed or candidate intersection 3x4 world-to-object matrix.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return 4x3 world-to-object column-major order matrix.
    glm::mat4x3 getIntersectionWorldToObjectGLM(const unsigned intersection) const {
        assert(intersection == 0 || intersection == 1);
        return tlas.getIntersectionWorldToObject(static_cast<bool>(intersection));
    }

    /// @brief Get the committed or candidate intersection 3x4 world-to-object matrix.
    /// @param intersection Type of intersection: 0-candidate, 1-committed.
    /// @return 4x3 world-to-object column-major order matrix.
    std::vector<std::vector<float>> getIntersectionWorldToObject(const unsigned intersection) const {
        glm::mat4x3 world_to_object = getIntersectionWorldToObjectGLM(intersection);

        std::vector<std::vector<float>> result;
        for (unsigned col = 0; col < 4; ++col) {
            result.push_back(
                    std::vector<float> {world_to_object[col].x, world_to_object[col].y, world_to_object[col].z});
        }

        return result;
    }
};
