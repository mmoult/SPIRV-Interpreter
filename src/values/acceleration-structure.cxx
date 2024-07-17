/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stack>
#include <string>
#include <tuple>

// TODO: plan to remove/change header(s) below
#include <iostream>

// TODO: probably want to reduce GLM files to link
#include <glm/ext.hpp>

#include "../external/spirv.hpp"
#include "type.hpp"
#include "value.hpp"

export module value.accelerationStructure;
import value.aggregate;
import value.primitive;

namespace Util {

/// @brief Get a repeated string.
/// @param num number of times to repeat a string.
/// @param str string to repeat.
/// @return single string of the repeated string.
std::string repeatedString(const unsigned num, const std::string& str) {
    std::stringstream result;
    for (unsigned i = 0; i < num; ++i)
        result << str;
    return result.str();
}

/// @brief Get the a GLM vec3 as a string. This is an alternative to GLM's
/// "to_string" function.
/// @param vec vec3 to print.
/// @return string version of the given 3-D vector.
std::string glmVec3ToString(const glm::vec3& vec) {
    std::stringstream result;

    result << "[ ";
    for (unsigned i = 0; i < vec.length() - 1; ++i)
        result << vec[i] << ", ";
    result << vec[vec.length() - 1] << " ]";

    return result.str();
}

}  // namespace Util

// TODO: handle the effects of winding order on intersections; currently, front face is CCW
class AccelerationStructure {
private:
    enum class NodeType { Box, Instance, Triangle, Procedural };

    struct Node {
        virtual std::shared_ptr<Node> clone() const = 0;
        virtual NodeType type() const = 0;
        virtual std::string toString(const unsigned indent = 0, const std::string& indent_string = "") const = 0;
    };

    struct BoxNode : public Node {
        const glm::vec4 minBounds;
        const glm::vec4 maxBounds;
        const std::vector<std::shared_ptr<Node>> children;

        /// @brief BoxNode constructor.
        /// @param min_bounds AABB minimum bounds as a 3-D point.
        /// @param max_bounds AABB maximum bounds as a 3-D point.
        /// @param children nodes that are witin the bounds of this BoxNode.
        BoxNode(
            const glm::vec4& min_bounds,
            const glm::vec4& max_bounds,
            const std::vector<std::shared_ptr<Node>>& children
        )
            : minBounds(min_bounds),
              maxBounds(max_bounds),
              children([](const std::vector<std::shared_ptr<Node>>& children) -> std::vector<std::shared_ptr<Node>> {
                  std::vector<std::shared_ptr<Node>> result;
                  for (const auto& child : children)
                      result.push_back(child->clone());
                  return result;
              }(children)) {}

        std::shared_ptr<Node> clone() const {
            std::vector<std::shared_ptr<Node>> children_copy;
            for (const auto& child : children)
                children_copy.push_back(child->clone());
            return std::make_shared<BoxNode>(minBounds, maxBounds, children_copy);
        }

        NodeType type() const {
            return NodeType::Box;
        }

