/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_PRIMITIVE_HPP
#define VALUES_PRIMITIVE_HPP

#include <cassert>
#include <cmath>  // for nanf
#include <cstdint>  // for uint32_t and int32_t
#include <sstream>
#include <stdexcept>

#include "../front/console.hpp"
#include "../util/compare.hpp"
#include "type.hpp"
#include "value.hpp"

struct Primitive final : public Value {

    union {
        float fp32;
        uint32_t u32;
        int32_t i32;
        bool b32;
        uint32_t all;
    } data;

    using enum DataType;

public:
    Primitive(float fp32, unsigned size = 32) : Value(Type::primitive(FLOAT, size)) {
        data.fp32 = fp32;
        assert(size <= 64);
    }
    Primitive(uint32_t u32, unsigned size = 32) : Value(Type::primitive(UINT, size)) {
        data.u32 = u32;
        assert(size <= 64);
    }
    Primitive(int32_t i32, unsigned size = 32) : Value(Type::primitive(INT, size)) {
        data.i32 = i32;
        assert(size <= 64);
    }
    Primitive(bool b32) : Value(Type::primitive(BOOL)) {
        data.b32 = b32;
    }
    // Create a blank primitive for the given type
    Primitive(const Type& t, bool undef = true) : Value(t) {
        assert(isPrimitive(t.getBase()));

        if (undef) {
            // Initialize to dummy values (instead of 0 to indicate visibility and help user catch errors)
            if (t.getBase() == FLOAT)
                data.fp32 = std::nanf("1");
            else
                // Although undefined values should not appear in outputs, they may be used in intermediate calculations
                // where the result is not used. Assuming 32 and 16 are the two most common precisions, avoid the
                // circumstance where the dummy value triggers an assert for being too large to fit in a signed integer.
                data.u32 = 0x1ABC2DEF;
        } else
            // Otherwise, set to "null" value, as described by OpConstantNull.
            data.u32 = 0;
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

    void copyFrom(const Value& new_val) noexcept(false) override;

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
        if (!Value::equals(val))  // guarantees matching types
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

    // TODO move this to util/fpconvert and standardize
    static uint32_t fpConvertTypeToEmu(uint32_t input, unsigned precision) {
        // literals are given in the precision of the primitive, which means we need to extend bits (since we emulate
        // all precisions in FP32).
        if (precision == 32)
            return input;

        if (precision != 16) {
            std::stringstream err;
            err << "The interpreter does not yet support float precision " << precision << "!";
            Console::warn(err.str());
            return input;
        } else {
            uint32_t sign = ((input >> 15) & 1) << 31;
            uint32_t mantissa = (input & 0b1111111111) << 13;
            uint32_t exponent = (input >> 10) & 0b11111;
            // Exponent is a little tricky, since it acts like a signed int within the fp bitfield
            // * 10001 => 10000001
            // * 01110 => 01111110
            exponent = ((exponent & 0b10000) << 3)  // top bit
                       | (((exponent & 0b01000) > 0) ? 0b01111000 : 0b0)  // sign extension
                       | (exponent & 0b00111);
            return sign | (exponent << 23) | mantissa;
        }
    }

    /// @brief Add the unsigned components of this and addend, saving into sum's unsigned value
    /// @return whether the addition overflowed
    bool uAdd(const Primitive* addend, Primitive* sum) const;

    /// @brief Subtract subtrahend from this, saving into difference's unsigned value, preventing underflow if necessary
    /// @return whether the borrow bit was used (ie, this's value < subtrahend's value)
    bool uSub(const Primitive* subtrahend, Primitive* difference) const;

    void uMul(const Primitive* multiplier, Primitive* product_lo, Primitive* product_hi = nullptr) const;
};
#endif
