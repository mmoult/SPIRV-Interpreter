/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stack>
#include <string>
#include <tuple>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>

// TODO it would be best for values/ to never import spv/, but here it is inconvenient not to use the flags which SPIR-V
// set up. In the future, remove this dependency by defining a new enum here which SPIR-V instructions can convert from
// as needed.
//#include "../../external/spirv.hpp"
#include "../type.hpp"
#include "../value.hpp"
#include "node.hpp"
export module value.raytrace.accelStruct;
import spv.rayFlags;
import util.string;
import value.aggregate;
import value.primitive;
import value.raytrace.shaderBindingTable;
import value.raytrace.trace;
import value.statics;
import value.string;

export class AccelStruct : public Value {
private:
    std::vector<Node*> bvh;
    ShaderBindingTable shaderBindingTable;

    NodeReference tlas = NodeReference(0, 0);
    // Necessary for reconstructing an externally-viewable struct
    unsigned boxIndex = 0;
    unsigned instanceIndex = 0;
    unsigned triangleIndex = 0;
    unsigned proceduralIndex = 0;

    inline static const std::vector<std::string> names {
        "tlas", "box_nodes", "instance_nodes", "triangle_nodes", "procedural_nodes", "shader_binding_table"
    };

    Trace trace;

private:
    // TODO: handle the effects of winding order on intersections; currently, front face is CCW

    /*
    enum class NodeType { Box, Instance, Triangle, Procedural };

    struct Node {
        virtual std::shared_ptr<Node> clone() const = 0;
        virtual NodeType type() const = 0;
    };

    struct BoxNode : public Node {
        const glm::vec4 minBounds;
        const glm::vec4 maxBounds;
        const std::vector<std::shared_ptr<Node>> children;

        NodeType type() const {
            return NodeType::Box;
        }
    };

    struct InstanceNode : public Node {
        const glm::mat4x3 objectToWorld;  // Column-major order
        const glm::mat4x3 worldToObject;  // Column-major order
        const unsigned id;  // Id relative to other instance nodes in the same acceleration structure
        const unsigned customIndex;  // For shading
        const unsigned mask;  // Mask that can make the ray ignore this instance
        const unsigned sbtRecordOffset;  // Shader binding table record offset (a.k.a. hit group id)
        const std::shared_ptr<AccelerationStructure> accelStruct;

        NodeType type() const {
            return NodeType::Instance;
        }
    };

    struct TriangleNode : public Node {
        const unsigned geometryIndex;  // Geometry this node is a part of
        const unsigned primitiveIndex;  // Index of node in geometry
        const bool opaque;  // Whether this triangle is opaque
        const std::vector<glm::vec3> vertices;

        NodeType type() const {
            return NodeType::Triangle;
        }
    };

    struct ProceduralNode : public Node {
        const unsigned geometryIndex;  // Geometry this node is a part of
        const unsigned primitiveIndex;  // Index of node in geometry
        const bool opaque;  // Whether this procedural is opaque
        const glm::vec4 minBounds;
        const glm::vec4 maxBounds;

        NodeType type() const {
            return NodeType::Procedural;
        }
    };
    */

    /*
    // Intersection related
    struct IntersectionProperties {
        std::shared_ptr<InstanceNode> instance = nullptr;  // Instance the intersection occured in
        int geometryIndex = -1;
        int primitiveIndex = -1;
        float hitT = std::numeric_limits<float>::max();
        glm::vec2 barycentrics = glm::vec2(0.0f, 0.0f);
        bool isOpaque = true;
        bool enteredTriangleFrontFace = false;
        unsigned hitKind = std::numeric_limits<unsigned>::max();
        const Value* hitAttribute = nullptr;
    };

    struct CandidateIntersection {
        CandidateIntersectionType type = CandidateIntersectionType::Triangle;
        IntersectionProperties properties {};

        /// @brief Update the candidate to the most recent intersection.
        /// @param is_triangle whether this is a triangle intersection.
        /// @param new_properties new candidate's properties.
        void update(const bool is_triangle, const IntersectionProperties& new_properties) {
            type = is_triangle ? CandidateIntersectionType::Triangle : CandidateIntersectionType::AABB;
            properties = new_properties;
        }
    };
    struct CommittedIntersection {
        CommittedIntersectionType type = CommittedIntersectionType::None;

        /// @brief Update the committed to match the given candidate.
        /// @param is_triangle whether this is a triangle intersection.
        /// @param candidate_intersection candidate to commit.
        void update(const bool is_triangle, const CandidateIntersection& candidate_intersection) {
            type = is_triangle ? CommittedIntersectionType::Triangle : CommittedIntersectionType::Generated;
            properties = candidate_intersection.properties;
        }
    };

    CommittedIntersection committedIntersection;  // Current committed intersection from stepping the ray
    CandidateIntersection candidateIntersection;  // Current candidate intersection from stepping the ray
    */

