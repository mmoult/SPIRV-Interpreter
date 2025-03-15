/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_RAYTRACE_TRACE_HPP
#define VALUES_RAYTRACE_TRACE_HPP

#include <limits>

#include "glm/ext.hpp"

#include "../value.hpp"
#include "node.hpp"
import spv.rayFlags;

struct Trace;

struct Intersection {
    enum class Type { None, Triangle, Generated, AABB };
    Type type = Type::None;
    const Node* search;

    // Ray properties which get transformed by InstanceNodes
    // Both should start as the identity matrix
    glm::mat4 worldToObj = glm::mat4(1.0);
    glm::mat4 objToWorld = glm::mat4(1.0);

    const InstanceNode* instance = nullptr;  // Most recent instance intersected
    int geometryIndex = -1;
    int primitiveIndex = -1;
    float hitT = std::numeric_limits<float>::max();
    glm::vec2 barycentrics = glm::vec2(0.0f, 0.0f);
    bool isOpaque = true;
    bool enteredTriangleFrontFace = false;
    unsigned hitKind = std::numeric_limits<unsigned>::max();

    glm::vec3 getRayPos(const Trace* trace) const;
    glm::vec3 getRayDir(const Trace* trace) const;

    Intersection(const Node* search): search(search) {}
};

struct Trace {
    bool active = false;
    std::vector<Intersection> candidates;
    unsigned candidate;  // the next candidate to consider
    unsigned committed;  // index of best intersection so far

    // Ray properties
    RayFlags rayFlags = RayFlags(0);
    unsigned cullMask;
    float rayTMin = 0.0f;
    float rayTMax = 0.0f;
    glm::vec3 rayOrigin {0.0f, 0.0f, 0.0f};
    glm::vec3 rayDirection {0.0f, 0.0f, 0.0f};

    // shader binding table info
    bool useSBT;
    unsigned offsetSBT;
    unsigned strideSBT;
    unsigned missIndex;

    inline Intersection& getCandidate() {
        if (candidate >= candidates.size())
            throw std::runtime_error("Attempt to fetch candidate outside of valid range!");
        return candidates[candidate];
    }
    inline const Intersection& getCandidate() const {
        if (candidate >= candidates.size())
            throw std::runtime_error("Attempt to fetch candidate outside of valid range!");
        return candidates[candidate];
    }

    inline bool hasCommitted() const {
        return committed < candidates.size();
    }

    inline Intersection& getCommitted() {
        if (committed >= candidates.size())
            throw std::runtime_error("Attempt to fetch committed outside of valid range!");
        return candidates[committed];
    }
    inline const Intersection& getCommitted() const {
        if (committed >= candidates.size())
            throw std::runtime_error("Attempt to fetch committed outside of valid range!");
        return candidates[committed];
    }
};

#endif
