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

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include "../external/spirv.hpp"
#include "type.hpp"
#include "value.hpp"

export module value.accelerationStructure;
import value.aggregate;
import value.primitive;

/*
    TODO: convert raw pointers to smart pointers to not worry about memory reclaimation

    TODO: may want to change how to identify nodes or how nodes work

    TODO: probably want a transformation matrix or something if ended up here via an instance node?
          How to transfer information from instance node to its respective acceleration structure?
*/
class AccelerationStructure {
private:
    enum class NodeType { Box, Instance, Triangle, Procedural };

    class Node {
    public:
        virtual ~Node() {};
        virtual NodeType type() = 0;
    };

    const unsigned id;  // identifier
    const bool isTLAS;  // true: TLAS, false: BLAS
    std::unique_ptr<Node> root;  // Start of this acceleration structure

public:
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
        std::vector<std::unique_ptr<Node>> nodes; // temporarily holds ownership of nodes

        // Procedural nodes
        for (unsigned i = 0; i < numProceduralNodes; ++i) {
            const Struct& primitiveInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i]));

            const Array& minBoundsInfo = static_cast<const Array&>(*(primitiveInfo[0]));
            const Array& maxBoundsInfo = static_cast<const Array&>(*(primitiveInfo[1]));
            glm::vec4 minBounds;
            glm::vec4 maxBounds;
            assert(minBoundsInfo.getSize() == maxBoundsInfo.getSize());
            for (unsigned j = 0; j < minBoundsInfo.getSize(); ++j) {
                minBounds[j] = static_cast<const Primitive&>(*(minBoundsInfo[j])).data.fp32;
                maxBounds[j] = static_cast<const Primitive&>(*(maxBoundsInfo[j])).data.fp32;
            }
            minBounds.w = 1.0f;
            maxBounds.w = 1.0f;

            nodes.push_back(std::make_unique<ProceduralNode>(minBounds, maxBounds));
        }

        // Triangle node
        for (unsigned i = 0; i < numTriangleNodes; ++i) {
            const Struct& primitiveInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i - numProceduralNodes]));

            // Vertices
            std::vector<glm::vec3> vertices;
            const Array& verticesInfo = static_cast<const Array&>(*(primitiveInfo[0]));
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
            const Array& indicesInfo = static_cast<const Array&>(*(primitiveInfo[1]));
            for (unsigned j = 0; j < indicesInfo.getSize(); ++j) {
                unsigned value = static_cast<const Primitive&>(*(indicesInfo[j])).data.u32;
                indices.push_back(value);
            }
            nodes.push_back(std::make_unique<TriangleNode>(vertices, indices));
        }

        // Instance nodes
        for (unsigned i = 0; i < numInstanceNodes; ++i) {
            const Struct& instanceInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i - numPrimitiveNodes]));
        
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
        
            nodes.push_back(std::make_unique<InstanceNode>(transformationMatrix, mask, as));
        }

        // Box nodes
        assert(numPrimitiveNodes == 0 || numInstanceNodes == 0);
        for (unsigned i = 0; i < numBoxNodes; ++i) {
            const Struct& boxInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i - numPrimitiveNodes - numInstanceNodes]));
        
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
            std::vector<std::unique_ptr<Node>> children;
            for (const auto& childIndex : childrenIndices) {
                assert(nodes[numNodes - 1 - childIndex] != nullptr);
                children.push_back(std::move(nodes[numNodes - 1 - childIndex]));
            }
        
            nodes.push_back(std::make_unique<BoxNode>(minBounds, maxBounds, children));
        }

        // Set the root node
        // TODO: assertion to make sure nodes list is within expectation
        for (unsigned i = 0; i < nodes.size(); ++i) {
            const auto& n = nodes[i];
            if (i == (numNodes - 1) - 0) {
                assert(n != nullptr);
            } else {
                assert(n == nullptr);
            }
        }

        root = std::move(nodes[numNodes - 1]);

        // All unique_ptr in "nodes" should be null
        for (const auto& n : nodes) {
            assert(n == nullptr);
        }
    };

    ~AccelerationStructure() {};

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
    void traceRay(const unsigned rayFlags,
            const unsigned cullMask,
            const int offsetSBT,
            const int strideSBT,
            const int missIndex,
            const glm::vec4 rayOrigin,
            const float rayTMin,
            const glm::vec4 rayDirection,
            const float rayTMax,
            bool& didIntersectGeometry) {

        using NodeRef = std::unique_ptr<Node>*;  // Raw pointer that points to smart pointer

        // TODO: flags
        // Handle ray flags if something other than none was given
        if ((rayFlags & spv::RayFlagsMask::RayFlagsMaskNone) != 0) {
            if ((rayFlags & spv::RayFlagsMask::RayFlagsOpaqueKHRMask) != 0) {
                // TODO: Force all intersections with the trace to be opaque.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsNoOpaqueKHRMask) != 0) {
                // TODO: Force all intersections with the trace to be non-opaque.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsTerminateOnFirstHitKHRMask) != 0) {
                // TODO: Accept the first hit discovered.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsSkipClosestHitShaderKHRMask) != 0) {
                // TODO: Do not execute a closest hit shader.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsCullBackFacingTrianglesKHRMask) != 0) {
                // TODO: Do not intersect with the back face of triangles.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsCullFrontFacingTrianglesKHRMask) != 0) {
                // TODO: Do not intersect with the front face of triangles.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsCullOpaqueKHRMask) != 0) {
                // TODO: Do not intersect with opaque geometry.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsCullNoOpaqueKHRMask) != 0) {
                // TODO: Do not intersect with non-opaque geometry.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsSkipTrianglesKHRMask) != 0) {
                // TODO: Do not intersect with any triangle geometries.
            }
            if ((rayFlags & spv::RayFlagsMask::RayFlagsSkipAABBsKHRMask) != 0) {
                // TODO: Do not intersect with any aabb geometries.
            }
        }

        std::stack<NodeRef> frontier;
        frontier.push(&root);

        std::cout << "AS id: " << id << std::endl;
        std::cout << "~Ray origin: " << glm::to_string(rayOrigin) << std::endl;
        std::cout << "~Ray direction: " << glm::to_string(rayDirection)<< std::endl;

        while (!frontier.empty()) {
            std::unique_ptr<Node>& currNodeRef = *(frontier.top());
            frontier.pop();

            Node* currNode = currNodeRef.release();  // Take ownership

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
                    for (auto& child : boxNode->children) {
                        frontier.push(&child);
                    }
                } else {
                    // Ray didn't intersect
                    std::cout << "Ray did not intersect with AABB" << std::endl;
                }
                break;
            }
            case NodeType::Instance: {
                // TODO
                InstanceNode* instanceNode = static_cast<InstanceNode*>(currNode);

                // TODO
                // Do not process this instance if it's invisible to the ray
                if ((instanceNode->instanceMask & cullMask) == 0)
                    break;

                bool foundGeometryIntersection = false;

                // Transform the ray to match the instance's space
                glm::mat4x4 objectToWorld(1.0f); // Turn 3x4 to 4x4 so we can invert it
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        objectToWorld[i][j] = instanceNode->transformationMatrix[i][j];
                    }
                }

                const glm::mat4x4 worldToObject = glm::inverse(objectToWorld);

                glm::vec4 newRayOrigin = (rayOrigin * worldToObject);
                glm::vec4 newRayDirection = (rayDirection * worldToObject);
                
                std::cout << "\tInstance node new ray origin and ray direction respectively: " << std::endl;
                std::cout << "\t\tnew origin: " << glm::to_string(newRayOrigin) << std::endl;
                std::cout << "\t\tnew direction: " << glm::to_string(newRayDirection) << std::endl;
                std::cout << std::endl;

                // Trace the ray in the respective acceleration structure
                instanceNode->accelerationStructure->traceRay(rayFlags,
                        cullMask,
                        offsetSBT,
                        strideSBT,
                        missIndex,
                        newRayOrigin,
                        rayTMin,
                        newRayDirection,
                        rayTMax,
                        foundGeometryIntersection);

                // Handle the result of tracing the ray in the instance
                if (foundGeometryIntersection) {
                    // TODO: may want to terminate early if ray does intersect any primitive
                    didIntersectGeometry = true;
                } else {
                    // Ray did not intersect
                }

                std::cout << "\t!!!!!!!!!!!!!!!!!!!!! Done back to AS id: " << id << std::endl;

                break;
            }
            case NodeType::Triangle: {
                // TODO: handle procedural nodes
                std::cout << "Trying triangle" << std::endl;
                TriangleNode* triangleNode = static_cast<TriangleNode*>(currNode);
                float t, u, v;  // t : distance to intersection, (u,v) : uv coordinates/coordinates in triangle
                bool result = rayTriangleIntersect(rayOrigin,
                        rayDirection,
                        rayTMin,
                        rayTMax,
                        triangleNode->vertices,
                        false,
                        t,
                        u,
                        v);
                if (result) {
                    // Ray intersected
                    std::printf("+++ Ray intersected a triangle; t:%f, u:%f, v:%f\n", t, u, v);
                    didIntersectGeometry = true;
                } else {
                    // Ray did not intersect
                }
                break;
            }
            case NodeType::Procedural: { // TODO
                throw std::runtime_error("Tracing a ray through a procedural is NOT implemented.");
            }
            }
            
            currNodeRef = std::unique_ptr<Node>(currNode);  // Give back ownership
        }
    }