        std::string toString(const unsigned indent = 0, const std::string& indent_string = "") const {
            std::stringstream result;

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
        const glm::mat4x3 objectToWorld;  // Column-major order
        const glm::mat4x3 worldToObject;  // Column-major order
        const unsigned id;  // Id relative to other instance nodes in the same acceleration structure
        const unsigned customIndex;  // For shading
        const unsigned geometryIndex;  // Geometry this node is a part of
        const unsigned primitiveIndex;  // Index of node in geometry
        const unsigned mask;  // Mask that can make the ray ignore this instance
        const unsigned sbtRecordOffset;  // Shader binding table record offset (a.k.a. hit group id)
        const std::shared_ptr<AccelerationStructure> accelerationStructure;

        /// @brief InstanceNode constructor.
        /// @param object_to_world object-to-world matrix.
        /// @param id identifier during construction.
        /// @param custom_index identifier for shading.
        /// @param geometry_index geometry it is a part of.
        /// @param primitive_index Iindex in the geometry.
        /// @param mask mask that determines if a ray will ignore thsi instance.
        /// @param sbt_record_offset index of the hit group.
        /// @param accel_struct acceleration structure this instance points to.
        InstanceNode(
            const glm::mat4x3& object_to_world,
            const unsigned id,
            const unsigned custom_index,
            const unsigned geometry_index,
            const unsigned primitive_index,
            const unsigned mask,
            const unsigned sbt_record_offset,
            const std::shared_ptr<AccelerationStructure>& accel_struct
        )
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
              accelerationStructure(accel_struct) {};

        std::shared_ptr<Node> clone() const {
            return std::make_shared<InstanceNode>(
                objectToWorld,
                id,
                customIndex,
                geometryIndex,
                primitiveIndex,
                mask,
                sbtRecordOffset,
                std::make_shared<AccelerationStructure>(*accelerationStructure)
            );
        }

        NodeType type() const {
            return NodeType::Instance;
        }

        std::string toString(const unsigned indent = 0, const std::string& indent_string = "") const {
            std::stringstream result;

            result << Util::repeatedString(indent, indent_string) << "instance_node" << std::endl;

            // Object-to-world
            result << Util::repeatedString(indent + 1, indent_string) << "object_to_world_matrix = [" << std::endl;
            unsigned num_cols = objectToWorld.length();
            unsigned num_rows = objectToWorld[0].length();
            for (unsigned row = 0; row < num_rows; ++row) {
                result << Util::repeatedString(indent + 2, indent_string) << "[ ";
                for (unsigned col = 0; col < num_cols - 1; ++col) {
                    result << objectToWorld[col][row] << ", ";
                }
                result << objectToWorld[num_cols - 1][row] << " ]" << std::endl;
            }
            result << Util::repeatedString(indent + 1, indent_string) << "]" << std::endl;

            // World-to-object
            result << Util::repeatedString(indent + 1, indent_string) << "world_to_object_matrix = [" << std::endl;
            num_cols = worldToObject.length();
            num_rows = worldToObject[0].length();
            for (unsigned row = 0; row < num_rows; ++row) {
                result << Util::repeatedString(indent + 2, indent_string) << "[ ";
                for (unsigned col = 0; col < num_cols - 1; ++col) {
                    result << worldToObject[col][row] << ", ";
                }
                result << worldToObject[num_cols - 1][row] << " ]" << std::endl;
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
        const unsigned geometryIndex;  // Geometry this node is a part of
        const unsigned primitiveIndex;  // Index of node in geometry
        const bool opaque;  // Whether this triangle is opaque
        const std::vector<glm::vec3> vertices;
        const std::vector<unsigned> indices;

        /// @brief TriangleNode constructor.
        /// @param geometry_index geometry it is a part of.
        /// @param primitive_index index in the geometry.
        /// @param opaque whether this triangle is opaque.
        /// @param vertices vertices data.
        /// @param indices indices data.
        TriangleNode(
            const unsigned geometry_index,
            const unsigned primitive_index,
            const bool opaque,
            const std::vector<glm::vec3>& vertices,
            const std::vector<unsigned>& indices
        )
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

        std::string toString(const unsigned indent = 0, const std::string& indent_string = "") const {
            std::stringstream result;

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
        const unsigned geometryIndex;  // Geometry this node is a part of
        const unsigned primitiveIndex;  // Index of node in geometry
        const bool opaque;  // Whether this procedural is opaque
        const glm::vec4 minBounds;
        const glm::vec4 maxBounds;

        /// @brief ProceduralNode constructor.
        /// @param geometry_index geometry it is a part of.
        /// @param primitive_index index in the geometry.
        /// @param opaque whether this procedural is opaque.
        /// @param min_bounds AABB minimum bounds as a 3-D point.
        /// @param max_bounds AABB maximum bounds as a 3-D point.
        ProceduralNode(
            const unsigned geometry_index,
            const unsigned primitive_index,
            const bool opaque,
            const glm::vec4& min_bounds,
            const glm::vec4& max_bounds
        )
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

        std::string toString(const unsigned indent = 0, const std::string& indent_string = "") const {
            std::stringstream result;

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

    const unsigned id;
    const bool isTLAS;  // Whether the acceleration structure is top-level
    std::shared_ptr<Node> root;  // Start of the acceleration structure

public:
    /// @brief AccelerationStructure constructor.
    /// @param id identifier of when it was constructed relative to other acceleration structures.
    /// @param structure_info acceleration structure information.
    /// @param node_counts number of each type of node.
    /// @param all_accel_structs all constructed acceleration structures.
    /// @param num_accel_structs total expected number of acceleration structures after construction.
    AccelerationStructure(
        const unsigned id,
        const Struct& structure_info,
        const Struct& node_counts,
        std::vector<std::shared_ptr<AccelerationStructure>>& all_accel_structs,
        const unsigned num_accel_structs
    )
        : id(id),
          isTLAS(static_cast<const Primitive&>(*(structure_info[0])).data.b32) {

        // Get node information
        const unsigned num_box_nodes = static_cast<const Primitive&>(*(node_counts[0])).data.u32;
        const unsigned num_instance_nodes = static_cast<const Primitive&>(*(node_counts[1])).data.u32;
        const unsigned num_triangle_nodes = static_cast<const Primitive&>(*(node_counts[2])).data.u32;
        const unsigned num_procedural_nodes = static_cast<const Primitive&>(*(node_counts[3])).data.u32;

        const unsigned num_non_instance_nodes = num_triangle_nodes + num_procedural_nodes;
        assert(
            (num_instance_nodes == 0 && num_non_instance_nodes > 0) ||
            (num_instance_nodes > 0 && num_non_instance_nodes == 0)
        );

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
                std::make_shared<ProceduralNode>(geometry_index, primitive_index, opaque, min_bounds, max_bounds)
            );
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
            const Array& vertices_info = static_cast<const Array&>(*(primitive_info[3]));
            for (unsigned j = 0; j < vertices_info.getSize(); ++j) {
                glm::vec3 vertex;
                const Array& vertexInfo = static_cast<const Array&>(*(vertices_info[j]));
                assert(vertexInfo.getSize() == 3);
                for (unsigned k = 0; k < vertexInfo.getSize(); ++k) {
                    vertex[k] = static_cast<const Primitive&>(*(vertexInfo[k])).data.fp32;
                }
                vertices.push_back(vertex);
            }

            // Indices
            std::vector<unsigned> indices;
            const Array& indices_info = static_cast<const Array&>(*(primitive_info[4]));
            for (unsigned j = 0; j < indices_info.getSize(); ++j) {
                unsigned value = static_cast<const Primitive&>(*(indices_info[j])).data.u32;
                indices.push_back(value);
            }

            nodes.push_back(std::make_shared<TriangleNode>(geometry_index, primitive_index, opaque, vertices, indices));
        }

        // Instance nodes
        for (unsigned i = 0; i < num_instance_nodes; ++i) {
            const Struct& instance_info =
                static_cast<const Struct&>(*(structure_info[num_nodes + offset - 1 - i - num_non_instance_nodes]));

            // Object-to-world matrix
            glm::mat4x3 object_to_world_matrix;  // GLM stores in column-major order
            const Array& object_to_world_matrix_info = static_cast<const Array&>(*(instance_info[0]));
            for (unsigned row = 0; row < object_to_world_matrix_info.getSize(); ++row) {
                const Array& row_info = static_cast<const Array&>(*(object_to_world_matrix_info[row]));
                for (unsigned col = 0; col < row_info.getSize(); ++col) {
                    object_to_world_matrix[col][row] = static_cast<const Primitive&>(*(row_info[col])).data.fp32;
                }
            }

            // Id
            const unsigned id = static_cast<const Primitive&>(*(instance_info[1])).data.u32;

            // Custom index
            const unsigned custom_index = static_cast<const Primitive&>(*(instance_info[2])).data.u32;

            // Geometry index
            const unsigned geometry_index = static_cast<const Primitive&>(*(instance_info[3])).data.u32;

            // Primitive index
            const unsigned primitive_index = static_cast<const Primitive&>(*(instance_info[4])).data.u32;

            // Mask
            const unsigned mask = static_cast<const Primitive&>(*(instance_info[5])).data.u32;

            // Shader binding table record offset
            const unsigned sbt_record_offset = static_cast<const Primitive&>(*(instance_info[6])).data.u32;

            // Get respective acceleration structure
            const unsigned accel_struct_index = static_cast<const Primitive&>(*(instance_info[7])).data.u32;
            const unsigned index = num_accel_structs - 1 - accel_struct_index;
            std::shared_ptr<AccelerationStructure>& as = all_accel_structs[index];

            nodes.push_back(std::make_shared<InstanceNode>(
                object_to_world_matrix,
                id,
                custom_index,
                geometry_index,
                primitive_index,
                mask,
                sbt_record_offset,
                as
            ));
        }

        // Box nodes
        assert(num_non_instance_nodes == 0 || num_instance_nodes == 0);
        for (unsigned i = 0; i < num_box_nodes; ++i) {
            const Struct& box_info = static_cast<const Struct&>(
                *(structure_info[num_nodes + offset - 1 - i - num_non_instance_nodes - num_instance_nodes])
            );

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

            // Get the children
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

private:
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

        /// @brief Resets the properties to its default values.
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
        IntersectionProperties properties {};

        /// @brief Resets the intersection to its default values.
        void reset() {
            type = CandidateIntersectionType::Triangle;
            properties.reset();
        }

        /// @brief Update the candidate to the most recent intersection.
        /// @param is_triangle whether this is a triangle intersection.
        /// @param new_properties new candidate's properties.
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
        IntersectionProperties properties {};

        /// @brief Resets the intersection to its default values.
        void reset() {
            type = CommittedIntersectionType::None;
            properties.reset();
        }

        /// @brief Update the committed to match the given candidate.
        /// @param is_triangle whether this is a triangle intersection.
        /// @param candidate_intersection candidate to commit.
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

    CommittedIntersection committedIntersection;  // Current committed intersection from stepping the ray
    CandidateIntersection candidateIntersection;  // Current candidate intersection from stepping the ray

    // Ray properties
    unsigned rayFlags = 0;
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

    /// @brief Set the flags based on the rayFlags given when initializing the trace.
    void setFlags() {
        rayFlagNone = (rayFlags | spv::RayFlagsMask::RayFlagsMaskNone) == 0;
        rayFlagOpaque = rayFlags & spv::RayFlagsMask::RayFlagsOpaqueKHRMask;
        rayFlagNoOpaque = rayFlags & spv::RayFlagsMask::RayFlagsNoOpaqueKHRMask;
        rayFlagTerminateOnFirstHit = rayFlags & spv::RayFlagsMask::RayFlagsTerminateOnFirstHitKHRMask;
        rayFlagSkipClosestHitShader = rayFlags & spv::RayFlagsMask::RayFlagsSkipClosestHitShaderKHRMask;
        rayFlagCullBackFacingTriangles = rayFlags & spv::RayFlagsMask::RayFlagsCullBackFacingTrianglesKHRMask;
        rayFlagCullFrontFacingTriangles = rayFlags & spv::RayFlagsMask::RayFlagsCullFrontFacingTrianglesKHRMask;
        rayFlagCullOpaque = rayFlags & spv::RayFlagsMask::RayFlagsCullOpaqueKHRMask;
        rayFlagCullNoOpaque = rayFlags & spv::RayFlagsMask::RayFlagsCullNoOpaqueKHRMask;
        rayFlagSkipTriangles = rayFlags & spv::RayFlagsMask::RayFlagsSkipTrianglesKHRMask;
        rayFlagSkipAABBs = rayFlags & spv::RayFlagsMask::RayFlagsSkipAABBsKHRMask;  // skip procedurals
    }

    /// @brief Make the trace empty.
    void clearTrace() {
        while (!nodesToEval.empty())
            nodesToEval.pop();
    }

    /// @brief Initialize the trace; the acceleration structure can now be stepped through.
    void initTrace() {
        committedIntersection.reset();
        candidateIntersection.reset();
        activeTrace = true;
        nodesToEval.push(&root);
    }

public:
    /// @brief Resets the trace to the beginning.
    void resetTrace() {
        activeTrace = false;
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

        this->rayFlags = ray_flags;
        this->cullMask = cull_mask;
        this->rayOrigin = ray_origin_glm;
        this->rayDirection = ray_direction_glm;
        this->rayTMin = ray_t_min;
        this->rayTMax = ray_t_max;

        this->useSBT = use_sbt;
        this->offsetSBT = offset_sbt;
        this->strideSBT = stride_sbt;
        this->missIndex = miss_index;

        setFlags();
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
        this->rayFlags = ray_flags;
        this->cullMask = cull_mask;
        this->rayOrigin = ray_origin;
        this->rayDirection = ray_direction;
        this->rayTMin = ray_t_min;
        this->rayTMax = ray_t_max;

        this->useSBT = use_sbt;
        this->offsetSBT = offset_sbt;
        this->strideSBT = stride_sbt;
        this->missIndex = miss_index;

        setFlags();
        resetTrace();
    }

private:
    bool didPopNodePreviously = true;  // Used in the stepTrace() method

public:
    /// @brief Take a step in the trace. Each step reaches the next non-instance primitive that was intersected.
    /// @return tuple containing (1) whether there is more to trace and (2) if a triangle or procedural was intersected
    /// respectively.
    std::tuple<bool, bool> stepTrace() {
        // Do not trace if the trace is inactive.
        if (!activeTrace)
            return std::make_tuple(false, false);

        // Traverse the acceleration structure until it reaches the next non-instance primitive.
        bool intersect_geometry = false;  // Refers to triangles and procedurals.
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

                const auto& ref_accel_struct = instance_node->accelerationStructure;

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
                const std::tuple<bool, bool> result = ref_accel_struct->stepTrace();
                const bool more_to_trace = get<0>(result);
                intersect_geometry = get<1>(result);
                didPopNodePreviously = !more_to_trace;

                // If can continue tracing, push the node back onto the stack.
                if (more_to_trace)
                    nodesToEval.push(curr_node_ref);

                // Handle the result of tracing the ray in the instance.
                if (intersect_geometry) {
                    // Update candidate
                    candidateIntersection = ref_accel_struct->candidateIntersection;
                    candidateIntersection.properties.instance = std::static_pointer_cast<InstanceNode>(*curr_node_ref);

                    // Terminate on the first hit if the flag was risen
                    if (rayFlagTerminateOnFirstHit)
                        return std::make_tuple(false, true);
                }

                // Either it will find a geometry mid-trace where it returns more to trace and intersected a geometry,
                // or find the last geometry where it returns no more to trace and intersected a geometry.
                // Therefore, a geometry will always be found when entering this switch case.
                found_primitive = true;

                break;
            }
            case NodeType::Triangle: {
                // Check skip triangle ray flag.
                if (rayFlagSkipTriangles)
                    break;

                TriangleNode* triangle_node = static_cast<TriangleNode*>(curr_node);

                // Check opaque related ray flags.
                bool is_opaque = triangle_node->opaque;
                assert(!(rayFlagOpaque && rayFlagNoOpaque));  // Cannot be both opaque and not opaque.
                if (rayFlagOpaque)
                    is_opaque = true;
                else if (rayFlagNoOpaque)
                    is_opaque = false;

                if ((rayFlagCullOpaque && is_opaque) || (rayFlagCullNoOpaque && !is_opaque))
                    break;

                // Check if the ray intersects the triangle
                std::tuple<bool, float, float, float, bool> result = rayTriangleIntersect(
                    rayOrigin,
                    rayDirection,
                    rayTMin,
                    rayTMax,
                    triangle_node->vertices,
                    rayFlagCullBackFacingTriangles,
                    rayFlagCullFrontFacingTriangles
                );

                intersect_geometry = get<0>(result);

                if (intersect_geometry) {
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
                    candidateIntersection.update(true, properties);

                    // Terminate on the first hit if the flag was risen
                    if (rayFlagTerminateOnFirstHit)
                        return std::make_tuple(false, true);
                }
                found_primitive = true;

                break;
            }
            case NodeType::Procedural: {
                // TODO: Not correct until shader binding table support.
                // Currently, it returns an intersection if it intersects the
                // respective AABB for the procedural.
                std::cout << "WARNING: encountered procedural; multi-shader invocation not a feature; will return "
                             "its AABB intersection result instead"
                          << std::endl;

                // Check skip AABBs (procedurals) flag
                if (rayFlagSkipAABBs)
                    break;

                ProceduralNode* procedural_node = static_cast<ProceduralNode*>(curr_node);

                // Check opaque related ray flags.
                bool is_opaque = procedural_node->opaque;
                assert(!(rayFlagOpaque && rayFlagNoOpaque));
                if (rayFlagOpaque)
                    is_opaque = true;
                else if (rayFlagNoOpaque)
                    is_opaque = false;

                if ((rayFlagCullOpaque && is_opaque) || (rayFlagCullNoOpaque && !is_opaque))
                    break;

                intersect_geometry = rayAABBIntersect(
                    rayOrigin,
                    rayDirection,
                    rayTMin,
                    rayTMax,
                    procedural_node->minBounds,
                    procedural_node->maxBounds
                );

                if (intersect_geometry) {
                    // Update candidate
                    IntersectionProperties properties;
                    properties.isOpaque = procedural_node->opaque;
                    candidateIntersection.update(false, properties);

                    // Terminate on the first hit if the flag was risen
                    if (rayFlagTerminateOnFirstHit)
                        return std::make_tuple(false, true);
                }
                found_primitive = true;

                break;
            }
            }
        }

        if (nodesToEval.empty())
            activeTrace = false;

        return std::make_tuple(!nodesToEval.empty(), intersect_geometry);
    }

    /// @brief Include the current AABB/procedural intersection in determining the closest hit.
    /// The candidate intersection must be of type AABB.
    /// @param hit_t distance from the ray to the intersection.
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
    bool traceRay(
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

        initTrace(
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

        bool intersect = false;
        bool intersect_once = false;
        bool continue_trace = false;
        do {
            std::tuple<bool, bool> result = stepTrace();
            continue_trace = get<0>(result);
            intersect = get<1>(result);
            if (intersect)
                intersect_once = true;
        } while (continue_trace);

        // TODO: Need to handle hit and miss shaders.
        // Root acceleration structure has an id of 0; shouldn't be calling the closest hit shader because it is a TLAS.
        if (id != 0 || rayFlagSkipClosestHitShader) {
            // Do not execute the closest hit shader.
            std::cout << "Skip the closest hit shader" << std::endl;
            return intersect_once;
        }

        // Invoke a hit or miss shader here
        std::cout << "Invoke closest hit shader / miss shader" << std::endl;
        return intersect_once;
    }

private:
    /// TODO:
    /// Adapted algorithm from "An Efficient and Robust Ray–Box Intersection Algorithm by Amy Williams et al., 2004."
    /// found on Scratchapixel
    /// (https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-box-intersection.html).
    ///
    /// @brief Check if a ray intersects an axis-aligned bounding box (AABB). If the ray is inside the box, it will be
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
        // Check if the ray if inside of the AABB; it is considered inside if right at the surface
        bool inside_aabb = ray_origin.x >= min_bounds.x && ray_origin.y >= min_bounds.y &&
                           ray_origin.z >= min_bounds.z && ray_origin.x <= max_bounds.x &&
                           ray_origin.y <= max_bounds.y && ray_origin.z <= max_bounds.z;
        if (inside_aabb)
            return true;

        // Otherwise, check if the ray intersects the surface of the AABB from the outside
        // Check the x-plane
        float t_min = (min_bounds.x - ray_origin.x) / ray_direction.x;
        float t_max = (max_bounds.x - ray_origin.x) / ray_direction.x;

        if (t_min > t_max)
            std::swap(t_min, t_max);

        // Check the y-plane
        float ty_min = (min_bounds.y - ray_origin.y) / ray_direction.y;
        float ty_max = (max_bounds.y - ray_origin.y) / ray_direction.y;

        if (ty_min > ty_max)
            std::swap(ty_min, ty_max);

        if ((t_min > ty_max) || (ty_min > t_max))
            return false;

        if (ty_min > t_min)
            t_min = ty_min;
        if (ty_max < t_max)
            t_max = ty_max;

        // Check the z-plane
        float tz_min = (min_bounds.z - ray_origin.z) / ray_direction.z;
        float tz_max = (max_bounds.z - ray_origin.z) / ray_direction.z;

        if (tz_min > tz_max)
            std::swap(tz_min, tz_max);

        if ((t_min > tz_max) || (tz_min > t_max))
            return false;

        if (tz_min > t_min)
            t_min = tz_min;
        if (tz_max < t_max)
            t_max = tz_max;

        // Only check the entry point of the ray into the box
        if (t_min < ray_t_min || t_min > ray_t_max)
            return false;

        return true;
    }

    /// TODO:
    /// Moller-Trumbore ray/triangle intersection algorithm
    /// return: triangle intersected, t, u, v, entered front
    ///
    /// @brief Check if the ray intersects a triangle.
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
            return std::make_tuple(false, 0.0f, 0.0f, 0.0f, false);

        constexpr float epsilon = std::numeric_limits<float>::epsilon();

        // Find vectors for 2 edges that share a vertex
        glm::vec3 edge_1 = vertices[1] - vertices[0];
        glm::vec3 edge_2 = vertices[2] - vertices[0];

        glm::vec3 pvec = glm::cross(ray_direction, edge_2);

        // If positive determinant, then the ray hit the front face
        // If negative determinant, then the ray hit the back face
        // If determinant is close to zero, then the ray missed the triangle
        float determinant = glm::dot(edge_1, pvec);
        bool intersect_front = determinant < epsilon ? false : true;

        if (cull_back_face) {
            if (determinant < epsilon)
                return std::make_tuple(false, 0.0f, 0.0f, 0.0f, intersect_front);
        } else if (cull_front_face) {
            if (determinant > epsilon)
                return std::make_tuple(false, 0.0f, 0.0f, 0.0f, intersect_front);
        } else {
            if (std::fabs(determinant) < epsilon)
                return std::make_tuple(false, 0.0f, 0.0f, 0.0f, intersect_front);
        }

        float inverse_determinant = 1.0f / determinant;

        glm::vec3 tvec = ray_origin - vertices[0];  // Distance from ray origin to shared vertex

        float u = glm::dot(tvec, pvec) * inverse_determinant;
        if (u < 0 || u > 1)
            return std::make_tuple(false, 0.0f, u, 0.0f, intersect_front);

        glm::vec3 qvec = glm::cross(tvec, edge_1);

        float v = glm::dot(ray_direction, qvec) * inverse_determinant;
        if (v < 0 || u + v > 1)
            return std::make_tuple(false, 0.0f, u, v, intersect_front);

        float t = glm::dot(edge_2, qvec) * inverse_determinant;
        if (t < ray_t_min || t > ray_t_max)
            return std::make_tuple(false, t, u, v, intersect_front);

        return std::make_tuple(true, t, u, v, intersect_front);
    }

public:
    /// @brief Get the string representation of the acceleration structure.
    /// @param tab_level number of tabs to indent each line with.
    /// @return string of the acceleration structure.
    std::string toString(unsigned tab_level = 0) {
        std::stringstream result;
        const std::string tab_string("|\t");

        result << Util::repeatedString(tab_level, tab_string) << "acceleration_structure_id = " << id << std::endl;
        result << Util::repeatedString(tab_level + 1, tab_string) << "is_TLAS = " << (isTLAS ? "true" : "false")
               << std::endl;

        using NodeRefAndNumTabs = std::tuple<ConstNodeRef, unsigned>;

        std::stack<NodeRefAndNumTabs> frontier;
        frontier.push(std::make_tuple(&root, tab_level));

        while (!frontier.empty()) {
            NodeRefAndNumTabs top = frontier.top();
            const std::shared_ptr<Node>& curr_node_ref = *(get<0>(top));
            const unsigned num_tabs = get<1>(top);
            frontier.pop();

            Node* curr_node = curr_node_ref.get();

            // Append string representation of node
            result << curr_node->toString(num_tabs + 1, tab_string);

            // Traverse down the hierarchy as necessary
            switch (curr_node->type()) {
            case NodeType::Box: {
                BoxNode* box_node = static_cast<BoxNode*>(curr_node);
                const auto& children = box_node->children;
                for (int i = children.size() - 1; i >= 0; --i)
                    frontier.push(std::make_tuple(&(children[i]), num_tabs + 1));
                break;
            }
            case NodeType::Instance: {
                InstanceNode* instance_node = static_cast<InstanceNode*>(curr_node);
                result << instance_node->accelerationStructure->toString(num_tabs + 2);
                break;
            }
            default:  // No paths after current node
                break;
            }
        }

        return result.str();
    }
};

export class AccelerationStructureManager : public Value {
private:
    std::shared_ptr<AccelerationStructure> root;
    std::unique_ptr<Struct> structureInfo;

public:
    AccelerationStructureManager(Type t): Value(t) {}

private:
    /// @brief Copy the type from "other".
    /// @param other "Value" to copy the type from.
    void copyType(const Value& other) {
        // "other" is a "Struct" instead of "AccelerationStructureManager" at the very first copy of input
        const Aggregate& other = other.getType().getBase() == DataType::RAY_TRACING_ACCELERATION_STRUCTURE
                                     ? static_cast<const Aggregate&>(
                                           *((static_cast<const AccelerationStructureManager*>(&other))->structureInfo)
                                       )
                                     : static_cast<const Aggregate&>(other);

        // Get data about the structure from the "other"
        std::vector<std::array<unsigned, 4>> structure_data;
        const Array& accel_struct_info = static_cast<const Array&>(*(other[0]));
        for (unsigned i = 0; i < accel_struct_info.getSize(); ++i) {
            const Struct& curr_accel_struct_info = static_cast<const Struct&>(*(accel_struct_info[i]));

            const unsigned num_box = static_cast<const Primitive&>(*(curr_accel_struct_info[0])).data.u32;
            const unsigned num_instance = static_cast<const Primitive&>(*(curr_accel_struct_info[1])).data.u32;
            const unsigned num_triangle = static_cast<const Primitive&>(*(curr_accel_struct_info[2])).data.u32;
            const unsigned num_procedural = static_cast<const Primitive&>(*(curr_accel_struct_info[3])).data.u32;

            structure_data.push_back(std::array<unsigned, 4> {num_box, num_instance, num_triangle, num_procedural});
        }

        // Change the current type to match
        auto info = getStructureFormat(&structure_data);
        type = Type::accelerationStructure(get<1>(info), get<0>(info));

        // Start copying the type
        structureInfo = std::make_unique<Struct>(type);
        structureInfo->dummyFill();
        structureInfo->copyFrom(other);
    }