    /*
    bool didPopNodePreviously = true;  // Used in the stepTrace() method
    bool intersectedProcedural = false;  // Used in the stepTrace() method
    */

    /*
    /// @brief Get populated shader inputs for the next shader in the ray tracing pipeline.
    /// @param shader provides the inputs to populate.
    /// @param get_committed whether to get the inputs of a candidate or committed intersection.
    /// @param payload used to populate a shader input.
    /// @return populated inputs that the respective shader can use.
    ValueMap getNewShaderInputs(const Program& shader, bool get_committed, const Value* payload) const {
        ValueMap result = shader.getInputs();

        // Handle built-ins
        for (const auto& [name, built_in] : shader.getBuiltIns()) {
            switch (built_in) {
            default:
                break;
            case spv::BuiltInObjectRayOriginKHR:  // 5323
            case spv::BuiltInObjectRayDirectionKHR:  // 5324
            case spv::BuiltIn::BuiltInObjectToWorldKHR:
            case spv::BuiltIn::BuiltInWorldToObjectKHR:
            case spv::BuiltIn::BuiltInHitKindKHR:
            case spv::BuiltIn::BuiltInIncomingRayFlagsKHR:
            case spv::BuiltIn::BuiltInRayGeometryIndexKHR:
                throw std::runtime_error("Unimplemented builtin!");
                break;
            case spv::BuiltIn::BuiltInWorldRayOriginKHR: {  // 5321
                Array* new_val = new Array(Type::primitive(DataType::FLOAT), 3);
                Primitive x(rayOrigin.x);
                Primitive y(rayOrigin.y);
                Primitive z(rayOrigin.z);
                std::vector<const Value*> world_ray_origin;
                world_ray_origin.push_back(&x);
                world_ray_origin.push_back(&y);
                world_ray_origin.push_back(&z);
                new_val->addElements(world_ray_origin);
                result[name] = new_val;
                break;
            }
            case spv::BuiltIn::BuiltInWorldRayDirectionKHR: {  // 5322
                Array* new_val = new Array(Type::primitive(DataType::FLOAT), 3);
                Primitive x(rayDirection.x);
                Primitive y(rayDirection.y);
                Primitive z(rayDirection.z);
                std::vector<const Value*> world_ray_direction;
                world_ray_direction.push_back(&x);
                world_ray_direction.push_back(&y);
                world_ray_direction.push_back(&z);
                new_val->addElements(world_ray_direction);
                result[name] = new_val;
                break;
            }
            case spv::BuiltIn::BuiltInRayTminKHR:  // 5325
                result[name] = new Primitive(rayTMin);
                break;
            case spv::BuiltIn::BuiltInRayTmaxKHR:  // 5326
                result[name] = new Primitive(rayTMax);
                break;
            case spv::BuiltIn::BuiltInInstanceCustomIndexKHR:  // 5327
                result[name] = new Primitive(getIntersectionInstanceCustomIndex(get_committed));
                break;
            }
        }

        // Handle storage classes
        for (const auto& [name, storage_class] : shader.getStorageClasses()) {
            switch (storage_class) {
            default:
                break;
            case spv::StorageClass::StorageClassCallableDataKHR: {  // 5328
                throw std::runtime_error("StorageClassCallableDataKHR not implemented!");
            }
            case spv::StorageClass::StorageClassIncomingCallableDataKHR: {  // 5329
                throw std::runtime_error("StorageClassIncomingCallableDataKHR not implemented!");
            }
            case spv::StorageClass::StorageClassRayPayloadKHR: {  // 5338
                // TODO: probably not necessary to handle this case, but will leave this comment
                // in the case this storage class could be the problem.
                break;
            }
            case spv::StorageClass::StorageClassHitAttributeKHR: {  // 5339
                if (committedIntersection.properties.hitAttribute != nullptr) {
                    const Value* hit = committedIntersection.properties.hitAttribute;
                    assert(hit->getType().getBase() == DataType::ARRAY);
                    const Array& hit_array = static_cast<const Array&>(*hit);
                    for (unsigned a = 0; a < hit_array.getSize(); ++a)
                        const Primitive& val = static_cast<const Primitive&>(*(hit_array[a]));
                    result[name] = committedIntersection.properties.hitAttribute;
                }
                break;
            }
            case spv::StorageClass::StorageClassIncomingRayPayloadKHR: {  // 5342
                if (payload == nullptr)
                    throw std::runtime_error("Trying to fill an incoming payload but it doesn't exist!");
                if (!(payload->getType().sameBase((result[name])->getType()))) {
                    std::stringstream err;
                    err << "Trying to fill an incoming payload but the input and payload types do not match (<payload "
                           "type> != <input type>): ";
                    err << payload->getType().getBase() << " != " << (result[name])->getType().getBase();
                    throw std::runtime_error(err.str());
                }
                Value* incoming_payload = payload->getType().construct();
                incoming_payload->copyFrom(*payload);
                result[name] = incoming_payload;
                break;
            }
            }
            // Note: case shader record buffer is handled in shader binding table execution.
        }

        // Any acceleration structure input is handled in SBT class when executing.
        // Same goes for any data tied to a shader record (shader record buffer).

        return result;
    }
    */

public:
    AccelStruct(): Value(Type::accelStruct()) {}

