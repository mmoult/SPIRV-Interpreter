/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint> // for uint32_t and int32_t
#include <sstream>
#include <stdexcept>

#include "type.hpp"
#include "value.hpp"
export module value.primitive;
import util;

export struct Primitive : public Value {

    union {
        float fp32;
        uint32_t u32;
        int32_t i32;
        bool b32;
    } data;

public:
    Primitive(float fp32, unsigned size = 32): Value(Type::primitive(DataType::FLOAT, size)) {
        data.fp32 = fp32;
    }
    Primitive(uint32_t u32, unsigned size = 32): Value(Type::primitive(DataType::UINT, size)) {
        data.u32 = u32;
    }
    Primitive(int32_t i32, unsigned size = 32): Value(Type::primitive(DataType::INT, size)) {
        data.i32 = i32;
    }
    Primitive(bool b32): Value(Type::primitive(DataType::BOOL)) {
        data.b32 = b32;
    }
    // Create a blank primitive from the given value
    Primitive(Type t): Value(t) {
        data.u32 = 0;
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Verify that the other is a primitive type
        // (Don't use the super check since we don't require the same base)
        const auto from_base = new_val.getType().getBase();
        switch (from_base) {
        case DataType::FLOAT:
        case DataType::UINT:
        case DataType::INT:
        case DataType::BOOL:
            break;
        default:
            throw std::runtime_error("Cannot copy from non-primitive to a primitive type!");
        }

        // Do the actual copy now
        const Primitive& other = static_cast<const Primitive&>(new_val);
        const Type& from = other.getType();

        // TODO precision handling
        switch (type.getBase()) { // cast to
        case DataType::FLOAT:
            switch (from_base) { // copy from
            case DataType::FLOAT:
                data.fp32 = other.data.fp32;
                break;
            case DataType::UINT:
                data.fp32 = static_cast<float>(other.data.u32);
                break;
            case DataType::INT:
                data.fp32 = static_cast<float>(other.data.i32);
                break;
            default:
                throw std::runtime_error("Cannot convert to float!");
            }
            break;
        case DataType::UINT:
            switch (from_base) {
            case DataType::UINT:
                data.u32 = other.data.u32;
                break;
            default:
                // No int -> uint since if it was int, it is probably negative
                // No float -> uint since if it was float, probably had decimal component
                throw std::runtime_error("Cannot convert to uint!");
            }
            break;
        case DataType::INT:
            switch (from_base) {
            case DataType::UINT:
                // TODO verify that it is not too large
                data.i32 = static_cast<int32_t>(other.data.u32);
                break;
            case DataType::INT:
                data.i32 = other.data.i32;
                break;
            default:
                throw std::runtime_error("Cannot convert to int!");
            }
            break;
        case DataType::BOOL:
            switch (from_base) {
            case DataType::BOOL:
                data.b32 = other.data.b32;
            case DataType::UINT:
                data.b32 = other.data.u32 != 0;
                break;
            default:
                throw std::runtime_error("Cannot convert to bool!");
            }
            break;
        default:
            assert(false);
        }
    }

    /// @brief changes the type of the primitive *without* changing the value
    void cast(Type t) {
        type = t;
    }

    virtual void print(std::stringstream& dst, unsigned indents = 0) const override {
        switch (type.getBase()) { // copy from
        case DataType::FLOAT:
            dst << data.fp32;
            break;
        case DataType::UINT:
            dst << data.u32;
            break;
        case DataType::INT:
            dst << data.i32;
            break;
        case DataType::BOOL:
            if (data.i32)
                dst << "true";
            else
                dst << "false";
            break;
        default:
            assert(false); // should not be possible to have another type!
        }
    }

    bool isNested() const override {
        return false;
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        const auto& other = static_cast<const Primitive&>(val);
        switch (type.getBase()) {
        case FLOAT:
            return Util::eq_float(data.fp32, other.data.fp32, 6);
        case UINT:
            return data.u32 == other.data.u32;
        case INT:
            return data.i32 == other.data.i32;
        case BOOL:
            return data.b32 == other.data.b32;
        case VOID:
            return true; // I don't know why this would happen, but just in case...
        default:
            assert(false);
            return false;
        }
    }
};