    /// @brief Build the acceleration structures. Works correctly if "structureInfo" is correctly filled.
    void buildAccelerationStructures() {
        assert(structureInfo != nullptr);

        // Note: different instance nodes can point to the same acceleration structure
        std::vector<std::shared_ptr<AccelerationStructure>> accel_structs;
        const Struct& structure_info_ref = *structureInfo;
        const Array& accel_structs_info = static_cast<const Array&>(*(structure_info_ref[0]));
        const unsigned num_accel_structs = accel_structs_info.getSize();

        // Construct each acceleration structure bottom-up
        const unsigned offset = 1;  // Offset to the start of all acceleration sturctures
        for (int i = num_accel_structs - 1; i >= 0; --i) {
            accel_structs.push_back(std::make_shared<AccelerationStructure>(
                i,
                static_cast<const Struct&>(*(structure_info_ref[i + offset])),
                static_cast<const Struct&>(*(accel_structs_info[i])),
                accel_structs,
                num_accel_structs
            ));
        }

        // Set the root acceleration structure
        root = std::move(accel_structs[num_accel_structs - 1]);
        assert(accel_structs[num_accel_structs - 1] == nullptr);
    }

public:
    AccelerationStructureManager& operator=(const AccelerationStructureManager& other) {
        // Copy the type
        copyType(other);

        // Copy the acceleration structures instead of building them
        root = std::make_shared<AccelerationStructure>(*(other.root));

        return *this;
    }

