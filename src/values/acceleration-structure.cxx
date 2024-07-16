/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <memory>
#include <stack>
#include <string>
#include <tuple>

// TODO: plan to remove/change header(s) below
#include <iostream>

// TODO: probably want to reduce GLM files to link
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include "../external/spirv.hpp"
#include "type.hpp"
#include "value.hpp"

export module value.accelerationStructure;
import value.aggregate;
import value.primitive;

namespace Util {
    std::string repeatedString(const unsigned num, const std::string str) {
        std::stringstream result("");
        for (unsigned i = 0; i < num; ++i)
            result << str;
        return result.str();
    }

    std::string glmVec3ToString(const glm::vec3 vec) {
        std::stringstream result("");

        result << "[ ";
        for (unsigned i = 0; i < vec.length() - 1; ++i) {
            result << vec[i] << ", ";
        }
        result << vec[vec.length() - 1] << " ]";

        return result.str();
    }
}

/*
    TODO: convert raw pointers to smart pointers to deal with memory reclaimation
    TODO: may want to change how to identify nodes or how nodes work
    TODO: handle the effects of winding order on intersections; right now, front face is CCW
*/
class AccelerationStructure {
private:
    enum class NodeType { Box, Instance, Triangle, Procedural };

    struct Node {
        virtual std::shared_ptr<Node> clone() const = 0;
        virtual NodeType type() const = 0;
        virtual std::string toString(const unsigned indent = 0, const std::string indent_string = "") const = 0;
    };

    struct BoxNode : public Node {
        const glm::vec4 minBounds; // min x, y, z, w
        const glm::vec4 maxBounds; // max x, y, z, w
        const std::vector<std::shared_ptr<Node>> children;

        /// @brief TODO
        /// @param min_bounds 
        /// @param max_bounds 
        /// @param children
        BoxNode(const glm::vec4& min_bounds,
                const glm::vec4& max_bounds,
                const std::vector<std::shared_ptr<Node>>& children)
            : minBounds(min_bounds),
              maxBounds(max_bounds),
              children([](const std::vector<std::shared_ptr<Node>>& children) -> std::vector<std::shared_ptr<Node>> {
                  std::vector<std::shared_ptr<Node>> result;
                  for (const auto& child : children)
                      result.push_back(child->clone());
                  return result;
              }(children)) {}

        std::shared_ptr<Node> clone() const {
            std::vector<std::shared_ptr<Node>> childrenCopies;
            for (const auto& child : children) {
                childrenCopies.push_back(child->clone());
            }
            return std::make_shared<BoxNode>(minBounds, maxBounds, childrenCopies);
        }

        NodeType type() const {
            return NodeType::Box;
        }

        std::string toString(const unsigned indent = 0, const std::string indent_string = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indent_string) << "box_node" << std::endl;

            result << Util::repeatedString(indent + 1, indent_string)
                   << "min_bounds = " << Util::glmVec3ToString(minBounds) << std::endl;

            result << Util::repeatedString(indent + 1, indent_string)
                   << "max_bounds = " << Util::glmVec3ToString(maxBounds) << std::endl;

            result << Util::repeatedString(indent + 1, indent_string) << "num_children = " << children.size()
                   << std::endl;

            return result.str();
        }
    };

    struct InstanceNode : public Node {
        const glm::mat4x3 objectToWorld; // Column-major order
        const glm::mat4x3 worldToObject; // Column-major order
        const unsigned id; // Id relative to other instance nodes in the same acceleration structure
        const unsigned customIndex; // For shading
        const unsigned geometryIndex; // Geometry this node is a part of
        const unsigned primitiveIndex; // Index of node in geometry
        const unsigned mask;
        const unsigned sbtRecordOffset; // Shader binding table record offset
        const std::shared_ptr<AccelerationStructure> accelerationStructure;

        InstanceNode(const glm::mat4x3& object_to_world,
                const unsigned id,
                const unsigned custom_index,
                const unsigned geometry_index,
                const unsigned primitive_index,
                const unsigned mask,
                const unsigned sbt_record_offset,
                const std::shared_ptr<AccelerationStructure>& accel_struct)
            : objectToWorld(object_to_world),
              worldToObject([](const glm::mat4x3& object_to_world) -> glm::mat4x3 {
                  glm::mat4x4 temp(1.0f);  // Turn 3x4 to 4x4 so we can invert it
                  for (int col = 0; col < 4; ++col)
                      for (int row = 0; row < 3; ++row)
                          temp[col][row] = object_to_world[col][row];
                  return glm::inverse(temp);
              }(objectToWorld)),
              id(id),
              customIndex(custom_index),
              geometryIndex(geometry_index),
              primitiveIndex(primitive_index),
              mask(mask),
              sbtRecordOffset(sbt_record_offset),
              accelerationStructure(std::make_shared<AccelerationStructure>(*accel_struct)) {};

        std::shared_ptr<Node> clone() const {
            return std::make_shared<InstanceNode>(objectToWorld,
                    id,
                    customIndex,
                    geometryIndex,
                    primitiveIndex,
                    mask,
                    sbtRecordOffset,
                    accelerationStructure);
        }

        NodeType type() const {
            return NodeType::Instance;
        }

        std::string toString(const unsigned indent = 0, const std::string indent_string = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indent_string) << "instance_node" << std::endl;

            // Object-to-world
            result << Util::repeatedString(indent + 1, indent_string) << "object_to_world_matrix = [" << std::endl;
            unsigned num_rows = objectToWorld.length();
            assert(num_rows > 0);
            unsigned num_cols = objectToWorld[0].length();
            for (unsigned row = 0; row < num_rows; ++row) {
                result << Util::repeatedString(indent + 2, indent_string) << "[ ";
                for (unsigned col = 0; col < num_cols - 1; ++col) {
                    result << objectToWorld[row][col] << ", ";
                }
                result << objectToWorld[row][num_cols - 1] << " ]" << std::endl;
            }
            result << Util::repeatedString(indent + 1, indent_string) << "]" << std::endl;

            // World-to-object
            result << Util::repeatedString(indent + 1, indent_string) << "world_to_object_matrix = [" << std::endl;
            num_rows = worldToObject.length();
            assert(num_rows > 0);
            num_cols = worldToObject[0].length();
            for (unsigned row = 0; row < num_rows; ++row) {
                result << Util::repeatedString(indent + 2, indent_string) << "[ ";
                for (unsigned col = 0; col < num_cols - 1; ++col) {
                    result << worldToObject[row][col] << ", ";
                }
                result << worldToObject[row][num_cols - 1] << " ]" << std::endl;
            }
            result << Util::repeatedString(indent + 1, indent_string) << "]" << std::endl;

            // Id
            result << Util::repeatedString(indent + 1, indent_string) << "id = " << id << std::endl;

            // Custom index
            result << Util::repeatedString(indent + 1, indent_string) << "custom_index = " << customIndex << std::endl;

            // Geometry index
            result << Util::repeatedString(indent + 1, indent_string) << "geometry_index = " << geometryIndex
                   << std::endl;

            // Primitive index
            result << Util::repeatedString(indent + 1, indent_string) << "primitive_index = " << primitiveIndex
                   << std::endl;

            // Mask
            result << Util::repeatedString(indent + 1, indent_string) << "mask = " << mask << std::endl;

            // Shader binding table record offset
            result << Util::repeatedString(indent + 1, indent_string)
                   << "shader_binding_table_record_offset = " << sbtRecordOffset << std::endl;

            // Acceleration structure pointer
            result << Util::repeatedString(indent + 1, indent_string)
                   << "points_to_acceleration_structure_id = " << accelerationStructure->id << std::endl;

            return result.str();
        }
    };

    struct TriangleNode : public Node {
        const unsigned geometryIndex; // Geometry this node is a part of
        const unsigned primitiveIndex; // Index of node in geometry
        const bool opaque;
        const std::vector<glm::vec3> vertices;
        const std::vector<unsigned> indices;

        TriangleNode(const unsigned geometry_index,
                const unsigned primitive_index,
                const bool opaque,
                const std::vector<glm::vec3>& vertices,
                const std::vector<unsigned>& indices)
            : geometryIndex(geometry_index),
              primitiveIndex(primitive_index),
              opaque(opaque),
              vertices(vertices),
              indices(indices) {};

        std::shared_ptr<Node> clone() const {
            return std::make_shared<TriangleNode>(geometryIndex, primitiveIndex, opaque, vertices, indices);
        }

        NodeType type() const {
            return NodeType::Triangle;
        }

        std::string toString(const unsigned indent = 0, const std::string indent_string = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indent_string) << "triangle_node" << std::endl;

            // Geometry index
            result << Util::repeatedString(indent + 1, indent_string) << "geometry_index = " << geometryIndex
                   << std::endl;

            // Primitive index
            result << Util::repeatedString(indent + 1, indent_string) << "primitive_index = " << primitiveIndex
                   << std::endl;

            // Opaque
            result << Util::repeatedString(indent + 1, indent_string) << "opaque = " << (opaque ? "true" : "false")
                   << std::endl;

            // Vertices
            result << Util::repeatedString(indent + 1, indent_string) << "vertices = [" << std::endl;
            for (unsigned i = 0; i < vertices.size() - 1; ++i) {
                result << Util::repeatedString(indent + 2, indent_string) << Util::glmVec3ToString(vertices[i]) << ","
                       << std::endl;
            }
            result << Util::repeatedString(indent + 2, indent_string)
                   << Util::glmVec3ToString(vertices[vertices.size() - 1]) << std::endl;
            result << Util::repeatedString(indent + 1, indent_string) << "]" << std::endl;

            // Indices
            result << Util::repeatedString(indent + 1, indent_string) << "indices = [ ";
            for (unsigned i = 0; i < indices.size() - 1; ++i) {
                result << indices[i] << ", ";
            }
            result << indices[indices.size() - 1] << " ]" << std::endl;

            return result.str();
        }
    };

    struct ProceduralNode : public Node {
        const unsigned geometryIndex; // Geometry this node is a part of
        const unsigned primitiveIndex; // Index of node in geometry
        const bool opaque;
        const glm::vec4 minBounds;
        const glm::vec4 maxBounds;

        ProceduralNode(const unsigned geometry_index,
                const unsigned primitive_index,
                const bool opaque,
                const glm::vec4& min_bounds,
                const glm::vec4& max_bounds)
            : geometryIndex(geometry_index),
              primitiveIndex(primitive_index),
              opaque(opaque),
              minBounds(min_bounds),
              maxBounds(max_bounds) {};

        std::shared_ptr<Node> clone() const {
            return std::make_shared<ProceduralNode>(geometryIndex, primitiveIndex, opaque, minBounds, maxBounds);
        }

        NodeType type() const {
            return NodeType::Procedural;
        }

        std::string toString(const unsigned indent = 0, const std::string indent_string = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indent_string) << "procedural_node" << std::endl;

            // Geometry index
            result << Util::repeatedString(indent + 1, indent_string) << "geometry_index = " << geometryIndex
                   << std::endl;

            // Primitive index
            result << Util::repeatedString(indent + 1, indent_string) << "primitive_index = " << primitiveIndex
                   << std::endl;

            // Opaque
            result << Util::repeatedString(indent + 1, indent_string) << "opaque = " << (opaque ? "true" : "false")
                   << std::endl;

            // Min bounds
            result << Util::repeatedString(indent + 1, indent_string)
                   << "min_bounds = " << Util::glmVec3ToString(minBounds) << std::endl;

            // Max bounds
            result << Util::repeatedString(indent + 1, indent_string)
                   << "max_bounds = " << Util::glmVec3ToString(maxBounds) << std::endl;

            return result.str();
        }
    };

    const bool isTLAS;  // true: TLAS, false: BLAS
    std::shared_ptr<Node> root;  // Start of this acceleration structure