    /// @brief Initialize the trace.
    /// @param ray_flags ray flags.
    /// @param cull_mask cull mask; culls respective instances.
    /// @param ray_origin ray origin.
    /// @param ray_direction ray direction.
    /// @param ray_t_min closest a ray will allow for an intersection.
    /// @param ray_t_max farthest a ray will allow for an intersection.
    /// @param use_sbt whether to use the shader binding table when tracing.
    /// @param offset_sbt shader binding table offset.
    /// @param stride_sbt shader binding table stride.
    /// @param miss_index shader binding table miss index.
    void initTrace(
        const unsigned ray_flags,
        const unsigned cull_mask,
        const std::vector<float>& ray_origin,
        const std::vector<float>& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const bool use_sbt,
        const unsigned offset_sbt = 0,
        const unsigned stride_sbt = 0,
        const unsigned miss_index = 0
    ) {
        trace.rayFlags = RayFlags(ray_flags);
        trace.cullMask = cull_mask;
        trace.rayTMin = ray_t_min;
        trace.rayTMax = ray_t_max;

        trace.useSBT = use_sbt;
        trace.offsetSBT = offset_sbt;
        trace.strideSBT = stride_sbt;
        trace.missIndex = miss_index;

        trace.candidate = 0;
        trace.committed = std::numeric_limits<unsigned>::max();
        trace.active = true;

        // Start the candidates fresh with the root node in the bvh
        trace.candidates.resize(1);
        auto candidate = trace.candidates[0];
        assert(ray_origin.size() == 3 && ray_direction.size() == 3);
        candidate.rayOrigin = glm::vec4(ray_origin[0], ray_origin[1], ray_origin[2], 1.0);
        candidate.rayDirection = glm::vec4(ray_direction[0], ray_direction[1], ray_direction[2], 0.0);
    }