    void copyFrom(const Value& other) noexcept(false) override {
        // Copy the type of "other"
        copyType(other);

        // Build the acceleration structures
        buildAccelerationStructures();
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
        std::tuple<bool, bool> result = root->stepTrace();
        return get<0>(result);
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
    bool traceRay(
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
    ) const {
        glm::vec4 ray_origin_glm = glm::make_vec4(ray_origin.data());
        ray_origin_glm.w = 1.0f;
        glm::vec4 ray_direction_glm = glm::make_vec4(ray_direction.data());
        ray_direction_glm.w = 0.0f;

        return root->traceRay(
            ray_flags,
            cull_mask,
            ray_origin_glm,
            ray_direction_glm,
            ray_t_min,
            ray_t_max,
            use_sbt,
            offset_sbt,
            stride_sbt,
            miss_index
        );
    }

    /// @brief Include the current AABB/procedural intersection in determining the closest hit.
    /// The candidate intersection must be of type AABB.
    /// @param hit_t distance from the ray to the intersection.
    void generateIntersection(float hit_t) const {
        root->generateIntersection(hit_t);
    }

    /// @brief Include the current triangle intersection in determining the closest hit.
    /// The candidate intersection must be of type triangle.
    void confirmIntersection() const {
        root->confirmIntersection();
    }

    /// @brief Get the intersection type.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return intersection type.
    unsigned getIntersectionType(bool get_committed) const {
        return get_committed ? static_cast<unsigned>(root->getCommittedIntersectionType())
                             : static_cast<unsigned>(root->getCandidateIntersectionType());
    }

    /// @brief Get the distance from the ray to the current intersection.
    /// @param get_committed Type of intersection: committed or candidate.
    /// @return distance between the ray and intersection.
    float getIntersectionT(bool get_committed) const {
        return root->getIntersectionT(get_committed);
    }

    /// @brief Get the current intersection instance's custom index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return custom index if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceCustomIndex(bool get_committed) const {
        return root->getIntersectionInstanceCustomIndex(get_committed);
    }