public:
    const unsigned id;

    /// @brief TODO: description
    /// @param id
    /// @param structure_info 
    /// @param node_counts 
    /// @param all_accel_structs 
    /// @param num_accel_structs
    AccelerationStructure(const unsigned id,
            const Struct& structure_info,
            const Struct& node_counts,
            std::vector<std::shared_ptr<AccelerationStructure>>& all_accel_structs,
            const unsigned num_accel_structs)
        : id(id),
          isTLAS(static_cast<const Primitive&>(*(structure_info[0])).data.b32) {

        // Get node information
        const unsigned num_box_nodes = static_cast<const Primitive&>(*(node_counts[0])).data.u32;
        const unsigned num_instance_nodes = static_cast<const Primitive&>(*(node_counts[1])).data.u32;
        const unsigned num_triangle_nodes = static_cast<const Primitive&>(*(node_counts[2])).data.u32;
        const unsigned num_procedural_nodes = static_cast<const Primitive&>(*(node_counts[3])).data.u32;
        
        const unsigned num_non_instance_nodes = num_triangle_nodes + num_procedural_nodes;
        assert((num_instance_nodes == 0 && num_non_instance_nodes > 0) || (num_instance_nodes > 0 && num_non_instance_nodes == 0));

        // Construct the nodes bottom-up
        const unsigned offset = 1;  // offset of struct fields to the start of nodes
        const unsigned num_nodes = num_box_nodes + num_instance_nodes + num_non_instance_nodes;
        std::vector<std::shared_ptr<Node>> nodes;

        // Procedural nodes
        for (unsigned i = 0; i < num_procedural_nodes; ++i) {
            const Struct& primitive_info = static_cast<const Struct&>(*(structure_info[num_nodes + offset - 1 - i]));

            // Geometry index
            const unsigned geometry_index = static_cast<const Primitive&>(*(primitive_info[0])).data.u32;

            // Primitive index
            const unsigned primitive_index = static_cast<const Primitive&>(*(primitive_info[1])).data.u32;

            // Opaque
            const bool opaque = static_cast<const Primitive&>(*(primitive_info[2])).data.b32;

            // Bounds
            const Array& min_bounds_info = static_cast<const Array&>(*(primitive_info[3]));
            const Array& max_bounds_info = static_cast<const Array&>(*(primitive_info[4]));
            glm::vec4 min_bounds;
            glm::vec4 max_bounds;
            assert(min_bounds_info.getSize() == max_bounds_info.getSize());
            for (unsigned j = 0; j < min_bounds_info.getSize(); ++j) {
                min_bounds[j] = static_cast<const Primitive&>(*(min_bounds_info[j])).data.fp32;
                max_bounds[j] = static_cast<const Primitive&>(*(max_bounds_info[j])).data.fp32;
            }
            min_bounds.w = 1.0f;
            max_bounds.w = 1.0f;

            nodes.push_back(
                    std::make_shared<ProceduralNode>(geometry_index, primitive_index, opaque, min_bounds, max_bounds));
        }

        // Triangle node
        for (unsigned i = 0; i < num_triangle_nodes; ++i) {
            const Struct& primitive_info =
                    static_cast<const Struct&>(*(structure_info[num_nodes + offset - 1 - i - num_procedural_nodes]));

            // Geometry index
            const unsigned geometry_index = static_cast<const Primitive&>(*(primitive_info[0])).data.u32;

            // Primitive index
            const unsigned primitive_index = static_cast<const Primitive&>(*(primitive_info[1])).data.u32;

            // Opaque
            const bool opaque = static_cast<const Primitive&>(*(primitive_info[2])).data.b32;

            // Vertices
            std::vector<glm::vec3> vertices;
            const Array& verticesInfo = static_cast<const Array&>(*(primitive_info[3]));
            for (unsigned j = 0; j < verticesInfo.getSize(); ++j) {
                glm::vec3 vertex;
                const Array& vertexInfo = static_cast<const Array&>(*(verticesInfo[j]));
                assert(vertexInfo.getSize() == 3);
                for (unsigned k = 0; k < vertexInfo.getSize(); ++k) {
                    vertex[k] = static_cast<const Primitive&>(*(vertexInfo[k])).data.fp32;
                }
                vertices.push_back(vertex);
            }

            // Indices
            std::vector<unsigned> indices;
            const Array& indicesInfo = static_cast<const Array&>(*(primitive_info[4]));
            for (unsigned j = 0; j < indicesInfo.getSize(); ++j) {
                unsigned value = static_cast<const Primitive&>(*(indicesInfo[j])).data.u32;
                indices.push_back(value);
            }

            nodes.push_back(std::make_shared<TriangleNode>(geometry_index, primitive_index, opaque, vertices, indices));
        }

        // Instance nodes
        for (unsigned i = 0; i < num_instance_nodes; ++i) {
            const Struct& instanceInfo =
                    static_cast<const Struct&>(*(structure_info[num_nodes + offset - 1 - i - num_non_instance_nodes]));

            // Object-to-world matrix
            glm::mat4x3 object_to_world_matrix;  // GLM stores in column-major order
            const Array& object_to_world_matrix_info = static_cast<const Array&>(*(instanceInfo[0]));
            for (unsigned rowIndex = 0; rowIndex < object_to_world_matrix_info.getSize(); ++rowIndex) {
                const Array& rowInfo = static_cast<const Array&>(*(object_to_world_matrix_info[rowIndex]));
                for (unsigned colIndex = 0; colIndex < rowInfo.getSize(); ++colIndex) {
                    float value = static_cast<const Primitive&>(*(rowInfo[colIndex])).data.fp32;
                    object_to_world_matrix[colIndex][rowIndex] = value;
                }
            }

            // Id
            const unsigned id = static_cast<const Primitive&>(*(instanceInfo[1])).data.u32;

            // Custom index
            const unsigned custom_index = static_cast<const Primitive&>(*(instanceInfo[2])).data.u32;

            // Geometry index
            const unsigned geometry_index = static_cast<const Primitive&>(*(instanceInfo[3])).data.u32;

            // Primitive index
            const unsigned primitive_index = static_cast<const Primitive&>(*(instanceInfo[4])).data.u32;

            // Mask
            const unsigned mask = static_cast<const Primitive&>(*(instanceInfo[5])).data.u32;

            // Shader binding table record offset
            const unsigned sbt_record_offset = static_cast<const Primitive&>(*(instanceInfo[6])).data.u32;

            // Get respective acceleration structure
            const unsigned accel_struct_index = static_cast<const Primitive&>(*(instanceInfo[7])).data.u32;
            const unsigned index = num_accel_structs - 1 - accel_struct_index;
            std::shared_ptr<AccelerationStructure>& as = all_accel_structs[index];

            nodes.push_back(std::make_shared<InstanceNode>(object_to_world_matrix,
                    id,
                    custom_index,
                    geometry_index,
                    primitive_index,
                    mask,
                    sbt_record_offset,
                    as));
        }

        // Box nodes
        assert(num_non_instance_nodes == 0 || num_instance_nodes == 0);
        for (unsigned i = 0; i < num_box_nodes; ++i) {
            const Struct& box_info = static_cast<const Struct&>(
                    *(structure_info[num_nodes + offset - 1 - i - num_non_instance_nodes - num_instance_nodes]));

            const Array& min_bounds_info = static_cast<const Array&>(*(box_info[0]));
            const Array& max_bounds_info = static_cast<const Array&>(*(box_info[1]));
            glm::vec4 min_bounds;
            glm::vec4 max_bounds;
            assert(min_bounds_info.getSize() == max_bounds_info.getSize());
            for (unsigned j = 0; j < min_bounds_info.getSize(); ++j) {
                min_bounds[j] = static_cast<const Primitive&>(*(min_bounds_info[j])).data.fp32;
                max_bounds[j] = static_cast<const Primitive&>(*(max_bounds_info[j])).data.fp32;
            }
            min_bounds.w = 1.0f;
            max_bounds.w = 1.0f;
        
            const Array& children_indices_info = static_cast<const Array&>(*(box_info[2]));
            std::vector<unsigned> children_indices;
            for (unsigned j = 0; j < children_indices_info.getSize(); ++j) {
                unsigned child_index = static_cast<const Primitive&>(*(children_indices_info[j])).data.u32;
                children_indices.push_back(child_index);
            }

            // Get the actual children
            std::vector<std::shared_ptr<Node>> children;
            for (const auto& child_index : children_indices) {
                assert(nodes[num_nodes - 1 - child_index] != nullptr);
                children.push_back(std::move(nodes[num_nodes - 1 - child_index]));
            }
        
            nodes.push_back(std::make_shared<BoxNode>(min_bounds, max_bounds, children));
        }

        // Assertion to make sure nodes list is within expectation
        unsigned root_index = num_nodes - 1;
        for (unsigned i = 0; i < nodes.size(); ++i)
            assert((i != root_index && nodes[i] == nullptr) || (i == root_index && nodes[i] != nullptr));

        // Set the root node
        root = std::move(nodes[num_nodes - 1]);

        // All shared_ptr in "nodes" should be null, so the root should be null
        assert(nodes[root_index] == nullptr);
    };

    AccelerationStructure(const AccelerationStructure& other): id(other.id), isTLAS(other.isTLAS) {
        activeTrace = other.activeTrace;
        candidateIntersection = other.candidateIntersection;
        committedIntersection = other.committedIntersection;

        cullMask = other.cullMask;
        didPopNodePreviously = other.didPopNodePreviously;
        nodesToEval = other.nodesToEval;
        root = other.root->clone();

        useSBT = other.useSBT;
        missIndex = other.missIndex;
        offsetSBT = other.offsetSBT;
        strideSBT = other.strideSBT;

        rayDirection = other.rayDirection;
        rayOrigin = other.rayOrigin;
        rayTMin = other.rayTMin;
        rayTMax = other.rayTMax;
        rayFlags = other.rayFlags;
        setFlags();
    }

    ~AccelerationStructure() {};

