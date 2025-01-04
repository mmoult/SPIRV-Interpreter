/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cmath>
#include <cstdint> // for uint32_t and int32_t
#include <sstream>
#include <stdexcept>

#include "type.hpp"
#include "value.hpp"
export module value.primitive;
import util.compare;

export struct Primitive : public Value {

    union {
        float fp32;
        uint32_t u32;
        int32_t i32;
        bool b32;
        uint32_t all;
    } data;

    using enum DataType;

public:
    Primitive(float fp32, unsigned size = 32): Value(Type::primitive(FLOAT, size)) {
        data.fp32 = fp32;
    }
    Primitive(uint32_t u32, unsigned size = 32): Value(Type::primitive(UINT, size)) {
        data.u32 = u32;
    }
    Primitive(int32_t i32, unsigned size = 32): Value(Type::primitive(INT, size)) {
        data.i32 = i32;
    }
    Primitive(bool b32): Value(Type::primitive(BOOL)) {
        data.b32 = b32;
    }
    // Create a blank primitive for the given type
    Primitive(const Type& t): Value(t) {
        assert(isPrimitive(t.getBase()));
        // Initialize to dummy values (instead of 0 to indicate visibility and help user catch errors)
        if (t.getBase() == FLOAT)
            data.fp32 = std::nanf("1");
        else
            // Although undefined values should not appear in outputs, they may be used in intermediate calculations
            // where the result is not used. Assuming 32 and 16 are the two most common precisions, avoid the
            // circumstance where the dummy value triggers an assert for being too large to fit in a signed integer.
            data.u32 = 0x1ABC2DEF;
    }

    static bool isPrimitive(DataType base) {
        switch (base) {
        case FLOAT:
        case UINT:
        case INT:
        case BOOL:
            return true;
        default:
            return false;
        }
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Verify that the other is a primitive type
        // (Don't use the super check since we don't require the same base)
        const auto from_base = new_val.getType().getBase();
        if (!isPrimitive(from_base))
            throw std::runtime_error("Cannot copy from non-primitive to a primitive type!");
        const Primitive& other = static_cast<const Primitive&>(new_val);

        // TODO precision handling
        switch (type.getBase()) { // cast to
        case FLOAT:
            switch (from_base) { // copy from
            case FLOAT:
                data.fp32 = other.data.fp32;
                break;
            case UINT:
                data.fp32 = static_cast<float>(other.data.u32);
                break;
            case INT:
                data.fp32 = static_cast<float>(other.data.i32);
                break;
            default:
                throw std::runtime_error("Cannot convert to float!");
            }
            break;
        case UINT:
            switch (from_base) {
            case UINT:
                data.u32 = other.data.u32;
                break;
            default:
                // No int -> uint since if it was int, it is probably negative
                // No float -> uint since if it was float, probably had decimal component
                throw std::runtime_error("Cannot convert to uint!");
            }
            break;
        case INT:
            switch (from_base) {
            case UINT:
                // TODO verify that it is not too large
                data.i32 = static_cast<int32_t>(other.data.u32);
                break;
            case INT:
                data.i32 = other.data.i32;
                break;
            default:
                throw std::runtime_error("Cannot convert to int!");
            }
            break;
        case BOOL:
            switch (from_base) {
            case BOOL:
                data.b32 = other.data.b32;
                break;
            case UINT:
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

    void copyReinterp(const Value& other) noexcept(false) override {
        // We can reinterpret from any other primitive
        const auto from_base = other.getType().getBase();
        if (!isPrimitive(from_base))
            throw std::runtime_error("Cannot copy reinterp from other non-primitive value!");
        data.all = static_cast<const Primitive&>(other).data.all;
        // TODO apply precision modification
    }

    /// @brief changes the type of the primitive *without* changing the value
    void cast(const Type& t) {
        type = t;
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        const auto& other = static_cast<const Primitive&>(val);
        switch (type.getBase()) {
        case FLOAT:
            return Compare::eq_float(data.fp32, other.data.fp32, 6);
        case UINT:
            return data.u32 == other.data.u32;
        case INT:
            return data.i32 == other.data.i32;
        case BOOL:
            return data.b32 == other.data.b32;
        default:
            assert(false);
            return false;
        }
    }
};