    /// @brief Get the current intersection instance's id.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return id if the instance exist, otherwise, a negative integer.
    int getIntersectionInstanceId(bool get_committed) const {
        return root->getIntersectionInstanceId(get_committed);
    }

    /// @brief Get the current intersection instance's shader binding table record offset.
    /// The instance must exist; not be null.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return SBT record offset
    unsigned getIntersectionInstanceShaderBindingTableRecordOffset(bool get_committed) const {
        return root->getIntersectionInstanceShaderBindingTableRecordOffset(get_committed);
    }

    /// @brief Get the current intersection's geometry index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return geometry index.
    int getIntersectionGeometryIndex(bool get_committed) const {
        return root->getIntersectionGeometryIndex(get_committed);
    }

    /// @brief Get the current intersection's primitive index.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return primitive index.
    int getIntersectionPrimitiveIndex(bool get_committed) const {
        return root->getIntersectionPrimitiveIndex(get_committed);
    }

    /// @brief Get the current intersection's barycentric coordinates.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return barycentrics.
    glm::vec2 getIntersectionBarycentrics(bool get_committed) const {
        return root->getIntersectionBarycentrics(get_committed);
    }

    /// @brief Get whether the ray entered the front face of a triangle.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return whether the intersection was entered from the front face of a triangle.
    bool getIntersectionFrontFace(bool get_committed) const {
        return root->getIntersectionFrontFace(get_committed);
    }

