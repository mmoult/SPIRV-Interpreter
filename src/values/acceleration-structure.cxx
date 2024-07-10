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

    class Node {
    public:
        virtual ~Node() {};
        virtual NodeType type() = 0;
        virtual std::string toString(const unsigned indent = 0, const std::string indentString = "") const = 0;
    };

    // TODO: change classes to structs? Since not using private fields.
    class BoxNode : public Node {
    public:
        const glm::vec4 minBounds; // min x, y, z, w
        const glm::vec4 maxBounds; // max x, y, z, w
        std::vector<std::shared_ptr<Node>> children;

        /// @brief Constructor for "BoxNode".
        /// @param bounds TODO
        /// @param children TODO
        BoxNode(const glm::vec4 minBounds, const glm::vec4 maxBounds, std::vector<std::shared_ptr<Node>>& children)
            : minBounds(minBounds),
              maxBounds(maxBounds) {
            for (auto& child : children) {
                this->children.push_back(std::move(child));
            }
        };

        NodeType type() {
            return NodeType::Box;
        }

        std::string toString(const unsigned indent = 0, const std::string indentString = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indentString) << "box_node" << std::endl;

            result << Util::repeatedString(indent + 1, indentString)
                   << "min_bounds = " << Util::glmVec3ToString(minBounds) << std::endl;

            result << Util::repeatedString(indent + 1, indentString)
                   << "max_bounds = " << Util::glmVec3ToString(maxBounds) << std::endl;

            result << Util::repeatedString(indent + 1, indentString) << "num_children = " << children.size()
                   << std::endl;

            return result.str();
        }
    };

    class InstanceNode : public Node {
    public:
        // TODO: some fields involving shaders, transformations, etc.
        glm::mat3x4 objectToWorld;
        unsigned instanceMask;
        std::shared_ptr<AccelerationStructure> accelerationStructure;

        InstanceNode(glm::mat3x4 transformationMatrix,
                unsigned mask,
                std::shared_ptr<AccelerationStructure>& accelerationStructure)
            : objectToWorld(transformationMatrix),
              instanceMask(mask) {
            this->accelerationStructure = accelerationStructure;
        };

        NodeType type() {
            return NodeType::Instance;
        }

        std::string toString(const unsigned indent = 0, const std::string indentString = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indentString) << "instance_node" << std::endl;

            result << Util::repeatedString(indent + 1, indentString) << "object_to_world_matrix = [" << std::endl;
            unsigned numRows = objectToWorld.length();
            assert(numRows > 0);
            unsigned numCols = objectToWorld[0].length();
            for (unsigned row = 0; row < numRows; ++row) {
                result << Util::repeatedString(indent + 2, indentString) << "[ ";
                for (unsigned col = 0; col < numCols - 1; ++col) {
                    result << objectToWorld[row][col] << ", ";
                }
                result << objectToWorld[row][numCols - 1] << " ]" << std::endl;
            }
            result << Util::repeatedString(indent + 1, indentString) << "]" << std::endl;

            result << Util::repeatedString(indent + 1, indentString) << "instance_mask = " << instanceMask << std::endl;

            result << Util::repeatedString(indent + 1, indentString)
                   << "points_to_acceleration_structure_id = " << accelerationStructure->id << std::endl;

            return result.str();
        }
    };

    class TriangleNode : public Node {
    public:
        const bool opaque;
        const std::vector<glm::vec3> vertices;
        const std::vector<unsigned> indices;

        TriangleNode(const bool& opaque, const std::vector<glm::vec3>& vertices, const std::vector<unsigned>& indices)
            : opaque(opaque),
              vertices(vertices),
              indices(indices) {};

        NodeType type() {
            return NodeType::Triangle;
        }

        std::string toString(const unsigned indent = 0, const std::string indentString = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indentString) << "triangle_node" << std::endl;

            result << Util::repeatedString(indent + 1, indentString) << "opaque = " << (opaque ? "true" : "false")
                   << std::endl;

            result << Util::repeatedString(indent + 1, indentString) << "vertices = [" << std::endl;
            for (unsigned i = 0; i < vertices.size() - 1; ++i) {
                result << Util::repeatedString(indent + 2, indentString) << Util::glmVec3ToString(vertices[i]) << ","
                       << std::endl;
            }
            result << Util::repeatedString(indent + 2, indentString)
                   << Util::glmVec3ToString(vertices[vertices.size() - 1]) << std::endl;
            result << Util::repeatedString(indent + 1, indentString) << "]" << std::endl;

            result << Util::repeatedString(indent + 1, indentString) << "indices = [ ";
            for (unsigned i = 0; i < indices.size() - 1; ++i) {
                result << indices[i] << ", ";
            }
            result << indices[indices.size() - 1] << " ]" << std::endl;

            return result.str();
        }
    };

    class ProceduralNode : public Node {
    public:
        const bool opaque;
        const glm::vec4 minBounds; // min x, y, z
        const glm::vec4 maxBounds; // max x, y, z

        ProceduralNode(const bool& opaque, const glm::vec4& minBounds, const glm::vec4& maxBounds)
            : opaque(opaque),
              minBounds(minBounds),
              maxBounds(maxBounds) {};

        NodeType type() {
            return NodeType::Procedural;
        }

        std::string toString(const unsigned indent = 0, const std::string indentString = "") const {
            std::stringstream result("");

            result << Util::repeatedString(indent, indentString) << "procedural_node" << std::endl;

            result << Util::repeatedString(indent + 1, indentString) << "opaque = " << (opaque ? "true" : "false")
                   << std::endl;

            result << Util::repeatedString(indent + 1, indentString)
                   << "min_bounds = " << Util::glmVec3ToString(minBounds) << std::endl;

            result << Util::repeatedString(indent + 1, indentString)
                   << "max_bounds = " << Util::glmVec3ToString(maxBounds) << std::endl;

            return result.str();
        }
    };

    const bool isTLAS;  // true: TLAS, false: BLAS
    std::shared_ptr<Node> root;  // Start of this acceleration structure

