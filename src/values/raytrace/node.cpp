/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "node.hpp"

#include <cstdint>
#include <tuple>
#include <sstream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>

#include "../value.hpp"
#include "trace.hpp"
import util.intersection;
import value.aggregate;
import value.primitive;
import value.statics;

// These also are an unwanted Vulkan/SPIR-V dependency.
static constexpr unsigned HIT_KIND_FRONT_FACING_TRIANGLE_KHR = 0xFE;
static constexpr unsigned HIT_KIND_BACK_FACING_TRIANGLE_KHR = 0xFF;

const Type& BoxNode::getType() {
    if (type.getBase() != DataType::VOID)
        return type;
    Statics statics;
    statics.init();
    childNodesType = Type::array(0, Statics::uvec2Type);
    const std::vector<const Type*> sub_list{&Statics::vec3Type, &Statics::vec3Type, &childNodesType};
    type = Type::structure(sub_list, names);
    return type;
}

bool BoxNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;
    Intersection& candidate = trace.getCandidate();
    auto ray_pos = candidate.getRayPos(trace_p);
    auto ray_dir = candidate.getRayDir(trace_p);

    const bool result = ray_AABB_intersect(
        ray_pos,
        ray_dir,
        trace.rayTMin,
        trace.rayTMax,
        minBounds,
        maxBounds
    );
    // If the ray intersects the bounding box, then add its children to be evaluated.
    if (result) {
        for (const auto& child_ref : children) {
            // Have to refetch the candidate each iteration since expanding the candidates list may have invalidated
            // the previous reference.
            Intersection& cand = trace.candidates.emplace_back(trace.getCandidate());
            cand.search = child_ref.ptr;
        }
    }
    return false;
}

[[nodiscard]] BoxNode* BoxNode::fromVal(const Value* val) {
    const Struct& str = Statics::extractStruct(val, "BoxNode", names);
    std::vector<float> mins = Statics::extractVec(str[0], names[0], 3);
    std::vector<float> maxs = Statics::extractVec(str[1], names[1], 3);

    const Array& child_nodes = Statics::extractArray(str[2], names[2]);
    BoxNode* bn = new BoxNode(mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
    for (unsigned i = 0; i < child_nodes.getSize(); ++i) {
        std::vector<uint32_t> child_ref = Statics::extractUvec(child_nodes[i], names[2], 2);
        bn->children.emplace_back(child_ref[0], child_ref[1]);
    }
    return bn;
}

[[nodiscard]] Struct* BoxNode::toStruct() const {
    std::vector<Value*> fields(3, nullptr);
    std::vector<Value*> min_v{
        new Primitive(minBounds.x), new Primitive(minBounds.y), new Primitive(minBounds.z)
    };
    fields[0] = new Array(min_v);
    std::vector<Value*> max_v{
        new Primitive(maxBounds.x), new Primitive(maxBounds.y), new Primitive(maxBounds.z)
    };
    fields[1] = new Array(max_v);
    if (children.empty()) {
        BoxNode::getType();  // force init of type values
        fields[2] = new Array(childNodesType, 0);
    } else {
        std::vector<Value*> refs;
        for (const auto& node_ref : children)
            refs.push_back(node_ref.toArray());
        fields[2] = new Array(refs);
    }
    return new Struct(fields, names);
}

const Type& InstanceNode::getType() {
    if (type.getBase() != DataType::VOID)
        return type;
    Statics statics;
    statics.init();
    mat4x3Type = Type::array(3, Statics::vec4Type);
    const std::vector<const Type*> sub_list{
        &mat4x3Type,
        &Statics::uvec2Type,
        &Statics::uintType,
        &Statics::uintType,
        &Statics::uintType,
        &Statics::uintType
    };
    type = Type::structure(sub_list, names);
    return type;
}

bool InstanceNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;

    // Do not process this instance if it's invisible to the ray.
    if ((this->mask & trace.cullMask) == 0)
        return false;

    // Transform the ray to match the instance's object-space.
    const Intersection& before = trace.getCandidate();
    // TODO find out the correct direction to multiply in...
    glm::mat4 objToWorld = before.objToWorld * transformation;
    glm::mat4 worldToObj = before.worldToObj * inverse;
    // TODO: determine whether the ray interval (t-min and t-max) also needs to be scaled.

    Intersection& cand = trace.candidates.emplace_back(before);
    cand.search = child.ptr;
    cand.objToWorld = objToWorld;
    cand.worldToObj = worldToObj;
    cand.instance = this;

    return false;
}