private:
    using ConstNodeRef = const std::shared_ptr<Node>*;

    std::stack<ConstNodeRef> nodesToEval;
    bool activeTrace = false;

    // Intersection related
    enum class CommittedIntersectionType { None = 0, Triangle = 1, Generated = 2 };
    enum class CandidateIntersectionType { Triangle = 0, AABB = 1 };

    struct IntersectionProperties {
        std::shared_ptr<InstanceNode> instance = nullptr;
        int geometryIndex = -1;
        int primitiveIndex = -1;
        float hitT = std::numeric_limits<float>::max();
        glm::vec2 barycentrics = glm::vec2(0.0f, 0.0f);
        bool isOpaque = true;
        bool enteredTriangleFrontFace = false;

        void reset() {
            instance = nullptr;
            geometryIndex = -1;
            primitiveIndex = -1;
            hitT = std::numeric_limits<float>::max();
            barycentrics = glm::vec2(0.0f, 0.0f);
            isOpaque = true;
            enteredTriangleFrontFace = false;
        }

        IntersectionProperties& operator=(const IntersectionProperties& other) {
            instance = other.instance;
            geometryIndex = other.geometryIndex;
            primitiveIndex = other.primitiveIndex;
            hitT = other.hitT;
            barycentrics = other.barycentrics;
            isOpaque = other.isOpaque;
            enteredTriangleFrontFace = other.enteredTriangleFrontFace;

            return *this;
        }
    };
    struct CandidateIntersection {
        CandidateIntersectionType type = CandidateIntersectionType::Triangle;
        IntersectionProperties properties{};

        void reset() {
            type = CandidateIntersectionType::Triangle;
            properties.reset();
        }

        void update(const bool is_triangle, const IntersectionProperties& new_properties) {
            type = is_triangle ? CandidateIntersectionType::Triangle : CandidateIntersectionType::AABB;
            properties = new_properties;
        }

        CandidateIntersection& operator=(const CandidateIntersection& other) {
            type = other.type;
            properties = other.properties;

            return *this;
        }
    };
    struct CommittedIntersection {
        CommittedIntersectionType type = CommittedIntersectionType::None;
        IntersectionProperties properties{};

        void reset() {
            type = CommittedIntersectionType::None;
            properties.reset();
        }

        void update(const bool is_triangle, const CandidateIntersection& candidate_intersection) {
            type = is_triangle ? CommittedIntersectionType::Triangle : CommittedIntersectionType::Generated;
            properties = candidate_intersection.properties;
        }

        CommittedIntersection& operator=(const CommittedIntersection& other) {
            type = other.type;
            properties = other.properties;

            return *this;
        }
    };

    CommittedIntersection committedIntersection;
    CandidateIntersection candidateIntersection;

    // Ray properties
    unsigned rayFlags = 0;
    unsigned cullMask = 0;
    glm::vec4 rayOrigin{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec4 rayDirection{ 0.0f, 0.0f, 0.0f, 0.0f };
    float rayTMin = 0.0f;
    float rayTMax = 0.0f;

    // Shader binding table information
    bool useSBT = false;
    unsigned offsetSBT = 0;
    unsigned strideSBT = 0;
    unsigned missIndex = 0;

    // Ray flags
    bool rayFlagNone = false;
    bool rayFlagOpaque = false;
    bool rayFlagNoOpaque = false;
    bool rayFlagTerminateOnFirstHit = false;
    bool rayFlagSkipClosestHitShader = false;
    bool rayFlagCullBackFacingTriangles = false;
    bool rayFlagCullFrontFacingTriangles = false;
    bool rayFlagCullOpaque = false;
    bool rayFlagCullNoOpaque = false;
    bool rayFlagSkipTriangles = false;
    bool rayFlagSkipAABBs = false;

    void setFlags() {
        rayFlagNone = (rayFlags | spv::RayFlagsMask::RayFlagsMaskNone) == 0; // TODO: reason to have this?
        rayFlagOpaque = rayFlags & spv::RayFlagsMask::RayFlagsOpaqueKHRMask;
        rayFlagNoOpaque = rayFlags & spv::RayFlagsMask::RayFlagsNoOpaqueKHRMask;
        rayFlagTerminateOnFirstHit = rayFlags & spv::RayFlagsMask::RayFlagsTerminateOnFirstHitKHRMask;
        rayFlagSkipClosestHitShader = rayFlags & spv::RayFlagsMask::RayFlagsSkipClosestHitShaderKHRMask;
        rayFlagCullBackFacingTriangles = rayFlags & spv::RayFlagsMask::RayFlagsCullBackFacingTrianglesKHRMask;
        rayFlagCullFrontFacingTriangles = rayFlags & spv::RayFlagsMask::RayFlagsCullFrontFacingTrianglesKHRMask;
        rayFlagCullOpaque = rayFlags & spv::RayFlagsMask::RayFlagsCullOpaqueKHRMask;
        rayFlagCullNoOpaque = rayFlags & spv::RayFlagsMask::RayFlagsCullNoOpaqueKHRMask;
        rayFlagSkipTriangles = rayFlags & spv::RayFlagsMask::RayFlagsSkipTrianglesKHRMask;
        rayFlagSkipAABBs = rayFlags & spv::RayFlagsMask::RayFlagsSkipAABBsKHRMask; // skip procedurals
    }

    void clearTrace() {
        while (!nodesToEval.empty()) {
            nodesToEval.pop();
        }
    }

    void initTrace() {
        committedIntersection.reset();
        candidateIntersection.reset();
        activeTrace = true;
        nodesToEval.push(&root);
    }

public:

    void resetTrace() {
        activeTrace = false;
        clearTrace();
        initTrace();
    }
    
    void initTrace(const unsigned& rayFlags,
            const unsigned& cullMask,
            const std::vector<float>& rayOrigin,
            const std::vector<float>& rayDirection,            
            const float& rayTMin,
            const float& rayTMax,
            const bool& useSBT,
            const unsigned offsetSBT = 0,
            const unsigned strideSBT = 0,
            const unsigned missIndex = 0) {
        
        assert(rayOrigin.size() == 3 && rayDirection.size() == 3);
        glm::vec4 convertedRayOrigin = glm::make_vec4(rayOrigin.data());
        convertedRayOrigin.w = 1.0f;
        glm::vec4 convertedRayDirection = glm::make_vec4(rayDirection.data());
        convertedRayDirection.w = 0.0f;

        this->rayFlags = rayFlags;
        this->cullMask = cullMask;
        this->rayOrigin = convertedRayOrigin;
        this->rayDirection = convertedRayDirection;
        this->rayTMin = rayTMin;
        this->rayTMax = rayTMax;

        this->useSBT = useSBT;
        this->offsetSBT = offsetSBT;
        this->strideSBT = strideSBT;
        this->missIndex = missIndex;

        setFlags();
        resetTrace();
    }

    void initTrace(const unsigned ray_flags,
            const unsigned cull_mask,
            const glm::vec3& ray_origin,
            const glm::vec3& ray_direction,
            const float ray_t_min,
            const float ray_t_max,
            const bool use_sbt,
            const unsigned offset_sbt = 0,
            const unsigned stride_sbt = 0,
            const unsigned miss_index = 0) {
                
        this->rayFlags = ray_flags;
        this->cullMask = cull_mask;
        this->rayOrigin = glm::vec4(ray_origin.x, ray_origin.y, ray_origin.z, 1.0f);
        this->rayDirection = glm::vec4(ray_direction.x, ray_direction.y, ray_direction.z, 0.0f);
        this->rayTMin = ray_t_min;
        this->rayTMax = ray_t_max;

        this->useSBT = use_sbt;
        this->offsetSBT = offset_sbt;
        this->strideSBT = stride_sbt;
        this->missIndex = miss_index;

        setFlags();
        resetTrace();
    }

    void initTrace(const unsigned& rayFlags,
            const unsigned& cullMask,
            const glm::vec4& rayOrigin,
            const glm::vec4& rayDirection,
            const float& rayTMin,
            const float& rayTMax,
            const bool& useSBT,
            const unsigned offsetSBT = 0,
            const unsigned strideSBT = 0,
            const unsigned missIndex = 0) {
                
        this->rayFlags = rayFlags;
        this->cullMask = cullMask;
        this->rayOrigin = rayOrigin;
        this->rayDirection = rayDirection;
        this->rayTMin = rayTMin;
        this->rayTMax = rayTMax;

        this->useSBT = useSBT;
        this->offsetSBT = offsetSBT;
        this->strideSBT = strideSBT;
        this->missIndex = missIndex;

        setFlags();
        resetTrace();
    }

private:
    bool didPopNodePreviously = true;

public:
    // TODO: step to the next primitive
    // TODO: change instance node pop and push logic
    bool stepTrace(bool& didIntersectGeometry) {
        if (!activeTrace) {
            std::cout << "Out of paths to trace ray for acceleration structure id = (" << id << ")." << std::endl
                      << "Please (re-)initialize (or reset) the trace." << std::endl;
            return false;
        }

        bool foundPrimitive = false;

        while (!foundPrimitive && (nodesToEval.size() > 0)) {

            ConstNodeRef currNodeRef = nodesToEval.top();
            Node* currNode = currNodeRef->get();
            nodesToEval.pop();

            std::cout << "Trace ray: node type: " << static_cast<unsigned>(currNode->type()) << std::endl;

            switch(currNode->type()) {
            default: {
                std::stringstream err;
                err << "Found unknown node type (" << static_cast<int>(currNode->type())
                    << ") in \"traceRay()\" method of class "
                        "\"AccelerationStructure\" in \"acceleration-structure.cxx\"";
                throw std::runtime_error(err.str());
            }
            case NodeType::Box: {
                // TODO
                BoxNode* boxNode = static_cast<BoxNode*>(currNode); 
                bool result = rayAABBIntersect(rayOrigin, rayDirection, rayTMin, rayTMax, boxNode->minBounds, boxNode->maxBounds);
                if (result) {
                    // Ray intersected; add it's children to be evaluated
                    std::cout << "Ray intersected with AABB in box node" << std::endl;
                    for (const auto& child : boxNode->children) {
                        nodesToEval.push(&child);
                    }
                } else {
                    // Ray didn't intersect
                    std::cout << "Ray did not intersect with AABB" << std::endl;
                }
                break;
            }
            case NodeType::Instance: {
                std::cout << "$$$ INSTANCE" << std::endl;
                InstanceNode* instanceNode = static_cast<InstanceNode*>(currNode);

                // TODO
                // Do not process this instance if it's invisible to the ray
                if ((instanceNode->mask & cullMask) == 0) {
                    std::cout << "\tInstance is invisible to ray" << std::endl;
                    break;
                }

                bool foundGeometryIntersection = false;

                // Transform the ray to match the instance's object-space
                glm::vec3 newRayOrigin = instanceNode->worldToObject * rayOrigin;
                glm::vec3 newRayDirection = instanceNode->worldToObject * rayDirection;
                
                std::cout << "\tInstance node new ray origin and ray direction respectively: " << std::endl;
                std::cout << "\t\tnew origin: " << glm::to_string(newRayOrigin) << std::endl;
                std::cout << "\t\tnew direction: " << glm::to_string(newRayDirection) << std::endl;
                std::cout << std::endl;

                const auto& ref_accel_struct = instanceNode->accelerationStructure;

                // Trace the ray in the respective acceleration structure.
                // Do not pop the node if we can still step through the instance node's acceleration structure.
                if (didPopNodePreviously) {
                    // If we did pop the previous node, then this is the first time we are stepping through the instance
                    // node's acceleration structure.
                    ref_accel_struct->initTrace(rayFlags,
                            cullMask,
                            newRayOrigin,
                            newRayDirection,
                            rayTMin,
                            rayTMax,
                            useSBT,
                            offsetSBT,
                            strideSBT,
                            missIndex);
                }
                didPopNodePreviously = !(ref_accel_struct->stepTrace(foundGeometryIntersection));
                if (!didPopNodePreviously) {
                    nodesToEval.push(currNodeRef);
                }

                // Handle the result of tracing the ray in the instance
                if (foundGeometryIntersection) {
                    didIntersectGeometry = true;
                    
                    // Update candidate
                    candidateIntersection = ref_accel_struct->candidateIntersection;
                    std::cout << "~~~~~~~~~~~~~~~~~updating candidate, just finished copying, now doing instance pointer casting" << std::endl;
                    candidateIntersection.properties.instance = std::static_pointer_cast<InstanceNode>(*currNodeRef);
                    std::cout << "~~~~~~~~~~~~~~~~~successfully casted instance pointer stuff." << std::endl;

                    // Terminate on the first hit if the flag was risen
                    if (rayFlagTerminateOnFirstHit) {
                        std::cout << "********** Terminated on first hit from instance node: " << didIntersectGeometry << std::endl;
                        return false;
                    }
                } else {
                    // Ray did not intersect
                }

                // Instance node's acceleration structure will either reach a primitive mid-search or the final primitive.
                // Therefore, it will always reach a primitive.
                foundPrimitive = true;

                std::cout << "\t!!!!!!!!!!!!!!!!!!!!! Done! Now going back to AS id: " << id << std::endl;

                break;
            }
            case NodeType::Triangle: {
                std::cout << "\t\t\tTesting triangle" << std::endl;
                // Ignore triangle if this flag is true
                if (rayFlagSkipTriangles) {
                    std::cout << "SKIPPING THE TRIANGLES" << std::endl;
                    break;
                }
                
                TriangleNode* triangleNode = static_cast<TriangleNode*>(currNode);
                bool isOpaque = triangleNode->opaque;
                assert(!(rayFlagOpaque && rayFlagNoOpaque));
                if (rayFlagOpaque) {
                    isOpaque = true;
                } else if (rayFlagNoOpaque) {
                    isOpaque = false;
                }

                if (rayFlagCullOpaque && isOpaque) {
                    std::cout << "Culling opaque triangle" << std::endl;
                    break;
                }
                if (rayFlagCullNoOpaque && !isOpaque) {
                    std::cout << "Culling none opaque triangle" << std::endl;
                    break;
                }

                // TODO: when multi-shader invocation is a thing, need to handle opacity

                // Check if the ray intersects the triangle
                float t, u, v;  // t : distance to intersection, (u,v) : uv coordinates/coordinates in triangle
                bool enteredFront = false;
                bool result = rayTriangleIntersect(rayOrigin,
                        rayDirection,
                        rayTMin,
                        rayTMax,
                        triangleNode->vertices,
                        rayFlagCullBackFacingTriangles,
                        rayFlagCullFrontFacingTriangles,
                        t,
                        u,
                        v,
                        enteredFront);

                if (result) {  // Ray intersected
                    std::cout << "+++ Ray intersected a triangle; (t, u, v) = (" << t << ", " << u << ", " << v << ")" << std::endl; 
                    didIntersectGeometry = true;

                    // Update candidate
                    IntersectionProperties properties;
                    properties.hitT = t;
                    properties.barycentrics = glm::vec2(u, v);
                    properties.isOpaque = triangleNode->opaque;
                    properties.enteredTriangleFrontFace = enteredFront;
                    candidateIntersection.update(true, properties);

                    // Terminate on the first hit if the flag was risen
                    if (rayFlagTerminateOnFirstHit) { 
                        std::cout << "Terminated on first hit!" << std::endl;
                        return false;
                    }
                }
                foundPrimitive = true;
                break;
            }
            case NodeType::Procedural: {
                // TODO: Not correct until multiple shader invocation support.
                // Currently, it returns an intersection if it intersects the 
                // respective AABB for the procedural.
                // TODO: add intersection shader invocation.
                std::cout << "WARNING: encountered procedural; multi-shader invocation not a feature; will return "
                                "its AABB intersection result instead"
                            << std::endl;

                // Ignore procedural if this flag is true
                if (rayFlagSkipAABBs) {
                    std::cout << "SKIPPING THE PROCEDURALS" << std::endl;
                    break;
                }

                ProceduralNode* proceduralNode = static_cast<ProceduralNode*>(currNode);
                bool isOpaque = proceduralNode->opaque;
                assert(!(rayFlagOpaque && rayFlagNoOpaque));
                if (rayFlagOpaque) {
                    isOpaque = true;
                } else if (rayFlagNoOpaque) {
                    isOpaque = false;
                }

                if (rayFlagCullOpaque && isOpaque) {
                    std::cout << "Culling opaque procedural" << std::endl;
                    break;
                }
                if (rayFlagCullNoOpaque && !isOpaque) {
                    std::cout << "Culling none opaque procedural" << std::endl;
                    break;
                }

                // TODO: when multi-shader invocation is a thing, need to handle opacity

                bool result = rayAABBIntersect(rayOrigin,
                        rayDirection,
                        rayTMin,
                        rayTMax,
                        proceduralNode->minBounds,
                        proceduralNode->maxBounds);

                if (result) {
                    // Procedural geometry was hit
                    // Terminate on the first hit if the flag was risen
                    std::cout << "+++ Ray intersected a procedural's AABB" << std::endl;
                    didIntersectGeometry = true;

                    // Update candidate
                    IntersectionProperties properties;
                    properties.isOpaque = proceduralNode->opaque;
                    candidateIntersection.update(false, properties);

                    // Terminate on the first hit if the flag was risen
                    if (rayFlagTerminateOnFirstHit) {
                        std::cout << "Terminated on first hit!" << std::endl;
                        return false;
                    }
                }
                foundPrimitive = true;
                break;
            }
            }
        }

        if (nodesToEval.empty())
            activeTrace = false;

        return !nodesToEval.empty();
    }

    /// @brief Include the current AABB/procedural intersection in determining the closest hit.
    /// The candidate intersection must be of type AABB
    /// @param hit_t 
    void generateIntersection(float hit_t) {
        assert(candidateIntersection.type == CandidateIntersectionType::AABB);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (hit_t >= committedIntersection.properties.hitT)
            return;

        committedIntersection.update(false, candidateIntersection);
    }

    /// @brief Include the current triangle intersection in determining the closest hit.
    /// The candidate intersection must be of type triangle.
    void confirmIntersection() {
        assert(candidateIntersection.type == CandidateIntersectionType::Triangle);

        // Do not update if candidate distance from intersection is greater than or equal to the closest distance
        if (candidateIntersection.properties.hitT >= committedIntersection.properties.hitT)
            return;

        committedIntersection.update(true, candidateIntersection);
    }

    /// @brief Get the current committed intersection type.
    /// @return Enum of committed intersection type.
    CommittedIntersectionType getCommittedIntersectionType() const {
        return committedIntersection.type;
    }

    /// @brief Get the current candidate intersection type.
    /// @return Enum of candidate intersection type.
    CandidateIntersectionType getCandidateIntersectionType() const {
        return candidateIntersection.type;
    }

    float getIntersectionT(bool get_committed) const {
        return get_committed ? committedIntersection.properties.hitT : candidateIntersection.properties.hitT;
    }

    int getIntersectionInstanceCustomIndex(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->customIndex : candidate_instance->customIndex;
    }

    int getIntersectionInstanceId(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->id : candidate_instance->id;
    }

    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->sbtRecordOffset : candidate_instance->sbtRecordOffset;
    }

    int getIntersectionGeometryIndex(bool get_committed) const {
        return get_committed ? committedIntersection.properties.geometryIndex
                             : candidateIntersection.properties.geometryIndex;
    }

    int getIntersectionPrimitiveIndex(bool get_committed) const {
        return get_committed ? committedIntersection.properties.primitiveIndex
                             : candidateIntersection.properties.primitiveIndex;
    }

    glm::vec2 getIntersectionBarycentrics(bool get_committed) const {
        return get_committed ? committedIntersection.properties.barycentrics
                             : candidateIntersection.properties.barycentrics;
    }

    bool getIntersectionFrontFace(bool get_committed) const {
        return get_committed ? committedIntersection.type == CommittedIntersectionType::Triangle &&
                                       committedIntersection.properties.enteredTriangleFrontFace
                             : candidateIntersection.type == CandidateIntersectionType::Triangle &&
                                       candidateIntersection.properties.enteredTriangleFrontFace;
    }

    bool getIntersectionCandidateAABBOpaque() const {
        return candidateIntersection.type == CandidateIntersectionType::AABB && candidateIntersection.properties.isOpaque;
    }

    glm::vec3 getIntersectionObjectRayDirection(bool get_committed) const {
        return get_committed ? (committedIntersection.properties.instance->worldToObject * rayDirection)
                             : (candidateIntersection.properties.instance->worldToObject * rayDirection);
    }

    glm::vec3 getIntersectionObjectRayOrigin(bool get_committed) const {
        return get_committed ? (committedIntersection.properties.instance->worldToObject * rayOrigin)
                             : (candidateIntersection.properties.instance->worldToObject * rayOrigin);
    }

    glm::mat4x3 getIntersectionObjectToWorld(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->objectToWorld : candidate_instance->objectToWorld;
    }

    glm::mat4x3 getIntersectionWorldToObject(bool get_committed) const {
        const auto& committed_instance = committedIntersection.properties.instance;
        const auto& candidate_instance = candidateIntersection.properties.instance;
        assert((committed_instance != nullptr) || (candidate_instance != nullptr));

        return get_committed ? committed_instance->worldToObject : candidate_instance->worldToObject;
    }

    void traceRay(bool& didIntersectGeometry,
            const unsigned rayFlags,
            const unsigned cullMask,
            const glm::vec4 rayOrigin,
            const glm::vec4 rayDirection,
            const float rayTMin,
            const float rayTMax,
            const bool& useSBT,
            const unsigned offsetSBT = 0,
            const unsigned strideSBT = 0,
            const unsigned missIndex = 0) {

        initTrace(rayFlags, cullMask, rayOrigin, rayDirection, rayTMin, rayTMax, useSBT, offsetSBT, strideSBT, missIndex);

        bool intersect = false;
        while (stepTrace(intersect)) {
            if (intersect)
                didIntersectGeometry = true;
        }
        if (intersect)
            didIntersectGeometry = true;

        // TODO: once multi-shader invocation is a thing, need to handle hit and miss shaders
        // TODO: currently, the root acceleration structure has an id of 0 but maybe want to allow more flexibility
        if (id != 0 || rayFlagSkipClosestHitShader) {
            // TODO: Do not execute a closest hit shader.
            std::cout << "Skip the closest hit shader" << std::endl;
            return;
            // didIntersectGeometry = false;
        }

        // Invoke a hit or miss shader here
        std::cout << "Invoke closest hit shader / miss shader" << std::endl;
    }