public:
    const unsigned id;  // identifier

    /// @brief TODO: description
    /// @param id
    /// @param structureInfo 
    /// @param nodeCounts 
    /// @param allAccelerationStructures 
    /// @param numAccelerationStructures
    AccelerationStructure(const unsigned id,
            const Struct& structureInfo,
            const Struct& nodeCounts,
            std::vector<std::shared_ptr<AccelerationStructure>>& allAccelerationStructures,
            const unsigned numAccelerationStructures)
        : id(id),
          isTLAS(static_cast<const Primitive&>(*(structureInfo[0])).data.b32) {

        // Get node information
        const unsigned numBoxNodes = static_cast<const Primitive&>(*(nodeCounts[0])).data.u32;
        const unsigned numInstanceNodes = static_cast<const Primitive&>(*(nodeCounts[1])).data.u32;
        const unsigned numTriangleNodes = static_cast<const Primitive&>(*(nodeCounts[2])).data.u32;
        const unsigned numProceduralNodes = static_cast<const Primitive&>(*(nodeCounts[3])).data.u32;
        
        const unsigned numPrimitiveNodes = numTriangleNodes + numProceduralNodes;
        assert((numInstanceNodes == 0 && numPrimitiveNodes > 0) || (numInstanceNodes > 0 && numPrimitiveNodes == 0));

        // Construct the nodes bottom-up
        const unsigned offset = 1;  // offset of struct fields to the start of nodes
        const unsigned numNodes = numBoxNodes + numInstanceNodes + numPrimitiveNodes;
        std::vector<std::shared_ptr<Node>> nodes;

        // Procedural nodes
        for (unsigned i = 0; i < numProceduralNodes; ++i) {
            const Struct& primitiveInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i]));

            const bool opaque = static_cast<const Primitive&>(*(primitiveInfo[0])).data.b32;

            const Array& minBoundsInfo = static_cast<const Array&>(*(primitiveInfo[1]));
            const Array& maxBoundsInfo = static_cast<const Array&>(*(primitiveInfo[2]));
            glm::vec4 minBounds;
            glm::vec4 maxBounds;
            assert(minBoundsInfo.getSize() == maxBoundsInfo.getSize());
            for (unsigned j = 0; j < minBoundsInfo.getSize(); ++j) {
                minBounds[j] = static_cast<const Primitive&>(*(minBoundsInfo[j])).data.fp32;
                maxBounds[j] = static_cast<const Primitive&>(*(maxBoundsInfo[j])).data.fp32;
            }
            minBounds.w = 1.0f;
            maxBounds.w = 1.0f;

            nodes.push_back(std::make_shared<ProceduralNode>(opaque, minBounds, maxBounds));
        }

        // Triangle node
        for (unsigned i = 0; i < numTriangleNodes; ++i) {
            const Struct& primitiveInfo =
                    static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i - numProceduralNodes]));

            // Opaque
            const bool opaque = static_cast<const Primitive&>(*(primitiveInfo[0])).data.b32;

            // Vertices
            std::vector<glm::vec3> vertices;
            const Array& verticesInfo = static_cast<const Array&>(*(primitiveInfo[1]));
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
            const Array& indicesInfo = static_cast<const Array&>(*(primitiveInfo[2]));
            for (unsigned j = 0; j < indicesInfo.getSize(); ++j) {
                unsigned value = static_cast<const Primitive&>(*(indicesInfo[j])).data.u32;
                indices.push_back(value);
            }

            nodes.push_back(std::make_shared<TriangleNode>(opaque, vertices, indices));
        }

        // Instance nodes
        for (unsigned i = 0; i < numInstanceNodes; ++i) {
            const Struct& instanceInfo =
                    static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i - numPrimitiveNodes]));

            // Transformation matrix
            glm::mat3x4 transformationMatrix;
            const Array& transformationMatrixInfo = static_cast<const Array&>(*(instanceInfo[0]));
            for (unsigned rowIndex = 0; rowIndex < transformationMatrixInfo.getSize(); ++rowIndex) {
                const Array& rowInfo = static_cast<const Array&>(*(transformationMatrixInfo[rowIndex]));
                for (unsigned colIndex = 0; colIndex < rowInfo.getSize(); ++colIndex) {
                    float value = static_cast<const Primitive&>(*(rowInfo[colIndex])).data.fp32;
                    transformationMatrix[rowIndex][colIndex] = value;
                }
            }

            // Mask
            unsigned mask = static_cast<const Primitive&>(*(instanceInfo[1])).data.u32;

            // Get respective acceleration structure
            unsigned accelerationStructureIndex = static_cast<const Primitive&>(*(instanceInfo[2])).data.u32;
            unsigned index = numAccelerationStructures - 1 - accelerationStructureIndex;
            std::shared_ptr<AccelerationStructure>& as = allAccelerationStructures[index];
        
            nodes.push_back(std::make_shared<InstanceNode>(transformationMatrix, mask, as));
        }

        // Box nodes
        assert(numPrimitiveNodes == 0 || numInstanceNodes == 0);
        for (unsigned i = 0; i < numBoxNodes; ++i) {
            const Struct& boxInfo = static_cast<const Struct&>(
                    *(structureInfo[numNodes + offset - 1 - i - numPrimitiveNodes - numInstanceNodes]));

            const Array& minBoundsInfo = static_cast<const Array&>(*(boxInfo[0]));
            const Array& maxBoundsInfo = static_cast<const Array&>(*(boxInfo[1]));
            glm::vec4 minBounds;
            glm::vec4 maxBounds;
            assert(minBoundsInfo.getSize() == maxBoundsInfo.getSize());
            for (unsigned j = 0; j < minBoundsInfo.getSize(); ++j) {
                minBounds[j] = static_cast<const Primitive&>(*(minBoundsInfo[j])).data.fp32;
                maxBounds[j] = static_cast<const Primitive&>(*(maxBoundsInfo[j])).data.fp32;
            }
            minBounds.w = 1.0f;
            maxBounds.w = 1.0f;
        
            const Array& childrenIndicesInfo = static_cast<const Array&>(*(boxInfo[2]));
            std::vector<unsigned> childrenIndices;
            for (unsigned j = 0; j < childrenIndicesInfo.getSize(); ++j) {
                unsigned childIndex = static_cast<const Primitive&>(*(childrenIndicesInfo[j])).data.u32;
                childrenIndices.push_back(childIndex);
            }

            // Get the actual children
            std::vector<std::shared_ptr<Node>> children;
            for (const auto& childIndex : childrenIndices) {
                assert(nodes[numNodes - 1 - childIndex] != nullptr);
                children.push_back(std::move(nodes[numNodes - 1 - childIndex]));
            }
        
            nodes.push_back(std::make_shared<BoxNode>(minBounds, maxBounds, children));
        }

        // Assertion to make sure nodes list is within expectation
        unsigned rootIndex = numNodes - 1;
        for (unsigned i = 0; i < nodes.size(); ++i)
            assert((i != rootIndex && nodes[i] == nullptr) || (i == rootIndex && nodes[i] != nullptr));

        // Set the root node
        root = std::move(nodes[numNodes - 1]);

        // All shared_ptr in "nodes" should be null, so the root should be null
        assert(nodes[rootIndex] == nullptr);
    };

    ~AccelerationStructure() {};

