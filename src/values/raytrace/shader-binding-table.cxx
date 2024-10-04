/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stack>
#include <string>
#include <tuple>
#include <vector>

#include "../../external/spirv.hpp"
#include "../type.hpp"
#include "../value.hpp"
export module value.raytrace.shaderBindingTable;
import value.aggregate;
import value.statics;
import value.string;

export struct ShaderRecord {
    inline static const std::vector<std::string> names{"shader", "input"};

    std::string shaderSource;
    std::string extraInput;

    ShaderRecord(): shaderSource(""), extraInput("") {}
    ShaderRecord(std::string src, std::string input = "") : shaderSource(src), extraInput(input) {}

    void copyFrom(const Value* other) {
        const Struct& str = Statics::extractStruct(other, "ShaderRecord", names);
        shaderSource = Statics::extractString(str[0], "shader");
        extraInput = Statics::extractString(str[1], "input");
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<Value*> elements{new String(shaderSource), new String(extraInput)};
        return new Struct(elements, names);
    }
};

export struct HitGroupRecord {
    inline static const std::vector<std::string> names{"any", "closest", "intersection"};

    ShaderRecord any;
    ShaderRecord closest;
    ShaderRecord intersection;

    void copyFrom(const Value* other) {
        const Struct& str = Statics::extractStruct(other, "ShaderRecord", names);
        any.copyFrom(str[0]);
        closest.copyFrom(str[1]);
        intersection.copyFrom(str[2]);
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<Value*> fields{any.toStruct(),closest.toStruct(), intersection.toStruct()};
        return new Struct(fields, names);
    }
};

export class ShaderBindingTable {
private:
    std::vector<ShaderRecord> miss;
    std::vector<HitGroupRecord> hit;
    std::vector<ShaderRecord> callable;

    // Internal type data to be used later
    inline static const std::vector<std::string> names{"miss_records", "hit_group_records", "callable_records"};
    inline static const Type stringType = Type::string();
    inline static Type shaderRecordType;
    inline static Type hitGroupType;

public:
    ShaderBindingTable() {
        // Will create the types needed for later toStruct calls
        if (shaderRecordType.getBase() != DataType::STRUCT) {
            const std::vector<const Type*> rec_sub{&stringType, &stringType};
            const std::vector<std::string> rec_name{"shader", "input"};
            shaderRecordType = Type::structure(rec_sub, rec_name);
            const std::vector<const Type*> hit_sub{&shaderRecordType, &shaderRecordType, &shaderRecordType};
            const std::vector<std::string> hit_name{"any", "closest", "intersection"};
            hitGroupType = Type::structure(hit_sub, hit_name);
        }
    }

    const std::vector<ShaderRecord>& getMissRecords() const {
        return miss;
    }
    const std::vector<HitGroupRecord>& getHitRecords() const {
        return hit;
    }
    const std::vector<ShaderRecord>& getCallableRecords() const {
        return callable;
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<Value*> fields(3, nullptr);

        if (miss.empty()) {
            fields[0] = new Array(shaderRecordType, 0);
        } else {
            std::vector<Value*> records;
            for (const auto& record: miss)
                records.push_back(record.toStruct());
            fields[0] = new Array(records);
        }

        if (hit.empty()) {
            fields[1] = new Array(hitGroupType, 0);
        } else {
            std::vector<Value*> records;
            for (const auto& grp: hit)
                records.push_back(grp.toStruct());
            fields[1] = new Array(records);
        }

        if (callable.empty()) {
            fields[2] = new Array(shaderRecordType, 0);
        } else {
            std::vector<Value*> records;
            for (const auto& record: callable)
                records.push_back(record.toStruct());
            fields[2] = new Array(records);
        }

        return new Struct(fields, names);
    }

    void copyFrom(const Value* other) {
        const Struct& str = Statics::extractStruct(other, "shader binding table", names);
        // All three fields of the shader binding table are arrays

        const Array& miss_r = Statics::extractArray(str[0], names[0]);
        miss.resize(miss_r.getSize());
        for (unsigned i = 0; i < miss_r.getSize(); ++i)
            miss[i].copyFrom(miss_r[i]);

        const Array& hit_r = Statics::extractArray(str[1], names[1]);
        hit.resize(hit_r.getSize());
        for (unsigned i = 0; i < hit_r.getSize(); ++i)
            hit[i].copyFrom(hit_r[i]);

        const Array& call_r = Statics::extractArray(str[2], names[2]);
        callable.resize(call_r.getSize());
        for (unsigned i = 0; i < call_r.getSize(); ++i)
            callable[i].copyFrom(call_r[i]);
    }

    bool isEmpty() const {
        return miss.empty() && hit.empty() && callable.empty();
    }
};