private:
    // Adapted algorithm from "An Efficient and Robust Ray–Box Intersection Algorithm by Amy Williams et al., 2004."
    // found on Scratchapixel
    // (https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-box-intersection.html)
    bool rayAABBIntersect(const glm::vec3& rayOrigin,
            const glm::vec3& rayDirection,
            const float& rayTMin,
            const float& rayTMax,
            const glm::vec3& minBounds,
            const glm::vec3& maxBounds) const {

        // Check if the ray if inside of the AABB; it is considered inside if right at the surface
        bool insideAABB = rayOrigin.x >= minBounds.x && rayOrigin.y >= minBounds.y && rayOrigin.z >= minBounds.z &&
                          rayOrigin.x <= maxBounds.x && rayOrigin.y <= maxBounds.y && rayOrigin.z <= maxBounds.z;
        if (insideAABB)
            return true;

        // Otherwise, check if the ray intersects the surface of the AABB from the outside
        // Check the x-plane
        float tmin = (minBounds.x - rayOrigin.x) / rayDirection.x;
        float tmax = (maxBounds.x - rayOrigin.x) / rayDirection.x;

        if (tmin > tmax)
            std::swap(tmin, tmax);

        // Check the y-plane
        float tymin = (minBounds.y - rayOrigin.y) / rayDirection.y;
        float tymax = (maxBounds.y - rayOrigin.y) / rayDirection.y;

        if (tymin > tymax)
            std::swap(tymin, tymax);

        if ((tmin > tymax) || (tymin > tmax))
            return false;

        if (tymin > tmin)
            tmin = tymin;
        if (tymax < tmax)
            tmax = tymax;

        // Check the z-plane
        float tzmin = (minBounds.z - rayOrigin.z) / rayDirection.z;
        float tzmax = (maxBounds.z - rayOrigin.z) / rayDirection.z;

        if (tzmin > tzmax)
            std::swap(tzmin, tzmax);

        if ((tmin > tzmax) || (tzmin > tmax))
            return false;

        if (tzmin > tmin)
            tmin = tzmin;
        if (tzmax < tmax)
            tmax = tzmax;

        // Only check the entry point of the ray into the box
        if (tmin < rayTMin || tmin > rayTMax)
            return false;

        return true;
    }

    // TODO: handle triangle face culling
    // Moller-Trumbore ray/triangle intersection algorithm
    bool rayTriangleIntersect(const glm::vec3 rayOrigin,
            const glm::vec3 rayDirection,
            const float rayTMin,
            const float rayTMax,
            const std::vector<glm::vec3> vertices,
            const bool cullBackFace,
            const bool cullFrontFace,
            float& t,
            float& u,
            float& v,
            bool& enteredFront) const {

        // Immediately return if culling both faces; triangle is nonexistent
        if (cullBackFace && cullFrontFace)
            return false;

        constexpr float epsilon = std::numeric_limits<float>::epsilon();

        // Find vectors for 2 edges that share a vertex
        glm::vec3 edge1 = vertices[1] - vertices[0];
        glm::vec3 edge2 = vertices[2] - vertices[0];

        glm::vec3 pvec = glm::cross(rayDirection, edge2);

        // If positive determinant, then the ray hit the front face
        // If negative determinant, then the ray hit the back face
        // If determinant is close to zero, then the ray missed the triangle
        float determinant = glm::dot(edge1, pvec);
        enteredFront = determinant < epsilon ? false : true;

        if (cullBackFace) {
            if (determinant < epsilon)
                return false;
        } else if (cullFrontFace) {
            if (determinant > epsilon)
                return false;
        } else {
            if (std::fabs(determinant) < epsilon)
                return false;
        }

        float inverseDeterminant = 1.0f / determinant;

        glm::vec3 tvec = rayOrigin - vertices[0];  // Distance from ray origin to shared vertex 

        u = glm::dot(tvec, pvec) * inverseDeterminant;
        if (u < 0 || u > 1)
            return false;

        glm::vec3 qvec = glm::cross(tvec, edge1);

        v = glm::dot(rayDirection, qvec) * inverseDeterminant;
        if (v < 0 || u + v > 1)
            return false;

        t = glm::dot(edge2, qvec) * inverseDeterminant;

        if (t < rayTMin || t > rayTMax)
            return false;

        return true;
    }

