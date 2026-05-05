/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "primitive.hpp"

#include <bit>  // countl_zero

#include "../util/fpconvert.hpp"

void Primitive::copyFrom(const Value& new_val) noexcept(false) {
    // Verify that the other is a primitive type
    // (Don't use the super check since we don't require the same base)
    const auto from_base = new_val.getType().getBase();
    if (!isPrimitive(from_base))
        throw std::runtime_error("Cannot copy from non-primitive to a primitive type!");
    const Primitive& other = static_cast<const Primitive&>(new_val);

    auto prec = type.getPrecision();
    // It is UB to shift by the bit width or more.
    assert(prec <= 64);
    uint64_t bitmask = (prec == 64) ? ~0ULL : (1ULL << prec) - 1;

    switch (type.getBase()) {  // cast to
    case FLOAT: {
        switch (from_base) {  // copy from
        case FLOAT:
            data.f = other.data.f;
            break;
        case UINT:
            data.f = static_cast<double>(other.data.u);
            break;
        case INT:
            data.f = static_cast<double>(other.data.i);
            break;
        default:
            throw std::runtime_error("Cannot convert to float!");
        }

        // quantize to the allowed precision.
        data.f = FpConvert::quantize(data.f, prec);
        break;
    }
    case UINT:
        switch (from_base) {
        case UINT:
            data.u = other.data.u;
            break;
        default:
            // No int -> uint since if it was int, it is probably negative
            // No float -> uint since if it was float, probably had decimal component
            throw std::runtime_error("Cannot convert to uint!");
        }

        // precision constraints are easy: filter out any disallowed bits
        data.all &= bitmask;
        break;
    case INT:
        switch (from_base) {
        case UINT:
            // TODO verify that it is not too large
            data.i = static_cast<int64_t>(other.data.u);
            break;
        case INT:
            data.i = other.data.i;
            break;
        default:
            throw std::runtime_error("Cannot convert to int!");
        }

        // copy the sign across all inactive bits
        if (data.i < 0)
            data.all |= ~bitmask;
        else
            data.all &= bitmask;
        break;
    case BOOL:
        switch (from_base) {
        case BOOL:
            data.b = other.data.b;
            break;
        case UINT:
            data.b = other.data.u != 0;
            break;
        default:
            throw std::runtime_error("Cannot convert to bool!");
        }
        break;
    default:
        assert(false);
    }
}

bool Primitive::uAdd(const Primitive* addend, Primitive* sum) const {
    assert(type.getBase() == DataType::UINT);
    assert(addend->getType().getBase() == DataType::UINT);
    uint64_t res = uint64_t(this->data.u) + uint64_t(addend->data.u);
    const unsigned need_prec = 64 - static_cast<unsigned>(std::countl_zero(res));
    unsigned res_prec = sum->getType().getPrecision();
    const uint64_t dest_mask = (1ULL << res_prec) - 1;
    sum->data.u = res & dest_mask;
    return (need_prec > res_prec);
}

bool Primitive::uSub(const Primitive* subtrahend, Primitive* difference) const {
    assert(type.getBase() == DataType::UINT);
    assert(subtrahend->getType().getBase() == DataType::UINT);
    uint64_t res = this->data.u;
    unsigned prec = type.getPrecision();
    assert(prec >= subtrahend->getType().getPrecision());
    if (prec < 64)
        res |= 1 << prec;
    res -= uint64_t(subtrahend->data.u);
    if (prec < 64) {
        res &= ~(1 << prec);  // return the borrow bit to normal
    } else {
        // We cannot create an artificial borrow bit for 64-bit sizes.
        // However, we can count on automatic rollover. overflow_result + 1 = expected
        if (subtrahend->data.u > this->data.u)
            res++;  // by definition, this cannot overflow
    }
    const uint64_t dest_mask = (1ULL << difference->getType().getPrecision()) - 1;
    difference->data.u = res & dest_mask;
    return this->data.u < subtrahend->data.u;
}

void Primitive::uMul(const Primitive* multiplier, Primitive* product_lo, Primitive* product_hi) const {
    assert(type.getBase() == DataType::UINT);
    assert(multiplier->getType().getBase() == DataType::UINT);

    // constraint which we should be able to relax later
    assert(
        (type.getPrecision() <= 32) && (type.getPrecision() == multiplier->getType().getPrecision()) &&
        (type.getPrecision() == product_lo->getType().getPrecision()) &&
        ((product_hi == nullptr) || (type.getPrecision() == product_hi->getType().getPrecision()))
    );

    // The product of multiplicand size X and multiplier size Y will *never* exceed size (X+Y)

    uint64_t res = uint64_t(this->data.u) * uint64_t(multiplier->data.u);
    unsigned prod_lo_prec = product_lo->getType().getPrecision();
    const uint64_t dest_mask = (1ULL << prod_lo_prec) - 1;
    product_lo->data.u = res & dest_mask;
    if (product_hi != nullptr)
        product_hi->data.u = res >> prod_lo_prec;
}