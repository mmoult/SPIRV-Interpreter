/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stack>
#include <tuple>
#include <vector>

#define SPV_ENABLE_UTILITY_CODE 1
#include "../../external/spirv.hpp"
#include "../type.hpp"
#include "../value.hpp"
#include "./shader-binding-table.hpp"

import format.json;
import format.yaml;
import format.parse;
import spv.program;
import value.aggregate;
import value.primitive;
import value.string;
import value.raytrace.accelStruct;

static Json json;
static Yaml yaml;
static const std::vector<std::string> input_file_format_names {".json", ".yaml"};
static const std::vector<ValueFormat*> input_file_formats {&json, &yaml};

// ---------- START: ShaderRecord
ShaderRecord::ShaderRecord(
    const std::vector<ValueFormat*>& formats,
    const std::vector<std::string>& shader_file_paths,
    const std::vector<Program*>& shaders,
    const std::vector<Struct*>& buffer
) {
    this->formats = formats;
    shaderFilePaths = shader_file_paths;
    for (const auto& s : shaders)
        this->shaders.push_back(std::shared_ptr<Program>(s));
    for (const auto& b : buffer)
        this->buffer.push_back(std::shared_ptr<Struct>(b));
};

const Program* ShaderRecord::getShader(const unsigned shader_index) const {
    return (shaders[shader_index]).get();
}

SBTShaderOutput ShaderRecord::execute(ValueMap& inputs, const unsigned shader_index, void* accel_struct_manager) const {
    assert(shader_index < shaders.size());

    // Note: currently, need to create a new program so that if a shader is called 2+ times and nested, the new shader
    // invocation won't override the old shader invocation.

    // Load in the shader.
    std::ifstream shader_file_stream(shaderFilePaths[shader_index], std::ios::binary);
    if (!shader_file_stream.is_open()) {
        std::stringstream err;
        err << "Could not open source file \"" << shaderFilePaths[shader_index] << "\"!" << std::endl;
        throw std::runtime_error(err.str());
    }

    // Get file size.
    shader_file_stream.seekg(0, shader_file_stream.end);
    int length = shader_file_stream.tellg();
    shader_file_stream.seekg(0, shader_file_stream.beg);

    // Allocate memory and read in data as a block.
    char* file_buffer = new char[length];
    shader_file_stream.read(file_buffer, length);
    shader_file_stream.close();

    // Create a program using the file.
    // The signedness of char is implementation defined. Use uint8_t to remove ambiguity.
    Program* shader_ptr = new Program();
    Program& shader = *shader_ptr;
    try {
        shader.parse(std::bit_cast<uint8_t*>(file_buffer), length);
    } catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
    delete[] file_buffer;  // Delete source now that it has been replaced with program.

    shader.init(inputs);

    // Fill in any acceleration structures
    for (const auto& [name, value] : inputs) {
        if (value->getType().getBase() == DataType::ACCEL_STRUCT) {
            Value* accel_struct = value->getType().construct();
            accel_struct->copyFrom(*(static_cast<Value*>(accel_struct_manager)));
            inputs[name] = accel_struct;
        }
    }

    // Fill in any buffers
    for (const auto& [name, storage_class] : shader.getStorageClasses()) {
        if (storage_class == spv::StorageClass::StorageClassShaderRecordBufferKHR) {
            assert(inputs[name]->getType().getBase() == DataType::STRUCT);
            const Struct& input = static_cast<const Struct&>(*(inputs[name]));
            bool found_buffer = false;
            for (const auto& datum : buffer) {
                const Struct& b = static_cast<const Struct&>(*datum);
                if ((input.getSize() != b.getSize()) || (input.getType() != b.getType()))
                    continue;

                // Probably the same so populate the input
                inputs[name] = datum.get();
                found_buffer = true;
                break;
            }
            if (!found_buffer)
                throw std::runtime_error("Did not find corresponding shader record buffer input!");
        }
    }

    // Populate any inputs as necessary before executions (e.g. built-ins)
    shader.checkInputs(inputs, false);
    shader.execute(false, false, *(formats[shader_index]), accel_struct_manager);

    ValueMap outputs = shader.getOutputs();
    auto storage_classes = shader.getStorageClasses();
    auto built_ins = shader.getBuiltIns();

    SBTShaderOutput result;
    for (const auto& [name, value] : outputs) {
        std::tuple e = {value, storage_classes[name]};
        result.emplace(name, e);
    }

    return result;
}
// ---------- END: ShaderRecord

