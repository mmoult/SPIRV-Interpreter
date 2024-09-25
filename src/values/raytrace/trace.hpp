/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_RAYTRACE_TRACE_HPP
#define VALUES_RAYTRACE_TRACE_HPP

#include <limits>

#include <glm/ext.hpp>

#include "../value.hpp"
#include "node.hpp"
import spv.rayFlags;

struct Intersection {
    enum class Type { None, Triangle, Generated, AABB };
    Type type = Type::None;
    const Node* search;

    // Probably put ray properties which get transformed by InstanceNodes here
    glm::vec4 rayOrigin {0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 rayDirection {0.0f, 0.0f, 0.0f, 0.0f};

    const InstanceNode* instance = nullptr;  // Instance the intersection occured in
    int geometryIndex = -1;
    int primitiveIndex = -1;
    float hitT = std::numeric_limits<float>::max();
    glm::vec2 barycentrics = glm::vec2(0.0f, 0.0f);
    bool isOpaque = true;
    bool enteredTriangleFrontFace = false;
    unsigned hitKind = std::numeric_limits<unsigned>::max();
    const Value* hitAttribute = nullptr;
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

    // shader binding table info
    bool useSBT;
    unsigned offsetSBT;
    unsigned strideSBT;
    unsigned missIndex;

    inline Intersection& getCandidate() {
        return candidates[candidate];
    }
    inline const Intersection& getCandidate() const {
        return candidates[candidate];
    }

    inline Intersection& getCommitted() {
        return candidates[committed];
    }
    inline const Intersection& getCommitted() const {
        return candidates[committed];
    }
};

#endif
