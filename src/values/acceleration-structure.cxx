/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <array>
#include <memory>
#include <stack>
#include <string>
#include <tuple>

// TODO: plan to remove/change header(s) below
#include <iostream>

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
export class AccelerationStructure {
private:
    // TODO: what should RayPayload be?
    struct RayPayload {
        float distance;
        std::vector<float> color;
    };

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
            const float rayTMax,
            const std::vector<float> rayDirection,
            RayPayload& payload) const {
        throw std::runtime_error("traceRay() in AccelerationStructure in acceleration-structure.cxx not implemented!");
    }

private:
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

        result << tabbedString(tabLevel, "+ accelerationStructure") << id << std::endl;
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
    // TODO: need to change return type
    void rayIntersect(Node* node) const {
        throw std::runtime_error("rayIntersect() in AccelerationStructure in acceleration-structure.cxx not implemented!");
    }

private:
    // TODO: change classes to structs? Since not using private fields.
    class BoxNode : public Node {
    public:
        const std::array<float, 6> bounds;  // [ min x, max x, min y, max y, min z, max z ]
        std::vector<std::unique_ptr<Node>> children; // TODO: look into unique_ptr and see how to deal with construction

        BoxNode(const std::array<float, 6> bounds, std::vector<std::unique_ptr<Node>>& children): bounds(bounds) {
            for (auto& child : children) {
                this->children.push_back(std::move(child));
            }
        };

        ~BoxNode() {};  // TODO

        NodeType type() {
            return NodeType::Box;
        }
    };

    class InstanceNode : public Node {
    public:
        // TODO: some fields involving shaders, transformations, etc.
        std::array<std::array<float, 4>, 3> transformationMatrix;  // 3 x 4 matrix
        std::shared_ptr<AccelerationStructure> accelerationStructure;

        InstanceNode(std::array<std::array<float, 4>, 3> transformationMatrix,
                std::shared_ptr<AccelerationStructure>& accelerationStructure)
            : transformationMatrix(transformationMatrix) {
            this->accelerationStructure = accelerationStructure;
        };

        ~InstanceNode() {}; // TODO

        NodeType type() {
            return NodeType::Instance;
        }
    };

    class PrimitiveNode : public Node {
    public:
        std::vector<std::vector<float>> vertices;
        std::vector<unsigned> indices;

        PrimitiveNode(std::vector<std::vector<float>> vertices, std::vector<unsigned> indices)
            : vertices(vertices),
              indices(indices) {};

        ~PrimitiveNode() {};  // TODO

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
        std::cout << toString() << std::endl;

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