[[nodiscard]] InstanceNode* InstanceNode::fromVal(const Value* val) {
    const Struct& str = Statics::extractStruct(val, "InstanceNode", names);
    const Array& transform = Statics::extractArray(str[0], names[0]);
    if (transform.getSize() != 3)
        throw std::runtime_error("InstanceNode field \"transformation\" must be a mat4x3!");
    glm::mat4x3 transformation;
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<float> row = Statics::extractVec(transform[i], "transformation", 4);
        for (unsigned j = 0; j < 4; ++j)
            transformation[j][i] = row[j];
    }

    std::vector<unsigned> ref = Statics::extractUvec(str[1], names[1], 2);
    uint32_t id = Statics::extractUint(str[2], names[2]);
    uint32_t custom_index = Statics::extractUint(str[3], names[3]);
    uint32_t mask = Statics::extractUint(str[4], names[4]);
    uint32_t sbt_record_offset = Statics::extractUint(str[5], names[5]);

    return new InstanceNode(ref[0], ref[1], transformation, id, custom_index, mask, sbt_record_offset);
}

[[nodiscard]] Struct* InstanceNode::toStruct() const {
    std::vector<Value*> cols(3, nullptr);
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<Value*> row(4, nullptr);
        for (unsigned j = 0; j < 4; ++j)
            row[j] = new Primitive(transformation[j][i]);
        cols[i] = new Array(row);
    }
    std::vector<Value*> fields{
        new Array(cols),
        child.toArray(),
        new Primitive(id),
        new Primitive(customIndex),
        new Primitive(mask),
        new Primitive(sbtRecordOffs)
    };
    return new Struct(fields, names);
}

const Type& TriangleNode::getType() {
    if (type.getBase() != DataType::VOID)
        return type;
    Statics statics;
    statics.init();
    mat3Type = Type::array(3, Statics::vec3Type);
    const std::vector<const Type*> sub_list{
        &Statics::uintType,
        &Statics::uintType,
        &Statics::boolType,
        &mat3Type
    };
    type = Type::structure(sub_list, names);
    return type;
}

bool TriangleNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;
    // Check skip triangle ray flag.
    if (trace.rayFlags.skipTriangles())
        return false;

    // Check opaque related ray flags.
    bool is_opaque = this->opaque;
    if (trace.rayFlags.opaque())
        is_opaque = true;
    else if (trace.rayFlags.noOpaque())
        is_opaque = false;

    if ((trace.rayFlags.cullOpaque() && is_opaque) || (trace.rayFlags.cullNoOpaque() && !is_opaque))
        return false;

    Intersection& candidate = trace.getCandidate();
    auto ray_pos = candidate.getRayPos(trace_p);
    auto ray_dir = candidate.getRayDir(trace_p);

    // Check if the ray intersects the triangle
    // t: Distance to intersection
    // u: Barycentric coordinate u
    // v: Barycentric coordinate v
    // entered_front: whether an intersection came from the front face
    auto [found, t, u, v, entered_front] = ray_triangle_intersect(
        ray_pos,
        ray_dir,
        trace.rayTMin,
        trace.rayTMax,
        this->vertices,
        trace.rayFlags.cullBackFacingTriangles(),
        trace.rayFlags.cullFrontFacingTriangles()
    );

    if (!found)
        return false;

    // Update candidate
    candidate.hitT = t;
    candidate.barycentrics = glm::vec2(u, v);
    candidate.isOpaque = this->opaque;
    candidate.enteredTriangleFrontFace = entered_front;
    candidate.geometryIndex = this->geomIndex;
    candidate.primitiveIndex = this->primIndex;
    candidate.hitKind = entered_front ? HIT_KIND_FRONT_FACING_TRIANGLE_KHR : HIT_KIND_BACK_FACING_TRIANGLE_KHR;
    candidate.type = Intersection::Type::Triangle;
    return found;
}

