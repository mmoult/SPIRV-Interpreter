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

#include "../external/spirv.hpp"
#include "type.hpp"
#include "value.hpp"

export module value.accelerationStructure;
import value.aggregate;
import value.primitive;

// TODO: Move somewhere else, don't put it here. Could change to using external library like GLM.
// TODO: Create classes or structs to better handle vectors and matrices
namespace MathUtil {
    /// @brief TODO
    /// @param a 
    /// @param b 
    /// @return 
    float dotProduct(const std::vector<float> a, const std::vector<float> b) {
        assert(a.size() == b.size());

        float result = 0.0f;

        for (unsigned i = 0; i < a.size(); ++i)
            result += (a[i] * b[i]);

        return result;
    }

    /// @brief TODO
    /// @param a 
    /// @param b 
    /// @return 
    std::vector<float> crossProduct(const std::vector<float> a, const std::vector<float> b) {
        // Limit to 3-D vectors
        assert(a.size() == 3 && a.size() == b.size());

        return std::vector<float> {
            (a[1] * b[2]) - (a[2] * b[1]),
            (a[2] * b[0]) - (a[0] * b[2]),
            (a[0] * b[1]) - (a[1] * b[0])
        };
    }

    /// @brief Scale vector a by s; for i in a do a[i] * s.
    /// @param a Dimension n vector.
    /// @param s Scale value.
    /// @return Resulting vector from scaling.
    std::vector<float> vectorScale(const std::vector<float> a, const float s) {
        std::vector<float> result = a;

        for (auto& value : result)
            value *= s;

        return result;
    }

    /// @brief Add vector a and vector b; (a + b). Vectors a and b must be the same dimension.
    /// @param a Dimension n vector.
    /// @param b Dimension n vector.
    /// @return Resulting vector from addition.
    std::vector<float> vectorAdd(const std::vector<float> a, const std::vector<float> b) {
        assert(a.size() == b.size());

        std::vector<float> result(a.size());
        
        for (unsigned i = 0; i < a.size(); ++i)
            result[i] = a[i] + b[i];

        return result;
    }

    /// @brief Subtract vector b from vector a; (a - b). Vectors a and b must be the same dimension.
    /// @param a Dimension n vector.
    /// @param b Dimension n vector.
    /// @return Resulting vector from subtraction.
    std::vector<float> vectorSubtract(const std::vector<float> a, const std::vector<float> b) {
        assert(a.size() == b.size());

        std::vector<float> result(a.size());
        
        for (unsigned i = 0; i < a.size(); ++i)
            result[i] = a[i] - b[i];

        return result;
    }

    /// @brief TODO
    /// @param vec 
    /// @param mat 
    /// @return 
    std::vector<float> vectorMatrixProduct(const std::vector<float> vec, const std::array<std::array<float, 4>, 3> mat) {
        // TODO: Maybe use Strassen algorithm
        // For now return a 3-D vector
        // Multiple matrix to vector so a (3x4 dim) * (3x1 dim) where ignoreing 4th column -> (3x3 dim) * (3x1 dim) instead
        std::vector<float> result;
        for (unsigned row = 0; row < mat.size(); ++row) {
            float value = 0.0f;
            for (unsigned col = 0; col < mat[row].size() - 1; ++col) { // For now ignore 4th column of matrix
                value += mat[row][col] * vec[col];
            }
            result.push_back(value);
        }
        return result;
    }
}

/*
    TODO: convert raw pointers to smart pointers to not worry about memory reclaimation

    TODO: may want to change how to identify nodes or how nodes work

    TODO: probably want a transformation matrix or something if ended up here via an instance node?
          How to transfer information from instance node to its respective acceleration structure?
*/
class AccelerationStructure {
private:
    enum class NodeType { Box, Instance, Primitive };

    class Node {
    public:
        virtual ~Node() {};
        virtual NodeType type() = 0;
    };

