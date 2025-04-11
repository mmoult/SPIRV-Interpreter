/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <string>
#include <vector>

#include "../type.hpp"
#include "../value.hpp"
export module value.raytrace.shaderBindingTable;
import value.aggregate;
import value.statics;
import value.string;

const std::string& extract_string(const Value* record, const std::string& type) {
    if (record->getType().getBase() != DataType::STRING) {
        std::stringstream err;
        err << "Type of " << type << " record must be a string!";
        throw std::runtime_error(err.str());
    }
    return static_cast<const String*>(record)->get();
}

export struct HitGroupRecord {
    inline static const std::vector<std::string> names {"any", "closest", "intersection"};

    std::string any;
    std::string closest;
    std::string intersection;

    void copyFrom(const Value* other) {
        const Struct& str = Statics::extractStruct(other, "HitGroupRecord", names);
        any = extract_string(str[0], "any hit");
        closest = extract_string(str[1], "closest hit");
        intersection = extract_string(str[2], "intersection hit");
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<Value*> fields {new String(any), new String(closest), new String(intersection)};
        return new Struct(fields, names);
    }
};

export class ShaderBindingTable {
private:
    std::vector<std::string> miss;
    std::vector<HitGroupRecord> hit;
    std::vector<std::string> callable;

    // Internal type data to be used later
    inline static const std::vector<std::string> names {"miss_records", "hit_group_records", "callable_records"};
    inline static const Type stringType = Type::string();
    inline static Type hitGroupType;

public:
    ShaderBindingTable() {
        // Initialize statics: Will create the types needed for later toStruct calls
        if (hitGroupType.getBase() != DataType::STRUCT) {
            const std::vector<const Type*> hit_sub {&stringType, &stringType, &stringType};
            const std::vector<std::string> hit_name {"any", "closest", "intersection"};
            hitGroupType = Type::structure(hit_sub, hit_name);
        }
    }

    const std::vector<std::string>& getMissRecords() const {
        return miss;
    }
    const std::vector<HitGroupRecord>& getHitRecords() const {
        return hit;
    }
    const std::vector<std::string>& getCallableRecords() const {
        return callable;
    }

    [[nodiscard]] Struct* toStruct() const {
        std::vector<Value*> fields(3, nullptr);

        if (miss.empty()) {
            fields[0] = new Array(stringType, 0);
        } else {
            std::vector<Value*> records;
            for (const auto& record : miss)
                records.push_back(new String(record));
            fields[0] = new Array(records);
        }

        if (hit.empty()) {
            fields[1] = new Array(hitGroupType, 0);
        } else {
            std::vector<Value*> records;
            for (const auto& grp : hit)
                records.push_back(grp.toStruct());
            fields[1] = new Array(records);
        }

        if (callable.empty()) {
            fields[2] = new Array(stringType, 0);
        } else {
            std::vector<Value*> records;
            for (const auto& record : callable)
                records.push_back(new String(record));
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
            miss[i] = extract_string(miss_r[i], "miss");

        const Array& hit_r = Statics::extractArray(str[1], names[1]);
        hit.resize(hit_r.getSize());
        for (unsigned i = 0; i < hit_r.getSize(); ++i)
            hit[i].copyFrom(hit_r[i]);

        const Array& call_r = Statics::extractArray(str[2], names[2]);
        callable.resize(call_r.getSize());
        for (unsigned i = 0; i < call_r.getSize(); ++i)
            callable[i] = extract_string(call_r[i], "callable");
    }

    bool isEmpty() const {
        return miss.empty() && hit.empty() && callable.empty();
    }
};
