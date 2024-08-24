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

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>

#include "../../external/spirv.hpp"
#include "../type.hpp"
#include "../value.hpp"
#include "shader-binding-table.hpp"

export module value.raytrace.accelManager;
import util.string;
import value.aggregate;
import value.primitive;
import value.raytrace.accelStruct;

export class AccelStructManager : public Value {
private:
    std::shared_ptr<AccelerationStructure> root;
    std::shared_ptr<ShaderBindingTable> shaderBindingTable;
    std::unique_ptr<Struct> structureInfo;

public:
    AccelStructManager(Type t): Value(t) {}

    [[nodiscard]] Struct* toStruct() const {
        const Type accel_struct_type = AccelStructManager::getExpectedType();
        Struct* structure = new Struct(Type::structure(accel_struct_type.getFields(), accel_struct_type.getNames()));
        structure->dummyFill();  // TODO don't do a dummy fill- fill with the actual values of the manager
        return structure;
    }

private:
    /// @brief Copy the type from "new_val".
    /// @param new_val "Value" to copy the type from.
    void copyType(const Value& new_val) {
        assert(
            new_val.getType().getBase() == DataType::ACCEL_STRUCT || new_val.getType().getBase() == DataType::STRUCT
        );

        // "new_val" could be an "Struct" or "AccelStructManager" depending on when it is copied
        const Struct& other = new_val.getType().getBase() == DataType::ACCEL_STRUCT
                                  ? *((static_cast<const AccelStructManager&>(new_val)).structureInfo)
                                  : static_cast<const Struct&>(new_val);

        // Change the current type to match
        type = getExpectedType();

        // Copy "other" into "structureInfo"; the "copyFrom(...)" method will fail if the input does not match
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
        for (int i = num_accel_structs - 1; i >= 0; --i) {
            accel_structs.push_back(std::make_shared<AccelerationStructure>(
                i,
                static_cast<const Struct&>(*(accel_structs_info[i])),
                accel_structs,
                num_accel_structs,
                shaderBindingTable
            ));
        }

        // Set the root acceleration structure
        root = std::move(accel_structs[num_accel_structs - 1]);
        assert(accel_structs[num_accel_structs - 1] == nullptr);
    }