public:
    std::string toString(unsigned tabLevel = 0) {
        std::stringstream result("");
        const std::string tabString("|\t");

        result << Util::repeatedString(tabLevel, tabString) << "acceleration_structure_id = " << id << std::endl;
        result << Util::repeatedString(tabLevel + 1, tabString) << "is_TLAS = " << (isTLAS ? "true" : "false") << std::endl;
        
        using NodeInfo = std::tuple<ConstNodeRef, unsigned>;  // Raw pointer to smart pointers

        std::stack<NodeInfo> frontier;
        frontier.push(std::make_tuple(&root, tabLevel));

        while (!frontier.empty()) {
            NodeInfo top = frontier.top();
            const std::shared_ptr<Node>& currNodeRef = *(get<0>(top));
            const unsigned numTabs = get<1>(top);
            frontier.pop();

            // Borrow ownership
            Node* currNode = currNodeRef.get();

            // Append string form of node
            result << currNode->toString(numTabs + 1, tabString);

            // Traverse down the hierarchy as necessary
            switch (currNode->type()) {
            case NodeType::Box: {
                BoxNode* boxNode = static_cast<BoxNode*>(currNode);
                for (const auto& child : boxNode->children) {
                    frontier.push(std::make_tuple(&child, numTabs + 1));
                }
                break;
            }
            case NodeType::Instance: {
                InstanceNode* instanceNode = static_cast<InstanceNode*>(currNode);
                result << instanceNode->accelerationStructure->toString(numTabs + 2);
                break;
            }
            default: // No paths after current node
                break;
            }
        }

        return result.str();
    }
};

