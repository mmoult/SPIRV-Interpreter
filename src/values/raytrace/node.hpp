/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_RAYTRACE_NODE_HPP
#define VALUES_RAYTRACE_NODE_HPP

#include <cstdint>
#include <tuple>
#include <sstream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>

#include "../value.hpp"
import value.aggregate;
import value.primitive;
import value.raytrace.trace;
import value.statics;

// These also are an unwanted Vulkan/SPIR-V dependency.
static constexpr unsigned HIT_KIND_FRONT_FACING_TRIANGLE_KHR = 0xFE;
static constexpr unsigned HIT_KIND_BACK_FACING_TRIANGLE_KHR = 0xFF;

class Node {
public:
    virtual ~Node() = default;

    virtual void resolveReferences(
        const std::vector<Node*>& nodes,
        unsigned box,
        unsigned inst,
        unsigned tri
    ) = 0;

    virtual bool step(Trace& trace) const = 0;

    [[nodiscard]] virtual Struct* toStruct() const = 0;
};

struct NodeReference {
    Node* ptr;
    unsigned major;
    unsigned minor;

    NodeReference(unsigned major, unsigned minor): ptr(nullptr), major(major), minor(minor) {}

    [[nodiscard]] inline Array* toArray() const {
        std::vector<Value*> uvec2{new Primitive(major), new Primitive(minor)};
        return new Array(uvec2);
    }

    inline void resolve(const std::vector<Node*>& nodes, unsigned box, unsigned inst, unsigned tri) {
        unsigned index;
        switch (major) {
        default: // 0
            index = 0;
            break;
        case 1:
            index = box;
            break;
        case 2:
            index = inst;
            break;
        case 3:
            index = tri;
            break;
        }
        ptr = nodes[index];
    }
};

class BoxNode : public Node {
    inline static Type type;
    inline static Type childNodesType;
    inline static const std::vector<std::string> names{"min_bounds", "max_bounds", "child_nodes"};

    float min_bounds[3];
    float max_bounds[3];
    std::vector<NodeReference> children;

public:
    BoxNode(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z) {
        min_bounds[0] = min_x;
        min_bounds[1] = min_y;
        min_bounds[2] = min_z;
        max_bounds[0] = max_x;
        max_bounds[1] = max_y;
        max_bounds[2] = max_z;
    }

    inline void resolveReferences(const std::vector<Node*>& nodes, unsigned box, unsigned inst, unsigned tri) override {
        for (auto& ref : children)
            ref.resolve(nodes, box, inst, tri);
    }

    static const Type& getType();

    bool step(Trace& trace) const override;

    [[nodiscard]] static BoxNode* fromVal(const Value* val);

    [[nodiscard]] Struct* toStruct() const override;
};

class InstanceNode : public Node {
    inline static Type type;
    inline static Type mat4x3Type;
    inline static const std::vector<std::string> names{
        "transformation", "child_node", "id", "custom_index", "mask", "sbt_record_offset"
    };

    NodeReference child;
    glm::mat4x3 transformation;
    uint32_t id;
    uint32_t customIndex;
    uint32_t mask;
    uint32_t sbtRecordOffs;

public:
    InstanceNode(
        unsigned major,
        unsigned minor,
        glm::mat4x3& transform,
        uint32_t id,
        uint32_t custom_index,
        uint32_t mask,
        uint32_t sbt_record_offset
    ):
        child(major, minor),
        transformation(transform),
        id(id),
        customIndex(custom_index),
        mask(mask),
        sbtRecordOffs(sbt_record_offset) {}

    inline void resolveReferences(const std::vector<Node*>& nodes, unsigned box, unsigned inst, unsigned tri) override {
        child.resolve(nodes, box, inst, tri);
    }

    static const Type& getType();

    bool step(Trace& trace) const override;

    [[nodiscard]] static InstanceNode* fromVal(const Value* val);

    [[nodiscard]] Struct* toStruct() const override;
};

class TriangleNode : public Node {
    inline static Type type;
    inline static Type mat3Type;
    inline static const std::vector<std::string> names{"geometry_index", "primitive_index", "opaque", "vertices"};

    uint32_t geomIndex;
    uint32_t primIndex;
    bool opaque;
    glm::mat3 vertices;  // 3 x 3D vertices form a triangle

public:
    TriangleNode(uint32_t geometry_index, uint32_t primitive_index, bool opaque, glm::mat3& vertices):
        geomIndex(geometry_index),
        primIndex(primitive_index),
        opaque(opaque),
        vertices(vertices) {}

    inline void resolveReferences(
        const std::vector<Node*>& nodes,
        unsigned box,
        unsigned inst,
        unsigned tri
    ) override {}

    static const Type& getType();

    bool step(Trace& trace) const override;

    [[nodiscard]] static TriangleNode* fromVal(const Value* val);

    [[nodiscard]] Struct* toStruct() const override;
};

class ProceduralNode : public Node {
    inline static Type type;
    inline static const std::vector<std::string> names{
        "min_bounds", "max_bounds", "opaque", "geometry_index", "primitive_index"
    };

    float min_bounds[3];
    float max_bounds[3];
    bool opaque;
    uint32_t geomIndex;
    uint32_t primIndex;

public:
    ProceduralNode(
        float min_x,
        float min_y,
        float min_z,
        float max_x,
        float max_y,
        float max_z,
        bool opaque,
        uint32_t geometry_index,
        uint32_t primitive_index
    ): opaque(opaque), geomIndex(geometry_index), primIndex(primitive_index) {
        min_bounds[0] = min_x;
        min_bounds[1] = min_y;
        min_bounds[2] = min_z;
        max_bounds[0] = max_x;
        max_bounds[1] = max_y;
        max_bounds[2] = max_z;
    }

    inline void resolveReferences(
        const std::vector<Node*>& nodes,
        unsigned box,
        unsigned inst,
        unsigned tri
    ) override {}

    static const Type& getType();

    bool step(Trace& trace) const override;

    [[nodiscard]] static ProceduralNode* fromVal(const Value* val);

    [[nodiscard]] Struct* toStruct() const override;
};
#endif