    /// @brief Build the shader binding table.
    void buildShaderBindingTable() {
        assert(structureInfo != nullptr);
        const Struct& structure_info_ref = *structureInfo;
        const Struct& shader_binding_table = static_cast<const Struct&>(*(structure_info_ref[1]));

        // Get the non-optional groups
        const Array& ray_gen_group = static_cast<const Array&>(*(shader_binding_table[0]));
        const Array& miss_group = static_cast<const Array&>(*(shader_binding_table[1]));
        const Array& hit_group = static_cast<const Array&>(*(shader_binding_table[2]));

        const unsigned ray_gen_group_size = ray_gen_group.getSize();
        const unsigned miss_group_size = miss_group.getSize();
        const unsigned hit_group_size = hit_group.getSize();

        // Check if an SBT needs to be used
        if ((ray_gen_group_size == 0) && (miss_group_size == 0) && (hit_group_size == 0))
            return;

        // Throw an error if any of the required groups in the SBT is empty
        if ((ray_gen_group_size == 0) || (miss_group_size == 0) || (hit_group_size == 0)) {
            std::stringstream err;
            err << "Cannot build an unusable shader binding table where the number of entries in the required groups "
                   "(ray generation, miss, hit) are "
                << ray_gen_group_size << ", " << miss_group_size << ", " << hit_group_size << " respectively!";
            throw std::runtime_error(err.str());
        }

        shaderBindingTable = std::make_shared<ShaderBindingTable>(shader_binding_table);
    }

public:
    AccelStructManager& operator=(const AccelStructManager& other) {
        // Copy the type
        copyType(other);

        // Copy the shader binding table (SBT) and make sure all shaders have access to the acceleration structures
        if (other.shaderBindingTable != nullptr) {
            shaderBindingTable = std::make_shared<ShaderBindingTable>(*(other.shaderBindingTable));
            shaderBindingTable->setAccelStructManager(static_cast<void*>(this));
        }

        // Build the acceleration structures
        buildAccelerationStructures();

        return *this;
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Construct the acceleration structures and shader binding table based on the type of "other"
        if (new_val.getType().getBase() == DataType::ACCEL_STRUCT) {
            *this = static_cast<const AccelStructManager&>(new_val);
        } else {
            copyType(new_val);
            buildShaderBindingTable();
            buildAccelerationStructures();
            if (shaderBindingTable != nullptr)
                shaderBindingTable->setAccelStructManager(static_cast<void*>(this));
        }
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
        return root->stepTrace();
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
    void traceRay(
        const unsigned ray_flags,
        const unsigned cull_mask,
        const std::vector<float>& ray_origin,
        const std::vector<float>& ray_direction,
        const float ray_t_min,
        const float ray_t_max,
        const unsigned offset_sbt,
        const unsigned stride_sbt,
        const unsigned miss_index,
        Value* payload
    ) const {
        glm::vec4 ray_origin_glm = glm::make_vec4(ray_origin.data());
        ray_origin_glm.w = 1.0f;
        glm::vec4 ray_direction_glm = glm::make_vec4(ray_direction.data());
        ray_direction_glm.w = 0.0f;

        root->traceRay(
            ray_flags,
            cull_mask,
            ray_origin_glm,
            ray_direction_glm,
            ray_t_min,
            ray_t_max,
            offset_sbt,
            stride_sbt,
            miss_index,
            payload
        );
    }

    /// @brief Check if some hit t is within the ray's interval.
    /// @param hit_t distance from the ray to the intersection.
    /// @return whether hit t is within the ray's interval.
    bool isIntersectionValid(const float hit_t) {
        return root->isIntersectionValid(hit_t);
    }

    /// @brief Invokes the any-hit shader.
    /// @param hit_t distance from the ray to the intersection.
    /// @param hit_kind object kind that was intersected.
    /// @return whether to accept the intersection.
    bool invokeAnyHitShader(const float hit_t, const unsigned hit_kind) {
        return root->invokeAnyHitShader(hit_t, hit_kind);
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

private:  // String helper methods
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
        frontier.push({std::string("Structure for acceleration structures"), structureInfo.get(), 0});

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
                result << repeatedString(num_tabs, tab_string) << name << " = "
                       << getPrimitiveValueAsString(*value) << std::endl;
                break;
            }
            case DataType::STRUCT:
            case DataType::ACCEL_STRUCT: {
                result << repeatedString(num_tabs, tab_string) << name << " {" << std::endl;
                frontier.push({" }", &custom_string, num_tabs});

                const Struct& info = static_cast<const Struct&>(*value);

                // Add the children to the stack
                const std::vector<std::string>& names = info.getType().getNames();
                assert(names.size() == info.getSize());

                for (int i = names.size() - 1; i >= 0; --i) {
                    std::stringstream message;
                    message << names[i];
                    frontier.push({message.str(), info[i], num_tabs + 1});
                }
                break;
            }
            case DataType::ARRAY: {
                result << repeatedString(num_tabs, tab_string) << name;

                const Array& info = static_cast<const Array&>(*value);

                const DataType child_data_type = info.getSize() > 0 ? info[0]->getType().getBase() : DataType::VOID;

                // Add the children to the stack if some kind of structure
                if (child_data_type == DataType::STRUCT || child_data_type == DataType::ARRAY ||
                    child_data_type == DataType::ACCEL_STRUCT) {
                    result << " [" << std::endl;
                    frontier.push({" ]", &custom_string, num_tabs});
                    for (int i = info.getSize() - 1; i >= 0; --i) {
                        frontier.push({array_element_marker, info[i], num_tabs + 1});
                    }
                } else {
                    result << " [ ";
                    if (info.getSize() != 0) {
                        for (unsigned i = 0; i < info.getSize() - 1; ++i)
                            result << getPrimitiveValueAsString(*(info[i])) << ", ";
                        result << getPrimitiveValueAsString(*(info[info.getSize() - 1]));
                    }
                    result << " ]" << std::endl;
                }
                break;
            }
            case DataType::STRING: {
                result << repeatedString(num_tabs, tab_string) << name << std::endl;
                break;
            }
            }
        }

        return result.str();
    }

private:  // Type helper methods
    static Type* makeShaderRecord(const unsigned num_shaders) {
        using Names = std::vector<std::string>;  // Field names
        using Fields = std::vector<const Type*>;  // Fields

        const Type* string_type = new Type(Type::string());

        Names shader_record_names {"inputs", "shaders", "buffer"};
        Fields shader_record_fields;
        {
            // <inputs>
            shader_record_fields.push_back(new Type(Type::array(num_shaders, *string_type)));

            // <shaders>
            shader_record_fields.push_back(new Type(Type::array(num_shaders, *string_type)));

            // <buffer>
            shader_record_fields.push_back(new Type(Type::array(0, *string_type)));
        }
        Type* shader_record_type = new Type(Type::structure(shader_record_fields, shader_record_names));

        return shader_record_type;
    }