export class AccelerationStructureManager : public Value {
private:
    std::shared_ptr<AccelerationStructure> root = nullptr; // Start of all acceleration structures TODO: make unique?
    Struct* structureInfo; // TODO: change to smart pointer?

public:
    AccelerationStructureManager(Type t): Value(t) {}
    ~AccelerationStructureManager() {
        delete structureInfo;
    }

private:
    void copyType(const Value& new_val) {
        // "new_val" is a "Struct" instead of "AccelerationStructureManager" at the initial copy of input
        const Aggregate& other =
                new_val.getType().getBase() == DataType::RAY_TRACING_ACCELERATION_STRUCTURE
                        ? static_cast<const Aggregate&>(
                                *((static_cast<const AccelerationStructureManager*>(&new_val))->structureInfo))
                        : static_cast<const Aggregate&>(new_val);

        // Get data about the structure from the "new_val"
        std::vector<std::array<unsigned, 4>> structureData;
        const Array& accelerationStructuresInfo = static_cast<const Array&>(*(other[0]));
        for (unsigned i = 0; i < accelerationStructuresInfo.getSize(); ++i) {
            const Struct& currAccelerationStructureInfo = static_cast<const Struct&>(*(accelerationStructuresInfo[i]));

            const unsigned numBoxNodes =
                    static_cast<const Primitive&>(*(currAccelerationStructureInfo[0])).data.u32;
            const unsigned numInstanceNodes =
                    static_cast<const Primitive&>(*(currAccelerationStructureInfo[1])).data.u32;
            const unsigned numTriangleNodes =
                    static_cast<const Primitive&>(*(currAccelerationStructureInfo[2])).data.u32;
            const unsigned numProceduralNodes =
                    static_cast<const Primitive&>(*(currAccelerationStructureInfo[3])).data.u32;

            structureData.push_back(
                    std::array<unsigned, 4> {numBoxNodes, numInstanceNodes, numTriangleNodes, numProceduralNodes});
        }

        // Change the current type to match
        auto info = getStructureFormat(&structureData);
        type = Type::accelerationStructure(get<1>(info), get<0>(info));

        // Actual copy
        structureInfo = new Struct(type);
        (static_cast<Aggregate*>(structureInfo))->dummyFill();
        (static_cast<Aggregate*>(structureInfo))->copyFrom(other);
    }