// ---------- START: ShaderBindingTable
ShaderBindingTable::ShaderBindingTable(const Struct& shader_binding_table) {
    std::map<std::string, Program*> shader_map;

    const Array& ray_gen_shader_records = static_cast<const Array&>(*(shader_binding_table[0]));
    const Array& miss_shader_records = static_cast<const Array&>(*(shader_binding_table[1]));
    const Array& hit_group_shader_records = static_cast<const Array&>(*(shader_binding_table[2]));
    const Array& callable_shader_records = static_cast<const Array&>(*(shader_binding_table[3]));

    for (unsigned group_index = 0; group_index < 4; ++group_index) {
        const Array& shader_group_info = static_cast<const Array&>(*(shader_binding_table[group_index]));
        std::vector<ShaderRecord> group;
        for (unsigned record_index = 0; record_index < shader_group_info.getSize(); ++record_index) {
            const Struct& shader_record_info = static_cast<const Struct&>(*(shader_group_info[record_index]));
            const Array& input_info = static_cast<const Array&>(*(shader_record_info[0]));
            const Array& shader_info = static_cast<const Array&>(*(shader_record_info[1]));
            const Array& buffer_info = static_cast<const Array&>(*(shader_record_info[2]));

            if (input_info.getSize() != shader_info.getSize()) {
                std::stringstream err;
                err << "The number of inputs does not match the number of shaders in record " << record_index
                    << " of group " << group_index;
                throw std::runtime_error(err.str());
            }

            std::vector<ValueFormat*> formats;
            std::vector<std::string> shader_file_paths;
            std::vector<Program*> shaders;
            std::vector<Struct*> buffer;

            // Handle the shader file paths.
            for (unsigned j = 0; j < shader_info.getSize(); ++j) {
                assert((shader_info[j])->getType().getBase() == DataType::STRING);
                const std::string shader_file_path = static_cast<const String&>(*(shader_info[j])).get();
                const std::string shader_input_file_path = static_cast<const String&>(*(input_info[j])).get();

                shader_file_paths.push_back(shader_file_path);

                ValueFormat* format = nullptr;
                Program* program = nullptr;
                if (shader_map.contains(shader_file_path)) {
                    // Get the program from the shader map.
                    program = shader_map[shader_file_path];
                } else if (shader_file_path != "") {
                    // Load in the shader.
                    std::ifstream shader_file_stream(shader_file_path, std::ios::binary);
                    if (!shader_file_stream.is_open()) {
                        std::stringstream err;
                        err << "Could not open source file \"" << shader_file_path << "\"!" << std::endl;
                        throw std::runtime_error(err.str());
                    }

                    // Get file size.
                    shader_file_stream.seekg(0, shader_file_stream.end);
                    int length = shader_file_stream.tellg();
                    shader_file_stream.seekg(0, shader_file_stream.beg);

                    // Allocate memory and read in data as a block.
                    char* file_buffer = new char[length];
                    shader_file_stream.read(file_buffer, length);
                    shader_file_stream.close();

                    // Create a program using the file.
                    // The signedness of char is implementation defined. Use uint8_t to remove ambiguity.
                    program = new Program();
                    try {
                        program->parse(std::bit_cast<uint8_t*>(file_buffer), length);
                    } catch (const std::exception& e) {
                        throw std::runtime_error(e.what());
                    }
                    delete[] file_buffer;  // Delete source now that it has been replaced with program.

                    // Load in the respective input.
                    std::ifstream input_file_stream(shader_input_file_path);
                    if (!input_file_stream.is_open()) {
                        std::stringstream err;
                        err << "Could not open input file \"" << shader_input_file_path << "\"!" << std::endl;
                        throw std::runtime_error(err.str());
                    }

                    // Figure out the input file format.
                    std::filesystem::path path = shader_input_file_path;
                    for (unsigned k = 0; k < input_file_format_names.size(); ++k) {
                        if (path.extension() == input_file_format_names[k]) {
                            format = input_file_formats[k];
                            break;
                        }
                    }
                    if (format == nullptr) {
                        std::stringstream err;
                        err << "Could not parse the input file with the extension " << path.extension();
                        throw std::runtime_error(err.str());
                    }

                    // Turn the inputs into a "ValueMap".
                    ValueMap inputs;
                    format->parseFile(inputs, input_file_stream);

                    // Initialize the program and load in the inputs.
                    program->init(inputs);

                    // TODO: Temporary solution where interpreter does not call checkInputs(...) on
                    // a ray generation shader. The problem with doing so is that it causes an
                    // infinite loop, where checkInputs(...) calls setVal(...) which constructs
                    // a shader binding table (here), which then calls checkInputs(...) with
                    // the ray generation shader, and so on.
                    if (group_index != 0) {
                        try {
                            // Allow for unused variables so that built-ins can be populated later.
                            program->checkInputs(inputs, true);
                        } catch (const std::exception& e) {
                            throw std::runtime_error(e.what());
                        }
                    }

                    // Add it to the shader map.
                    shader_map.emplace(shader_file_path, program);
                }

                formats.push_back(format);
                shaders.push_back(program);
            }

            assert(shaders.size() == shader_file_paths.size());

            // Handle the buffer
            for (unsigned j = 0; j < buffer_info.getSize(); ++j) {
                assert((buffer_info[j])->getType().getBase() == DataType::STRING);
                const std::string buffer_file_path = static_cast<const String&>(*(buffer_info[j])).get();

                ValueFormat* format = nullptr;

                // Load in the respective input.
                std::ifstream input_file_stream(buffer_file_path);
                if (!input_file_stream.is_open()) {
                    std::stringstream err;
                    err << "Could not open input file \"" << buffer_file_path << "\"!" << std::endl;
                    throw std::runtime_error(err.str());
                }

                // Figure out the input file format.
                std::filesystem::path path = buffer_file_path;
                for (unsigned k = 0; k < input_file_format_names.size(); ++k) {
                    if (path.extension() == input_file_format_names[k]) {
                        format = input_file_formats[k];
                        break;
                    }
                }
                if (format == nullptr) {
                    std::stringstream err;
                    err << "Could not parse the input file with the extension " << path.extension();
                    throw std::runtime_error(err.str());
                }

                // Turn the inputs into a "ValueMap".
                ValueMap input_value_map;
                format->parseFile(input_value_map, input_file_stream);

                // Put all the inputs into a "Struct" data type.
                std::vector<std::string> names;
                std::vector<const Type*> fields;
                std::vector<const Value*> values;
                for (const auto& [key, value] : input_value_map) {
                    names.push_back(key);
                    fields.push_back(&(value->getType()));
                    values.push_back(value);
                }
                Type* structure_type = new Type(Type::structure(fields, names));
                Struct* structure = static_cast<Struct*>(structure_type->construct(values));
                buffer.push_back(structure);
            }

            // Create and add the shader record.
            group.emplace_back(formats, shader_file_paths, shaders, buffer);
        }
        shaderRecordGroups[group_index] = group;
    }
};

