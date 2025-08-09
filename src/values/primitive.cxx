/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>  // for uint32_t and int32_t
#include <limits>
#include <stdexcept>

#include "type.hpp"
#include "value.hpp"
export module value.primitive;
import front.console;
import util.compare;

export struct Primitive final : public Value {

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
    Primitive(const Type& t) : Value(t) {
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
        switch (type.getBase()) {  // cast to
        case FLOAT:
            switch (from_base) {  // copy from
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

            // modify the current value to match the allowed precision
            if (uint32_t prec = type.getPrecision(); prec != 32) {
                // TODO support lower precision other than FP16
                if (prec <= 16) {
                    // TODO: be mindful of rounding mode!
                    auto trunc_mantissa = [&](uint32_t bits) {
                        // We need to round up if the first digit to be truncated was a 1
                        if (bits > 0 && ((1 << (bits - 1)) & data.u32) != 0) {
                            // Rounding must not go beyond available mantissa bits (23)!
                            for (unsigned i = bits; i < 23; ++i) {
                                uint32_t complement = (1 << i);
                                if ((data.u32 & complement) == 0) {
                                    // Found a 0 digit.
                                    // Set the digit to 1 and clear all digits below.
                                    data.u32 |= complement;
                                    bits = i;
                                    break;
                                }
                            }
                        }

                        // mask out the lower bits as requested
                        data.u32 &= (std::numeric_limits<uint32_t>::max() >> bits) << bits;
                    };

                    // Recall that our FP32 input has 1 sign bit, 8 exponent bits, and 23 mantissa bits:
                    //   S EEEEEEEE MMMMMMMMMMMMMMMMMMMMMMM
                    // We try to convert that to FP16, which has 1 sign bit, 5 exponent bits, and 10 mantissa bits:
                    //   S EEEEE MMMMMMMMMM
                    // Do the conversion in-place, since we emulate the FP16 value with FP32, but we want the FP32's
                    // value to always match what the FP16 would hold.

                    uint32_t exponent = (data.all >> 23) & 0xFF;
                    if ((exponent & 0b10000000) > 0) {
                        // First exponent bit is 1: |Input| >= 2.0 OR nan OR inf

                        // Note, this is a little over 65504. TODO: work on rounding for the edge values
                        if (exponent >= 0b10001111) {
                            // |Input| > FP16_MAX{65504} OR nan OR inf
                            // A nan input should produce a nan output
                            // All other inputs should yield either inf or max, depending on rounding mode.

                            // Check if all exponent bits are set:
                            // 0b0'11111111'00000000000000000000000
                            if (exponent == 0xFF) {
                                // |Input| is nan OR inf
                                // Check if any of mantissa bits are set, which indicates nan
                                if ((data.all & 0x7FFFFF) > 0) {
                                    // Force set a bit within the top 10 bits for obvious nan results
                                    data.all |= 0x400000;
                                }
                                // else: inf in = inf out. we are done
                            } else {
                                // |Input| > FP16_MAX{65504}
                                // TODO: rounding mode may make this max or inf
                                data.fp32 = std::copysign(std::numeric_limits<float>::infinity(), data.fp32);
                            }
                        } else {
                            // 2 <= |Input| <= FP16_MAX{65504}

                            // We have a clever conversion trick- we can chop out the three highest exponent bits (not
                            // including the leading bit) and correct the mantissa to produce an FP16 value.

                            // The exponent bits are ok, now truncate the mantissa
                            trunc_mantissa(13);
                        }
                    } else {
                        // First exponent bit is 0: |Input| < 2.0

                        // If the three high exponent bits (excluding leading) are set and at least one other bit, we
                        // can convert to FP16 by removing them and truncating the mantissa. For example:
                        //   0 01110101 01000011001000000000000
                        //   =>
                        //   0 00101 0100001101

                        if ((exponent & 0b01110000) == 0b01110000 && (exponent & 0b00001111) > 0) {
                            // 2^-14 <= |Input| < 2.0
                            // Truncate the lower mantissa bits
                            trunc_mantissa(13);
                        } else if (exponent <= 0b01100101) {
                            // Too small to be represented: 0 <= |Input| < 2^-24{0.000000059604644775390625}
                            // TODO: rounding mode if not exactly 0
                            data.all = 0;
                        } else {
                            // 0b01100110{102} >= Exponent >= 0b01110000{112}
                            // 2^-24 <= |Input| < 2^-14{0.00006103515625}. Denormal mode required for FP16

                            // There is an interesting property that we can insert a leading one into the previous
                            // mantissa, shift it right some number of bits, and truncate to the upper 10, which yields
                            // the FP16 mantissa we want.
                            //
                            // Example 1)
                            //   0 01110000 01000011101010101000000
                            // The exponent matches the upper bound. Insert a leading 1 and this is our new mantissa:
                            //   101000011101010101000000
                            // In this example, we don't require a shift. Lastly, truncate to the correct size:
                            //   1010000111
                            // And that forms our complete FP16 value:
                            //   0 00000000 1010000111
                            //
                            // Example 2)
                            //   0 01101100 10101010101010101010101
                            // The exponent is 108, which is 4 away from 112. Thus, we will need to shift four times:
                            //   0000110101010101010101010101
                            // And truncate it down to:
                            //   0 00000000 0000110101
                            //
                            // Example 3)
                            //   0 01100110 11011111111111111111111
                            // The exponent is 102, which is 10 away from 112. Thus, we need to shift 10 times:
                            //   0000000000111011111111111111111111
                            // Truncate down to the upper 10, but may want to round up on the next-most digit
                            //   0 00000000 0000000001

                            // 23 original mantissa bits converted into 10 FP16 mantissa bits. Recall the insertion of
                            // the leading bit which knocks off one precision even for the upper bound of 112 exponent.
                            uint32_t mask_off = std::min(113 - exponent, 10u) + 13;
                            trunc_mantissa(mask_off);
                        }
                    }
                }
                if (prec != 16) {
                    std::stringstream err;
                    err << "The interpreter does not yet support float precision " << prec << "!";
                    Console::warn(err.str());
                }
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

            // precision constraints are easy: filter out any disallowed bits
            if (uint32_t prec = type.getPrecision(); prec < 32)
                data.all &= (1 << prec) - 1;
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

    /// @brief Add the unsigned components of this and addend, saving into sum's unsigned value
    /// @return whether the addition overflowed
    bool uAdd(const Primitive* addend, Primitive* sum) const {
        assert(type.getBase() == DataType::UINT);
        assert(addend->getType().getBase() == DataType::UINT);
        uint64_t res = uint64_t(this->data.u32) + uint64_t(addend->data.u32);
        const unsigned need_prec = 64 - static_cast<unsigned>(std::countl_zero(res));
        unsigned res_prec = sum->getType().getPrecision();
        const uint64_t dest_mask = (1ULL << res_prec) - 1;
        sum->data.u32 = res & dest_mask;
        return (need_prec > res_prec);
    }

    /// @brief Subtract subtrahend from this, saving into difference's unsigned value, preventing underflow if necessary
    /// @return whether the borrow bit was used (ie, this's value < subtrahend's value)
    bool uSub(const Primitive* subtrahend, Primitive* difference) const {
        assert(type.getBase() == DataType::UINT);
        assert(subtrahend->getType().getBase() == DataType::UINT);
        uint64_t res = this->data.u32;
        unsigned prec = type.getPrecision();
        assert(prec >= subtrahend->getType().getPrecision());
        if (prec < 64)
            res |= 1 << prec;
        res -= uint64_t(subtrahend->data.u32);
        if (prec < 64) {
            res &= ~(1 << prec);  // return the borrow bit to normal
        } else {
            // We cannot create an artificial borrow bit for 64-bit sizes.
            // However, we can count on automatic rollover. overflow_result + 1 = expected
            if (subtrahend->data.u32 > this->data.u32)
                res++;  // by definition, this cannot overflow
        }
        const uint64_t dest_mask = (1ULL << difference->getType().getPrecision()) - 1;
        difference->data.u32 = res & dest_mask;
        return this->data.u32 < subtrahend->data.u32;
    }

    void uMul(const Primitive* multiplier, Primitive* product_lo, Primitive* product_hi = nullptr) const {
        assert(type.getBase() == DataType::UINT);
        assert(multiplier->getType().getBase() == DataType::UINT);

        // constraint which we should be able to relax later
        assert(
            (type.getPrecision() <= 32) && (type.getPrecision() == multiplier->getType().getPrecision()) &&
            (type.getPrecision() == product_lo->getType().getPrecision()) &&
            ((product_hi == nullptr) || (type.getPrecision() == product_hi->getType().getPrecision()))
        );

        // The product of multiplicand size X and multiplier size Y will *never* exceed size (X+Y)

        uint64_t res = uint64_t(this->data.u32) * uint64_t(multiplier->data.u32);
        unsigned prod_lo_prec = product_lo->getType().getPrecision();
        const uint64_t dest_mask = (1ULL << prod_lo_prec) - 1;
        product_lo->data.u32 = res & dest_mask;
        if (product_hi != nullptr)
            product_hi->data.u32 = res >> prod_lo_prec;
    }
};
