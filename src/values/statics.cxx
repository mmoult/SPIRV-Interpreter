/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint>
#include <string>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "type.hpp"
#include "value.hpp"
export module value.statics;
import value.aggregate;
import value.primitive;
import value.string;

export struct Statics {

    inline static const Type uintType = Type::primitive(DataType::UINT);
    inline static const Type boolType = Type::primitive(DataType::BOOL);
    inline static const Type fp32Type = Type::primitive(DataType::FLOAT);
    inline static Type uvec2Type;
    inline static Type vec3Type;
    inline static Type vec4Type;

    void init() {
        if (uvec2Type.getBase() == DataType::VOID) {
            uvec2Type = Type::array(2, uintType);
            vec3Type = Type::array(3, fp32Type);
            vec4Type = Type::array(4, fp32Type);
        }
    }

    static const Array& extractArray(const Value* field, const std::string& name) {
        if (field == nullptr) {
            std::stringstream err;
            err << "Cannot extract vec from \"" << name << "\" because it is null!";
            throw std::runtime_error(err.str());
        }
        const Type& type = field->getType();
        if (type.getBase() != DataType::ARRAY) {
            std::stringstream err;
            err << "Cannot extract vec from \"" << name << "\" because it is not an array!";
            throw std::runtime_error(err.str());
        }
        return static_cast<const Array&>(*field);
    }

    static std::vector<float> extractVec(const Value* field, const std::string& name, unsigned size) {
        const Array& arr = extractArray(field, name);
        if (arr.getType().getElement().getBase() != DataType::FLOAT) {
            std::stringstream err;
            err << "Cannot extract vec" << size << " from \"" << name << "\" because array element is not a float!";
            throw std::runtime_error(err.str());
        }
        if (unsigned got_size = arr.getSize(); got_size != size) {
            std::stringstream err;
            err << "Cannot extract vec" << size << " from \"" << name;
            err << "\" because the array has an incorrect size (" << got_size << ")!";
            throw std::runtime_error(err.str());
        }
        std::vector<float> output(size);
        for (unsigned i = 0; i < size; ++i)
            output[i] = static_cast<const Primitive*>(arr[i])->data.fp32;
        return output;
    }

    static std::vector<uint32_t> extractUvec(const Value* field, const std::string& name, unsigned size) {
        const Array& arr = extractArray(field, name);
        if (arr.getType().getElement().getBase() != DataType::UINT) {
            std::stringstream err;
            err << "Cannot extract uvec" << size << " from \"" << name << "\" because array element is not uint!";
            throw std::runtime_error(err.str());
        }
        if (unsigned got_size = arr.getSize(); got_size != size) {
            std::stringstream err;
            err << "Cannot extract uvec" << size << " from \"" << name;
            err << "\" because the array has an incorrect size (" << got_size << ")!";
            throw std::runtime_error(err.str());
        }
        std::vector<uint32_t> output(size);
        for (unsigned i = 0; i < size; ++i)
            output[i] = static_cast<const Primitive*>(arr[i])->data.u32;
        return output;
    }

    static std::string extractString(const Value* field, const std::string& name) {
        if (field == nullptr) {
            std::stringstream err;
            err << "Cannot extract string from \"" << name << "\" because it is null!";
            throw std::runtime_error(err.str());
        }
        if (field->getType().getBase() != DataType::STRING) {
            std::stringstream err;
            err << "Cannot extract string from non-string \"" << name << "\"!";
        }
        return static_cast<const String&>(*field).get();
    }

    static uint32_t extractUint(const Value* field, const std::string& name) {
        if (field == nullptr) {
            std::stringstream err;
            err << "Cannot extract uint from \"" << name << "\" because it is null!";
            throw std::runtime_error(err.str());
        }
        if (field->getType().getBase() != DataType::UINT) {
            std::stringstream err;
            err << "Cannot extract uint from non-uint \"" << name << "\"!";
        }
        return static_cast<const Primitive&>(*field).data.u32;
    }

    static const Struct& extractStruct(
        const Value* field,
        const std::string& name,
        const std::vector<std::string>& fields
    ) {
        if (field == nullptr) {
            std::stringstream err;
            err << "Cannot extract \"" << name << "\" from a null value!";
            throw std::runtime_error(err.str());
        }
        const Type& type = field->getType();
        if (type.getBase() != DataType::STRUCT) {
            std::stringstream err;
            err << "Cannot extract \"" << name << "\" from a non-struct value!";
            throw std::runtime_error(err.str());
        }
        const std::vector<std::string>& names = type.getNames();
        unsigned num_exp_fields = fields.size();
        unsigned num_got_fields = names.size();
        if (num_got_fields > num_exp_fields) {
            std::stringstream err;
            err << "Cannot extract struct from a value with too many fields! Expected: ";
            bool first = true;
            for (const auto& s: fields) {
                if (first)
                    first = false;
                else
                    err << ", ";

                err << "\"" << s << "\"";
            }
            throw std::runtime_error(err.str());
        }
        for (unsigned i = 0; i < num_exp_fields; ++i) {
            if (i >= num_got_fields || names[i] != fields[i]) {
                std::stringstream err;
                err << "Cannot extract struct from a value which is missing field #" << (i + 1);
                err << ": \"" << fields[i] << "\"!";
                throw std::runtime_error(err.str());
            }
        }
        return static_cast<const Struct&>(*field);
    }

};