    /// @brief Get whether the intersection is an opaque procedural.
    /// @return whether the intersection was an opaque procedural.
    bool getIntersectionCandidateAABBOpaque() const {
        return root->getIntersectionCandidateAABBOpaque();
    }

    /// @brief Get the object-space ray direction depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray direction.
    glm::vec3 getIntersectionObjectRayDirection(bool get_committed) const {
        return root->getIntersectionObjectRayDirection(get_committed);
    }

    /// @brief Get the object-space ray origin depending on the instance intersected.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-space ray origin.
    glm::vec3 getIntersectionObjectRayOrigin(bool get_committed) const {
        return root->getIntersectionObjectRayOrigin(get_committed);
    }

    /// @brief Get the object-to-world matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return object-to-world matrix.
    glm::mat4x3 getIntersectionObjectToWorld(bool get_committed) const {
        return root->getIntersectionObjectToWorld(get_committed);
    }

    /// @brief Get the world-to-object matrix of the intersected instance.
    /// @param get_committed type of intersection: committed or candidate.
    /// @return world-to-object matrix.
    glm::mat4x3 getIntersectionWorldToObject(bool get_committed) const {
        return root->getIntersectionWorldToObject(get_committed);
    }

    /// @brief (TODO: change "payload_info" to not be a pseudo-return, TODO: change once SBTs are implemented)
    /// Fills the payload will whether a geometry was intersected.
    /// @param payload_info payload to modify.
    /// @param intersected whether a geometry was intersected.
    void fillPayloadWithBool(Value* payload_info, const bool intersected) const {
        std::stack<Value*> frontier;
        frontier.push(payload_info);

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
                for (auto it = agg.begin(); it != agg.end(); ++it)
                    frontier.push(*it);
                break;
            }
            }
        }
    }

