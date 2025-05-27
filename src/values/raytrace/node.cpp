/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "node.hpp"

#include <cstdint>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/ext.hpp"

#include "../value.hpp"
#include "trace.hpp"
import util.arrayMath;
import util.intersection;
import util.ternary;
import value.aggregate;
import value.primitive;
import value.statics;

const Type& BoxNode::getType() {
    if (type.getBase() != DataType::VOID)
        return type;
    Statics statics;
    statics.init();
    childNodesType = Type::array(0, Statics::uvec2Type);
    const std::vector<const Type*> sub_list {&Statics::vec3Type, &Statics::vec3Type, &childNodesType};
    type = Type::structure(sub_list, names);
    return type;
}

Ternary BoxNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;
    const Intersection candidate = trace.getCandidate();
    auto ray_pos = candidate.getRayPos(trace_p);
    auto ray_dir = candidate.getRayDir(trace_p);

    // If the ray intersects the bounding box, then add its children to be evaluated.
    if (ray_AABB_intersect(ray_pos, ray_dir, trace.rayTMin, trace.rayTMax, minBounds, maxBounds)) {
        for (const auto& child_ref : children) {
            // Most of the fields are the same (such as origin and direction), so copy from parent
            Intersection& cand = trace.candidates.emplace_back(candidate);
            // With exception of the next node to search, which must be updated.
            cand.search = child_ref.ptr;
        }
    }
    return Ternary::NO;
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
    std::vector<Value*> min_v {new Primitive(minBounds.x), new Primitive(minBounds.y), new Primitive(minBounds.z)};
    fields[0] = new Array(min_v);
    std::vector<Value*> max_v {new Primitive(maxBounds.x), new Primitive(maxBounds.y), new Primitive(maxBounds.z)};
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
    // matrix with 4 columns and 3 rows. This is confusing because columns are stored horizontally
    mat4x3Type = Type::array(4, Statics::vec3Type);
    const std::vector<const Type*> sub_list {
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

Ternary InstanceNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;

    // Do not process this instance if it's invisible to the ray.
    if ((this->mask & trace.cullMask) == 0)
        return Ternary::NO;

    // Transform the ray to match the instance's object-space.
    const Intersection& before = trace.getCandidate();
    glm::mat4 world_to_obj = this->worldToObj * before.worldToObj;
    glm::mat4 obj_to_world = this->inverse * before.objToWorld;

    Intersection& cand = trace.candidates.emplace_back(before);
    cand.search = child.ptr;
    cand.worldToObj = world_to_obj;
    cand.objToWorld = obj_to_world;
    cand.instance = this;

    return Ternary::NO;
}

[[nodiscard]] InstanceNode* InstanceNode::fromVal(const Value* val) {
    const Struct& str = Statics::extractStruct(val, "InstanceNode", names);
    const Array& transform = Statics::extractArray(str[0], names[0]);
    if (transform.getSize() != 4)
        throw std::runtime_error("InstanceNode field \"world_to_obj\" must be a mat4x3!");
    glm::mat4x3 world_to_obj;
    ArrayMath::value_to_glm<glm::mat4x3, 4, 3>(transform, world_to_obj, true);

    std::vector<unsigned> ref = Statics::extractUvec(str[1], names[1], 2);
    uint32_t id = Statics::extractUint(str[2], names[2]);
    uint32_t custom_index = Statics::extractUint(str[3], names[3]);
    uint32_t mask = Statics::extractUint(str[4], names[4]);
    uint32_t sbt_record_offset = Statics::extractUint(str[5], names[5]);

    return new InstanceNode(ref[0], ref[1], world_to_obj, id, custom_index, mask, sbt_record_offset);
}

