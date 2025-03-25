/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "glm/ext.hpp"

#include "../type.hpp"
#include "../value.hpp"
#include "node.hpp"
#include "trace.hpp"
export module value.raytrace.accelStruct;
import spv.rayFlags;
import util.intersection;
import util.string;
import util.ternary;
import value.aggregate;
import value.primitive;
import value.statics;
import value.string;

export class AccelStruct : public Value {
private:
    bool ownNodes = false;
    std::vector<Node*> bvh;

    NodeReference tlas = NodeReference(0, 0);
    // Necessary for reconstructing an externally-viewable struct
    unsigned boxIndex = 0;
    unsigned instanceIndex = 0;
    unsigned triangleIndex = 0;
    unsigned proceduralIndex = 0;

    inline static const std::vector<std::string> names {
        "tlas",
        "box_nodes",
        "instance_nodes",
        "triangle_nodes",
        "procedural_nodes",
    };

    Trace trace;

private:
    // TODO: handle the effects of winding order on intersections; currently, front face is CCW

    static glm::mat4x3 removeLastRow(glm::mat4 mat) {
        glm::mat4x3 ret;
        for (unsigned i = 0; i < 4; ++i) {
            for (unsigned j = 0; j < 3; ++j)
                ret[i][j] = mat[i][j];
        }
        return ret;
    }

public:
    AccelStruct() : Value(Type::accelStruct()) {}
    ~AccelStruct() {
        if (ownNodes) {
            for (unsigned i = 0; i < bvh.size(); ++i)
                delete bvh[i];
        }
    }
    // We could clone all nodes and resolve references, but there shouldn't be any need since AccelStructs created from
    // others have a lifetime equal or less than the AccelStruct created from. AccelStructs copied from a struct are
    // definitionally top-level, so we can borrow the nodes for all derivative copies.
    AccelStruct(const AccelStruct& other)
        : Value(other.type)
        , ownNodes(false)
        , bvh(other.bvh)
        , tlas(other.tlas)
        , boxIndex(other.boxIndex)
        , instanceIndex(other.instanceIndex)
        , triangleIndex(other.triangleIndex)
        , proceduralIndex(other.proceduralIndex)
        , trace(other.trace) {}

    AccelStruct& operator=(const AccelStruct& other) {
        if (this == &other)
            return *this;

        bool ownNodes = false;
        bvh = other.bvh;
        tlas = other.tlas;
        boxIndex = other.boxIndex;
        instanceIndex = other.instanceIndex;
        triangleIndex = other.triangleIndex;
        proceduralIndex = other.proceduralIndex;
        trace = other.trace;
        return *this;
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
    /// @return whether there is more to trace (if so, there must have been an intersection)
    Ternary stepTrace() {
        if (!trace.active)
            return Ternary::NO;

        // Pre-increment the candidate index (because after a search, we may need to access the current intersection).
        // Note: unsigned overflow is *defined* as reduced modulo by the C99 spec (§6.2.5/9)
        if (trace.candidate >= trace.candidates.size())  // for the first iteration in trace only
            trace.candidate = std::numeric_limits<decltype(trace.candidate)>::max();

        // Traverse the acceleration structure until it reaches the next non-instance primitive.
        Ternary found_primitive = Ternary::NO;
        // Note: the node may make the trace inactive, so check that each iteration
        while (trace.active && (found_primitive == Ternary::NO) && ++trace.candidate < trace.candidates.size()) {
            const auto& cand = trace.getCandidate();
            found_primitive = cand.search->step(&trace);
        }

        // Terminate on the first hit if the flag was set
        // Or terminate the search if there are no nodes left to look at
        if (trace.candidate >= trace.candidates.size() - 1)
            trace.active = false;

        return found_primitive;
    }

    /// @brief Completely trace through the acceleration structure.
    /// @param skip_trace whether to skip the first step and go right to analyzing the result
    Ternary traceRay(bool skip_trace) {
        bool intersect_once = false;
        Ternary found_primitive = Ternary::NO;
        do {
            if (!skip_trace) {
                found_primitive = stepTrace();
                if (found_primitive == Ternary::YES && trace.rayFlags.terminateOnFirstHit())
                    trace.active = false;
            } else {
                found_primitive = Ternary::YES;
                skip_trace = false;  // don't skip again on the next iteration
            }

            if (found_primitive == Ternary::YES) {
                intersect_once = true;
                Intersection& candidate = trace.getCandidate();
                if (candidate.type == Intersection::Type::Triangle)
                    confirmIntersection();
                else  // ... == Intersection::Type::AABB
                    generateIntersection(candidate.hitT);
            }
        } while (found_primitive == Ternary::YES);

        if (intersect_once)
            return Ternary::YES;
        return found_primitive;  // could be no for no more to trace or maybe for something to check
    }

    /// @brief Check whether some hit distance is within the ray's interval.
    /// @param t_hit distance from the ray to the intersection.
    /// @return whether the hit is within the ray's interval.
    bool isIntersectionValid(const float t_hit) {
        return (t_hit >= trace.rayTMin) && (t_hit <= trace.rayTMax);
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
        unsigned index = get_committed ? trace.committed : trace.candidate;
        return trace.candidates[index].hitT;
    }

    /// @brief Get the current intersection instance's custom index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return custom index if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceCustomIndex(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed ? trace.committed : trace.candidate];
        if (intersect.instance != nullptr)
            return static_cast<int>(intersect.instance->getCustomIndex());
        return -1;
    }

