/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm>
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

#include "../../external/spirv.hpp"
#include "../type.hpp"
#include "../value.hpp"
export module value.raytrace.accelStruct;
import util.string;
import value.aggregate;
import value.primitive;
import value.raytrace.node;
import value.raytrace.rayFlags;
import value.raytrace.shaderBindingTable;
import value.statics;
import value.string;

static constexpr unsigned HIT_KIND_FRONT_FACING_TRIANGLE_KHR = 0xFE;
static constexpr unsigned HIT_KIND_BACK_FACING_TRIANGLE_KHR = 0xFF;

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

private:
    // TODO: handle the effects of winding order on intersections; currently, front face is CCW

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

    const unsigned id;
    const bool isTLAS;  // Whether the acceleration structure is top-level
    std::shared_ptr<Node> root;  // Start of the acceleration structure
    std::shared_ptr<ShaderBindingTable> shaderBindingTable;

    using ConstNodeRef = const std::shared_ptr<Node>*;

    std::stack<ConstNodeRef> nodesToEval;
    bool activeTrace = false;

    // Intersection related
    enum class CommittedIntersectionType { None = 0, Triangle = 1, Generated = 2 };
    enum class CandidateIntersectionType { Triangle = 0, AABB = 1 };

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
        IntersectionProperties properties {};

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

    // Ray properties
    RayFlags rayFlags(0);
    unsigned cullMask = 0;
    glm::vec4 rayOrigin {0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 rayDirection {0.0f, 0.0f, 0.0f, 0.0f};
    float rayTMin = 0.0f;
    float rayTMax = 0.0f;

    // Shader binding table information
    bool useSBT = false;
    unsigned offsetSBT = 0;
    unsigned strideSBT = 0;
    unsigned missIndex = 0;

    /// @brief Make the trace empty.
    void clearTrace() {
        while (!nodesToEval.empty())
            nodesToEval.pop();
    }

    /// @brief Initialize the trace; the acceleration structure can now be stepped through.
    void initTrace() {
        committedIntersection = CommittedIntersection{};
        candidateIntersection = CandidateIntersection{};
        nodesToEval.push(&root);
        activeTrace = true;
    }

    bool didPopNodePreviously = true;  // Used in the stepTrace() method
    bool intersectedProcedural = false;  // Used in the stepTrace() method

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
    bool rayAABBIntersect(
        const glm::vec3& ray_origin,
        const glm::vec3& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const glm::vec3& min_bounds,
        const glm::vec3& max_bounds
    ) const {
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
    std::tuple<bool, float, float, float, bool> rayTriangleIntersect(
        const glm::vec3& ray_origin,
        const glm::vec3& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const std::vector<glm::vec3>& vertices,
        const bool cull_back_face,
        const bool cull_front_face
    ) const {
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

    /// @brief Get populated shader inputs for the next shader in the ray tracing pipeline.
    /// @param shader provides the inputs to populate.
    /// @param get_committed whether to get the inputs of a candidate or committed intersection.
    /// @param payload used to populate a shader input.
    /// @return populated inputs that the respective shader can use.
    ValueMap getNewShaderInputs(const Program& shader, bool get_committed, const Value* payload) const {
        assert(isTLAS);
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

public:
    AccelStruct(): Value(Type::accelStruct()) {}

    /// @brief Resets the trace to the beginning.
    void resetTrace() {
        activeTrace = false;

        // Reset children instance nodes
        if (isTLAS && root->type() == NodeType::Box) {
            std::stack<std::shared_ptr<Node>> frontier;
            frontier.push(root);
            while (!frontier.empty()) {
                std::shared_ptr<Node> node = frontier.top();
                frontier.pop();
                switch (node->type()) {
                default: {
                    std::stringstream err;
                    err << "Cannot reset the trace of node type enumeration value: "
                        << static_cast<unsigned>(node->type());
                    throw std::runtime_error(err.str());
                }
                case NodeType::Box: {
                    BoxNode& box_node = static_cast<BoxNode&>(*node);
                    for (auto& child : box_node.children)
                        frontier.push(child);
                    break;
                }
                case NodeType::Instance: {
                    InstanceNode& instance_node = static_cast<InstanceNode&>(*node);
                    instance_node.accelStruct->resetTrace();
                    break;
                }
                }
            }
        }

        clearTrace();
        initTrace();
    }

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
        assert(ray_origin.size() == 3 && ray_direction.size() == 3);
        glm::vec4 ray_origin_glm = glm::make_vec4(ray_origin.data());
        ray_origin_glm.w = 1.0f;
        glm::vec4 ray_direction_glm = glm::make_vec4(ray_direction.data());
        ray_direction_glm.w = 0.0f;

        rayFlags = RayFlags(ray_flags);
        this->cullMask = cull_mask;
        this->rayOrigin = ray_origin_glm;
        this->rayDirection = ray_direction_glm;
        this->rayTMin = ray_t_min;
        this->rayTMax = ray_t_max;

        this->useSBT = use_sbt;
        this->offsetSBT = offset_sbt;
        this->strideSBT = stride_sbt;
        this->missIndex = miss_index;

        resetTrace();
    }

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
        const glm::vec3& ray_origin,
        const glm::vec3& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const bool use_sbt,
        const unsigned offset_sbt = 0,
        const unsigned stride_sbt = 0,
        const unsigned miss_index = 0
    ) {
        rayFlags = RayFlags(ray_flags);
        this->cullMask = cull_mask;
        this->rayOrigin = glm::vec4(ray_origin.x, ray_origin.y, ray_origin.z, 1.0f);
        this->rayDirection = glm::vec4(ray_direction.x, ray_direction.y, ray_direction.z, 0.0f);
        this->rayTMin = ray_t_min;
        this->rayTMax = ray_t_max;

        this->useSBT = use_sbt;
        this->offsetSBT = offset_sbt;
        this->strideSBT = stride_sbt;
        this->missIndex = miss_index;

        resetTrace();
    }

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
        const glm::vec4& ray_origin,
        const glm::vec4& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const bool use_sbt,
        const unsigned offset_sbt = 0,
        const unsigned stride_sbt = 0,
        const unsigned miss_index = 0
    ) {
        rayFlags = RayFlags(ray_flags);
        this->cullMask = cull_mask;
        this->rayOrigin = ray_origin;
        this->rayDirection = ray_direction;
        this->rayTMin = ray_t_min;
        this->rayTMax = ray_t_max;

        this->useSBT = use_sbt;
        this->offsetSBT = offset_sbt;
        this->strideSBT = stride_sbt;
        this->missIndex = miss_index;

        resetTrace();
    }

public:
    /// @brief Take a step in the trace. Each step reaches the next non-instance primitive that was intersected.
    /// @return if a triangle or procedural was intersected; implies if there is more to trace.
    bool stepTrace() {
        // Do not trace if the trace is inactive.
        if (!activeTrace)
            return false;

        // Traverse the acceleration structure until it reaches the next non-instance primitive.
        bool found_primitive = false;
        while (!found_primitive && (nodesToEval.size() > 0)) {

            ConstNodeRef curr_node_ref = nodesToEval.top();
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
                // If the ray intersects the bounding box, then add it's children to be evaluated.
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
                    ref_accel_struct->initTrace(
                        rayFlags,
                        cullMask,
                        object_ray_origin,
                        object_ray_direction,
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
                    const bool sbt_exist = useSBT && (shaderBindingTable != nullptr);
                    if (sbt_exist && (getCandidateIntersectionType() == CandidateIntersectionType::AABB)) {
                        intersectedProcedural = true;
                        const int geometry_index = getIntersectionGeometryIndex(false);
                        const unsigned instance_sbt_offset =
                            getIntersectionInstanceShaderBindingTableRecordOffset(false);
                        const Program* shader = shaderBindingTable->getHitShader(
                            offsetSBT,
                            strideSBT,
                            geometry_index,
                            instance_sbt_offset,
                            HitGroupType::Intersection
                        );
                        ValueMap inputs = getNewShaderInputs(*shader, true, nullptr);
                        SBTShaderOutput outputs = shaderBindingTable->executeHit(
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
                        assert(isTLAS);
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

                    // Terminate on the first hit if the flag was risen
                    if (rayFlags.terminateOnFirstHit()) {
                        activeTrace = false;
                        return true;
                    }
                }

                break;
            }
            case NodeType::Triangle: {
                // Check skip triangle ray flag.
                if (rayFlags.SkipTriangles())
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
                        activeTrace = false;
                        return true;
                    }
                }

                break;
            }
            case NodeType::Procedural: {
                // Check skip AABBs (procedurals) flag
                if (rayFlags.SkipAABBs())
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
                    assert(getCandidateIntersectionType() == CandidateIntersectionType::AABB);

                    // Cannot terminate early if ray flag "terminate on first hit" was raised because we do not know if
                    // the ray actually intersected the primitive.
                }
                break;
            }
            }
        }

        // Make sure to deactivate the trace if there is no more to traverse
        if (nodesToEval.empty())
            activeTrace = false;

        return found_primitive;
    }

    /// @brief Check if some hit t is within the ray's interval.
    /// @param hit_t distance from the ray to the intersection.
    /// @return whether hit t is within the ray's interval.
    bool isIntersectionValid(const float hit_t) {
        return (hit_t >= rayTMin) && (hit_t <= rayTMax);
    }

    /// @brief Invokes the any-hit shader.
    /// @param hit_t distance from the ray to the intersection.
    /// @param hit_kind object kind that was intersected.
    /// @return whether to accept the intersection.
    bool invokeAnyHitShader(const float hit_t, const unsigned hit_kind) {
        assert(isTLAS);
        const int geometry_index = getIntersectionGeometryIndex(false);
        const unsigned instance_sbt_offset = getIntersectionInstanceShaderBindingTableRecordOffset(false);
        const Program* shader = shaderBindingTable->getHitShader(
                                    offsetSBT, strideSBT, geometry_index, instance_sbt_offset, HitGroupType::Any
                                );
        ValueMap inputs = getNewShaderInputs(*shader, true, nullptr);
        bool ignore_intersection = false;
        SBTShaderOutput outputs = shaderBindingTable->executeHit(
            inputs,
            offsetSBT,
            strideSBT,
            geometry_index,
            instance_sbt_offset,
            HitGroupType::Any,
            static_cast<void*>(&ignore_intersection)
        );

        if (ignore_intersection) {
            intersectedProcedural = false;
            return false;
        }

        // Otherwise, update respective properties
        candidateIntersection.properties.hitT = hit_t;
        candidateIntersection.properties.hitKind = hit_kind;

        return true;
    }

    /// @brief Include the current AABB/procedural intersection in determining the closest hit.
    /// The candidate intersection must be of type AABB.
    /// @param hit_t distance from the ray to the intersection.
    void generateIntersection(float hit_t) {
        assert(candidateIntersection.type == CandidateIntersectionType::AABB);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (hit_t >= committedIntersection.properties.hitT)
            return;

        rayTMax = hit_t;
        candidateIntersection.properties.hitT = hit_t;
        committedIntersection.update(false, candidateIntersection);
    }

    /// @brief Include the current triangle intersection in determining the closest hit.
    /// The candidate intersection must be of type triangle.
    void confirmIntersection() {
        assert(candidateIntersection.type == CandidateIntersectionType::Triangle);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (candidateIntersection.properties.hitT >= committedIntersection.properties.hitT)
            return;

        rayTMax = candidateIntersection.properties.hitT;
        committedIntersection.update(true, candidateIntersection);
    }

    /// @brief Get the current committed intersection type.
    /// @return committed intersection type.
    CommittedIntersectionType getCommittedIntersectionType() const {
        return committedIntersection.type;
    }

    /// @brief Get the current candidate intersection type.
    /// @return candidate intersection type.
    CandidateIntersectionType getCandidateIntersectionType() const {
        return candidateIntersection.type;
    }

    /// @brief Get the distance from the ray to the current intersection.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return distance between the ray and intersection.
    float getIntersectionT(bool get_committed) const {
        return get_committed ? committedIntersection.properties.hitT : candidateIntersection.properties.hitT;
    }

    /// @brief Get the current intersection instance's custom index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return custom index if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceCustomIndex(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;

        if (committed_instance == nullptr && candidate_instance == nullptr)
            return -1;

        return get_committed ? committed_instance->customIndex : candidate_instance->customIndex;
    }

    /// @brief Get the current intersection instance's id.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return id if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceId(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;

        if (committed_instance == nullptr && candidate_instance == nullptr)
            return -1;

        return get_committed ? committed_instance->id : candidate_instance->id;
    }

    /// @brief Get the current intersection instance's shader binding table record offset.
    /// The instance must exist; not be null.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return SBT record offset
    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->sbtRecordOffset : candidate_instance->sbtRecordOffset;
    }

    /// @brief Get the current intersection's geometry index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return geometry index.
    int getIntersectionGeometryIndex(bool get_committed) const {
        return get_committed ? committedIntersection.properties.geometryIndex
                             : candidateIntersection.properties.geometryIndex;
    }

    /// @brief Get the current intersection's primitive index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return primitive index.
    int getIntersectionPrimitiveIndex(bool get_committed) const {
        return get_committed ? committedIntersection.properties.primitiveIndex
                             : candidateIntersection.properties.primitiveIndex;
    }

    /// @brief Get the current intersection's barycentric coordinates.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return barycentrics.
    glm::vec2 getIntersectionBarycentrics(bool get_committed) const {
        return get_committed ? committedIntersection.properties.barycentrics
                             : candidateIntersection.properties.barycentrics;
    }

    /// @brief Get whether the ray entered the front face of a triangle.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return whether the intersection was entered from the front face of a triangle.
    bool getIntersectionFrontFace(bool get_committed) const {
        return get_committed ? committedIntersection.type == CommittedIntersectionType::Triangle &&
                                   committedIntersection.properties.enteredTriangleFrontFace
                             : candidateIntersection.type == CandidateIntersectionType::Triangle &&
                                   candidateIntersection.properties.enteredTriangleFrontFace;
    }

    /// @brief Get whether the intersection is an opaque procedural.
    /// @return whether the intersection was an opaque procedural.
    bool getIntersectionCandidateAABBOpaque() const {
        return candidateIntersection.type == CandidateIntersectionType::AABB &&
               candidateIntersection.properties.isOpaque;
    }

    /// @brief Get the object-space ray direction depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray direction.
    glm::vec3 getIntersectionObjectRayDirection(bool get_committed) const {
        return get_committed ? (committedIntersection.properties.instance->worldToObject * rayDirection)
                             : (candidateIntersection.properties.instance->worldToObject * rayDirection);
    }

    /// @brief Get the object-space ray origin depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray origin.
    glm::vec3 getIntersectionObjectRayOrigin(bool get_committed) const {
        return get_committed ? (committedIntersection.properties.instance->worldToObject * rayOrigin)
                             : (candidateIntersection.properties.instance->worldToObject * rayOrigin);
    }

    /// @brief Get the object-to-world matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-to-world matrix.
    glm::mat4x3 getIntersectionObjectToWorld(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->objectToWorld : candidate_instance->objectToWorld;
    }

    /// @brief Get the world-to-object matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return world-to-object matrix.
    glm::mat4x3 getIntersectionWorldToObject(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->worldToObject : candidate_instance->worldToObject;
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
        const glm::vec4& ray_origin,
        const glm::vec4& ray_direction,
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
                if (getCandidateIntersectionType() == CandidateIntersectionType::Triangle)
                    confirmIntersection();
                else  // ... == CandidateIntersectionType::AABB
                    generateIntersection(getIntersectionT(false));
            }

        } while (found_primitive);

        // Do not invoke any shaders if a shader binding table was not specified
        if (!useSBT || shaderBindingTable == nullptr) {
            std::stack<Value*> frontier;
            frontier.push(payload);

            while (!frontier.empty()) {
                Value* curr = frontier.top();
                frontier.pop();

                switch (curr->getType().getBase()) {
                default: {
                    std::stringstream err;
                    err << "Encountered unsupported data type in fill payload: " << curr->getType().getBase();
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
                    const Aggregate& agg = static_cast<const Aggregate&>(*curr);
                    for (auto it = agg.begin(); it != agg.end(); ++it)
                        frontier.push(*it);
                    break;
                }
                }
            }
            return;
        }
        assert(shaderBindingTable != nullptr);

        // Otherwise, invoke either the closest hit or miss shaders
        SBTShaderOutput outputs;
        if (getCommittedIntersectionType() != CommittedIntersectionType::None) {
            // Closest hit
            if (rayFlags.SkipClosestHitShader())
                return;
            const int geometry_index = getIntersectionGeometryIndex(true);
            const unsigned instance_sbt_offset = getIntersectionInstanceShaderBindingTableRecordOffset(true);
            const Program* shader =
                shaderBindingTable
                    ->getHitShader(sbt_offset, sbt_stride, geometry_index, instance_sbt_offset, HitGroupType::Closest);
            ValueMap inputs = getNewShaderInputs(*shader, true, payload);
            outputs = shaderBindingTable->executeHit(
                inputs,
                sbt_offset,
                sbt_stride,
                geometry_index,
                instance_sbt_offset,
                HitGroupType::Closest
            );
        } else {
            // Miss
            const Program* shader = shaderBindingTable->getMissShader(miss_index);
            ValueMap inputs = getNewShaderInputs(*shader, true, payload);
            outputs = shaderBindingTable->executeMiss(inputs, miss_index);
        }

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
    }

    /// @brief Initialize the step trace.
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
    void initStepTraceRay(
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
        root->initTrace(
            ray_flags,
            cull_mask,
            ray_origin,
            ray_direction,
            ray_t_min,
            ray_t_max,
            use_sbt,
            offset_sbt,
            stride_sbt,
            miss_index
        );
    }

    /// @brief Take a step in the trace.
    /// @return whether there is more to trace.
    bool stepTraceRay() {
        //return root->stepTrace();
        return false;
    }

    /// @brief Completely trace the acceleration structure.
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
    /// @return whether a geometry was intersected.
    void traceRay(
        const unsigned ray_flags,
        const unsigned cull_mask,
        const std::vector<float>& ray_origin,
        const std::vector<float>& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const unsigned offset_sbt,
        const unsigned stride_sbt,
        const unsigned miss_index,
        Value* payload
    ) const {
        glm::vec4 ray_origin_glm = glm::make_vec4(ray_origin.data());
        ray_origin_glm.w = 1.0f;
        glm::vec4 ray_direction_glm = glm::make_vec4(ray_direction.data());
        ray_direction_glm.w = 0.0f;

        root->traceRay(
            ray_flags,
            cull_mask,
            ray_origin_glm,
            ray_direction_glm,
            ray_t_min,
            ray_t_max,
            offset_sbt,
            stride_sbt,
            miss_index,
            payload
        );
    }

    /// @brief Get the intersection type.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return intersection type.
    unsigned getIntersectionType(bool get_committed) const {
        return get_committed ? static_cast<unsigned>(root->getCommittedIntersectionType())
                             : static_cast<unsigned>(root->getCandidateIntersectionType());
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