private:
    bool rayAABBIntersect(const glm::vec3 rayOrigin,
            const glm::vec3 rayDirection,
            const float rayTMin,
            const float rayTMax,
            const glm::vec3 minBounds,
            const glm::vec3 maxBounds) const {

        // Algorithm from "An Efficient and Robust Ray–Box Intersection Algorithm by Amy Williams et al., 2004." found
        // on Scratchapixel
        // (https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-box-intersection.html)

        // Bounds organized as (0)xmin, (1)xmax, (2)ymin, (3)ymax, (4)zmin, (5)zmax
        // Ray organized as (0)x, (1)y, (2)z
        
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

        // // TODO: correct logic?
        // // Only check the entry point of the ray into the box
        // if (tmin < rayTMin || tmin > rayTMax)
        //     return false;

        return true;
    }

    bool rayTriangleIntersect(const glm::vec3 rayOrigin,
            const glm::vec3 rayDirection,
            const float rayTMin,
            const float rayTMax,
            const std::vector<glm::vec3> vertices,
            const bool cullBackFace,
            float& t,
            float& u,
            float& v) const {

        // Moller-Trumbore ray/triangle intersection algorithm

        constexpr float epsilon = std::numeric_limits<float>::epsilon();

        // Find vectors for 2 edges that share a vertex
        glm::vec3 edge1 = vertices[1] - vertices[0];
        glm::vec3 edge2 = vertices[2] - vertices[0];

        glm::vec3 pvec = glm::cross(rayDirection, edge2);

        float determinant = glm::dot(edge1, pvec);

        if (cullBackFace) {
            if (determinant < epsilon)
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

    std::string tabbedString(unsigned numTabs, std::string message) {
        std::stringstream result("");

        for (unsigned i = 0; i < numTabs; ++i)
            result << "\t";
        result << message;

        return result.str();
    }

public:
    // TODO: make this less complex; easier to read and understand (goal: change tabbing logic)
    std::string toString(unsigned tabLevel = 0) {
        std::stringstream result("");

        result << tabbedString(tabLevel, "+ accelerationStructure id = ") << id << std::endl;
        result << tabbedString(tabLevel + 1, "* isTLAS") << " = " << (isTLAS ? "true" : "false") << std::endl;

        using NodeRef = std::unique_ptr<Node>*;  // Raw pointer to smart pointers

        std::stack<NodeRef> frontier;
        frontier.push(&root);

        // Variables for box node case
        bool applyExtraTab = false;
        std::stack<unsigned> remainingToExtraTab;

        while (!frontier.empty()) {

            // Extra tab logic only executes if entered box node previously
            if (applyExtraTab && !remainingToExtraTab.empty()) {
                unsigned& top = remainingToExtraTab.top();
                if (top > 0) {
                    --top;
                } else if (top == 0) {
                    --tabLevel;
                    remainingToExtraTab.pop();
                    if (remainingToExtraTab.empty()) {
                        applyExtraTab = false;
                    }
                }
            }

            std::unique_ptr<Node>& currNodeRef = *(frontier.top());
            frontier.pop();

            Node* currNode = currNodeRef.release();  // Take ownership

            switch (currNode->type()) {
            default: {
                std::stringstream err;
                err << "Found unknown node type (" << static_cast<int>(currNode->type())
                    << ") in \"toString()\" method of class "
                       "\"AccelerationStructure\" in \"acceleration-structure.cxx\"";
                throw std::runtime_error(err.str());
            }
            case NodeType::Box: {
                BoxNode* boxNode = static_cast<BoxNode*>(currNode);
                for (auto& child : boxNode->children) {
                    frontier.push(&child);
                }
                
                if (boxNode->children.size() > 0) {
                    applyExtraTab = true;
                    remainingToExtraTab.push(boxNode->children.size());
                }

                result << tabbedString(tabLevel + 1, "> boxNode") << std::endl;
                result << tabbedString(tabLevel + 2, "* minBounds = ") << glm::to_string(boxNode->minBounds)
                       << std::endl;
                result << tabbedString(tabLevel + 2, "* maxBounds = ") << glm::to_string(boxNode->maxBounds)
                       << std::endl;

                ++tabLevel;
                break;
            }
            case NodeType::Instance: {
                InstanceNode* instanceNode = static_cast<InstanceNode*>(currNode);
                result << tabbedString(tabLevel + 1, "> instanceNode") << std::endl;
                result << tabbedString(tabLevel + 2, "* transformationMatrix") << " = "
                       << glm::to_string(instanceNode->transformationMatrix) << std::endl;
                result << tabbedString(tabLevel + 2, "]") << std::endl;

                result << instanceNode->accelerationStructure->toString(tabLevel + 2) << std::endl;

                break;
            }
            case NodeType::Triangle: {
                TriangleNode* triangleNode = static_cast<TriangleNode*>(currNode);
                result << tabbedString(tabLevel + 1, "> triangleNode") << std::endl;
                result << tabbedString(tabLevel + 2, "* vertices") << " = [" << std::endl;
                for (unsigned i = 0; i < triangleNode->vertices.size(); ++i) {
                    result << tabbedString(tabLevel + 3, glm::to_string(triangleNode->vertices[i])) << std::endl;
                }
                result << tabbedString(tabLevel + 2, "]") << std::endl;
                result << tabbedString(tabLevel + 2, "* indices") << " = [ ";
                for (const auto& value : triangleNode->indices) {
                    result << value << ", ";
                }
                result << "]" << std::endl;

                break;
            }
            case NodeType::Procedural: {
                ProceduralNode* proceduralNode = static_cast<ProceduralNode*>(currNode);

                result << tabbedString(tabLevel + 1, "> proceduralNode") << std::endl;
                result << tabbedString(tabLevel + 2, "* minBounds = ") << glm::to_string(proceduralNode->minBounds)
                       << std::endl;
                result << tabbedString(tabLevel + 2, "* maxBounds = ") << glm::to_string(proceduralNode->maxBounds)
                       << std::endl;

                break;
            }
            }

            currNodeRef = std::unique_ptr<Node>(currNode);  // Give back ownership
        }

        return result.str();
    }

private:
    // TODO: change classes to structs? Since not using private fields.
    class BoxNode : public Node {
    public:
        const glm::vec4 minBounds; // min x, y, z
        const glm::vec4 maxBounds; // max x, y, z
        std::vector<std::unique_ptr<Node>> children;

        /// @brief Constructor for "BoxNode". IMPORTANT: Will transfer ownership of unique pointers.
        /// @param bounds TODO
        /// @param children TODO
        BoxNode(const glm::vec4 minBounds, const glm::vec4 maxBounds, std::vector<std::unique_ptr<Node>>& children)
            : minBounds(minBounds),
              maxBounds(maxBounds) {
            for (auto& child : children) {
                this->children.push_back(std::move(child));
            }
        };

        NodeType type() {
            return NodeType::Box;
        }
    };

    class InstanceNode : public Node {
    public:
        // TODO: some fields involving shaders, transformations, etc.
        glm::mat3x4 transformationMatrix;
        unsigned instanceMask;
        std::shared_ptr<AccelerationStructure> accelerationStructure;

        InstanceNode(glm::mat3x4 transformationMatrix,
                unsigned mask,
                std::shared_ptr<AccelerationStructure>& accelerationStructure)
            : transformationMatrix(transformationMatrix),
              instanceMask(mask) {
            this->accelerationStructure = accelerationStructure;
        };

        NodeType type() {
            return NodeType::Instance;
        }
    };

    class TriangleNode : public Node {
    public:
        std::vector<glm::vec3> vertices;
        std::vector<unsigned> indices;

        TriangleNode(std::vector<glm::vec3> vertices, std::vector<unsigned> indices)
            : vertices(vertices),
              indices(indices) {};

        NodeType type() {
            return NodeType::Triangle;
        }
    };

    class ProceduralNode : public Node {
    public:
        const glm::vec4 minBounds; // min x, y, z
        const glm::vec4 maxBounds; // max x, y, z

        ProceduralNode(const glm::vec4 minBounds, const glm::vec4 maxBounds)
            : minBounds(minBounds),
              maxBounds(maxBounds) {};

        NodeType type() {
            return NodeType::Procedural;
        }
    };
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
        std::cout << toString() << std::endl;

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
                    std::make_unique<AccelerationStructure>(
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

            std::cout << "Printing acceleration structures based on tree construction:" << std::endl;
            std::cout << root->toString() << std::endl; // TODO: print it out to verify correct tree
        }
    }

    void traceRay(const unsigned rayFlags,
            const unsigned cullMask,
            const int offsetSBT,
            const int strideSBT,
            const int missIndex,
            const std::vector<float> rayOrigin,
            const float rayTMin,
            const std::vector<float> rayDirection,
            const float rayTMax,
            bool& didIntersectGeometry) const {

        glm::vec4 convertedRayOrigin = glm::make_vec4(rayOrigin.data());
        convertedRayOrigin.w = 1.0f;
        glm::vec4 convertedRayDirection = glm::make_vec4(rayDirection.data());
        convertedRayDirection.w = 0.0f;

        root->traceRay(rayFlags,
                cullMask,
                offsetSBT,
                strideSBT,
                missIndex,
                convertedRayOrigin,
                rayTMin,
                convertedRayDirection,
                rayTMax,
                didIntersectGeometry);
    }

    // TODO
    void fillPayload(Value* payloadInfo, const bool intersected) const {
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
                // case DataType::UINT: {
                //     // TODO
                //     break;
                // }
                // case DataType::INT: {
                //     // TODO
                //     break;
                // }
                // case DataType::BOOL: {
                //     // TODO
                //     break;
                // }
                // case DataType::STRING: {
                //     // TODO
                //     break;
                // }
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
    // TODO: combine with one in AccelerationStructure class?
    std::string tabbedString(unsigned numTabs, std::string message, std::string messageBeforeEachTab = "") {
        std::stringstream result("");

        for (unsigned i = 0; i < numTabs; ++i)
            result << messageBeforeEachTab << "\t";
        result << message;

        return result.str();
    }

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
                    result << tabbedString(numTabs, name, "|") << " = " << getPrimitiveValueAsString(*value) << std::endl;
                    break;
                }
                case DataType::STRUCT:
                case DataType::RAY_TRACING_ACCELERATION_STRUCTURE: {
                    result << tabbedString(numTabs, name, "|") << " {" << std::endl;
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
                    result << tabbedString(numTabs, name, "|");

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
                    result << tabbedString(numTabs, name, "|") << std::endl;
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
                        Names triangleNodeFieldNames { "vertices", "indices" };
                        Fields triangleNodeFields;
                        {
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
                        Names proceduralNodeFieldNames { "bounds" };
                        Fields proceduralNodeFields;
                        {
                            // --- bounds
                            Type* bounds = new Type(Type::primitive(DataType::FLOAT));
                            proceduralNodeFields.push_back(new Type(Type::array(6, *bounds)));
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