    /// @brief Get the current intersection instance's id.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return id if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceId(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed ? trace.committed : trace.candidate];
        if (intersect.instance != nullptr)
            return static_cast<int>(intersect.instance->getId());
        return -1;
    }

    /// @brief Get the current intersection instance's shader binding table record offset.
    /// The instance must exist; not be null.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return SBT record offset
    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed ? trace.committed : trace.candidate];
        assert(intersect.instance != nullptr);
        return intersect.instance->getSbtRecordOffs();
    }

    /// @brief Get the current intersection's geometry index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return geometry index.
    int getIntersectionGeometryIndex(bool get_committed) const {
        unsigned index = get_committed ? trace.committed : trace.candidate;
        return trace.candidates[index].geometryIndex;
    }

    /// @brief Get the current intersection's primitive index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return primitive index.
    int getIntersectionPrimitiveIndex(bool get_committed) const {
        unsigned index = get_committed ? trace.committed : trace.candidate;
        return trace.candidates[index].primitiveIndex;
    }

    /// @brief Get the current intersection's barycentric coordinates.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return barycentrics.
    glm::vec2 getIntersectionBarycentrics(bool get_committed) const {
        unsigned index = get_committed ? trace.committed : trace.candidate;
        return trace.candidates[index].barycentrics;
    }

    /// @brief Get whether the ray entered the front face of a triangle.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return whether the intersection was entered from the front face of a triangle.
    bool getIntersectionFrontFace(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed ? trace.committed : trace.candidate];
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
        unsigned index = get_committed ? trace.committed : trace.candidate;
        return trace.candidates[index].getRayDir(&trace);
    }

    /// @brief Get the object-space ray origin depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray origin.
    glm::vec3 getIntersectionObjectRayOrigin(bool get_committed) const {
        unsigned index = get_committed ? trace.committed : trace.candidate;
        return trace.candidates[index].getRayPos(&trace);
    }

    std::vector<Primitive> getWorldRayDirection() const {
        const glm::vec3& ray_dir = trace.rayDirection;
        return std::vector<Primitive> {ray_dir.x, ray_dir.y, ray_dir.z};
    }

    std::vector<Primitive> getWorldRayOrigin() const {
        const glm::vec3& ray_pos = trace.rayOrigin;
        return std::vector<Primitive> {ray_pos.x, ray_pos.y, ray_pos.z};
    }

    /// @brief Get the object-to-world matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-to-world matrix.
    glm::mat4x3 getIntersectionObjectToWorld(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed ? trace.committed : trace.candidate];
        assert(intersect.instance != nullptr);
        return removeLastRow(intersect.objToWorld);
    }

    /// @brief Get the world-to-object matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return world-to-object matrix.
    glm::mat4x3 getIntersectionWorldToObject(bool get_committed) const {
        const Intersection& intersect = trace.candidates[get_committed ? trace.committed : trace.candidate];
        assert(intersect.instance != nullptr);
        return removeLastRow(intersect.worldToObj);
    }

    /// @brief Get the intersection type.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return intersection type.
    Intersection::Type getIntersectionType(bool get_committed) const {
        unsigned index = get_committed ? trace.committed : trace.candidate;
        if (index >= trace.candidates.size())
            return Intersection::Type::None;
        return trace.candidates[index].type;
    }

    const Trace& getTrace() const {
        return trace;
    }
    Intersection& getCandidate() {
        return trace.getCandidate();
    }
    Intersection& getCommitted() {
        return trace.getCommitted();
    }

    void terminate() {
        trace.active = false;
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<Value*> fields(names.size(), nullptr);
        fields[0] = tlas.toArray();

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
        if (auto base = from_type.getBase(); base == DataType::ACCEL_STRUCT) {
            *this = static_cast<const AccelStruct&>(new_val);
            return;
        } else if (from_type.getBase() != DataType::STRUCT)
            throw std::runtime_error("Cannot copy acceleration structure from non-structure type!");

        const Struct& other = Statics::extractStruct(&new_val, "acceleration structure", names);

        // tlas: uvec2
        std::vector<unsigned> tlas_got = Statics::extractUvec(other[0], names[0], 2);
        tlas = NodeReference(tlas_got[0], tlas_got[1]);

        // Clear any nodes previously held
        if (ownNodes) {
            for (unsigned i = 0; i < bvh.size(); ++i)
                delete bvh[i];
        }
        bvh.clear();
        ownNodes = true;

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
    }

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to acceleration struct!");
    }
};