    const unsigned id;  // identifier
    const bool isTLAS;  // true: TLAS, false: BLAS
    const unsigned splitValue;  // What kind of BVH? A BVH2 (binary BVH)? etc.
    // unsigned transformationMatrix;  // TODO: need to make it an actual matrix. Is it needed?
    std::unique_ptr<Node> root;  // Start of this acceleration structure

public:
    /// @brief TODO: description
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
          isTLAS(static_cast<const Primitive&>(*(structureInfo[0])).data.b32),
          splitValue(static_cast<const Primitive&>(*(structureInfo[1])).data.u32) {

        // TODO: Keep track of an object-to-world matrix? Maybe just transform the ray instead (what data do I account for)?
        // transformationMatrix = static_cast<const Primitive&>(*(structureInfo[2])).data.u32; // TODO: figure out

        // Get node information
        const unsigned numBoxNodes = static_cast<const Primitive&>(*(nodeCounts[0])).data.u32;
        const unsigned numInstanceNodes = static_cast<const Primitive&>(*(nodeCounts[1])).data.u32;
        const unsigned numPrimitiveNodes = static_cast<const Primitive&>(*(nodeCounts[2])).data.u32;
        assert((numInstanceNodes == 0 && numPrimitiveNodes > 0) || (numInstanceNodes > 0 && numPrimitiveNodes == 0));

        // Construct the nodes bottom-up
        const unsigned offset = 4;  // offset of struct fields to the start of nodes
        const unsigned numNodes = numBoxNodes + numInstanceNodes + numPrimitiveNodes;
        std::vector<std::unique_ptr<Node>> nodes; // temporarily holds ownership of nodes

        // Primitive nodes
        for (unsigned i = 0; i < numPrimitiveNodes; ++i) {
            const Struct& primitiveInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i]));

            // Vertices
            std::vector<std::vector<float>> vertices;
            const Array& verticesInfo = static_cast<const Array&>(*(primitiveInfo[0]));
            for (unsigned j = 0; j < verticesInfo.getSize(); ++j) {
                std::vector<float> vertex;
                const Array& vertexInfo = static_cast<const Array&>(*(verticesInfo[j]));
                for (unsigned k = 0; k < vertexInfo.getSize(); ++k) {
                    float value = static_cast<const Primitive&>(*(vertexInfo[k])).data.fp32;
                    vertex.push_back(value);
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

            nodes.push_back(std::make_unique<PrimitiveNode>(vertices, indices));
        }

        // Instance nodes
        for (unsigned i = 0; i < numInstanceNodes; ++i) {
            const Struct& instanceInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i - numPrimitiveNodes]));
        
            // Transformation matrix
            std::array<std::array<float, 4>, 3> transformationMatrix;
            const Array& transformationMatrixInfo = static_cast<const Array&>(*(instanceInfo[0]));
            for (unsigned rowIndex = 0; rowIndex < transformationMatrixInfo.getSize(); ++rowIndex) {
                const Array& rowInfo = static_cast<const Array&>(*(transformationMatrixInfo[rowIndex]));
                for (unsigned colIndex = 0; colIndex < rowInfo.getSize(); ++colIndex) {
                    float value = static_cast<const Primitive&>(*(rowInfo[colIndex])).data.fp32;
                    transformationMatrix[rowIndex][colIndex] = value;
                }
            }

            // Get respective acceleration structure
            unsigned accelerationStructureIndex = static_cast<const Primitive&>(*(instanceInfo[1])).data.u32;
            unsigned index = numAccelerationStructures - 1 - accelerationStructureIndex;
            std::shared_ptr<AccelerationStructure>& as = allAccelerationStructures[index];
        
            nodes.push_back(std::make_unique<InstanceNode>(transformationMatrix, as));
        }

        // Box nodes
        assert(numPrimitiveNodes == 0 || numInstanceNodes == 0);
        for (unsigned i = 0; i < numBoxNodes; ++i) {
            const Struct& boxInfo = static_cast<const Struct&>(*(structureInfo[numNodes + offset - 1 - i - numPrimitiveNodes - numInstanceNodes]));
        
            const Array& boundsInfo = static_cast<const Array&>(*(boxInfo[0]));
            std::array<float, 6> bounds;
            for (unsigned j = 0; j < boundsInfo.getSize(); ++j) {
                float bound = static_cast<const Primitive&>(*(boundsInfo[j])).data.fp32;
                bounds[j] = bound;
            }
        
            const Array& childrenIndicesInfo = static_cast<const Array&>(*(boxInfo[1]));
            std::vector<unsigned> childrenIndices;
            for (unsigned j = 0; j < childrenIndicesInfo.getSize(); ++j) {
                unsigned childIndex = static_cast<const Primitive&>(*(childrenIndicesInfo[j])).data.u32;
                childrenIndices.push_back(childIndex);
            }
            assert(childrenIndices.size() <= splitValue); // TODO: not sure how split value works

            // Get the actual children
            std::vector<std::unique_ptr<Node>> children;
            for (const auto& childIndex : childrenIndices) {
                assert(nodes[numNodes - 1 - childIndex] != nullptr);
                children.push_back(std::move(nodes[numNodes - 1 - childIndex]));
            }
        
            nodes.push_back(std::make_unique<BoxNode>(bounds, children));
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

        const unsigned rootIndex = static_cast<const Primitive&>(*(structureInfo[3])).data.u32;
        root = std::move(nodes[numNodes - 1 - rootIndex]);

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
    // Modifies payload parameter
    void traceRay(const unsigned rayFlags,
            const unsigned cullMask,
            const int offsetSBT,
            const int strideSBT,
            const int missIndex,
            const std::vector<float> rayOrigin,
            const float rayTMin,
            const std::vector<float> rayDirection,
            const float rayTMax,
            bool& didIntersectGeometry) {

        // TODO: figure out payload
        using NodeRef = std::unique_ptr<Node>*;  // Raw pointer to smart pointers

        // TODO: can ignore SBT? Since only dealing with a single shader at a time

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

        std::cout << "~Ray origin: [ ";
        for (const auto& a : rayOrigin) {
            std::cout << a << " ";
        }
        std::cout << "]" << std::endl;
        std::cout << "~Ray direction: [ ";
        for (const auto& a : rayDirection) {
            std::cout << a << " ";
        }
        std::cout << "]" << std::endl;

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
                bool result = rayAABBIntersect(rayOrigin, rayDirection, rayTMin, rayTMax, boxNode->bounds);
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
                std::vector<float> newRayOrigin =
                        MathUtil::vectorMatrixProduct(rayOrigin, instanceNode->transformationMatrix);
                std::vector<float> newRayDirection =
                        MathUtil::vectorMatrixProduct(rayDirection, instanceNode->transformationMatrix);
                
                std::cout << "\tInstance node new ray origin and ray direction respectively: " << std::endl;
                std::cout << "\t\torigin: [ ";
                for (const auto& a : newRayOrigin) {
                    std::cout << a << " ";
                }
                std::cout << "]" << std::endl;
                std::cout << "\t\tdirection: [ ";
                for (const auto& a : newRayDirection) {
                    std::cout << a << " ";
                }
                std::cout << "]" << std::endl;

                // Trace the ray in the respective acceleration structure
                instanceNode->accelerationStructure->traceRay(rayFlags,
                        cullMask,
                        offsetSBT,
                        strideSBT,
                        missIndex,
                        rayOrigin,
                        rayTMin,
                        rayDirection,
                        rayTMax,
                        foundGeometryIntersection);

                // Handle the result of tracing the ray in the instance
                if (foundGeometryIntersection) {
                    // TODO: may want to terminate early if ray does intersect any primitive
                    didIntersectGeometry = true;
                } else {
                    // Ray did not intersect
                }

                break;
            }
            case NodeType::Primitive: {
                // TODO: handle procedural nodes
                std::cout << "Trying primitive" << std::endl;
                PrimitiveNode* primitiveNode = static_cast<PrimitiveNode*>(currNode);
                float t, u, v;  // t : distance to intersection, (u,v) : uv coordinates/coordinates in triangle
                bool result = rayTriangleIntersect(rayOrigin,
                        rayDirection,
                        rayTMin,
                        rayTMax,
                        primitiveNode->vertices,
                        false,
                        t,
                        u,
                        v);
                if (result) {
                    // Ray intersected
                    std::printf("Ray intersected a primitive; t:%f, u:%f, v:%f\n", t, u, v);
                    didIntersectGeometry = true;
                } else {
                    // Ray did not intersect
                }
                break;
            }
            }
            
            currNodeRef = std::unique_ptr<Node>(currNode);  // Give back ownership
        }
    }