    void buildAccelerationStructures() {
        assert(structureInfo != nullptr);

        // Note: different instance nodes can point to the same acceleration structure
        std::vector<std::shared_ptr<AccelerationStructure>> accelerationStructures;
        const Struct& structureInfoRef = *structureInfo;
        const Array& accelerationStructuresInfo = static_cast<const Array&>(*(structureInfoRef[0]));
        unsigned numAccelerationStructures = accelerationStructuresInfo.getSize();

        // Construct each acceleration structure bottom-up
        unsigned offset = 1; // Offset to the first acceleration structure
        for (int i = numAccelerationStructures - 1; i >= 0; --i) {
            accelerationStructures.push_back(
                std::make_shared<AccelerationStructure>(
                    i,
                    static_cast<const Struct&>(*(structureInfoRef[i + offset])), 
                    static_cast<const Struct&>(*(accelerationStructuresInfo[i])), 
                    accelerationStructures,
                    numAccelerationStructures
                )
            );
        }

        // Set the root acceleration structure 
        root = std::move(accelerationStructures[numAccelerationStructures - 1]);
        assert(accelerationStructures[numAccelerationStructures - 1] == nullptr);
    }

public:
    AccelerationStructureManager& operator=(const AccelerationStructureManager& other) {
        // Copy the type
        copyType(other);

        // Copy the acceleration structures instead of building them
        root = std::make_shared<AccelerationStructure>(*(other.root));

        return *this;
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Copy the type of "new_val"
        copyType(new_val);

        // Build the acceleration structures
        buildAccelerationStructures();
    }

    void initStepTraceRay(const unsigned& rayFlags,
            const unsigned& cullMask,
            const std::vector<float>& rayOrigin,
            const std::vector<float>& rayDirection,
            const float& rayTMin,
            const float& rayTMax,
            const bool& useSBT,
            const unsigned offsetSBT = 0,
            const unsigned strideSBT = 0,
            const unsigned missIndex = 0) {

        root->initTrace(rayFlags,
                cullMask,
                rayOrigin,
                rayDirection,
                rayTMin,
                rayTMax,
                useSBT,
                offsetSBT,
                strideSBT,
                missIndex);
    }

    bool stepTraceRay() {
        // TODO: do something with intersected geometry argument
        bool intersectedGeometry = false;
        return root->stepTrace(intersectedGeometry);
    }

    void traceRay(bool& didIntersectGeometry,
            const unsigned& rayFlags,
            const unsigned& cullMask,
            const std::vector<float>& rayOrigin,
            const std::vector<float>& rayDirection,
            const float& rayTMin,
            const float& rayTMax,
            const bool& useSBT,
            const unsigned offsetSBT = 0,
            const unsigned strideSBT = 0,
            const unsigned missIndex = 0) const {

        glm::vec4 convertedRayOrigin = glm::make_vec4(rayOrigin.data());
        convertedRayOrigin.w = 1.0f;
        glm::vec4 convertedRayDirection = glm::make_vec4(rayDirection.data());
        convertedRayDirection.w = 0.0f;

        root->traceRay(didIntersectGeometry,
                rayFlags,
                cullMask,
                convertedRayOrigin,
                convertedRayDirection,
                rayTMin,
                rayTMax,
                useSBT,
                offsetSBT,
                strideSBT,
                missIndex);
    }

    void generateIntersection(float hit_t) const {
        root->generateIntersection(hit_t);
    }

    void confirmIntersection() const {
        root->confirmIntersection();
    }

    /// @brief Get the intersection type of the current iteration of tracing a ray.
    /// @param getCommitted Determine if the method should return the committed or candidate intersection type.
    /// @return Intersection type.
    unsigned getIntersectionType(bool get_committed) const {
        return get_committed ? static_cast<unsigned>(root->getCommittedIntersectionType())
                             : static_cast<unsigned>(root->getCandidateIntersectionType());
    }

    /// @brief Get the current ray to intersection distance.
    /// @param getCommitted Determine if the method should return the committed or candidate intersection type.
    /// @return Intersection distance.
    float getIntersectionT(bool get_committed) const {
        return root->getIntersectionT(get_committed);
    }

    int getIntersectionInstanceCustomIndex(bool get_committed) const {
        return root->getIntersectionInstanceCustomIndex(get_committed);
    }

    int getIntersectionInstanceId(bool get_committed) const {
        return root->getIntersectionInstanceId(get_committed);
    }

    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(bool get_committed) const {
        return root->getIntersectionInstanceShaderBindingTableRecordOffset(get_committed);
    }

    int getIntersectionGeometryIndex(bool get_committed) const {
        return root->getIntersectionGeometryIndex(get_committed);
    }

    int getIntersectionPrimitiveIndex(bool get_committed) const {
        return root->getIntersectionPrimitiveIndex(get_committed);
    }

    glm::vec2 getIntersectionBarycentrics(bool get_committed) const {
        return root->getIntersectionBarycentrics(get_committed);
    }

    bool getIntersectionFrontFace(bool get_committed) const {
        return root->getIntersectionFrontFace(get_committed);
    }

    bool getIntersectionCandidateAABBOpaque() const {
        return root->getIntersectionCandidateAABBOpaque();
    }

    glm::vec3 getIntersectionObjectRayDirection(bool get_committed) const {
        return root->getIntersectionObjectRayDirection(get_committed);
    }

    glm::vec3 getIntersectionObjectRayOrigin(bool get_committed) const {
        return root->getIntersectionObjectRayOrigin(get_committed);
    }

    glm::mat4x3 getIntersectionObjectToWorld(bool get_committed) const {
        return root->getIntersectionObjectToWorld(get_committed);
    }

    glm::mat4x3 getIntersectionWorldToObject(bool get_committed) const {
        return root->getIntersectionWorldToObject(get_committed);    
    }

    // TODO:
    void fillPayloadWithBool(Value* payloadInfo, const bool intersected) const {
        std::stack<Value*> frontier;
        frontier.push(payloadInfo);

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
                    val.copyFrom(Primitive(static_cast<float>(intersected)));
                    break;
                }
                case DataType::UINT: {
                    Primitive& val = static_cast<Primitive&>(*curr);
                    val.copyFrom(Primitive(static_cast<unsigned>(intersected)));
                    break;
                }
                case DataType::INT: {
                    Primitive& val = static_cast<Primitive&>(*curr);
                    val.copyFrom(Primitive(static_cast<int>(intersected)));
                    break;
                }
                case DataType::BOOL: {
                    Primitive& val = static_cast<Primitive&>(*curr);
                    val.copyFrom(Primitive(intersected));
                    break;
                }
                case DataType::ARRAY:
                case DataType::STRUCT: {
                    const Aggregate& agg = static_cast<const Aggregate&>(*curr);
                    for (auto it = agg.begin(); it != agg.end(); ++it) {
                        frontier.push(*it);
                    }
                    break;
                }
            }
        }
    }

private:
    std::string getPrimitiveValueAsString(const Value& value) const {
        std::stringstream result("");
        const DataType& dataType = value.getType().getBase();

        switch (dataType) {
            default: {
                std::stringstream err;
                err << "Unsupported data type; cannot convert to primitive string: " << dataType;
                throw std::runtime_error(err.str());
            }
            case DataType::FLOAT: {
                result << static_cast<const Primitive&>(value).data.fp32;
                break;
            }
            case DataType::UINT: {
                result << static_cast<const Primitive&>(value).data.u32;
                break;
            }
            case DataType::INT: {
                result << static_cast<const Primitive&>(value).data.i32;
                break;
            }
            case DataType::BOOL: {
                result << (static_cast<const Primitive&>(value).data.b32 ? "true" : "false");
                break;
            }
        }

        return result.str();
    }