    /// @brief Take a step in the trace. Each step reaches the next non-instance primitive that was intersected.
    /// @return if a triangle or procedural was intersected; implies if there is more to trace.
    bool stepTrace() {
        // Do not trace if the trace is inactive.
        if (!trace.active)
            return false;

        // Traverse the acceleration structure until it reaches the next non-instance primitive.
        bool found_primitive = false;
        for (; trace.candidate < trace.candidates.size(); ++trace.candidate) {

        }
        /*
        while (!found_primitive && (nodesToEval.size() > 0)) {

            const auto* curr_node_ref = nodesToEval.top();
            Node* curr_node = curr_node_ref->get();
            nodesToEval.pop();

            switch (curr_node->type()) {
            default: {
                std::stringstream err;
                err << "Found unknown node type (" << static_cast<int>(curr_node->type())
                    << ") in \"traceRay()\" method of class "
                       "\"AccelerationStructure\" in \"acceleration-structure.cxx\"";
                throw std::runtime_error(err.str());
            }
            case NodeType::Box: {
                BoxNode* box_node = static_cast<BoxNode*>(curr_node);
                const bool result = rayAABBIntersect(
                    rayOrigin,
                    rayDirection,
                    rayTMin,
                    rayTMax,
                    box_node->minBounds,
                    box_node->maxBounds
                );
                // If the ray intersects the bounding box, then add its children to be evaluated.
                if (result)
                    for (const auto& child : box_node->children)
                        nodesToEval.push(&child);
                break;
            }
            case NodeType::Instance: {
                InstanceNode* instance_node = static_cast<InstanceNode*>(curr_node);

                // Do not process this instance if it's invisible to the ray.
                if ((instance_node->mask & cullMask) == 0)
                    break;

                // Transform the ray to match the instance's object-space.
                glm::vec3 object_ray_origin = instance_node->worldToObject * rayOrigin;
                glm::vec3 object_ray_direction = instance_node->worldToObject * rayDirection;

                // TODO: the ray interval might need to be scaled (t-min and t-max) if any problems arise in the future.
                const auto& ref_accel_struct = instance_node->accelStruct;

                // Trace the ray in the respective acceleration structure.
                // Do not pop the node if we can still step through the instance node's acceleration structure.
                if (didPopNodePreviously) {
                    // If we did pop the previous node, then this is the first time we are stepping through the instance
                    // node's acceleration structure.
                    std::vector<float> ray_origin {object_ray_origin[0], object_ray_origin[1], object_ray_origin[2]};
                    std::vector<float> ray_direction {
                        object_ray_direction[0], object_ray_direction[1], object_ray_direction[2]
                    };
                    ref_accel_struct->initTrace(
                        rayFlags,
                        cullMask,
                        ray_origin,
                        ray_direction,
                        rayTMin,
                        rayTMax,
                        useSBT,
                        offsetSBT,
                        strideSBT,
                        missIndex
                    );
                }
                found_primitive = ref_accel_struct->stepTrace();
                didPopNodePreviously = !found_primitive;

                // Handle the result of tracing the ray in the instance.
                if (found_primitive) {
                    // Can try the instance's acceleration structure again
                    nodesToEval.push(curr_node_ref);

                    // Update candidate
                    candidateIntersection = ref_accel_struct->candidateIntersection;
                    candidateIntersection.properties.instance = std::static_pointer_cast<InstanceNode>(*curr_node_ref);

                    // Run the intersection shader if the intersection was with a procedural node.
                    // Running the shader here because we need the instance SBT offset for calculating the index.
                    if (useSBT && (candidateIntersection.type == CandidateIntersectionType::AABB)) {
                        intersectedProcedural = true;
                        const int geometry_index = getIntersectionGeometryIndex(false);
                        const unsigned instance_sbt_offset =
                            getIntersectionInstanceShaderBindingTableRecordOffset(false);
                        //
                        const Program* shader = shaderBindingTable.getHitShader(
                            offsetSBT,
                            strideSBT,
                            geometry_index,
                            instance_sbt_offset,
                            HitGroupType::Intersection
                        );
                        if (shader != nullptr) {
                            ValueMap inputs = getNewShaderInputs(*shader, true, nullptr);
                            SBTShaderOutput outputs = shaderBindingTable.executeHit(
                                inputs,
                                offsetSBT,
                                strideSBT,
                                geometry_index,
                                instance_sbt_offset,
                                HitGroupType::Intersection
                            );

                            // If it fails the intersection, then cancel it.
                            // Note: variable <intersectedProcedural> will be modified when invoking intersection and
                            // any-hit shaders.
                            const bool missed = !intersectedProcedural;
                            if (missed) {
                                found_primitive = false;
                                candidateIntersection.update(false, IntersectionProperties {});
                                break;
                            }

                            // Otherwise, store the hit attribute for later use.
                            for (const auto& [name, info] : outputs) {
                                const auto value = get<0>(info);
                                const auto storage_class = get<1>(info);
                                if (storage_class == spv::StorageClass::StorageClassHitAttributeKHR) {
                                    Value* hit_attribute = value->getType().construct();
                                    hit_attribute->copyFrom(*value);
                                    candidateIntersection.properties.hitAttribute = hit_attribute;
                                }
                            }
                        }
                        //
                    }

                    // Terminate on the first hit if the flag was risen
                    if (rayFlags.terminateOnFirstHit()) {
                        trace.active = false;
                        return true;
                    }
                }

                break;
            }
            case NodeType::Triangle: {
                // Check skip triangle ray flag.
                if (rayFlags.skipTriangles())
                    break;

                TriangleNode* triangle_node = static_cast<TriangleNode*>(curr_node);

                // Check opaque related ray flags.
                bool is_opaque = triangle_node->opaque;
                if (rayFlags.opaque())
                    is_opaque = true;
                else if (rayFlags.noOpaque())
                    is_opaque = false;

                if ((rayFlags.cullOpaque() && is_opaque) || (rayFlags.cullNoOpaque() && !is_opaque))
                    break;

                // Check if the ray intersects the triangle
                std::tuple<bool, float, float, float, bool> result = rayTriangleIntersect(
                    rayOrigin,
                    rayDirection,
                    rayTMin,
                    rayTMax,
                    triangle_node->vertices,
                    rayFlags.cullBackFacingTriangles(),
                    rayFlags.cullFrontFacingTriangles()
                );

                found_primitive = get<0>(result);

                if (found_primitive) {
                    // Get triangle intersection data
                    float t = get<1>(result);  // Distance to intersection
                    float u = get<2>(result);  // Barycentric coordinate u
                    float v = get<3>(result);  // Barycentric coordinate v
                    bool entered_front = get<4>(result);  // Interested a triangle from the front

                    // Update candidate
                    IntersectionProperties properties;
                    properties.hitT = t;
                    properties.barycentrics = glm::vec2(u, v);
                    properties.isOpaque = triangle_node->opaque;
                    properties.enteredTriangleFrontFace = entered_front;
                    properties.geometryIndex = triangle_node->geometryIndex;
                    properties.primitiveIndex = triangle_node->primitiveIndex;
                    properties.hitKind =
                        entered_front ? HIT_KIND_FRONT_FACING_TRIANGLE_KHR : HIT_KIND_BACK_FACING_TRIANGLE_KHR;
                    candidateIntersection.update(true, properties);

                    // Terminate on the first hit if the flag was risen
                    if (rayFlags.terminateOnFirstHit()) {
                        trace.active = false;
                        return true;
                    }
                }

                break;
            }
            case NodeType::Procedural: {
                // Check skip AABBs (procedurals) flag
                if (rayFlags.skipAABBs())
                    break;

                ProceduralNode* procedural_node = static_cast<ProceduralNode*>(curr_node);

                // Check opaque related ray flags.
                bool is_opaque = procedural_node->opaque;
                if (rayFlags.opaque())
                    is_opaque = true;
                else if (rayFlags.noOpaque())
                    is_opaque = false;

                if ((rayFlags.cullOpaque() && is_opaque) || (rayFlags.cullNoOpaque() && !is_opaque))
                    break;

                found_primitive = rayAABBIntersect(
                    rayOrigin,
                    rayDirection,
                    rayTMin,
                    rayTMax,
                    procedural_node->minBounds,
                    procedural_node->maxBounds
                );

                if (found_primitive) {
                    // Update candidate
                    IntersectionProperties properties;
                    properties.isOpaque = procedural_node->opaque;
                    properties.geometryIndex = procedural_node->geometryIndex;
                    properties.primitiveIndex = procedural_node->primitiveIndex;
                    candidateIntersection.update(false, properties);
                    assert(candidateIntersection.type == CandidateIntersectionType::AABB);

                    // Cannot terminate early if ray flag "terminate on first hit" was raised because we do not know if
                    // the ray actually intersected the primitive.
                }
                break;
            }
            }
        }
        */

        // Make sure to deactivate the trace if there is no more to traverse
        // TODO should deactivate when end of list reached
        if (trace.candidates.empty())
            trace.active = false;

        return found_primitive;
    }

