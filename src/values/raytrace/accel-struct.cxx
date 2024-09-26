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
#include "trace.hpp"
export module value.raytrace.accelStruct;
import spv.rayFlags;
import util.intersection;
import util.string;
import value.aggregate;
import value.primitive;
import value.raytrace.shaderBindingTable;
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

    static glm::mat4x3 removeLastRow(glm::mat4 mat) {
        glm::mat4x3 ret;
        for (unsigned i = 0; i < 4; ++i) {
            for (unsigned j = 0; j < 3; ++j)
                ret[i][j] = mat[i][j];
        }
        return ret;
    }

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
        assert(ray_origin.size() == 3 && ray_direction.size() == 3);
        trace.rayOrigin = glm::vec3(ray_origin[0], ray_origin[1], ray_origin[2]);
        trace.rayDirection = glm::vec3(ray_direction[0], ray_direction[1], ray_direction[2]);

        trace.useSBT = use_sbt;
        trace.offsetSBT = offset_sbt;
        trace.strideSBT = stride_sbt;
        trace.missIndex = miss_index;

        trace.committed = std::numeric_limits<decltype(trace.committed)>::max();
        trace.active = true;
        trace.candidates.clear();
        // Start the candidates fresh with the root node in the bvh
        trace.candidates.emplace_back(tlas.ptr);
        // start at the end of the list because stepTrace is pre-increment
        trace.candidate = std::numeric_limits<decltype(trace.candidate)>::max();
    }

    /// @brief Take a step in the trace. Each step reaches the next non-instance primitive that was intersected.
    /// @return if a triangle or procedural was intersected; implies if there is more to trace.
    bool stepTrace() {
        if (!trace.active)
            return false;

        // Pre-increment the candidate index (because after a search, we may need to access the current intersection).
        // Note: unsigned overflow is *defined* as reduced modulo by the C99 spec (§6.2.5/9)
        if (trace.candidate >= trace.candidates.size())  // for the first iteration in trace only
            trace.candidate = std::numeric_limits<decltype(trace.candidate)>::max();

        // Traverse the acceleration structure until it reaches the next non-instance primitive.
        bool found_primitive = false;
        // Note: the node may make the trace inactive, so check that each iteration
        while (trace.active && !found_primitive && ++trace.candidate < trace.candidates.size()) {
            const auto& cand = trace.getCandidate();
            found_primitive = cand.search->step(&trace);
        }

        // Terminate on the first hit if the flag was risen
        // Or terminate the search if there are no nodes left to look at
        if ((found_primitive && trace.rayFlags.terminateOnFirstHit()) ||
        (trace.candidate >= trace.candidates.size() - 1))
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
                if (trace.getCandidate().type == Intersection::Type::Triangle)
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
            if (trace.hasCommitted()) {
                // Closest hit
                if (!trace.rayFlags.skipClosestHitShader()) {
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
                }
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
        Intersection& cand = trace.getCandidate();
        assert(cand.type == Intersection::Type::AABB);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (trace.hasCommitted() && t_hit >= trace.getCommitted().hitT)
            return;

        trace.rayTMax = t_hit;
        cand.hitT = t_hit;
        trace.committed = trace.candidate;
    }

    /// @brief Include the current triangle intersection in determining the closest hit.
    /// The candidate intersection must be of type triangle.
    void confirmIntersection() {
        Intersection& cand = trace.getCandidate();
        assert(cand.type == Intersection::Type::Triangle);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (trace.hasCommitted() && cand.hitT >= trace.getCommitted().hitT)
            return;

        trace.rayTMax = cand.hitT;
        cand.type = Intersection::Type::Generated;
        trace.committed = trace.candidate;
    }

    /// @brief Get the distance from the ray to the current intersection.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return distance between the ray and intersection.
    float getIntersectionT(bool get_committed) const {
        unsigned index = get_committed? trace.committed : trace.candidate;
        return trace.candidates[index].hitT;
    }

    /// @brief Get the current intersection instance's custom index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return custom index if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceCustomIndex(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed? trace.committed : trace.candidate];
        if (intersect.instance != nullptr)
            return static_cast<int>(intersect.instance->getCustomIndex());
        return -1;
    }

    /// @brief Get the current intersection instance's id.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return id if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceId(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed? trace.committed : trace.candidate];
        if (intersect.instance != nullptr)
            return static_cast<int>(intersect.instance->getId());
        return -1;
    }

    /// @brief Get the current intersection instance's shader binding table record offset.
    /// The instance must exist; not be null.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return SBT record offset
    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed? trace.committed : trace.candidate];
        assert(intersect.instance != nullptr);
        return intersect.instance->getSbtRecordOffs();
    }

    /// @brief Get the current intersection's geometry index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return geometry index.
    int getIntersectionGeometryIndex(bool get_committed) const {
        unsigned index = get_committed? trace.committed : trace.candidate;
        return trace.candidates[index].geometryIndex;
    }

    /// @brief Get the current intersection's primitive index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return primitive index.
    int getIntersectionPrimitiveIndex(bool get_committed) const {
        unsigned index = get_committed? trace.committed : trace.candidate;
        return trace.candidates[index].primitiveIndex;
    }

    /// @brief Get the current intersection's barycentric coordinates.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return barycentrics.
    glm::vec2 getIntersectionBarycentrics(bool get_committed) const {
        unsigned index = get_committed? trace.committed : trace.candidate;
        return trace.candidates[index].barycentrics;
    }

    /// @brief Get whether the ray entered the front face of a triangle.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return whether the intersection was entered from the front face of a triangle.
    bool getIntersectionFrontFace(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed? trace.committed : trace.candidate];
        return intersect.type == Intersection::Type::Triangle && intersect.enteredTriangleFrontFace;
    }

    /// @brief Get whether the intersection is an opaque procedural.
    /// @return whether the intersection was an opaque procedural.
    bool getIntersectionCandidateAABBOpaque() const {
        const Intersection& intersect = trace.getCandidate();
        return intersect.type == Intersection::Type::AABB && intersect.isOpaque;
    }

    /// @brief Get the object-space ray direction depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray direction.
    glm::vec3 getIntersectionObjectRayDirection(bool get_committed) const {
        unsigned index = get_committed? trace.committed : trace.candidate;
        return trace.candidates[index].getRayDir(&trace);
    }

    /// @brief Get the object-space ray origin depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray origin.
    glm::vec3 getIntersectionObjectRayOrigin(bool get_committed) const {
        unsigned index = get_committed? trace.committed : trace.candidate;
        return trace.candidates[index].getRayPos(&trace);
    }

    /// @brief Get the object-to-world matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-to-world matrix.
    glm::mat4x3 getIntersectionObjectToWorld(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed? trace.committed : trace.candidate];
        assert(intersect.instance != nullptr);
        return removeLastRow(intersect.objToWorld);
    }

    /// @brief Get the world-to-object matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return world-to-object matrix.
    glm::mat4x3 getIntersectionWorldToObject(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed? trace.committed : trace.candidate];
        assert(intersect.instance != nullptr);
        return removeLastRow(intersect.worldToObj);
    }

    /// @brief Get the intersection type.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return intersection type.
    Intersection::Type getIntersectionType(bool get_committed) const {
        unsigned index = get_committed? trace.committed : trace.candidate;
        return trace.candidates[index].type;
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