SBTShaderOutput ShaderBindingTable::executeHit(
    ValueMap& inputs,
    const unsigned sbt_offset,
    const unsigned sbt_stride,
    const int geometry_index,
    const unsigned instance_sbt_offset,
    const HitGroupType type,
    void* extra_data
) const {
    if (geometry_index < 0)
        throw std::runtime_error("Encountered a negative geometry index while trying to execute a hit shader");

    const unsigned index = instance_sbt_offset + sbt_offset + (geometry_index * sbt_stride);
    const ShaderRecord& entry = shaderRecordGroups[HIT_GROUP_INDEX][index];
    void* data = extra_data != nullptr ? extra_data : accelStructManager;
    return entry.execute(inputs, static_cast<const unsigned>(type), data);
}

void ShaderBindingTable::setAccelStructManager(void* accel_struct_manager) {
    accelStructManager = accel_struct_manager;
}

const Program* ShaderBindingTable::getHitShader(
    const unsigned sbt_offset,
    const unsigned sbt_stride,
    const int geometry_index,
    const unsigned instance_sbt_offset,
    const HitGroupType type
) const {
    if (geometry_index < 0)
        throw std::runtime_error("Encountered a negative geometry index while trying to get a hit shader.");

    const unsigned index = instance_sbt_offset + sbt_offset + (geometry_index * sbt_stride);
    const ShaderRecord& entry = shaderRecordGroups[HIT_GROUP_INDEX][index];
    const Program* result = entry.getShader(static_cast<const unsigned>(type));
    if (result == nullptr) {
        std::stringstream err;
        err << "Trying to get a hit shader type (" << hitGroupTypeToString(type)
            << ") that was not specified in the input at index " << index << " of the hit groups!" << std::endl;
        throw std::runtime_error(err.str());
    }
    return result;
}

SBTShaderOutput ShaderBindingTable::executeMiss(ValueMap& inputs, const unsigned miss_index, void* extra_data) const {
    const ShaderRecord& entry = shaderRecordGroups[MISS_INDEX][miss_index];
    void* data = extra_data != nullptr ? extra_data : accelStructManager;
    return entry.execute(inputs, 0, data);
}

const Program* ShaderBindingTable::getMissShader(const unsigned miss_index) const {
    const ShaderRecord& entry = shaderRecordGroups[MISS_INDEX][miss_index];
    const Program* result = entry.getShader(0);
    if (result == nullptr) {
        std::stringstream err;
        err << "Trying to get a miss shader that was not specified in the input at index " << miss_index
            << " of the miss group!" << std::endl;
        throw std::runtime_error(err.str());
    }
    return result;
}
// ---------- END: ShaderBindingTable
