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

// Forward declaration needed for step
struct Trace;

class Node {
public:
    virtual ~Node() = default;

    virtual void resolveReferences(
        const std::vector<Node*>& nodes,
        unsigned box,
        unsigned inst,
        unsigned tri
    ) = 0;

    virtual bool step(Trace* trace) const = 0;

    [[nodiscard]] virtual Struct* toStruct() const = 0;
};

struct NodeReference {
    const Node* ptr;
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

    glm::vec3 minBounds;
    glm::vec3 maxBounds;
    std::vector<NodeReference> children;

public:
    BoxNode(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z) {
        minBounds = glm::vec3(min_x, min_y, min_z);
        maxBounds = glm::vec3(max_x, max_y, max_z);
    }

    inline void resolveReferences(const std::vector<Node*>& nodes, unsigned box, unsigned inst, unsigned tri) override {
        for (auto& ref : children)
            ref.resolve(nodes, box, inst, tri);
    }

    static const Type& getType();

    bool step(Trace* trace) const override;

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
    glm::mat4x3 transformation;  // column-major order
    uint32_t id;                 // Id relative to other instance nodes in the same acceleration structure
    uint32_t customIndex;        // For shading
    uint32_t mask;               // Mask that can make the ray ignore this instance
    uint32_t sbtRecordOffs;      // Shader binding table record offset (a.k.a. hit group id)

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

    bool step(Trace* trace) const override;

    uint32_t getId() const {
        return id;
    }
    uint32_t getCustomIndex() const {
        return customIndex;
    }

    [[nodiscard]] static InstanceNode* fromVal(const Value* val);
    [[nodiscard]] Struct* toStruct() const override;
};

class TriangleNode : public Node {
    inline static Type type;
    inline static Type mat3Type;
    inline static const std::vector<std::string> names{"geometry_index", "primitive_index", "opaque", "vertices"};

    uint32_t geomIndex;  // Geometry this node is a part of
    uint32_t primIndex;  // Index of node in geometry
    bool opaque;         // Whether this triangle is opaque
    std::vector<glm::vec3> vertices;  // 3 x 3D vertices form a triangle

public:
    TriangleNode(uint32_t geometry_index, uint32_t primitive_index, bool opaque, std::vector<glm::vec3>& verts):
            geomIndex(geometry_index),
            primIndex(primitive_index),
            opaque(opaque) {
        assert(verts.size() == 3);
        this->vertices.resize(3);
        for (unsigned i = 0; i < 3; ++i)
            this->vertices[i] = verts[i];
    }

    inline void resolveReferences(
        const std::vector<Node*>& nodes,
        unsigned box,
        unsigned inst,
        unsigned tri
    ) override {}

    static const Type& getType();

    bool step(Trace* trace) const override;

    [[nodiscard]] static TriangleNode* fromVal(const Value* val);
    [[nodiscard]] Struct* toStruct() const override;
};

class ProceduralNode : public Node {
    inline static Type type;
    inline static const std::vector<std::string> names{
        "min_bounds", "max_bounds", "opaque", "geometry_index", "primitive_index"
    };

    glm::vec3 minBounds;
    glm::vec3 maxBounds;
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
        minBounds = glm::vec3(min_x, min_y, min_z);
        maxBounds = glm::vec3(max_x, max_y, max_z);
    }

    inline void resolveReferences(
        const std::vector<Node*>& nodes,
        unsigned box,
        unsigned inst,
        unsigned tri
    ) override {}

    static const Type& getType();

    bool step(Trace* trace) const override;

    [[nodiscard]] static ProceduralNode* fromVal(const Value* val);
    [[nodiscard]] Struct* toStruct() const override;
};
#endif