[[nodiscard]] TriangleNode* TriangleNode::fromVal(const Value* val) {
    const Struct& str = Statics::extractStruct(val, "TriangleNode", names);

    uint32_t geom_index = Statics::extractUint(str[0], names[0]);
    uint32_t prim_index = Statics::extractUint(str[1], names[1]);

    const Value* opaque_v = str[2];
    if (opaque_v == nullptr || opaque_v->getType().getBase() != DataType::BOOL)
        throw std::runtime_error("TriangleNode field \"opaque\" must be a boolean!");
    bool opaque = static_cast<const Primitive*>(opaque_v)->data.b32;

    const Array& vertices_a = Statics::extractArray(str[3], names[3]);
    if (vertices_a.getSize() != 3)
        throw std::runtime_error("TriangleNode field \"vertices\" must be three vec3!");
    std::vector<glm::vec3> verts;
    verts.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<float> row = Statics::extractVec(vertices_a[i], "vertices", 3);
        for (unsigned j = 0; j < 3; ++j)
            verts[i][j] = row[j];
    }

    return new TriangleNode(geom_index, prim_index, opaque, verts);
}

[[nodiscard]] Struct* TriangleNode::toStruct() const {
    std::vector<Value*> cols(3, nullptr);
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<Value*> row(3, nullptr);
        for (unsigned j = 0; j < 3; ++j)
            row[j] = new Primitive(vertices[i][j]);
        cols[i] = new Array(row);
    }
    std::vector<Value*> fields{
        new Primitive(geomIndex),
        new Primitive(primIndex),
        new Primitive(opaque),
        new Array(cols)
    };
    return new Struct(fields, names);
}

const Type& ProceduralNode::getType() {
    if (type.getBase() != DataType::VOID)
        return type;
    Statics statics;
    statics.init();
    const std::vector<const Type*> sub_list{
        &Statics::vec3Type, &Statics::vec3Type, &Statics::boolType, &Statics::uintType, &Statics::uintType
    };
    type = Type::structure(sub_list, names);
    return type;
}

bool ProceduralNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;

    // Check skip AABBs (procedurals) flag
    if (trace.rayFlags.skipAABBs())
        return false;

    // Check opaque related ray flags.
    bool is_opaque = this->opaque;
    if (trace.rayFlags.opaque())
        is_opaque = true;
    else if (trace.rayFlags.noOpaque())
        is_opaque = false;

    if ((trace.rayFlags.cullOpaque() && is_opaque) || (trace.rayFlags.cullNoOpaque() && !is_opaque))
        return false;

    Intersection& candidate = trace.getCandidate();
    auto ray_pos = candidate.getRayPos(trace_p);
    auto ray_dir = candidate.getRayDir(trace_p);

    bool found = ray_AABB_intersect(
        ray_pos,
        ray_dir,
        trace.rayTMin,
        trace.rayTMax,
        this->minBounds,
        this->maxBounds
    );

    if (!found)
        return false;

    // Here is the fun part: we passed the bounding box, now we want to call the intersection shader to determine
    // whether there is an actual intersection.
    // TODO

    /*//////////////////////////////////
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
    }
    *////////////////////////////

    if (found) {
        candidate.isOpaque = this->opaque;
        candidate.geometryIndex = this->geomIndex;
        candidate.primitiveIndex = this->primIndex;
        candidate.type = Intersection::Type::AABB;
    }

    return found;
}

[[nodiscard]] ProceduralNode* ProceduralNode::fromVal(const Value* val) {
    const Struct& str = Statics::extractStruct(val, "ProceduralNode", names);
    std::vector<float> mins = Statics::extractVec(str[0], names[0], 3);
    std::vector<float> maxs = Statics::extractVec(str[1], names[1], 3);

    const Value* opaque_v = str[2];
    if (opaque_v == nullptr || opaque_v->getType().getBase() != DataType::BOOL)
        throw std::runtime_error("ProceduralNode field \"opaque\" must be a boolean!");
    bool opaque = static_cast<const Primitive*>(opaque_v)->data.b32;

    uint32_t geom_index = Statics::extractUint(str[3], names[3]);
    uint32_t prim_index = Statics::extractUint(str[4], names[4]);

    return new ProceduralNode(mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2], opaque, geom_index, prim_index);
}

[[nodiscard]] Struct* ProceduralNode::toStruct() const {
    std::vector<Value*> min_v{
        new Primitive(minBounds.x), new Primitive(minBounds.y), new Primitive(minBounds.z)
    };
    auto* mins = new Array(min_v);
    std::vector<Value*> max_v{
        new Primitive(maxBounds.x), new Primitive(maxBounds.y), new Primitive(maxBounds.z)
    };
    auto* maxs = new Array(max_v);
    std::vector<Value*> fields{
        mins, maxs, new Primitive(opaque), new Primitive(geomIndex), new Primitive(primIndex)
    };
    return new Struct(fields, names);
}
