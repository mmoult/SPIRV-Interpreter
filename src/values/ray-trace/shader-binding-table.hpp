/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef RAY_TRACE_SHADER_BINDING_TABLE_HPP
#define RAY_TRACE_SHADER_BINDING_TABLE_HPP

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <memory>
#include <tuple>
#include <vector>

#include "../../external/spirv.hpp"
#include "../type.hpp"
#include "../value.hpp"

import format.parse;
import spv.program;
import value.aggregate;

using SBTShaderOutput = std::map<std::string, std::tuple<const Value*, spv::StorageClass>>;

enum class HitGroupType { Closest = 0, Any = 1, Intersection = 2 };
static std::string hitGroupTypeToString(HitGroupType type) {
    switch (type) {
    case HitGroupType::Closest:
        return std::string("Closest");
    case HitGroupType::Any:
        return std::string("Any");
    case HitGroupType::Intersection:
        return std::string("Intersection");
    }
}

class ShaderRecord {
private:
    std::vector<ValueFormat*> formats;
    std::vector<std::string> shaderFilePaths;
    std::vector<std::shared_ptr<Program>> shaders;
    std::vector<std::shared_ptr<Struct>> buffer;

public:
    ShaderRecord(
        const std::vector<ValueFormat*>& formats,
        const std::vector<std::string>& shader_file_paths,
        const std::vector<Program*>& shaders,
        const std::vector<Struct*>& buffer
    );
    ShaderRecord(const ShaderRecord& other) = default;

    SBTShaderOutput execute(ValueMap& inputs, const unsigned shader_index, void* accel_struct_manager) const;
    const Program* getShader(const unsigned shader_index) const;
};

class ShaderBindingTable {
private:
    const unsigned RAY_GEN_INDEX = 0;
    const unsigned MISS_INDEX = 1;
    const unsigned HIT_GROUP_INDEX = 2;
    const unsigned CALLABLE_INDEX = 3;

    // Contains 4 shader record groups in the order:
    // ray generation, miss, hit group, and callable.
    // Each shader record group contains shader records.
    std::array<std::vector<ShaderRecord>, 4> shaderRecordGroups;

    void* accelStructManager = nullptr;

public:
    ShaderBindingTable(const Struct& shader_binding_table);
    ShaderBindingTable(const ShaderBindingTable& other) = default;

    void setAccelStructManager(void* accel_struct_manager);

    SBTShaderOutput executeHit(
        ValueMap& inputs,
        const unsigned sbt_offset,
        const unsigned sbt_stride,
        const int geometry_index,
        const unsigned instance_sbt_offset,
        const HitGroupType type,
        void* extra_data = nullptr
    ) const;

    const Program* getHitShader(
        const unsigned sbt_offset,
        const unsigned sbt_stride,
        const int geometry_index,
        const unsigned instance_sbt_offset,
        const HitGroupType type
    ) const;

    SBTShaderOutput executeMiss(ValueMap& inputs, const unsigned miss_index, void* extra_data = nullptr) const;
    const Program* getMissShader(const unsigned miss_index) const;
};

#endif  // RAY_TRACE_SHADER_BINDING_TABLE_HPP