    /// @brief Completely trace through the acceleration structure.
    /// @param ray_flags ray flags.
    /// @param cull_mask cull mask; culls respective instances.
    /// @param ray_origin ray origin.
    /// @param ray_direction ray direction.
    /// @param ray_t_min closest a ray will allow for an intersection.
    /// @param ray_t_max farthest a ray will allow for an intersection.
    /// @param use_sbt whether to use the shader binding table when tracing.
    /// @param offset_sbt shader binding table offset.
    /// @param stride_sbt shader binding table stride.
    /// @param miss_index shader binding table miss index.
    /// @return whether the trace intersected a geometry.
    void traceRay(
        const unsigned ray_flags,
        const unsigned cull_mask,
        const std::vector<float>& ray_origin,
        const std::vector<float>& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const unsigned sbt_offset,
        const unsigned sbt_stride,
        const unsigned miss_index,
        Value* payload
    ) {
        initTrace(
            ray_flags,
            cull_mask,
            ray_origin,
            ray_direction,
            ray_t_min,
            ray_t_max,
            true,
            sbt_offset,
            sbt_stride,
            miss_index
        );

        bool intersect_once = false;
        bool found_primitive = false;
        do {
            found_primitive = stepTrace();
            if (found_primitive)
                intersect_once = true;

            if (found_primitive) {
                if (trace.candidates[trace.candidate].intersectionType == Intersection::Type::Triangle)
                    confirmIntersection();
                else  // ... == Intersection::Type::AABB
                    generateIntersection(getIntersectionT(false));
            }
        } while (found_primitive);

        // Do not invoke any shaders if a shader binding table was not specified
        bool used_sbt = false;
        if (trace.useSBT) {
            // Otherwise, invoke either the closest hit or miss shaders
            //SBTShaderOutput outputs;
            if (trace.candidates[trace.committed].intersectionType != Intersection::Type::None) {
                // Closest hit
                if (trace.rayFlags.skipClosestHitShader())
                    return;
                const int geometry_index = getIntersectionGeometryIndex(true);
                const unsigned instance_sbt_offset = getIntersectionInstanceShaderBindingTableRecordOffset(true);
                /*
                const Program* shader = shaderBindingTable.getHitShader(
                    sbt_offset, sbt_stride, geometry_index, instance_sbt_offset, HitGroupType::Closest
                );
                if (shader != nullptr) {
                    used_sbt = true;
                    ValueMap inputs = getNewShaderInputs(*shader, true, payload);
                    outputs = shaderBindingTable.executeHit(
                        inputs,
                        sbt_offset,
                        sbt_stride,
                        geometry_index,
                        instance_sbt_offset,
                        HitGroupType::Closest
                    );
                }
                */
            } else {
                // Miss
                /*
                const Program* shader = shaderBindingTable.getMissShader(miss_index);
                if (shader != nullptr) {
                    used_sbt = true;
                    ValueMap inputs = getNewShaderInputs(*shader, true, payload);
                    outputs = shaderBindingTable.executeMiss(inputs, miss_index);
                }
                */
            }

            /*if (used_sbt) {
                // Get the payload from the output
                const Value* incoming_payload = nullptr;
                for (const auto& [name, info] : outputs) {
                    const auto value = get<0>(info);
                    const auto storage_class = get<1>(info);
                    if (storage_class == spv::StorageClass::StorageClassIncomingRayPayloadKHR)
                        incoming_payload = value;
                }
                if (incoming_payload == nullptr)
                    throw std::runtime_error("Could not find the payload from a closest-hit / miss shader!");

                // Update the payload
                if (!(payload->getType().sameBase(incoming_payload->getType())))
                    throw std::runtime_error("Incoming payload type does not match payload type!");
                payload->copyFrom(*incoming_payload);
                assert(payload->equals(*incoming_payload));
            }*/
        }

        // If the expected shader was missing from the SBT or if we shouldn't use the SBT, fill in default
        if (!used_sbt) {
            std::stack<Value*> frontier;
            frontier.push(payload);

            while (!frontier.empty()) {
                Value* curr = frontier.top();
                frontier.pop();

                switch (curr->getType().getBase()) {
                default: {
                    std::stringstream err;
                    err << "Cannot fill data of unsupported payload type: " << curr->getType().getBase();
                    throw std::runtime_error(err.str());
                }
                case DataType::FLOAT: {
                    Primitive& val = static_cast<Primitive&>(*curr);
                    val.copyFrom(Primitive(static_cast<float>(intersect_once)));
                    break;
                }
                case DataType::UINT: {
                    Primitive& val = static_cast<Primitive&>(*curr);
                    val.copyFrom(Primitive(static_cast<unsigned>(intersect_once)));
                    break;
                }
                case DataType::INT: {
                    Primitive& val = static_cast<Primitive&>(*curr);
                    val.copyFrom(Primitive(static_cast<int>(intersect_once)));
                    break;
                }
                case DataType::BOOL: {
                    Primitive& val = static_cast<Primitive&>(*curr);
                    val.copyFrom(Primitive(intersect_once));
                    break;
                }
                case DataType::ARRAY:
                case DataType::STRUCT: {
                    for (auto it : static_cast<const Aggregate&>(*curr))
                        frontier.push(it);
                    break;
                }
                }
            }
        }
    }

