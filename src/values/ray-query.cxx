/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
// TODO: headers here
#include <iostream>
#include <vector>

// TODO: probably want to reduce GLM files to link
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include "type.hpp"
#include "value.hpp"

export module value.rayQuery;
import value.accelerationStructure;

export class RayQuery : public Value {
private:
    bool active; // Can the ray query be traced (stepped through)?

    AccelerationStructureManager TLAS;
    unsigned rayFlags;
    unsigned cullMask;
    glm::vec4 rayOrigin;
    glm::vec4 rayDirection;
    float rayTMin;
    float rayTMax;

public:
    RayQuery(Type t) : Value(t), TLAS(t), active(false) {}
    ~RayQuery() {} // TODO

    /// @brief TODO
    /// @param as 
    /// @param rayFlags 
    /// @param cullMask 
    /// @param origin 
    /// @param direction 
    /// @param tMin 
    /// @param tMax 
    void initialize(const AccelerationStructureManager& as,
            const unsigned& rayFlags,
            const unsigned& cullMask,
            const std::vector<float>& origin,
            const std::vector<float>& direction,
            const float& tMin,
            const float& tMax) {

        assert(origin.size() == 3);
        assert(direction.size() == 3);

        TLAS = as;
        this->rayFlags = rayFlags;
        this->cullMask = cullMask;
        rayOrigin = glm::vec4(origin[0], origin[1], origin[2], 1.0f);
        rayDirection = glm::vec4(direction[0], direction[1], direction[2], 0.0f);
        rayTMin = tMin;
        rayTMax = tMax;

        active = true;
        TLAS.initStepTraceRay(rayFlags, cullMask, origin, direction, rayTMin, rayTMax, false);
    }

    /// @brief Take one step in tracing the ray where each step reaches the next geometry.
    /// @return True if there is more to trace, otherwise false.
    bool proceed() {
        if (active) {
            active = TLAS.stepTraceRay();
        }
        return active;
    }

    /// @brief Make this ray query inactive.
    void terminate() {
        active = false;
    }

    /// @brief Get the ray's t-min; ray's minimum interval.
    /// @return Ray's t-min as a 32-bit float.
    float getRayTMin() const {
        return rayTMin;
    }

    /// @brief Get the ray's flags.
    /// @return Flags as a 32-bit unsigned integer.
    unsigned getRayFlags() const {
        return rayFlags;
    }

    /// @brief Get the ray's world-space origin.
    /// @return 3-D vector of 32-bit floats.
    std::vector<float> getWorldRayOrigin() const {
        return std::vector<float>{ rayOrigin.x, rayOrigin.y, rayOrigin.z };
    }

    /// @brief Get the ray's world-space origin.
    /// @return 3-D vector of 32-bit floats.
    glm::vec3 getWorldRayOriginGLM() const {
        return glm::vec3(rayOrigin);
    }

    /// @brief Get the ray's world-space direction.
    /// @return 3-D vector of 32-bit floats.
    std::vector<float> getWorldRayDirection() const {
        return std::vector<float>{ rayDirection.x, rayDirection.y, rayDirection.z };
    }

    /// @brief Get the ray's world-space direction.
    /// @return 3-D vector of 32-bit floats.
    glm::vec3 getWorldRayDirectionGLM() const {
        return glm::vec3(rayDirection);
    }

    /// @brief Get the committed or candidate intersection type
    /// @param intersection 0:candidate, 1:committed
    /// @return Enum represented as a 32-bit scalar integer
    unsigned getIntersectionType(unsigned intersection) const {
        throw std::runtime_error("getIntersectionType() has not been implemented yet.");
    }
};