public:
    /// @brief TODO: description
    /// @return 
    std::string toString() const {
        std::stringstream result("");
        const std::string tabString("|\t");

        // Contains the name, value, and number of tabs
        using NameAndValue = std::tuple<const std::string, const Value*, const unsigned>;

        std::stack<NameAndValue> frontier;
        const Value customStringValue = Value(Type::string());
        frontier.push(std::make_tuple(std::string("Structure for acceleration structures"), structureInfo, 0));

        while (!frontier.empty()) {
            const NameAndValue top = frontier.top();
            frontier.pop();

            const std::string& name = get<0>(top);
            const Value* value = get<1>(top);
            const unsigned& numTabs = get<2>(top);
            const DataType& dataType = value->getType().getBase();
            const std::string arrayElementIndicator("");

            switch (dataType) {
                default: {
                    std::stringstream err;
                    err << "Unsupported data type; cannot convert to string: " << dataType;
                    throw std::runtime_error(err.str());
                }
                case DataType::FLOAT:
                case DataType::UINT:
                case DataType::INT:
                case DataType::BOOL: {
                    result << Util::repeatedString(numTabs, tabString) << name << " = " << getPrimitiveValueAsString(*value) << std::endl;
                    break;
                }
                case DataType::STRUCT:
                case DataType::RAY_TRACING_ACCELERATION_STRUCTURE: {
                    result << Util::repeatedString(numTabs, tabString) << name << " {" << std::endl;
                    frontier.push(std::make_tuple(" }", &customStringValue, numTabs));

                    const Struct& info = static_cast<const Struct&>(*value);

                    // Add the children to the stack
                    const std::vector<std::string>& names = info.getType().getNames();
                    assert(names.size() == info.getSize());

                    for (int i = names.size() - 1; i >= 0; --i) {
                        std::stringstream message;
                        message << names[i];
                        frontier.push(std::make_tuple(message.str(), info[i], numTabs + 1));
                    }

                    break;
                }
                case DataType::ARRAY: {
                    result << Util::repeatedString(numTabs, tabString) << name;

                    const Array& info = static_cast<const Array&>(*value);
                    const DataType childDataType = info.getSize() > 0 ? info[0]->getType().getBase() : DataType::VOID;

                    // Add the children to the stack if some kind of structure
                    if (childDataType == DataType::STRUCT || childDataType == DataType::ARRAY ||
                            childDataType == DataType::RAY_TRACING_ACCELERATION_STRUCTURE) {
                        result << " [" << std::endl;
                        frontier.push(std::make_tuple(" ]", &customStringValue, numTabs));
                        for (int i = info.getSize() - 1; i >= 0; --i) {
                            frontier.push(std::make_tuple(arrayElementIndicator, info[i], numTabs + 1));
                        }
                    } else {
                        result << " [ ";
                        for (unsigned i = 0; i < info.getSize() - 1; ++i) {
                            result << getPrimitiveValueAsString(*(info[i])) << ", ";
                        }
                        result << getPrimitiveValueAsString(*(info[info.getSize() - 1])) << " ]" << std::endl;
                    }

                    break;
                }
                case DataType::STRING: {
                    result << Util::repeatedString(numTabs, tabString) << name << std::endl;
                    break;
                }
            }
        }

        return result.str();
    }

    /// @brief Gives the fields and field names of the structure for acceleration structures.
    /// @param data Number of nodes in each acceleration structure.
    /// @return Field names as strings and fields as types.
    static std::tuple<std::vector<std::string>, std::vector<const Type*>> getStructureFormat(
            std::vector<std::array<unsigned, 4>>* data = nullptr) {

        using Names = std::vector<std::string>;
        using Fields = std::vector<const Type*>;

        Names names {"accelerationStructuresInfo"};
        Fields fields;

        // Note: "--- <field name>" means which field we are populating with the code below it

        // --- accelerationStructuresInfo
        const Names acceleration_structures_info_names {"numBoxNodes",
                "numInstanceNodes",
                "numTriangleNodes",
                "numProceduralNodes"};
        Fields acceleration_structures_info_fields;
        {
            // --- numBoxNodes
            acceleration_structures_info_fields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numInstanceNodes
            acceleration_structures_info_fields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numTriangleNodes
            acceleration_structures_info_fields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numProceduralNodes
            acceleration_structures_info_fields.push_back(new Type(Type::primitive(DataType::UINT)));
        }
        Type* acceleration_structures_info_struct =
                new Type(Type::structure(acceleration_structures_info_fields, acceleration_structures_info_names));
        Type* acceleration_structures_info_array = new Type(Type::array(0, *acceleration_structures_info_struct));
        fields.push_back(acceleration_structures_info_array);

        // Add the acceleration structures if we know how many there are
        // Note: data contains information about the acceleration structures
        if (data != nullptr) {

            // Add on the acceleration structures
            for (int i = 0; i < data->size(); ++i) {
                const unsigned num_box_nodes = (*data)[i][0];
                const unsigned num_instance_nodes = (*data)[i][1];
                const unsigned num_triangle_nodes = (*data)[i][2];
                const unsigned num_procedural_nodes = (*data)[i][3];

                // --- accelerationStructure
                Names acceleration_structure_field_names {"isTLAS"};
                Fields acceleration_structure_fields;
                {
                    // --- isTLAS
                    acceleration_structure_fields.push_back(new Type(Type::primitive(DataType::BOOL)));

                    // --- boxNodes
                    if (num_box_nodes > 0) {
                        const Names box_node_field_names {"minBounds", "maxBounds", "childrenIndices"};
                        Fields box_node_fields;
                        {
                            Type* bounds = new Type(Type::primitive(DataType::FLOAT));

                            // --- minBounds
                            box_node_fields.push_back(new Type(Type::array(3, *bounds)));

                            // --- maxBounds
                            box_node_fields.push_back(new Type(Type::array(3, *bounds)));

                            // --- childrenIndices
                            Type* children_index = new Type(Type::primitive(DataType::UINT));
                            box_node_fields.push_back(new Type(Type::array(0, *children_index)));
                        }
                        Type* box_node = new Type(Type::structure(box_node_fields, box_node_field_names));

                        for (int j = 0; j < num_box_nodes; ++j) {
                            std::stringstream box_node_name;
                            box_node_name << "box" << j;
                            acceleration_structure_field_names.push_back(box_node_name.str());
                            acceleration_structure_fields.push_back(box_node);
                        }
                    }

                    // --- instanceNodes
                    if (num_instance_nodes > 0) {
                        Names instance_node_field_names {"objectToWorld",
                                "id",
                                "customIndex",
                                "geometryIndex",
                                "primitiveIndex",
                                "mask",
                                "shaderBindingTableRecordOffset",
                                "accelerationStructureIndex"};
                        Fields instance_node_fields;
                        {
                            // --- objectToWorld
                            unsigned num_rows = 3;
                            unsigned num_cols = 4;
                            Type* float_value = new Type(Type::primitive(DataType::FLOAT));
                            Type* rows = new Type(Type::array(num_cols, *float_value));
                            Type* matrix = new Type(Type::array(num_rows, *rows));
                            instance_node_fields.push_back(matrix);

                            // --- id
                            instance_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- customIndex
                            instance_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- geometryIndex
                            instance_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- primitiveIndex
                            instance_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- mask
                            instance_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- shaderBindingTableRecordOffset
                            instance_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- accelerationStructureIndex
                            instance_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));
                        }
                        Type* instance_node =
                                new Type(Type::structure(instance_node_fields, instance_node_field_names));

                        for (int j = 0; j < num_instance_nodes; ++j) {
                            std::stringstream instance_node_name;
                            instance_node_name << "instance" << j;
                            acceleration_structure_field_names.push_back(instance_node_name.str());
                            acceleration_structure_fields.push_back(instance_node);
                        }
                    }

                    // --- triangleNodes
                    if (num_triangle_nodes > 0) {
                        Names triangle_node_field_names {"geometryIndex",
                                "primitiveIndex",
                                "opaque",
                                "vertices",
                                "indices"};
                        Fields triangle_node_fields;
                        {
                            // --- geometryIndex
                            triangle_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- primitiveIndex
                            triangle_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- opaque
                            triangle_node_fields.push_back(new Type(Type::primitive(DataType::BOOL)));

                            // --- vertices
                            Type* float_value = new Type(Type::primitive(DataType::FLOAT));
                            Type* vertex = new Type(Type::array(0, *float_value));
                            triangle_node_fields.push_back(new Type(Type::array(0, *vertex)));

                            // --- indices
                            Type* indices = new Type(Type::primitive(DataType::UINT));
                            triangle_node_fields.push_back(new Type(Type::array(0, *indices)));
                        }
                        Type* triangle_node =
                                new Type(Type::structure(triangle_node_fields, triangle_node_field_names));

                        for (int j = 0; j < num_triangle_nodes; ++j) {
                            std::stringstream triangle_node_name;
                            triangle_node_name << "triangle" << j;
                            acceleration_structure_field_names.push_back(triangle_node_name.str());
                            acceleration_structure_fields.push_back(triangle_node);
                        }
                    }

                    // --- proceduralNodes
                    if (num_procedural_nodes > 0) {
                        Names procedural_node_field_names {"geometryIndex",
                                "primitiveIndex",
                                "opaque",
                                "minBounds",
                                "maxBounds"};
                        Fields procedural_node_fields;
                        {
                            // --- geometryIndex
                            procedural_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- primitiveIndex
                            procedural_node_fields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- opaque
                            procedural_node_fields.push_back(new Type(Type::primitive(DataType::BOOL)));

                            // --- minBounds
                            Type* bounds = new Type(Type::primitive(DataType::FLOAT));
                            procedural_node_fields.push_back(new Type(Type::array(3, *bounds)));

                            // --- maxBounds
                            procedural_node_fields.push_back(new Type(Type::array(3, *bounds)));
                        }
                        Type* proceduralNode =
                                new Type(Type::structure(procedural_node_fields, procedural_node_field_names));

                        for (int j = 0; j < num_procedural_nodes; ++j) {
                            std::stringstream procedural_node_name;
                            procedural_node_name << "procedural" << j;
                            acceleration_structure_field_names.push_back(procedural_node_name.str());
                            acceleration_structure_fields.push_back(proceduralNode);
                        }
                    }
                }

                Type* acceleration_structure =
                        new Type(Type::structure(acceleration_structure_fields, acceleration_structure_field_names));
                std::stringstream acceleration_structure_name;
                acceleration_structure_name << "accelerationStructure" << i;
                names.push_back(acceleration_structure_name.str());
                fields.push_back(acceleration_structure);
            }
        }

        return std::make_tuple(names, fields);
    }
};