    /// @brief Check whether some hit distance is within the ray's interval.
    /// @param t_hit distance from the ray to the intersection.
    /// @return whether the hit is within the ray's interval.
    bool isIntersectionValid(const float t_hit) {
        return (t_hit >= trace.rayTMin) && (t_hit <= trace.rayTMax);
    }

    /// @brief Invokes the any-hit shader.
    /// @param t_hit distance from the ray to the intersection.
    /// @param hit_kind object kind that was intersected.
    /// @return whether to accept the intersection.
    bool invokeAnyHitShader(const float t_hit, const unsigned hit_kind) {
        const int geometry_index = getIntersectionGeometryIndex(false);
        const unsigned instance_sbt_offset = getIntersectionInstanceShaderBindingTableRecordOffset(false);
        bool ignore_intersection = false;
        /*
        const Program* shader = shaderBindingTable.getHitShader(
            offsetSBT, strideSBT, geometry_index, instance_sbt_offset, HitGroupType::Any
        );
        if (shader != nullptr) {
            ValueMap inputs = getNewShaderInputs(*shader, true, nullptr);
            SBTShaderOutput outputs = shaderBindingTable.executeHit(
                inputs,
                offsetSBT,
                strideSBT,
                geometry_index,
                instance_sbt_offset,
                HitGroupType::Any,
                static_cast<void*>(&ignore_intersection)
            );
        }
        */

        if (ignore_intersection) {
            //intersectedProcedural = false;
            return false;
        }

        // Otherwise, update respective properties
        /*
        candidateIntersection.properties.hitT = t_hit;
        candidateIntersection.properties.hitKind = hit_kind;
        */

        return true;
    }