private: // TODO: traversal related stuff
    using NodeRef = std::shared_ptr<Node>*;  // Raw pointer that points to smart pointer

    std::stack<NodeRef> nodesToEval;
    bool activeTrace = false;

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
        activeTrace = true;
        nodesToEval.push(&root);
    }

public: // TODO: also traversal stuff

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

            NodeRef currNodeRef = nodesToEval.top();
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
                    for (auto& child : boxNode->children) {
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
                if ((instanceNode->instanceMask & cullMask) == 0) {
                    std::cout << "\tInstance is invisible to ray" << std::endl;
                    break;
                }

                bool foundGeometryIntersection = false;

                // Transform the ray to match the instance's space
                glm::mat4x4 objectToWorld(1.0f); // Turn 3x4 to 4x4 so we can invert it
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        objectToWorld[i][j] = instanceNode->objectToWorld[i][j];
                    }
                }

                const glm::mat4x4 worldToObject = glm::inverse(objectToWorld);

                glm::vec4 newRayOrigin = (rayOrigin * worldToObject);
                glm::vec4 newRayDirection = (rayDirection * worldToObject);
                
                std::cout << "\tInstance node new ray origin and ray direction respectively: " << std::endl;
                std::cout << "\t\tnew origin: " << glm::to_string(newRayOrigin) << std::endl;
                std::cout << "\t\tnew direction: " << glm::to_string(newRayDirection) << std::endl;
                std::cout << std::endl;

                // Trace the ray in the respective acceleration structure.
                // Do not pop the node if we can still step through the instance node's acceleration structure.
                if (didPopNodePreviously) {
                    // If we did pop the previous node, then this is the first time we are stepping through the instance
                    // node's acceleration structure.
                    instanceNode->accelerationStructure->initTrace(rayFlags,
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
                didPopNodePreviously = !(instanceNode->accelerationStructure->stepTrace(foundGeometryIntersection));
                if (!didPopNodePreviously) {
                    nodesToEval.push(currNodeRef);
                }

                // Handle the result of tracing the ray in the instance
                if (foundGeometryIntersection) {
                    // TODO: may want to terminate early if ray does intersect any primitive
                    didIntersectGeometry = true;
                    
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
            // TODO: could maybe combine triangle and procedural due to similarities?
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
                bool result = rayTriangleIntersect(rayOrigin,
                        rayDirection,
                        rayTMin,
                        rayTMax,
                        triangleNode->vertices,
                        rayFlagCullBackFacingTriangles,
                        rayFlagCullFrontFacingTriangles,
                        t,
                        u,
                        v);

                if (result) {
                    // Ray intersected
                    std::cout << "+++ Ray intersected a triangle; (t, u, v) = (" << t << ", " << u << ", " << v << ")" << std::endl; 
                    didIntersectGeometry = true;

                    // Terminate on the first hit if the flag was risen
                    if (rayFlagTerminateOnFirstHit) { 
                        std::cout << "Terminated on first hit!" << std::endl;
                        return false;
                    }
                } else {
                    // Ray did not intersect
                }
                foundPrimitive = true;
                break;
            }
            // TODO: Not correct until multiple shader invocation support.
            // Currently, it returns an intersection if it intersects the 
            // respective AABB for the procedural.
            // TODO: add intersection shader invocation.
            case NodeType::Procedural: {
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

                    if (rayFlagTerminateOnFirstHit) {
                        std::cout << "Terminated on first hit!" << std::endl;
                        return false;
                    }
                } else {
                    // Ray did not hit
                }
                foundPrimitive = true;
                break;
            }
            }
        }

        
        if (nodesToEval.empty()) {
            std::cout << "@@@@@@@@@@ Making trace inactive." << std::endl;
            activeTrace = false;
        }
        return !nodesToEval.empty();
    }

