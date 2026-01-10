/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "accel-struct.hpp"

#include "../statics.hpp"

Ternary AccelStruct::stepTrace() {
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

Ternary AccelStruct::traceRay(bool skip_trace) {
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
            else {
                assert(candidate.type == Intersection::Type::AABB);
                generateIntersection(candidate.hitT);
            }
        }
    } while (found_primitive == Ternary::YES);

    if (intersect_once)
        return Ternary::YES;
    return found_primitive;  // could be no for no more to trace or maybe for something to check
}

[[nodiscard]] Struct* AccelStruct::toStruct() const {
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

void AccelStruct::copyFrom(const Value& new_val) noexcept(false) {
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