[[nodiscard]] Struct* InstanceNode::toStruct() const {
    std::vector<Value*> cols(4, nullptr);
    for (unsigned i = 0; i < 4; ++i) {
        std::vector<Value*> row(3, nullptr);
        for (unsigned j = 0; j < 3; ++j)
            row[j] = new Primitive(this->worldToObj[i][j]);
        cols[i] = new Array(row);
    }
    std::vector<Value*> fields {
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
    const std::vector<const Type*> sub_list {&Statics::uintType, &Statics::uintType, &Statics::boolType, &mat3Type};
    type = Type::structure(sub_list, names);
    return type;
}

Ternary TriangleNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;
    // Check skip triangle ray flag.
    if (trace.rayFlags.skipTriangles())
        return Ternary::NO;

    // Check opaque related ray flags.
    bool is_opaque = this->opaque;
    if (trace.rayFlags.opaque())
        is_opaque = true;
    else if (trace.rayFlags.noOpaque())
        is_opaque = false;

    if ((trace.rayFlags.cullOpaque() && is_opaque) || (trace.rayFlags.cullNoOpaque() && !is_opaque))
        return Ternary::NO;

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
        return Ternary::NO;

    // Update candidate
    candidate.hitT = t;
    candidate.barycentrics = glm::vec2(u, v);
    candidate.isOpaque = this->opaque;
    candidate.geometryIndex = this->geomIndex;
    candidate.primitiveIndex = this->primIndex;
    candidate.hitKind = entered_front ? HitKind::FRONT_FACING_TRIANGLE : HitKind::BACK_FACING_TRIANGLE;
    candidate.type = Intersection::Type::Triangle;
    return (this->opaque || !trace.useSBT) ? Ternary::YES : Ternary::MAYBE;
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
    std::vector<Value*>
        fields {new Primitive(geomIndex), new Primitive(primIndex), new Primitive(opaque), new Array(cols)};
    return new Struct(fields, names);
}

const Type& ProceduralNode::getType() {
    if (type.getBase() != DataType::VOID)
        return type;
    Statics statics;
    statics.init();
    const std::vector<const Type*>
        sub_list {&Statics::vec3Type, &Statics::vec3Type, &Statics::boolType, &Statics::uintType, &Statics::uintType};
    type = Type::structure(sub_list, names);
    return type;
}

Ternary ProceduralNode::step(Trace* trace_p) const {
    Trace& trace = *trace_p;

    // Check skip AABBs (procedurals) flag
    if (trace.rayFlags.skipAABBs())
        return Ternary::NO;

    // Check opaque related ray flags.
    bool is_opaque = this->opaque;
    if (trace.rayFlags.opaque())
        is_opaque = true;
    else if (trace.rayFlags.noOpaque())
        is_opaque = false;

    if ((trace.rayFlags.cullOpaque() && is_opaque) || (trace.rayFlags.cullNoOpaque() && !is_opaque))
        return Ternary::NO;

    Intersection& candidate = trace.getCandidate();
    auto ray_pos = candidate.getRayPos(trace_p);
    auto ray_dir = candidate.getRayDir(trace_p);

    bool found = ray_AABB_intersect(ray_pos, ray_dir, trace.rayTMin, trace.rayTMax, this->minBounds, this->maxBounds);

    if (!found)
        return Ternary::NO;

    // Assume that the intersection is successful, we can backpeddle if it turns out not to be true.
    candidate.isOpaque = this->opaque;
    candidate.geometryIndex = this->geomIndex;
    candidate.primitiveIndex = this->primIndex;
    candidate.type = Intersection::Type::AABB;
    return trace.useSBT ? Ternary::MAYBE : Ternary::YES;
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
    std::vector<Value*> min_v {new Primitive(minBounds.x), new Primitive(minBounds.y), new Primitive(minBounds.z)};
    auto* mins = new Array(min_v);
    std::vector<Value*> max_v {new Primitive(maxBounds.x), new Primitive(maxBounds.y), new Primitive(maxBounds.z)};
    auto* maxs = new Array(max_v);
    std::vector<Value*> fields {mins, maxs, new Primitive(opaque), new Primitive(geomIndex), new Primitive(primIndex)};
    return new Struct(fields, names);
}