    /// @brief Include the current AABB/procedural intersection in determining the closest hit.
    /// The candidate intersection must be of type AABB.
    /// @param t_hit distance from the ray to the intersection.
    void generateIntersection(float t_hit) {
/*
        assert(candidateIntersection.type == CandidateIntersectionType::AABB);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (t_hit >= committedIntersection.properties.hitT)
            return;

        rayTMax = t_hit;
        candidateIntersection.properties.hitT = t_hit;
        committedIntersection.update(false, candidateIntersection);
*/
    }

    /// @brief Include the current triangle intersection in determining the closest hit.
    /// The candidate intersection must be of type triangle.
    void confirmIntersection() {
/*
        assert(candidateIntersection.type == CandidateIntersectionType::Triangle);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (candidateIntersection.properties.hitT >= committedIntersection.properties.hitT)
            return;

        rayTMax = candidateIntersection.properties.hitT;
        committedIntersection.update(true, candidateIntersection);
*/
    }

    /// @brief Get the distance from the ray to the current intersection.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return distance between the ray and intersection.
    float getIntersectionT(bool get_committed) const {
        //return get_committed ? committedIntersection.properties.hitT : candidateIntersection.properties.hitT;
        return 0.0;
    }

    /// @brief Get the current intersection instance's custom index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return custom index if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceCustomIndex(bool get_committed) const {
/*
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;

        if (committed_instance == nullptr && candidate_instance == nullptr)
            return -1;

        return get_committed ? committed_instance->customIndex : candidate_instance->customIndex;
*/
        return -1;
    }

    /// @brief Get the current intersection instance's id.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return id if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceId(bool get_committed) const {
        /*
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;

        if (committed_instance == nullptr && candidate_instance == nullptr)
            return -1;

        return get_committed ? committed_instance->id : candidate_instance->id;
        */
        return -1;
    }

    /// @brief Get the current intersection instance's shader binding table record offset.
    /// The instance must exist; not be null.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return SBT record offset
    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(bool get_committed) const {
        /*
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->sbtRecordOffset : candidate_instance->sbtRecordOffset;
        */
        return 0;
    }

    /// @brief Get the current intersection's geometry index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return geometry index.
    int getIntersectionGeometryIndex(bool get_committed) const {
        /*
        return get_committed ? committedIntersection.properties.geometryIndex
                             : candidateIntersection.properties.geometryIndex;
        */
        return -1;
    }

    /// @brief Get the current intersection's primitive index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return primitive index.
    int getIntersectionPrimitiveIndex(bool get_committed) const {
        /*
        return get_committed ? committedIntersection.properties.primitiveIndex
                             : candidateIntersection.properties.primitiveIndex;
        */
        return -1;
    }

    /// @brief Get the current intersection's barycentric coordinates.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return barycentrics.
    glm::vec2 getIntersectionBarycentrics(bool get_committed) const {
        /*
        return get_committed ? committedIntersection.properties.barycentrics
                             : candidateIntersection.properties.barycentrics;
        */
        return glm::vec2(0.0);
    }

    /// @brief Get whether the ray entered the front face of a triangle.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return whether the intersection was entered from the front face of a triangle.
    bool getIntersectionFrontFace(bool get_committed) const {
        /*
        return get_committed ? committedIntersection.type == CommittedIntersectionType::Triangle &&
                                   committedIntersection.properties.enteredTriangleFrontFace
                             : candidateIntersection.type == CandidateIntersectionType::Triangle &&
                                   candidateIntersection.properties.enteredTriangleFrontFace;
        */
        return false;
    }

    /// @brief Get whether the intersection is an opaque procedural.
    /// @return whether the intersection was an opaque procedural.
    bool getIntersectionCandidateAABBOpaque() const {
        /*
        return candidateIntersection.type == CandidateIntersectionType::AABB &&
               candidateIntersection.properties.isOpaque;
        */
        return false;
    }

    /// @brief Get the object-space ray direction depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray direction.
    glm::vec3 getIntersectionObjectRayDirection(bool get_committed) const {
        /*
        return get_committed ? (committedIntersection.properties.instance->worldToObject * rayDirection)
                             : (candidateIntersection.properties.instance->worldToObject * rayDirection);
        */
        return glm::vec3(0.0);
    }

    /// @brief Get the object-space ray origin depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray origin.
    glm::vec3 getIntersectionObjectRayOrigin(bool get_committed) const {
        /*
        return get_committed ? (committedIntersection.properties.instance->worldToObject * rayOrigin)
                             : (candidateIntersection.properties.instance->worldToObject * rayOrigin);
        */
        return glm::vec3(0.0);
    }

    /// @brief Get the object-to-world matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-to-world matrix.
    glm::mat4x3 getIntersectionObjectToWorld(bool get_committed) const {
        /*
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->objectToWorld : candidate_instance->objectToWorld;
        */
        return glm::mat4x3(0.0);
    }

    /// @brief Get the world-to-object matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return world-to-object matrix.
    glm::mat4x3 getIntersectionWorldToObject(bool get_committed) const {
        /*
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->worldToObject : candidate_instance->worldToObject;
        */
        return glm::mat4x3(0.0);
    }

    /// @brief Get the intersection type.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return intersection type.
    unsigned getIntersectionType(bool get_committed) const {
        /*
        return get_committed ? static_cast<unsigned>(committedIntersection.type)
                             : static_cast<unsigned>(candidateIntersection.type);
        */
        return 0;
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<Value*> fields(6, nullptr);
        fields[0] = tlas.toArray();
        fields[5] = shaderBindingTable.toStruct();

        // Have to fill in the fields of the node types, where necessary
        if (boxIndex > 0) {
            std::vector<Value*> nodes;
            for (unsigned i = 0; i < boxIndex; ++i)
                nodes.push_back(bvh[i]->toStruct());
            fields[1] = new Array(nodes);
        } else {
            fields[1] = new Array(BoxNode::getType(), 0);
        }
        if (instanceIndex > boxIndex) {
            std::vector<Value*> nodes;
            for (unsigned i = boxIndex; i < instanceIndex; ++i)
                nodes.push_back(bvh[i]->toStruct());
            fields[2] = new Array(nodes);
        } else {
            fields[2] = new Array(InstanceNode::getType(), 0);
        }
        if (triangleIndex > instanceIndex) {
            std::vector<Value*> nodes;
            for (unsigned i = instanceIndex; i < triangleIndex; ++i)
                nodes.push_back(bvh[i]->toStruct());
            fields[3] = new Array(nodes);
        } else {
            fields[3] = new Array(TriangleNode::getType(), 0);
        }
        if (proceduralIndex > triangleIndex) {
            std::vector<Value*> nodes;
            for (unsigned i = triangleIndex; i < proceduralIndex; ++i)
                nodes.push_back(bvh[i]->toStruct());
            fields[4] = new Array(nodes);
        } else {
            fields[4] = new Array(ProceduralNode::getType(), 0);
        }
        return new Struct(fields, names);
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Construct the acceleration structures and shader binding table based on the type of "other"
        const Type& from_type = new_val.getType();
        if (from_type.getBase() == DataType::ACCEL_STRUCT) {
            // TODO this is a difficult case for memory management. Return to this later.
            *this = static_cast<const AccelStruct&>(new_val);
            return;
        }
        if (from_type.getBase() != DataType::STRUCT)
            throw std::runtime_error("Cannot copy acceleration structure from non-structure type!");

        const Struct& other = Statics::extractStruct(&new_val, "acceleration structure", names);

        // tlas: uvec2
        std::vector<unsigned> tlas_got = Statics::extractUvec(other[0], names[0], 2);
        tlas = NodeReference(tlas_got[0], tlas_got[1]);

        // Clear any nodes previously held
        for (unsigned i = 0; i < bvh.size(); ++i)
            delete bvh[i];
        bvh.clear();

        // box_nodes
        const Array& box_nodes = Statics::extractArray(other[1], names[1]);
        for (unsigned i = 0; i < box_nodes.getSize(); ++i)
            bvh.push_back(BoxNode::fromVal(box_nodes[i]));
        boxIndex = bvh.size();

        // instance_nodes
        const Array& instance_nodes = Statics::extractArray(other[2], names[2]);
        for (unsigned i = 0; i < instance_nodes.getSize(); ++i)
            bvh.push_back(InstanceNode::fromVal(instance_nodes[i]));
        instanceIndex = bvh.size();

        // triangle_nodes
        const Array& triangle_nodes = Statics::extractArray(other[3], names[3]);
        for (unsigned i = 0; i < triangle_nodes.getSize(); ++i)
            bvh.push_back(TriangleNode::fromVal(triangle_nodes[i]));
        triangleIndex = bvh.size();

        // procedural_nodes
        const Array& procedural_nodes = Statics::extractArray(other[4], names[4]);
        for (unsigned i = 0; i < procedural_nodes.getSize(); ++i)
            bvh.push_back(ProceduralNode::fromVal(procedural_nodes[i]));
        proceduralIndex = bvh.size();

        // Now that all the nodes have been populated, it is time to resolve all references from uvec2 into Node*
        tlas.resolve(bvh, boxIndex, instanceIndex, triangleIndex);
        for (Node* node : bvh)
            node->resolveReferences(bvh, boxIndex, instanceIndex, triangleIndex);

        // shader_binding_table
        shaderBindingTable.copyFrom(other[5]);
    }

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to acceleration struct!");
    }
};