public:
    /// @brief Get the type for an acceleration structure manager.
    /// @return acceleration structure type.
    static Type getExpectedType() {
        using Names = std::vector<std::string>;  // Field names
        using Fields = std::vector<const Type*>;  // Fields

        const Type* float_type = new Type(Type::primitive(DataType::FLOAT));
        const Type* bool_type = new Type(Type::primitive(DataType::BOOL));
        const Type* uint_type = new Type(Type::primitive(DataType::UINT));
        const Type* void_type = new Type(Type::primitive(DataType::VOID));

        // TODO: allow user-defined names (names from input), and if not provided to this method, use default names
        Names names {"acceleration_structures", "shader_binding_table"};
        Fields fields;

        // Note: <field> comment defines the input structure field being populated.

        // <acceleration_structures>
        Names acceleration_structure_names {
            "is_tlas",
            "box_nodes",
            "instance_nodes",
            "triangle_nodes",
            "procedural_nodes"
        };
        Fields acceleration_structure_fields;
        {
            // <is_tlas>
            acceleration_structure_fields.push_back(bool_type);

            // <box_nodes>
            Names box_node_names {"min_bounds", "max_bounds", "children_indices"};
            Fields box_node_fields;
            {
                // <min_bounds>
                box_node_fields.push_back(new Type(Type::array(3, *float_type)));

                // <max_bounds>
                box_node_fields.push_back(new Type(Type::array(3, *float_type)));

                // <children_indices>
                const Type* child_index_type = new Type(Type::array(2, *uint_type));
                box_node_fields.push_back(new Type(Type::array(0, *child_index_type)));
            }
            const Type* box_node_type = new Type(Type::structure(box_node_fields, box_node_names));
            acceleration_structure_fields.push_back(new Type(Type::array(0, *box_node_type)));

            // <instance_nodes>
            Names instance_node_names {
                "object_to_world_matrix",
                "id",
                "custom_index",
                "mask",
                "shader_binding_table_record_offset",
                "acceleration_structure_index"
            };
            Fields instance_node_fields;
            {
                // <object_to_world_matrix>
                const unsigned num_rows = 3;
                const unsigned num_cols = 4;
                const Type* row_of_floats_type = new Type(Type::array(num_cols, *float_type));
                const Type* matrix_type = new Type(Type::array(num_rows, *row_of_floats_type));
                instance_node_fields.push_back(matrix_type);

                // <id>
                instance_node_fields.push_back(uint_type);

                // <custom_index>
                instance_node_fields.push_back(uint_type);

                // <mask>
                instance_node_fields.push_back(uint_type);

                // <shader_binding_table_record_offset>
                instance_node_fields.push_back(uint_type);

                // <acceleration_structure_index>
                instance_node_fields.push_back(uint_type);
            }
            const Type* instance_node_type = new Type(Type::structure(instance_node_fields, instance_node_names));
            acceleration_structure_fields.push_back(new Type(Type::array(0, *instance_node_type)));

            // <triangle_nodes>
            Names triangle_node_names {"geometry_index", "primitive_index", "opaque", "vertices", "indices"};
            Fields triangle_node_fields;
            {
                // <geometry_index>
                triangle_node_fields.push_back(uint_type);

                // <primitive_index>
                triangle_node_fields.push_back(uint_type);

                // <opaque>
                triangle_node_fields.push_back(bool_type);

                // <vertices>
                const Type* vertex_type = new Type(Type::array(3, *float_type));
                triangle_node_fields.push_back(new Type(Type::array(0, *vertex_type)));

                // <indices>
                triangle_node_fields.push_back(new Type(Type::array(0, *uint_type)));
            }
            const Type* triangle_node_type = new Type(Type::structure(triangle_node_fields, triangle_node_names));
            acceleration_structure_fields.push_back(new Type(Type::array(0, *triangle_node_type)));

            // <procedural_nodes>
            Names procedural_node_names {"geometry_index", "primitive_index", "opaque", "min_bounds", "max_bounds"};
            Fields procedural_node_fields;
            {
                // <geometry_index>
                procedural_node_fields.push_back(uint_type);

                // <primitive_index>
                procedural_node_fields.push_back(uint_type);

                // <opaque>
                procedural_node_fields.push_back(bool_type);

                // <min_bounds>
                procedural_node_fields.push_back(new Type(Type::array(3, *float_type)));

                // <max_bounds>
                procedural_node_fields.push_back(new Type(Type::array(3, *float_type)));
            }
            const Type* procedural_node_type = new Type(Type::structure(procedural_node_fields, procedural_node_names));
            acceleration_structure_fields.push_back(new Type(Type::array(0, *procedural_node_type)));
        }
        Type* acceleration_structure_type =
            new Type(Type::structure(acceleration_structure_fields, acceleration_structure_names));
        fields.push_back(new Type(Type::array(0, *acceleration_structure_type)));

        // <shader_binding_table>
        Names shader_binding_table_names {
            "ray_gen_shader_records",
            "miss_shader_records",
            "hit_group_shader_records",
            "callable_shader_records"
        };
        Fields shader_binding_table_fields;
        {
            // <ray_gen_shader_records>
            shader_binding_table_fields.push_back(new Type(Type::array(0, *(makeShaderRecord(1)))));

            // <miss_shader_records>
            shader_binding_table_fields.push_back(new Type(Type::array(0, *(makeShaderRecord(1)))));

            // <hit_group_shader_records>
            shader_binding_table_fields.push_back(new Type(Type::array(0, *(makeShaderRecord(3)))));

            // <callable_shader_records>
            shader_binding_table_fields.push_back(new Type(Type::array(0, *(makeShaderRecord(0)))));
        }
        Type* shader_binding_table_type =
            new Type(Type::structure(shader_binding_table_fields, shader_binding_table_names));
        fields.push_back(shader_binding_table_type);

        return Type::accelStruct(fields, names);
    }
};
