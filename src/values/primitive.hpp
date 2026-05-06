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
#include <stdexcept>

#include "../util/compare.hpp"
#include "../util/fpconvert.hpp"
#include "type.hpp"
#include "value.hpp"

struct Primitive final : public Value {

    // NOTE: A very important point is that all values are stored at the largest possible precision and saved after
    // necessary rounding/truncation is applied. This greatly simplifies computation logic and ensures a more accurate
    // result (which is required by the spec).
    union {
        bool b;
        double f;
        uint64_t u;
        int64_t i;
        uint64_t all;
    } data;

    using enum DataType;

    // Gets the raw bits of the primitive by the type. In other words, go from emulation bits to compressed exact bits.
    uint64_t getRaw() const;

public:
    Primitive(double fp64, unsigned size = 64) : Value(Type::primitive(FLOAT, size)) {
        data.f = static_cast<double>(fp64);
        assert(size <= 64);
    }
    Primitive(uint64_t u64, unsigned size = 64) : Value(Type::primitive(UINT, size)) {
        data.u = static_cast<uint64_t>(u64);
        assert(size <= 64);
    }
    Primitive(uint32_t u32) : Value(Type::primitive(UINT, 32)) {
        data.u = static_cast<uint64_t>(u32);
    }
    Primitive(int64_t i64, unsigned size = 64) : Value(Type::primitive(INT, size)) {
        data.i = static_cast<int64_t>(i64);
        assert(size <= 64);
    }
    Primitive(int32_t i32) : Value(Type::primitive(INT, 32)) {
        data.i = static_cast<int64_t>(i32);
    }
    Primitive(bool b32) : Value(Type::primitive(BOOL)) {
        data.b = b32;
    }

    // Create a blank primitive for the given type
    Primitive(const Type& t, bool undef = true) : Value(t) {
        assert(isPrimitive(t.getBase()));

        if (undef) {
            // Initialize to dummy values (instead of 0 to indicate visibility and help user catch errors)
            if (t.getBase() == FLOAT)
                data.f = std::nan("1");
            else {
                uint64_t dummy = 0x7890'ABCD'1234'DEAFULL;
                // Avoid using the highest bit, since it would trigger an assert for being too large to fit in a signed
                // integer of the given bit width.
                data.u = dummy >> (64 - t.getPrecision());
            }
        } else
            // Otherwise, set to "null" value, as described by OpConstantNull.
            data.all = 0;
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

    void copyReinterp(const Value& other) noexcept(false) override;

    /// @brief changes the type of the primitive *without* changing the value
    void cast(const Type& t) {
        type = t;
    }

    bool equals(const Value& val) const override;

    /// @brief Add the unsigned components of this and addend, saving into sum's unsigned value
    /// @return whether the addition overflowed
    bool uAdd(const Primitive* addend, Primitive* sum) const;

    /// @brief Subtract subtrahend from this, saving into difference's unsigned value, preventing underflow if necessary
    /// @return whether the borrow bit was used (ie, this's value < subtrahend's value)
    bool uSub(const Primitive* subtrahend, Primitive* difference) const;

    void uMul(const Primitive* multiplier, Primitive* product_lo, Primitive* product_hi = nullptr) const;
};
#endif