public:
    /*
    Typically, a BLAS corresponds to individual 3D models within a scene,
    and a TLAS corresponds to an entire scene built by positioning
    (with 3-by-4 transformation matrices) individual referenced BLASes.
    */
    // TODO
    // TODO: change return type to a ray payload or something related
    // TODO: change parameter types
    // TODO: SBT once can do multiple shader invocations
    // Modifies payload parameter
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
            float& v) const {

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
        
        using NodeRef = std::tuple<std::shared_ptr<Node>*, unsigned>;  // Raw pointer to smart pointers

        std::stack<NodeRef> frontier;
        frontier.push(std::make_tuple(&root, tabLevel + 1));

        while (!frontier.empty()) {
            NodeRef top = frontier.top();
            std::shared_ptr<Node>& currNodeRef = *(get<0>(top));
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
                for (auto& child : boxNode->children) {
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
        // TODO memory management
        delete structureInfo;
    }

    void copyFrom(const Value& new_val) noexcept(false) override {

        // Copy from "new_val"
        {
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

        // Print it
        // std::cout << toString() << std::endl;

        // Build the BVH tree
        {
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

            // std::cout << root->toString() << std::endl; // TODO: print it out to verify correct tree
        }
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
    std::string getPrimitiveValueAsString(const Value& value) {
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
    std::string toString() {
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
        // TODO: memory management?
        using Names = std::vector<std::string>;
        using Fields = std::vector<const Type*>;

        Names names { "accelerationStructuresInfo" };
        Fields fields;

        // Note: "--- <field name>" means which field we are populating with the code below it

        // --- accelerationStructuresInfo
        const Names accelerationStructuresInfoName { "numBoxNodes", "numInstanceNodes", "numTriangleNodes", "numProceduralNodes" };
        Fields accelerationStructuresInfoFields;
        {
            // --- numBoxNodes
            accelerationStructuresInfoFields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numInstanceNodes
            accelerationStructuresInfoFields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numTriangleNodes
            accelerationStructuresInfoFields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numProceduralNodes
            accelerationStructuresInfoFields.push_back(new Type(Type::primitive(DataType::UINT)));
        }
        Type* accelerationStructuresInfoStruct =
                new Type(Type::structure(accelerationStructuresInfoFields, accelerationStructuresInfoName));
        Type* accelerationStructuresInfoArray = new Type(Type::array(0, *accelerationStructuresInfoStruct));
        fields.push_back(accelerationStructuresInfoArray);

        // Add the acceleration structures if we know how many there are
        // Note: data contains information about the acceleration structures
        if (data != nullptr) {

            // Add on the acceleration structures
            for (int i = 0; i < data->size(); ++i) {
                const unsigned numBoxNodes = (*data)[i][0];
                const unsigned numInstanceNodes = (*data)[i][1];
                const unsigned numTriangleNodes = (*data)[i][2];
                const unsigned numProceduralNodes = (*data)[i][3];

                // --- accelerationStructure
                Names accelerationStructureFieldNames { "isTLAS" };
                Fields accelerationStructureFields;
                {
                    // --- isTLAS
                    accelerationStructureFields.push_back(new Type(Type::primitive(DataType::BOOL)));

                    // --- boxNodes
                    if (numBoxNodes > 0) {
                        const Names boxNodeFieldNames { "minBounds", "maxBounds", "childrenIndices" };
                        Fields boxNodeFields;
                        {
                            Type* bounds = new Type(Type::primitive(DataType::FLOAT));

                            // --- minBounds
                            boxNodeFields.push_back(new Type(Type::array(3, *bounds)));

                            // --- maxBounds
                            boxNodeFields.push_back(new Type(Type::array(3, *bounds)));

                            // --- childrenIndices
                            Type* childrenIndex = new Type(Type::primitive(DataType::UINT));
                            boxNodeFields.push_back(new Type(Type::array(0, *childrenIndex)));
                        }
                        Type* boxNode = new Type(Type::structure(boxNodeFields, boxNodeFieldNames));

                        for (int j = 0; j < numBoxNodes; ++j) {
                            std::stringstream boxNodeName;
                            boxNodeName << "box" << j;
                            accelerationStructureFieldNames.push_back(boxNodeName.str());
                            accelerationStructureFields.push_back(boxNode);
                        }
                    }

                    // --- instanceNodes
                    if (numInstanceNodes > 0) {
                        Names instanceNodeFieldNames { "transformationMatrix", "mask", "index" };
                        Fields instanceNodeFields;
                        {
                            // --- transformationMatrix
                            unsigned numRows = 3;
                            unsigned numCols = 4;
                            Type* floatValue = new Type(Type::primitive(DataType::FLOAT));
                            Type* rows = new Type(Type::array(numCols, *floatValue));
                            Type* matrix = new Type(Type::array(numRows, *rows));
                            instanceNodeFields.push_back(matrix);

                            // --- mask
                            instanceNodeFields.push_back(new Type(Type::primitive(DataType::UINT)));

                            // --- index
                            instanceNodeFields.push_back(new Type(Type::primitive(DataType::UINT)));
                        }
                        Type* instanceNode = new Type(Type::structure(instanceNodeFields, instanceNodeFieldNames));

                        for (int j = 0; j < numInstanceNodes; ++j) {
                            std::stringstream instanceNodeName;
                            instanceNodeName << "instance" << j;
                            accelerationStructureFieldNames.push_back(instanceNodeName.str());
                            accelerationStructureFields.push_back(instanceNode);
                        }
                    }

                    // --- triangleNodes
                    if (numTriangleNodes > 0) {
                        Names triangleNodeFieldNames { "opaque", "vertices", "indices" };
                        Fields triangleNodeFields;
                        {
                            // --- opaque
                            triangleNodeFields.push_back(new Type(Type::primitive(DataType::BOOL)));

                            // --- vertices
                            Type* floatValue = new Type(Type::primitive(DataType::FLOAT));
                            Type* vertex = new Type(Type::array(0, *floatValue));
                            triangleNodeFields.push_back(new Type(Type::array(0, *vertex)));

                            // --- indices
                            Type* indices = new Type(Type::primitive(DataType::UINT));
                            triangleNodeFields.push_back(new Type(Type::array(0, *indices)));
                        }
                        Type* triangleNode = new Type(Type::structure(triangleNodeFields, triangleNodeFieldNames));

                        for (int j = 0; j < numTriangleNodes; ++j) {
                            std::stringstream triangleNodeName;
                            triangleNodeName << "triangle" << j;
                            accelerationStructureFieldNames.push_back(triangleNodeName.str());
                            accelerationStructureFields.push_back(triangleNode);
                        }
                    }

                    // --- proceduralNodes
                    if (numProceduralNodes > 0) {
                        Names proceduralNodeFieldNames { "opaque", "minBounds", "maxBounds" };
                        Fields proceduralNodeFields;
                        {
                            // --- opaque
                            proceduralNodeFields.push_back(new Type(Type::primitive(DataType::BOOL)));

                            Type* bounds = new Type(Type::primitive(DataType::FLOAT));

                            // --- minBounds
                            proceduralNodeFields.push_back(new Type(Type::array(3, *bounds)));

                            // --- maxBounds
                            proceduralNodeFields.push_back(new Type(Type::array(3, *bounds)));
                        }
                        Type* proceduralNode = new Type(Type::structure(proceduralNodeFields, proceduralNodeFieldNames));

                        for (int j = 0; j < numProceduralNodes; ++j) {
                            std::stringstream proceduralNodeName;
                            proceduralNodeName << "procedural" << j;
                            accelerationStructureFieldNames.push_back(proceduralNodeName.str());
                            accelerationStructureFields.push_back(proceduralNode);
                        }
                    }
                }

                Type* accelerationStructure =
                        new Type(Type::structure(accelerationStructureFields, accelerationStructureFieldNames));
                std::stringstream accelerationStructureName;
                accelerationStructureName << "accelerationStructure" << i;
                names.push_back(accelerationStructureName.str());
                fields.push_back(accelerationStructure);
            }
        }

        return std::make_tuple(names, fields);
    }
};
