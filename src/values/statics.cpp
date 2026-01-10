/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "statics.hpp"

const Array& Statics::extractArray(const Value* field, const std::string& name) {
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

std::vector<float> Statics::extractVec(const Value* field, const std::string& name, unsigned size) {
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

std::vector<uint32_t> Statics::extractUvec(const Value* field, const std::string& name, unsigned size) {
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

std::string Statics::extractString(const Value* field, const std::string& name) {
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

uint32_t Statics::extractUint(const Value* field, const std::string& name) {
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

const Struct&
Statics::extractStruct(const Value* field, const std::string& name, const std::vector<std::string>& fields) {
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
        for (const auto& s : fields) {
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