private:
    /// @brief Get "Primitive" as a string.
    /// @param primitive value to get a string representation.
    /// @return string representation of primitive.
    std::string getPrimitiveValueAsString(const Value& primitive) const {
        std::stringstream result;
        const DataType& data_type = primitive.getType().getBase();

        switch (data_type) {
        default: {
            std::stringstream err;
            err << "Unsupported data type; cannot convert to primitive string: " << data_type;
            throw std::runtime_error(err.str());
        }
        case DataType::FLOAT: {
            result << static_cast<const Primitive&>(primitive).data.fp32;
            break;
        }
        case DataType::UINT: {
            result << static_cast<const Primitive&>(primitive).data.u32;
            break;
        }
        case DataType::INT: {
            result << static_cast<const Primitive&>(primitive).data.i32;
            break;
        }
        case DataType::BOOL: {
            result << (static_cast<const Primitive&>(primitive).data.b32 ? "true" : "false");
            break;
        }
        }

        return result.str();
    }

public:
    /// @brief Get the string representation of the acceleration structures input that was provided.
    /// @return string representation of acceleration structures input.
    std::string toString() const {
        std::stringstream result;
        const std::string tab_string("|\t");

        // Contains the name, value, and number of tabs
        using NameAndValue = std::tuple<const std::string, const Value*, const unsigned>;

        std::stack<NameAndValue> frontier;
        const Value custom_string = Value(Type::string());
        frontier.push(std::make_tuple(std::string("Structure for acceleration structures"), structureInfo.get(), 0));

        while (!frontier.empty()) {
            const NameAndValue top = frontier.top();
            frontier.pop();

            const std::string& name = get<0>(top);
            const Value* value = get<1>(top);
            const unsigned& num_tabs = get<2>(top);
            const DataType& data_type = value->getType().getBase();
            const std::string array_element_marker("");

            switch (data_type) {
            default: {
                std::stringstream err;
                err << "Unsupported data type; cannot convert to string: " << data_type;
                throw std::runtime_error(err.str());
            }
            case DataType::FLOAT:
            case DataType::UINT:
            case DataType::INT:
            case DataType::BOOL: {
                result << Util::repeatedString(num_tabs, tab_string) << name << " = "
                       << getPrimitiveValueAsString(*value) << std::endl;
                break;
            }
            case DataType::STRUCT:
            case DataType::RAY_TRACING_ACCELERATION_STRUCTURE: {
                result << Util::repeatedString(num_tabs, tab_string) << name << " {" << std::endl;
                frontier.push(std::make_tuple(" }", &custom_string, num_tabs));

                const Struct& info = static_cast<const Struct&>(*value);

                // Add the children to the stack
                const std::vector<std::string>& names = info.getType().getNames();
                assert(names.size() == info.getSize());

                for (int i = names.size() - 1; i >= 0; --i) {
                    std::stringstream message;
                    message << names[i];
                    frontier.push(std::make_tuple(message.str(), info[i], num_tabs + 1));
                }

                break;
            }
            case DataType::ARRAY: {
                result << Util::repeatedString(num_tabs, tab_string) << name;

                const Array& info = static_cast<const Array&>(*value);
                const DataType child_data_type = info.getSize() > 0 ? info[0]->getType().getBase() : DataType::VOID;

                // Add the children to the stack if some kind of structure
                if (child_data_type == DataType::STRUCT || child_data_type == DataType::ARRAY ||
                    child_data_type == DataType::RAY_TRACING_ACCELERATION_STRUCTURE) {
                    result << " [" << std::endl;
                    frontier.push(std::make_tuple(" ]", &custom_string, num_tabs));
                    for (int i = info.getSize() - 1; i >= 0; --i) {
                        frontier.push(std::make_tuple(array_element_marker, info[i], num_tabs + 1));
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
                result << Util::repeatedString(num_tabs, tab_string) << name << std::endl;
                break;
            }
            }
        }

        return result.str();
    }

    /// @brief Gives the fields and field names of the structure for acceleration structures.
    /// @param data number of nodes in each acceleration structure.
    /// @return field names as strings and fields as types.
    static std::tuple<std::vector<std::string>, std::vector<const Type*>>
    getStructureFormat(const std::vector<std::array<unsigned, 4>>* data = nullptr) {

        using Names = std::vector<std::string>;
        using Fields = std::vector<const Type*>;

        Names names {"accelerationStructuresInfo"};
        Fields fields;

        // Note: "--- <field name>" means which field we are populating with the code below it

        // --- accelerationStructuresInfo
        const Names acceleration_structures_info_names {
            "numBoxNodes",
            "numInstanceNodes",
            "numTriangleNodes",
            "numProceduralNodes"
        };
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
                        Names instance_node_field_names {
                            "objectToWorld",
                            "id",
                            "customIndex",
                            "geometryIndex",
                            "primitiveIndex",
                            "mask",
                            "shaderBindingTableRecordOffset",
                            "accelerationStructureIndex"
                        };
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
                        Names triangle_node_field_names {
                            "geometryIndex",
                            "primitiveIndex",
                            "opaque",
                            "vertices",
                            "indices"
                        };
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
                        Names procedural_node_field_names {
                            "geometryIndex",
                            "primitiveIndex",
                            "opaque",
                            "minBounds",
                            "maxBounds"
                        };
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