private:
    bool rayAABBIntersect(const std::vector<float> rayOrigin,
            const std::vector<float> rayDirection,
            const float rayTMin,
            const float rayTMax,
            const std::array<float, 6> bounds) const {

        // Algorithm from "An Efficient and Robust Ray–Box Intersection Algorithm by Amy Williams et al., 2004." found
        // on Scratchapixel
        // (https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-box-intersection.html)

        // Bounds organized as (0)xmin, (1)xmax, (2)ymin, (3)ymax, (4)zmin, (5)zmax
        // Ray organized as (0)x, (1)y, (2)z
        
        // Check the x-plane
        float tmin = (bounds[0] - rayOrigin[0]) / rayDirection[0];
        float tmax = (bounds[1] - rayOrigin[0]) / rayDirection[0];

        if (tmin > tmax)
            std::swap(tmin, tmax);

        // Check the y-plane
        float tymin = (bounds[2] - rayOrigin[1]) / rayDirection[1];
        float tymax = (bounds[3] - rayOrigin[1]) / rayDirection[1];

        if (tymin > tymax)
            std::swap(tymin, tymax);

        if ((tmin > tymax) || (tymin > tmax))
            return false;

        if (tymin > tmin)
            tmin = tymin;
        if (tymax < tmax)
            tmax = tymax;

        // Check the z-plane
        float tzmin = (bounds[4] - rayOrigin[2]) / rayDirection[2];
        float tzmax = (bounds[5] - rayOrigin[2]) / rayDirection[2];

        if (tzmin > tzmax)
            std::swap(tzmin, tzmax);

        if ((tmin > tzmax) || (tzmin > tmax))
            return false;

        if (tzmin > tmin)
            tmin = tzmin;
        if (tzmax < tmax)
            tmax = tzmax;

        // TODO: correct logic?
        // Only check the entry point of the ray into the box
        if (tmin < rayTMin || tmin > rayTMax)
            return false;

        return true;
    }

    bool rayTriangleIntersect(const std::vector<float> rayOrigin,
            const std::vector<float> rayDirection,
            const float rayTMin,
            const float rayTMax,
            const std::vector<std::vector<float>> vertices,
            const bool cullBackFace,
            float& t,
            float& u,
            float& v) const {

        // Moller-Trumbore ray/triangle intersection algorithm

        constexpr float epsilon = std::numeric_limits<float>::epsilon();

        // Find vectors for 2 edges that share a vertex
        std::vector<float> edge1 = MathUtil::vectorSubtract(vertices[1], vertices[0]);
        std::vector<float> edge2 = MathUtil::vectorSubtract(vertices[2], vertices[0]);

        std::vector<float> pvec = MathUtil::crossProduct(rayDirection, edge2);

        float determinant = MathUtil::dotProduct(edge1, pvec);

        if (cullBackFace) {
            if (determinant < epsilon)
                return false;
        } else {
            if (std::fabs(determinant) < epsilon)
                return false;
        }

        float inverseDeterminant = 1.0f / determinant;

        std::vector<float> tvec = MathUtil::vectorSubtract(rayOrigin, vertices[0]);  // Distance from ray origin to shared vertex 

        u = MathUtil::dotProduct(tvec, pvec) * inverseDeterminant;
        if (u < 0 || u > 1)
            return false;

        std::vector<float> qvec = MathUtil::crossProduct(tvec, edge1);

        v = MathUtil::dotProduct(rayDirection, qvec) * inverseDeterminant;
        if (v < 0 || u + v > 1)
            return false;

        t = MathUtil::dotProduct(edge2, qvec) * inverseDeterminant;

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
    std::string toString(unsigned tabLevel = 0) {
        std::stringstream result("");

        result << tabbedString(tabLevel, "+ accelerationStructure id = ") << id << std::endl;
        result << tabbedString(tabLevel + 1, "* isTLAS") << " = " << (isTLAS ? "true" : "false") << std::endl;
        result << tabbedString(tabLevel + 1, "* splitValue") << " = " << splitValue << std::endl;

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
                result << tabbedString(tabLevel + 2, "* bounds") << " = [ ";
                for (const auto& value : boxNode->bounds) {
                    result << value << ", ";
                }
                result << "]" << std::endl;

                ++tabLevel;
                break;
            }
            case NodeType::Instance: {
                InstanceNode* instanceNode = static_cast<InstanceNode*>(currNode);
                result << tabbedString(tabLevel + 1, "> instanceNode") << std::endl;
                result << tabbedString(tabLevel + 2, "* transformationMatrix") << " = [" << std::endl;
                for (unsigned row = 0; row < instanceNode->transformationMatrix.size(); ++row) {
                    result << tabbedString(tabLevel + 3, "[ ");
                    for (unsigned col = 0; col < instanceNode->transformationMatrix[row].size(); ++col) {
                        result << instanceNode->transformationMatrix[row][col] << ", ";
                    }
                    result << "]" << std::endl;
                }
                result << tabbedString(tabLevel + 2, "]") << std::endl;

                result << instanceNode->accelerationStructure->toString(tabLevel + 2) << std::endl;

                break;
            }
            case NodeType::Primitive: {
                PrimitiveNode* primitiveNode = static_cast<PrimitiveNode*>(currNode);
                result << tabbedString(tabLevel + 1, "> primitiveNode") << std::endl;
                result << tabbedString(tabLevel + 2, "* vertices") << " = [" << std::endl;
                for (unsigned row = 0; row < primitiveNode->vertices.size(); ++row) {
                    result << tabbedString(tabLevel + 3, "[ ");
                    for (unsigned col = 0; col < primitiveNode->vertices[row].size(); ++col) {
                        result << primitiveNode->vertices[row][col] << ", ";
                    }
                    result << "]" << std::endl;
                }
                result << tabbedString(tabLevel + 2, "]") << std::endl;
                result << tabbedString(tabLevel + 2, "* indices") << " = [ ";
                for (const auto& value : primitiveNode->indices) {
                    result << value << ", ";
                }
                result << "]" << std::endl;

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
        const std::array<float, 6> bounds;  // [ min x, max x, min y, max y, min z, max z ]
        std::vector<std::unique_ptr<Node>> children;

        /// @brief Constructor for "BoxNode". IMPORTANT: Will transfer ownership of unique pointers.
        /// @param bounds TODO
        /// @param children TODO
        BoxNode(const std::array<float, 6> bounds, std::vector<std::unique_ptr<Node>>& children): bounds(bounds) {
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
        std::array<std::array<float, 4>, 3> transformationMatrix;  // 3 x 4 matrix
        unsigned instanceMask;  // TODO: handle
        std::shared_ptr<AccelerationStructure> accelerationStructure;

        InstanceNode(std::array<std::array<float, 4>, 3> transformationMatrix,
                std::shared_ptr<AccelerationStructure>& accelerationStructure)
            : transformationMatrix(transformationMatrix),
              instanceMask(0xff) {
            this->accelerationStructure = accelerationStructure;
        };

        NodeType type() {
            return NodeType::Instance;
        }
    };

    // TODO: Split into triangle and procedural
    class PrimitiveNode : public Node {
    public:
        std::vector<std::vector<float>> vertices;
        std::vector<unsigned> indices;

        PrimitiveNode(std::vector<std::vector<float>> vertices, std::vector<unsigned> indices)
            : vertices(vertices),
              indices(indices) {};

        NodeType type() {
            return NodeType::Primitive;
        }
    };
};

export class AccelerationStructureManager : public Value {
private:
    std::shared_ptr<AccelerationStructure> root = nullptr; // Start of all acceleration structures TODO: make unique?
    Struct* structureInfo = nullptr;

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
            std::vector<std::array<unsigned, 3>> structureData;
            const Array& accelerationStructuresInfo = static_cast<const Array&>(*(other[1]));
            for (unsigned i = 0; i < accelerationStructuresInfo.getSize(); ++i) {
                const Struct& currAccelerationStructureInfo = static_cast<const Struct&>(*(accelerationStructuresInfo[i]));

                const unsigned numBoxNodes =
                        static_cast<const Primitive&>(*(currAccelerationStructureInfo[0])).data.u32;
                const unsigned numInstanceNodes =
                        static_cast<const Primitive&>(*(currAccelerationStructureInfo[1])).data.u32;
                const unsigned numPrimitiveNodes =
                        static_cast<const Primitive&>(*(currAccelerationStructureInfo[2])).data.u32;
                
                structureData.push_back(std::array<unsigned, 3> { numBoxNodes, numInstanceNodes, numPrimitiveNodes });
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
            // TODO: Build tree bottom-up

            // Note: different instance nodes can point to the same acceleration structure
            std::vector<std::shared_ptr<AccelerationStructure>> accelerationStructures;

            const Struct& structureInfoRef = *structureInfo;
            unsigned rootIndex = static_cast<const Primitive&>(*(structureInfoRef[0])).data.u32;
            const Array& accelerationStructuresInfo = static_cast<const Array&>(*(structureInfoRef[1]));
            unsigned numAccelerationStructures = accelerationStructuresInfo.getSize();

            // Construct each acceleration structure bottom-up
            unsigned offset = 2; // Offset to the first acceleration structure
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
            root = std::move(accelerationStructures[(numAccelerationStructures - 1) - rootIndex]);
            assert(accelerationStructures[(numAccelerationStructures - 1) - rootIndex] == nullptr);

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

        root->traceRay(rayFlags,
                cullMask,
                offsetSBT,
                strideSBT,
                missIndex,
                rayOrigin,
                rayTMin,
                rayDirection,
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

    // TODO: change to use the type for names instead so it's not hard coded
    std::string toString() {
        std::stringstream result("");
        result << "++++ Structure for acceleration structures: ++++" << std::endl;

        // !---------
        unsigned rootIndex = static_cast<Primitive*>((*structureInfo)[0])->data.u32;
        result << "rootIndex = " << rootIndex << std::endl;

        // !---------
        Array* accelerationStructuresInfoArray = static_cast<Array*>((*structureInfo)[1]);
        result << "accelerationStructuresInfo = [" << std::endl;
        unsigned numAccelerationStructures = accelerationStructuresInfoArray->getSize();
        std::vector<std::vector<unsigned>> counts;
        counts.reserve(numAccelerationStructures);
        for (int i = 0; i < numAccelerationStructures; ++i) {
            Struct* accelerationStructureInfoStruct = static_cast<Struct*>((*accelerationStructuresInfoArray)[i]);
            result << "\t{" << std::endl;

            unsigned numBoxNodes = static_cast<Primitive*>((*accelerationStructureInfoStruct)[0])->data.u32;
            result << "\t\tnumBoxNodes = " << numBoxNodes << std::endl;

            unsigned numInstanceNodes = static_cast<Primitive*>((*accelerationStructureInfoStruct)[1])->data.u32;
            result << "\t\tnumInstanceNodes = " << numInstanceNodes << std::endl;

            unsigned numPrimitiveNodes = static_cast<Primitive*>((*accelerationStructureInfoStruct)[2])->data.u32;
            result << "\t\tnumPrimitiveNodes = " << numPrimitiveNodes << std::endl;

            result << "\t}," << std::endl;
            
            counts.push_back(std::vector<unsigned>{numBoxNodes, numInstanceNodes, numPrimitiveNodes});
        }
        result << "]" << std::endl;

        // !---------
        for (int i = 0; i < numAccelerationStructures; ++i) {
            result << "accelerationStructure" << i << " {" << std::endl;

            Struct* accelerationStructure = static_cast<Struct*>((*structureInfo)[i + 2]);

            bool isTLAS = static_cast<Primitive*>((*accelerationStructure)[0])->data.b32;
            result << "\tisTLAS = " << (isTLAS ? "true" : "false") << std::endl;

            unsigned splitValue = static_cast<Primitive*>((*accelerationStructure)[1])->data.u32;
            result << "\tsplitValue = " << splitValue << std::endl;

            unsigned transformationMatrix = static_cast<Primitive*>((*accelerationStructure)[2])->data.u32;
            result << "\ttransformationMatrix = " << transformationMatrix << std::endl;

            unsigned rootNodeIndex = static_cast<Primitive*>((*accelerationStructure)[3])->data.u32;
            result << "\trootNodeIndex = " << rootNodeIndex << std::endl;

            unsigned numBoxNodes = counts[i][0];
            unsigned numInstanceNodes = counts[i][1];
            unsigned numPrimitiveNodes = counts[i][2];

            unsigned offset = 4;
            for (int j = 0; j < numBoxNodes; ++j) {
                result << "\tbox" << j << " = {" << std::endl; 

                Struct* boxNode = static_cast<Struct*>((*accelerationStructure)[j + offset]);
                {
                    Array* bounds = static_cast<Array*>((*boxNode)[0]);
                    result << "\t\tbounds = [ ";
                    for (int k = 0; k < bounds->getSize(); ++k)
                        result << static_cast<Primitive*>((*bounds)[k])->data.fp32 << " ";
                    result << "]" << std::endl;

                    Array* childrenIndices = static_cast<Array*>((*boxNode)[1]);
                    result << "\t\tchildrenIndices = [ ";
                    for (int k = 0; k < childrenIndices->getSize(); ++k)
                        result << static_cast<Primitive*>((*childrenIndices)[k])->data.u32 << " ";
                    result << "]" << std::endl;
                }

                result << "\t}" << std::endl;
            }

            offset += numBoxNodes;
            for (int j = 0; j < numInstanceNodes; ++j) {
                result << "\tinstance" << j << " = {" << std::endl; 

                Struct* instanceNode = static_cast<Struct*>((*accelerationStructure)[j + offset]);
                {
                    result << "\t\ttransformationMatrix = [" << std::endl;
                    Array* transformationMatrixInstance = static_cast<Array*>((*instanceNode)[0]);
                    for (int k = 0; k < transformationMatrixInstance->getSize(); ++k) {
                        result << "\t\t\t[ ";
                        Array* rowInstance = static_cast<Array*>((*transformationMatrixInstance)[k]);
                        for (int l = 0; l < rowInstance->getSize(); ++l) {
                            float value = static_cast<Primitive*>((*rowInstance)[l])->data.fp32;
                            result << value << " ";
                        }
                        result << "]" << std::endl;
                    }
                    result << "\t\t]" << std::endl;

                    unsigned indexInstance = static_cast<Primitive*>((*instanceNode)[1])->data.u32;
                    result << "\t\tindex = " << indexInstance << std::endl;
                }

                result << "\t}" << std::endl;
            }

            offset += numInstanceNodes;
            for (int j = 0; j < numPrimitiveNodes; ++j) {
                result << "\tprimitive" << j << " = {" << std::endl; 

                Struct* primitiveNode = static_cast<Struct*>((*accelerationStructure)[j + offset]);
                {
                    Array* vertices = static_cast<Array*>((*primitiveNode)[0]);
                    result << "\t\tvertices = [ ";
                    for (int k = 0; k < vertices->getSize(); ++k) {
                        Array* vertex = static_cast<Array*>((*vertices)[k]);
                        result << "[ ";
                        for (int l = 0; l < vertex->getSize(); ++l)
                            result << static_cast<Primitive*>((*vertex)[l])->data.fp32 << " ";
                        result << "] ";
                    }
                    result << "]" << std::endl;

                    Array* indices = static_cast<Array*>((*primitiveNode)[1]);
                    result << "\t\tindices = [ ";
                    for (int k = 0; k < indices->getSize(); ++k)
                        result << static_cast<Primitive*>((*indices)[k])->data.u32 << " ";
                    result << "]" << std::endl;
                }

                result << "\t}" << std::endl;
            }

            result << "}" << std::endl;
        }

        return result.str();
    }

    /// @brief Gives the fields and field names of the structure for acceleration structures.
    /// @param data Number of nodes in each acceleration structure.
    /// @return Field names as strings and fields as types.
    static std::tuple<std::vector<std::string>, std::vector<const Type*>> getStructureFormat(
            std::vector<std::array<unsigned, 3>>* data = nullptr) {
        // TODO: memory management?
        using Names = std::vector<std::string>;
        using Fields = std::vector<const Type*>;

        Names names { "rootIndex", "accelerationStructuresInfo" };
        Fields fields;

        // Note: "--- <field name>" means which field we are populating with the code below it
        // --- rootIndex
        fields.push_back(new Type(Type::primitive(DataType::UINT)));

        // --- accelerationStructuresInfo
        const Names accelerationStructuresInfoName {"numBoxNodes", "numInstanceNodes", "numPrimitiveNodes"};
        Fields accelerationStructuresInfoFields;
        {
            // --- numBoxNodes
            accelerationStructuresInfoFields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numInstanceNodes
            accelerationStructuresInfoFields.push_back(new Type(Type::primitive(DataType::UINT)));

            // --- numPrimitiveNodes
            accelerationStructuresInfoFields.push_back(new Type(Type::primitive(DataType::UINT)));
        }
        Type* accelerationStructuresInfoStruct =
                new Type(Type::structure(accelerationStructuresInfoFields, accelerationStructuresInfoName));
        Type* accelerationStructuresInfoArray = new Type(Type::array(0, *accelerationStructuresInfoStruct));
        fields.push_back(accelerationStructuresInfoArray);

        // Add the acceleration structures if we know how many there are
        if (data != nullptr) {

            // Add on the acceleration structures
            for (int i = 0; i < data->size(); ++i) {
                const unsigned numBoxNodes = (*data)[i][0];
                const unsigned numInstanceNodes = (*data)[i][1];
                const unsigned numPrimitiveNodes = (*data)[i][2];

                // --- accelerationStructure
                Names accelerationStructureFieldNames {"isTLAS", "splitValue", "transformationMatrix", "rootNodeIndex"};
                Fields accelerationStructureFields;
                {
                    // --- isTLAS
                    accelerationStructureFields.push_back(new Type(Type::primitive(DataType::BOOL)));

                    // --- splitValue
                    accelerationStructureFields.push_back(new Type(Type::primitive(DataType::UINT)));

                    // --- transformationMatrix
                    accelerationStructureFields.push_back(new Type(Type::primitive(DataType::UINT)));

                    // --- rootNode
                    accelerationStructureFields.push_back(new Type(Type::primitive(DataType::UINT)));

                    // --- boxNodes
                    if (numBoxNodes > 0) {
                        const Names boxNodeFieldNames {"bounds", "childrenIndices"};
                        Fields boxNodeFields;
                        {
                            // --- bounds
                            Type* bounds = new Type(Type::primitive(DataType::FLOAT));
                            boxNodeFields.push_back(new Type(Type::array(6, *bounds)));

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
                        Names instanceNodeFieldNames {"transformationMatrix", "index"};
                        Fields instanceNodeFields;
                        {
                            // --- transformationMatrix
                            unsigned numRows = 3;
                            unsigned numCols = 4;
                            Type* floatValue = new Type(Type::primitive(DataType::FLOAT));
                            Type* rows = new Type(Type::array(numCols, *floatValue));
                            Type* matrix = new Type(Type::array(numRows, *rows));
                            instanceNodeFields.push_back(matrix);

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

                    // --- primitiveNodes
                    if (numPrimitiveNodes > 0) {
                        Names primitiveNodeFieldNames {"vertices", "indices"};
                        Fields primitiveNodeFields;
                        {
                            // --- vertices
                            Type* floatValue = new Type(Type::primitive(DataType::FLOAT));
                            Type* vertex = new Type(Type::array(0, *floatValue));
                            primitiveNodeFields.push_back(new Type(Type::array(0, *vertex)));

                            // --- indices
                            Type* indices = new Type(Type::primitive(DataType::UINT));
                            primitiveNodeFields.push_back(new Type(Type::array(0, *indices)));
                        }
                        Type* primitiveNode = new Type(Type::structure(primitiveNodeFields, primitiveNodeFieldNames));

                        for (int j = 0; j < numPrimitiveNodes; ++j) {
                            std::stringstream primitiveNodeName;
                            primitiveNodeName << "primitive" << j;
                            accelerationStructureFieldNames.push_back(primitiveNodeName.str());
                            accelerationStructureFields.push_back(primitiveNode);
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

private: // TODO: add some helper methods to navigate the struct?
};
