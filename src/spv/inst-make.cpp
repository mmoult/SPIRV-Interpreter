/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "instruction.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>  // for nan
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "../../external/GLSL.std.450.h"
#include "../front/console.hpp"
#include "../spv/ray-flags.hpp"
#include "../util/array-math.hpp"
#include "../util/fpconvert.hpp"
#include "../values/aggregate.hpp"
#include "../values/coop-matrix.hpp"
#include "../values/image.hpp"
#include "../values/pointer.hpp"
#include "../values/primitive.hpp"
#include "../values/raytrace/accel-struct.hpp"
#include "../values/raytrace/ray-query.hpp"
#include "../values/raytrace/trace.hpp"
#include "../values/sampled-img.hpp"
#include "../values/sampler.hpp"
#include "../values/statics.hpp"
#include "../values/string.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
#include "data/data.hpp"
#include "data/manager.hpp"
#include "frame.hpp"
#include "token.hpp"

Value* construct_from_vec(const std::vector<Primitive>& vec, const Type* res_type) {
    Array& res = static_cast<Array&>(*res_type->construct());
    for (size_t i = 0; i < vec.size(); ++i)
        res[i]->copyFrom(vec[i]);
    return &res;
}

const std::vector<unsigned>* find_request(Instruction::DecoQueue* queue, unsigned at) {
    if (queue != nullptr) {
        for (const auto& request : *queue) {
            if (request.toDecorate == at) {
                // should be no more than one request
                return &request.pending;
            }
        }
    }
    return nullptr;
}

void Instruction::applyVarDeco(Instruction::DecoQueue* queue, Variable& var, unsigned result_at) const {
    if (const auto* decorations = find_request(queue, result_at); decorations != nullptr) {
        for (auto location : *decorations) {
            const Instruction& deco = queue->insts[location];
            switch (deco.opcode) {
            case spv::OpName: {  // 5
                assert(deco.operands[1].type == Token::Type::STRING);
                std::string name = std::get<std::string>(deco.operands[1].raw);
                var.setName(name);
                break;
            }
            case spv::OpDecorate: {  // 71
                switch (std::get<uint32_t>(deco.operands[1].raw)) {
                case spv::Decoration::DecorationBuiltIn:
                    var.setBuiltIn(static_cast<spv::BuiltIn>(std::get<uint32_t>(deco.operands[2].raw)));
                    break;
                case spv::Decoration::DecorationNonWritable:
                    var.forbidWrite();
                    break;
                case spv::Decoration::DecorationLocation:
                case spv::Decoration::DecorationBinding:
                    // These two have mutually exclusive use, so we can handle them in the same space
                    var.setBinding(std::get<uint32_t>(deco.operands[2].raw));
                    break;
                case spv::Decoration::DecorationDescriptorSet:
                    var.setDescriptorSet(std::get<uint32_t>(deco.operands[2].raw));
                    break;
                }
                break;
            }
            default:
                break;  // other decorations should not occur
            }
        }
    }
}

void Instruction::selectName(Variable& var) const {
    // Should NOT be called unless var has a value!
    if (!var.getName().empty())
        return;
    // In the event that no / a blank name is provided, attempt to name the variable based on its attributes. Priority
    // of name selection:
    // 1) Using an explicit OpName (set before this is called)
    // 2) After the builtin it follows
    // 3) From the name of its type
    // 4) Stringize the return location
    // It is possible for these names to collide with each other, but we don't check this yet...
    if (auto builtin = var.getBuiltIn(); builtin != spv::BuiltIn::BuiltInMax)
        var.setName(spv::BuiltInToString(builtin));
    else {
        // Use the name of the type (if that type has a custom name)
        const Type& type = var.getVal().getType();
        std::string new_name;
        if (std::string type_name = type.getName(); !type_name.empty())
            new_name = type_name;
        else
            new_name = std::to_string(getResult());
        var.setName(new_name);
    }
}

Value* Instruction::handleImage(
    DataView& data,
    const Value& img,
    const Value* coords,
    unsigned img_qualifier,
    bool proj
) const {
    Type* res_type = getType(0, data);
    Value* to_ret = res_type->construct();
    float lod = 0.0;
    const Image* image;
    if (img.getType().getBase() == DataType::SAMPLED_IMG) {
        const auto& sampler = static_cast<const SampledImage&>(img);
        image = &sampler.getImage();
        lod = sampler.getImplicitLod();
    } else {
        image = static_cast<const Image*>(&img);
    }
    auto [x, y, z, q] = Image::extractCoords(coords, image->getDimensionality(), proj);
    if (proj) {
        if (q == 0.0)
            throw std::runtime_error("Invalid projection value (0.0) in image access!");
        x /= q;
        y /= q;
        z /= q;
    }

    if (img_qualifier < operands.size()) {
        assert(operands[img_qualifier].type == Token::Type::CONST);
        auto descriptors = std::get<unsigned>(operands[img_qualifier].raw);
        unsigned next = img_qualifier;
        auto getNext = [&]() {
            if (++next >= operands.size())
                throw std::runtime_error("Missing necessary operand(s) for image qualifiers!");
            return getValue(next, data);
        };
        // https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#Image_Operands
        for (uint32_t i = 1; (i < spv::ImageOperandsMax) && (descriptors != 0); i <<= 1) {
            if ((descriptors & i) == 0)
                continue;
            descriptors &= ~i;
#define CASE(SHIFT) case 1 << spv::SHIFT
            switch (i) {
                CASE(ImageOperandsBiasShift) : {
                    const Value* bias = getNext();
                    // bias must be a float per the spec
                    assert(bias->getType().getBase() == DataType::FLOAT);
                    lod += static_cast<const Primitive*>(bias)->data.f;
                    break;
                }
                CASE(ImageOperandsConstOffsetShift) : CASE(ImageOperandsOffsetShift) : {
                    const Value* shifts = getNext();
                    // Per the spec, these must be of integer type and match the number of coordinates
                    auto shift_type = shifts->getType().getBase();
                    if (shift_type == DataType::ARRAY) {
                        assert(shifts->getType().getElement().getBase() == DataType::INT);
                        const auto& sh = static_cast<const Array&>(*shifts);
                        for (unsigned j = 0; j < sh.getSize(); ++j) {
                            const auto& shp = static_cast<const Primitive&>(*(sh[j]));
                            switch (j) {
                            case 0:
                                x += shp.data.i;
                                break;
                            case 1:
                                y += shp.data.i;
                                break;
                            case 2:
                                z += shp.data.i;
                                break;
                            default:
                                throw std::runtime_error("Offset coordinate count exceeds components usable!");
                            }
                        }
                    } else {
                        assert(shift_type == DataType::INT);
                        x += static_cast<const Primitive&>(*shifts).data.i;
                    }
                    break;
                }
                CASE(ImageOperandsLodShift) : {
                    const Value* lodv = getNext();
                    const auto& lodp = static_cast<const Primitive&>(*lodv);
                    Primitive prim(0.0);
                    prim.copyFrom(lodp);
                    lod = prim.data.f;
                    break;
                }
                CASE(ImageOperandsMinLodShift) : {
                    const Value* min_lodv = getNext();
                    assert(min_lodv->getType().getBase() == DataType::FLOAT);
                    // spec explicitly requires it to be a floating point scalar
                    lod = std::max(lod, static_cast<float>(static_cast<const Primitive&>(*min_lodv).data.f));
                }
            default:
                throw std::runtime_error("Cannot handle unsupported image qualifier operand!");
            }
#undef CASE
        }
        if (++next < operands.size())
            throw std::runtime_error("Unused image qualifier operands!");
    }

    const Array& arr = *image->read(x, y, z, lod);
    if (arr.getSize() == 1)
        to_ret->copyFrom(*(arr[0]));
    else
        to_ret->copyFrom(arr);
    delete &arr;
    return to_ret;
}

/**
 * Multiplies the two primitives, x and y, of unknown type, although their type must be the same. Returns the value of
 * the same type.
 */
Primitive multiply_same(const Primitive& x, const Primitive& y) {
    switch (x.getType().getBase()) {
    case DataType::FLOAT:
        return Primitive(x.data.f * y.data.f);
    case DataType::UINT:
        return Primitive(x.data.u * y.data.u);
    case DataType::INT:
        return Primitive(x.data.i * y.data.i);
    default:
        throw std::invalid_argument("Can only multiply primitives of type float, uint, or int!");
    }
}

/**
 * Adds the two primitives, storing the result into x.
 */
void accum_same(Primitive& x, const Primitive& y) {
    switch (x.getType().getBase()) {
    case DataType::FLOAT:
        x.data.f += y.data.f;
        break;
    case DataType::UINT:
        x.data.u += y.data.u;
        break;
    case DataType::INT:
        x.data.i += y.data.i;
        break;
    default:
        throw std::invalid_argument("Can only multiply primitives of type float, uint, or int!");
    }
}

Value* composite_extract(Value* composite, unsigned index_start, const std::vector<Token>& operands) {
    // Construct a pointer value and dereference it
    std::vector<unsigned> indices;
    for (unsigned i = index_start; i < operands.size(); ++i) {
        assert(operands[i].type == Token::Type::UINT);
        indices.push_back(std::get<unsigned>(operands[i].raw));
    }
    Pointer ptr(0, indices, Type::forwardPointer());
    return ptr.dereference(*composite);
}

const Type& element_type(const Type& type) {
    if (type.isArray())
        return type.getElement();
    return type;
}
DataType type_base(const Type& type) {
    auto base = type.getBase();
    if (type.isArray())
        base = type.getElement().getBase();
    return base;
}
DataType element_base(const Value& operand) {
    return type_base(operand.getType());
}

struct OpDst {
    unsigned type;
    unsigned at;
};

using BinOp = std::function<Primitive(const Primitive*, const Primitive*)>;

/// @brief Element-wise binary operation between 2 scalars or two arrays of equal length
///
/// @param bin0 The location in data for the first operand
/// @param bin1 The location in data for the second operand
/// @param dst  The destination type and data locations
/// @param data The data to use in fetching values
/// @param op   The binary operation to perform on pairs of primitive elements
/// @param type The expected type of elements or use VOID to disable checking
void element_bin_op(
    unsigned bin0,
    unsigned bin1,
    const OpDst& dst,
    DataView& data,
    BinOp& op,
    DataType type = DataType::VOID
) {
    const Value* src1 = data[bin0].getValue();
    const Value* src2 = data[bin1].getValue();

    // Operate on two primitive arrays or two primitive scalars
    const Type& type1 = src1->getType();
    const Type& type2 = src2->getType();
    assert(
        type == DataType::VOID || ((element_base(*src1) == element_base(*src2)) &&
                                   "Cannot perform element-wise operation on operands of different bases!")
    );

    Value* res = data[dst.type].getType()->construct();

    if (type1.isArray()) {
        assert(type2.isArray());
        const Array& op1 = *static_cast<const Array*>(src1);
        const Array& op2 = *static_cast<const Array*>(src2);
        assert((op1.getSize() == op2.getSize()) && "Cannot do binary operation on arrays of different size!");
        Array& arr = static_cast<Array&>(*res);
        if (arr.getType().getBase() == DataType::COOP_MATRIX)
            arr.dummyFill(true, op1.getSize());

        for (unsigned i = 0; i < op1.getSize(); ++i) {
            auto result = op(static_cast<const Primitive*>(op1[i]), static_cast<const Primitive*>(op2[i]));
            arr[i]->copyFrom(result);
        }
    } else {
        assert(!type2.isArray());
        const Primitive* op1 = static_cast<const Primitive*>(src1);
        const Primitive* op2 = static_cast<const Primitive*>(src2);
        auto result = op(op1, op2);
        res->copyFrom(result);
    }

    data[dst.at].redefine(res);
}

// Sources can be either of the integral types (int or uint)
void element_int_bin_op(
    unsigned bin0,
    unsigned bin1,
    const OpDst& dst,
    DataView& data,
    BinOp& uu_op,
    BinOp& ui_op,
    BinOp& iu_op,
    BinOp& ii_op
) {
    auto is_uint = [&](unsigned loc) {
        auto base = element_base(*data[loc].getValue());
        assert(
            (base == DataType::INT || base == DataType::UINT) &&
            "Cannot perform integer-typed binary operation on non-integer base operands!"
        );
        return base == DataType::UINT;
    };

    bool first = is_uint(bin0);
    bool second = is_uint(bin1);

    element_bin_op(bin0, bin1, dst, data, first ? (second ? uu_op : ui_op) : (second ? iu_op : ii_op));
}

// Sources can be either integral. Result must be *casted* from unsigned result to type which dest specifies.
void element_shift_op(
    unsigned bin0,
    unsigned bin1,
    const OpDst& dst,
    DataView& data,
    std::function<uint64_t(const Primitive*, const Primitive*)>& op
) {
    const Value* src1 = data[bin0].getValue();
    const Value* src2 = data[bin1].getValue();
    const Type& dst_type = *data[dst.type].getType();

    // Operate on two primitive arrays or two primitive scalars
    const Type& tbase = src1->getType();
    auto tb = element_base(*src1);
    assert((tb == DataType::UINT || tb == DataType::INT) && "Cannot perform shift operation on non-integral element!");
    Value* res = dst_type.construct();

    if (tbase.isArray()) {
        const Array& op1 = *static_cast<const Array*>(src1);
        const Array& op2 = *static_cast<const Array*>(src2);
        assert((op1.getSize() == op2.getSize()) && "Cannot do shift operation on arrays of different size!");
        unsigned asize = op1.getSize();
        const auto& dbase = dst_type.getElement();
        Array& arr = static_cast<Array&>(*res);
        if (arr.getType().getBase() == DataType::COOP_MATRIX)
            arr.dummyFill(true, asize);

        for (unsigned i = 0; i < asize; ++i) {
            auto result = op(static_cast<const Primitive*>(op1[i]), static_cast<const Primitive*>(op2[i]));
            Primitive prim(result);
            prim.cast(dbase);
            arr[i]->copyFrom(prim);
        }
    } else {
        const Primitive* op1 = static_cast<const Primitive*>(src1);
        const Primitive* op2 = static_cast<const Primitive*>(src2);
        auto result = op(op1, op2);
        Primitive prim(result);
        prim.cast(dst_type);
        res->copyFrom(prim);
    }

    data[dst.at].redefine(res);
}

using ExtArithOp = std::function<void(const Primitive*, const Primitive*, Primitive*, Primitive*)>;

/// @brief Element-wise binary extended arithmetic operation between 2 scalars or two arrays of equal length
///
/// @param bin0 The location in data for the first operand
/// @param bin1 The location in data for the second operand
/// @param dst  The destination type and data locations
/// @param data The data to use in fetching values
/// @param op   The binary operation to perform on pairs of primitive elements
/// @param type The expected type of elements or use VOID to disable checking
void element_extended_arith_op(
    unsigned bin0,
    unsigned bin1,
    const OpDst& dst,
    DataView& data,
    ExtArithOp& op,
    DataType type = DataType::VOID
) {
    const Value* src1 = data[bin0].getValue();
    const Value* src2 = data[bin1].getValue();

    // Operate on two primitive arrays or two primitive scalars
    const Type& type1 = src1->getType();
    const Type& type2 = src2->getType();
    assert(
        type == DataType::VOID || ((element_base(*src1) == element_base(*src2)) &&
                                   "Cannot perform element-wise operation on operands of different bases!")
    );
    Value* res_v = data[dst.type].getType()->construct();
    Struct& res = static_cast<Struct&>(*res_v);
    assert(res.getSize() == 2);

    if (type1.isArray()) {
        assert(type2.isArray());
        const Array& op1 = *static_cast<const Array*>(src1);
        const Array& op2 = *static_cast<const Array*>(src2);
        unsigned asize = op1.getSize();
        assert((asize == op2.getSize()) && "Cannot do arithmetic operation on arrays of different size!");

        Array& res_lo = static_cast<Array&>(*(res[0]));
        Array& res_hi = static_cast<Array&>(*(res[1]));
        assert(res_lo.getSize() == asize);
        assert(res_hi.getSize() == asize);
        for (unsigned i = 0; i < asize; ++i) {
            op(static_cast<const Primitive*>(op1[i]),
               static_cast<const Primitive*>(op2[i]),
               static_cast<Primitive*>(res_lo[i]),
               static_cast<Primitive*>(res_hi[i]));
        }
    } else {
        assert(!type2.isArray());
        const Primitive* op1 = static_cast<const Primitive*>(src1);
        const Primitive* op2 = static_cast<const Primitive*>(src2);
        op(op1, op2, static_cast<Primitive*>(res[0]), static_cast<Primitive*>(res[1]));
    }

    data[dst.at].redefine(res_v);
}

using UnOp = std::function<Primitive(const Primitive*)>;

void element_unary_op(DataType chtype, unsigned unary, const OpDst& dst, DataView& data, UnOp& op) {
    const Value* src1 = data[unary].getValue();
    assert(element_base(*src1) == chtype && "Cannot do unary operation on other-typed element!");

    const Type& type = *data[dst.type].getType();
    Value* res = type.construct();

    // Operate on a single primitive scalar or array of primitives
    if (type.isArray()) {
        const Array& operand = *static_cast<const Array*>(src1);
        Array& arr = static_cast<Array&>(*res);
        if (arr.getType().getBase() == DataType::COOP_MATRIX)
            arr.dummyFill(true, operand.getSize());

        for (unsigned i = 0; i < operand.getSize(); ++i) {
            auto result = op(static_cast<const Primitive*>(operand[i]));
            arr[i]->copyFrom(result);
        }
    } else {
        const Primitive* operand = static_cast<const Primitive*>(src1);
        auto result = op(operand);
        res->copyFrom(result);
    }

    data[dst.at].redefine(res);
}

// Source can be either of the integral types (int or uint)
void element_int_unary_op(unsigned unary, const OpDst& dst, DataView& data, UnOp& u_op, UnOp& i_op) {
    DataType dt = element_base(*data[unary].getValue());
    assert(
        (dt == DataType::INT || dt == DataType::UINT) &&
        "Cannot perform integer-typed unary operation on non-integer base operand!"
    );

    element_unary_op(dt, unary, dst, data, (dt == DataType::UINT) ? u_op : i_op);
}

// Unary operation that is limited to FP16 or FP32 precision in the spec
void element_prec_unary_op(unsigned unary, const OpDst& dst, DataView& data, std::function<float(float)>& op) {
    const Value* src1 = data[unary].getValue();
    const Type& type = *data[dst.type].getType();

    const Type& el_type = element_type(type);
    DataType btype = el_type.getBase();
    unsigned prec = el_type.getPrecision();
    assert(btype == DataType::FLOAT && "Cannot do unary operation on non-float element!");
    assert(prec == 16 || prec == 32);

    Value* res = type.construct();
    // Operate on a single primitive scalar or array of primitives
    if (type.isArray()) {
        const Array& operand = *static_cast<const Array*>(src1);
        Array& arr = static_cast<Array&>(*res);
        if (arr.getType().getBase() == DataType::COOP_MATRIX)
            arr.dummyFill(true, operand.getSize());

        for (unsigned i = 0; i < operand.getSize(); ++i) {
            const auto* element = static_cast<const Primitive*>(operand[i]);
            Primitive prim(static_cast<double>(op(element->data.f)));
            arr[i]->copyFrom(prim);
        }
    } else {
        const Primitive* operand = static_cast<const Primitive*>(src1);
        Primitive prim(static_cast<double>(op(operand->data.f)));
        res->copyFrom(prim);
    }
    data[dst.at].redefine(res);
}

using TernOp = std::function<Primitive(const Primitive*, const Primitive*, const Primitive*)>;

void element_tern_op(
    DataType type,
    unsigned tern0,
    unsigned tern1,
    unsigned tern2,
    const OpDst& dst,
    DataView& data,
    TernOp& op
) {
    const Value* src1 = data[tern0].getValue();
    const Value* src2 = data[tern1].getValue();
    const Value* src3 = data[tern2].getValue();

    // Operate on two primitive arrays or two primitive scalars
    const Type& type1 = src1->getType();
    const Type& type2 = src2->getType();
    const Type& type3 = src3->getType();
    assert(
        type == DataType::VOID ||
        (element_base(*src1) == element_base(*src2) && element_base(*src2) == element_base(*src3) &&
         "Cannot use operands of different bases!")
    );
    Value* res = data[dst.type].getType()->construct();

    if (type1.isArray()) {
        assert(type2.isArray() && type3.isArray());
        const Array& op1 = *static_cast<const Array*>(src1);
        const Array& op2 = *static_cast<const Array*>(src2);
        const Array& op3 = *static_cast<const Array*>(src3);
        assert(
            (op1.getSize() == op2.getSize()) && (op2.getSize() == op3.getSize()) &&
            "Cannot do binary operation on arrays of different size!"
        );
        Array& arr = static_cast<Array&>(*res);
        if (arr.getType().getBase() == DataType::COOP_MATRIX)
            arr.dummyFill(true, op1.getSize());

        for (unsigned i = 0; i < op1.getSize(); ++i) {
            auto result =
                op(static_cast<const Primitive*>(op1[i]),
                   static_cast<const Primitive*>(op2[i]),
                   static_cast<const Primitive*>(op3[i]));
            arr[i]->copyFrom(result);
        }
    } else {
        assert(!type2.isArray() && !type3.isArray());
        const Primitive* op1 = static_cast<const Primitive*>(src1);
        const Primitive* op2 = static_cast<const Primitive*>(src2);
        const Primitive* op3 = static_cast<const Primitive*>(src3);
        auto result = op(op1, op2, op3);
        res->copyFrom(result);
    }

    data[dst.at].redefine(res);
}

// Typical element-wise binary operation
#define TYPICAL_E_BIN_OP(E_TYPE, BIN_OP) \
    { \
        BinOp fx = [](const Primitive* a, const Primitive* b) { return BIN_OP; }; \
        OpDst dst {checkRef(dst_type_at, data_len), result_at}; \
        element_bin_op(checkRef(src_at, data_len), checkRef(src_at + 1, data_len), dst, data, fx, DataType::E_TYPE); \
        break; \
    }
// Integer (either signedness) element-wise binary operation
// Spec requires a very specific type of edge behavior where: "
//   The resulting value equals the low-order N bits of the correct result R, where N is the component width and R is
//   computed with enough precision to avoid overflow and underflow.
// ".
// For the time being, we are ignoring this stipulation because checking is slow and well-formed programs are typically
// expected not to overflow or underflow.
#define INT_E_BIN_OP(BIN_OP) \
    { \
        BinOp uufx = [](const Primitive* a, const Primitive* b) { return a->data.u BIN_OP b->data.u; }; \
        BinOp uifx = [](const Primitive* a, const Primitive* b) { return a->data.u BIN_OP b->data.u; }; \
        BinOp iufx = [](const Primitive* a, const Primitive* b) { return a->data.u BIN_OP b->data.u; }; \
        BinOp iifx = [](const Primitive* a, const Primitive* b) { return a->data.i BIN_OP b->data.i; }; \
        element_int_bin_op( \
            checkRef(src_at, data_len), \
            checkRef(src_at + 1, data_len), \
            OpDst {checkRef(dst_type_at, data_len), result_at}, \
            data, \
            uufx, \
            uifx, \
            iufx, \
            iifx \
        ); \
        break; \
    }
#define INT_E_UNARY_OP(UNARY_OP) \
    { \
        UnOp ufx = [](const Primitive* a) { return UNARY_OP a->data.u; }; \
        UnOp ifx = [](const Primitive* a) { return UNARY_OP a->data.i; }; \
        element_int_unary_op( \
            checkRef(src_at, data_len), \
            OpDst {checkRef(dst_type_at, data_len), result_at}, \
            data, \
            ufx, \
            ifx \
        ); \
        break; \
    }
// Element shift operation, which may have integral operands
#define E_SHIFT_OP(SHIFT_OP) \
    std::function<uint64_t(const Primitive*, const Primitive*)> op = SHIFT_OP; \
    OpDst dst {checkRef(dst_type_at, data_len), result_at}; \
    element_shift_op(checkRef(src_at, data_len), checkRef(src_at + 1, data_len), dst, data, op);
// Typical unary operation
#define TYPICAL_E_UNARY_OP(E_TYPE, UNARY_OP) \
    { \
        UnOp op = [](const Primitive* a) { return UNARY_OP; }; \
        OpDst dst {checkRef(dst_type_at, data_len), result_at}; \
        element_unary_op(DataType::E_TYPE, checkRef(src_at, data_len), dst, data, op); \
        break; \
    }
#define PRECISION_E_UNARY_OP(UNARY_OP) \
    { \
        std::function<float(float)> op = [](float a) { return UNARY_OP; }; \
        OpDst dst {checkRef(dst_type_at, data_len), result_at}; \
        element_prec_unary_op(checkRef(src_at, data_len), dst, data, op); \
        break; \
    }
#define E_TERN_OP(E_TYPE, OP_LAMBDA) \
    OpDst dst {checkRef(dst_type_at, data_len), result_at}; \
    element_tern_op( \
        DataType::E_TYPE, \
        checkRef(src_at, data_len), \
        checkRef(src_at + 1, data_len), \
        checkRef(src_at + 2, data_len), \
        dst, \
        data, \
        OP_LAMBDA \
    );

bool Instruction::makeResult(DataView& data, unsigned location, Instruction::DecoQueue* queue) const noexcept(false) {
    if (!hasResult && opcode != spv::OpTypeForwardPointer)
        return false;  // no result made!

    // Result type comes before result, if present
    unsigned data_len = data.getBound();
    unsigned result_at = checkRef(hasResultType, data_len);

    constexpr unsigned dst_type_at = 0;
    constexpr unsigned src_at = 2;

    switch (opcode) {
    default: {
        std::stringstream err;
        err << "Cannot make result for unsupported instruction " << spv::OpToString(opcode) << "!";
        throw std::runtime_error(err.str());
    }
    case spv::OpUndef:  // 3
    case spv::OpConstantNull:  // 46
    {
        const Type* ret_type = getType(dst_type_at, data);
        data[result_at].redefine(ret_type->construct(opcode == spv::OpUndef));
        break;
    }
    case spv::OpString:  // 7
        assert(operands[1].type == Token::Type::STRING);
        data[result_at].redefine(new String(std::get<std::string>(operands[1].raw)));
        break;
    case spv::OpExtInstImport: {  // 11
        // Determine which extension the string represents
        assert(operands[1].type == Token::Type::STRING);
        std::string ext_name = std::get<std::string>(operands[1].raw);
        Extension ext = extensionFromString(ext_name);
        if (ext == Extension::INVALID) {
            std::stringstream err;
            err << "Unsupported extension: " << ext_name;
            throw std::runtime_error(err.str());
        }
        data[result_at].redefine(new Primitive(static_cast<unsigned>(ext)));
        break;
    }
    case spv::OpExtInst: {  // 12
        // This is a tricky one because the semantics rely entirely on the extension used
        // First, pull the extension to find where to go next
        Value* val = getValue(src_at, data);
        if (val->getType().getBase() != DataType::UINT)
            throw std::runtime_error("Corrupted extension information!");
        const Primitive& prim = *static_cast<Primitive*>(val);
        Extension ext = static_cast<Extension>(prim.data.u);
        switch (ext) {
        case Extension::GLSL_STD_450:
            return makeResultGlsl(data, location, result_at);
        case Extension::NONSEMANTIC_SHADER_DEBUG_INFO:
        case Extension::NONSEMANTIC_CLSPV_REFLECTION:
            break;  // do nothing presently. Debug info only which could be printed.
        case Extension::NONSEMANTIC_DEBUG_PRINTF: {
            return makeResultPrintf(data, location, result_at);
        }
        default:
            assert(false);
            throw std::runtime_error("Attempt to use `OpExtInst` from unsupported extension!");
        }
        break;
    }
    case spv::OpTypeVoid:  // 19
        data[result_at].redefine(new Type());
        break;
    case spv::OpTypeBool:  // 20
        data[result_at].redefine(new Type(Type::primitive(DataType::BOOL)));
        break;
    case spv::OpTypeInt:  // 21
        assert(operands[1].type == Token::Type::UINT);
        assert(operands[2].type == Token::Type::UINT);
        data[result_at].redefine(new Type(
            Type::primitive(
                std::get<unsigned>(operands[2].raw) == 0 ? DataType::UINT : DataType::INT,
                std::get<unsigned>(operands[1].raw)
            )
        ));
        break;
    case spv::OpTypeFloat:  // 22
        assert(operands[1].type == Token::Type::UINT);
        data[result_at].redefine(new Type(Type::primitive(DataType::FLOAT, std::get<unsigned>(operands[1].raw))));
        break;
    case spv::OpTypeVector:  // 23
    case spv::OpTypeMatrix:  // 24
    {
        // Element type for vectors, Column type for matrices
        // A matrix is an array of columns. This is a little confusing because its "columns" are displayed horizontally
        Type* sub = getType(1, data);
        assert(operands[2].type == Token::Type::UINT);
        data[result_at].redefine(new Type(Type::array(std::get<unsigned>(operands[2].raw), *sub)));
        break;
    }
    case spv::OpTypeImage: {  // 25
        Type* texel_type = getType(1, data);

        assert(operands[7].type == Token::Type::CONST);
        unsigned comps = 0;
        switch (static_cast<spv::ImageFormat>(std::get<unsigned>(operands[7].raw))) {
        case spv::ImageFormatRgba32f:
        case spv::ImageFormatRgba16f:
        case spv::ImageFormatRgba8:
        case spv::ImageFormatRgba8Snorm:
        case spv::ImageFormatRgba16:
        case spv::ImageFormatRgb10A2:
        case spv::ImageFormatRgba16Snorm:
        case spv::ImageFormatRgba32i:
        case spv::ImageFormatRgba16i:
        case spv::ImageFormatRgba8i:
        case spv::ImageFormatRgba32ui:
        case spv::ImageFormatRgba16ui:
        case spv::ImageFormatRgba8ui:
        case spv::ImageFormatRgb10a2ui:
            comps = 1234;
            break;
        case spv::ImageFormatR32f:
        case spv::ImageFormatR16f:
        case spv::ImageFormatR16:
        case spv::ImageFormatR8:
        case spv::ImageFormatR16Snorm:
        case spv::ImageFormatR8Snorm:
        case spv::ImageFormatR32i:
        case spv::ImageFormatR16i:
        case spv::ImageFormatR8i:
        case spv::ImageFormatR32ui:
        case spv::ImageFormatR16ui:
        case spv::ImageFormatR8ui:
        case spv::ImageFormatR64ui:
        case spv::ImageFormatR64i:
            comps = 1000;
            break;
        case spv::ImageFormatRg32f:
        case spv::ImageFormatRg16f:
        case spv::ImageFormatRg16:
        case spv::ImageFormatRg8:
        case spv::ImageFormatRg16Snorm:
        case spv::ImageFormatRg8Snorm:
        case spv::ImageFormatRg32i:
        case spv::ImageFormatRg16i:
        case spv::ImageFormatRg8i:
        case spv::ImageFormatRg32ui:
        case spv::ImageFormatRg16ui:
        case spv::ImageFormatRg8ui:
            comps = 1200;
            break;
        case spv::ImageFormatR11fG11fB10f:
            comps = 1230;
            break;
        case spv::ImageFormatUnknown:
            comps = 0;  // punt the format type to the object copied from
            break;
        default:
            throw std::runtime_error("Cannot handle unsupported image format!");
        }

        assert(operands[2].type == Token::Type::CONST);
        unsigned dim = 0;
        switch (static_cast<spv::Dim>(std::get<unsigned>(operands[2].raw))) {
        case spv::Dim1D:
        case spv::DimBuffer:
            dim = 1;
            break;
        case spv::Dim2D:
        case spv::DimRect:
            dim = 2;
            break;
        case spv::Dim3D:
        case spv::DimCube:
            dim = 3;
            break;
        default:
            throw std::runtime_error("Cannot handle unsupported dimension!");
        }
        data[result_at].redefine(new Type(Type::image(texel_type, dim, comps)));
        break;
    }
    case spv::OpTypeSampler: {  // 26
        data[result_at].redefine(new Type(Type::sampler()));
        break;
    }
    case spv::OpTypeSampledImage: {  // 27
        Type* image = getType(1, data);
        data[result_at].redefine(new Type(Type::sampledImage(image)));
        break;
    }
    case spv::OpTypeArray: {  // 28
        Type* sub = getType(1, data);
        // Unlike OpTypeVector, the length is stored in an OpConstant
        Primitive& len_val = *static_cast<Primitive*>(getValue(src_at, data));
        data[result_at].redefine(new Type(
            // The size must be a positive integer, so we can safely pull from u
            Type::array(len_val.data.u, *sub)
        ));
        break;
    }
    case spv::OpTypeRuntimeArray: {  // 29
        Type* sub = getType(1, data);
        // We use a length of 0 to indicate unknown
        data[result_at].redefine(new Type(Type::array(0, *sub)));
        break;
    }
    case spv::OpTypeStruct: {  // 30
        std::vector<const Type*> fields;
        for (unsigned i = 1; i < operands.size(); ++i) {
            const Type* sub = getType(i, data);
            fields.push_back(sub);
        }
        Type* strct = new Type(Type::structure(fields));
        // Search for any decorations which apply
        if (const auto* decorations = find_request(queue, result_at); decorations != nullptr) {
            for (auto location : *decorations) {
                const Instruction& deco = queue->insts[location];
                switch (deco.opcode) {
                case spv::OpName: {  // 5
                    assert(deco.operands[1].type == Token::Type::STRING);
                    std::string name = std::get<std::string>(deco.operands[1].raw);
                    strct->setName(name);
                    break;
                }
                case spv::OpMemberName: {  // 6
                    assert(deco.operands[1].type == Token::Type::UINT);
                    unsigned idx = std::get<unsigned>(deco.operands[1].raw);
                    assert(deco.operands[2].type == Token::Type::STRING);
                    std::string name = std::get<std::string>(deco.operands[2].raw);
                    strct->nameMember(idx, name);
                    break;
                }
                case spv::OpDecorate: {  // 71
                    assert(deco.operands[1].type == Token::Type::CONST);
                    unsigned idx = std::get<unsigned>(deco.operands[1].raw);
                    if (idx == spv::DecorationBufferBlock)
                        strct->setBufferBlock();
                    break;
                }
                case spv::OpMemberDecorate:  // 72
                    break;  // not currently needed
                default:
                    break;  // other decorations should not occur
                }
            }
        }
        data[result_at].redefine(strct);
        break;
    }
    case spv::OpTypePointer: {  // 32
        Type* pt_to = getType(2, data);
        assert(operands[1].type == Token::Type::CONST);  // storage class we don't need
        // Could be the implementation of a forward reference
        Type* already_result = data[result_at].getType();
        if (already_result == nullptr)
            data[result_at].redefine(new Type(Type::pointer(*pt_to)));
        else
            already_result->unforward(*pt_to);
        break;
    }
    case spv::OpTypeFunction: {  // 33
        // OpTypeFunction %return %params...
        Type* ret = getType(1, data);

        // Now cycle through all parameters
        std::vector<const Type*> params;
        for (unsigned i = 2; i < operands.size(); ++i) {
            const Type* param = getType(i, data);
            params.push_back(param);
        }
        data[result_at].redefine(new Type(Type::function(ret, params)));
        break;
    }
    case spv::OpTypeForwardPointer:  // 39
        data[checkRef(0, data_len)].redefine(new Type(Type::forwardPointer()));
        break;
    case spv::OpConstantTrue:  // 41
    case spv::OpConstantFalse:  // 42
        data[result_at].redefine(new Primitive(opcode == spv::OpConstantTrue));
        break;
    case spv::OpConstant:  // 43
    case spv::OpSpecConstant: {  // 50
        // integer or floating point constant
        Type& type = *getType(dst_type_at, data);
        assert(operands[2].type == Token::Type::UINT);
        uint64_t raw = std::get<unsigned>(operands[2].raw);
        auto prec = type.getPrecision();
        if (prec > 32) {
            assert(operands[3].type == Token::Type::UINT);
            raw |= static_cast<uint64_t>(std::get<unsigned>(operands[3].raw)) << 32;
        }

        Primitive* prim = new Primitive(type, false);
        // Convert from a literal uint to the required emulation type
        switch (type.getBase()) {
        case DataType::INT: {
            // Copy the sign bits
            assert(prec <= 64);
            uint64_t bitmask = (prec == 64) ? ~0ULL : (1ULL << prec) - 1;
            if (raw & (1ULL << (prec - 1)))
                raw |= ~bitmask;
            else
                raw &= bitmask;
            prim->data.all = raw;
            break;
        }
        case DataType::FLOAT:
            switch (prec) {
            case 16:
                prim->data.f = FpConvert::decode_flt16<double>(raw);
                break;
            case 32:
                prim->data.f = static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(raw)));
                break;
            case 64:
                prim->data.f = std::bit_cast<double>(raw);
                break;
            default:
                throw std::runtime_error("Unsupported float precision for literal!");
            }
            break;
        default:
            // No special conversion necessary
            prim->data.all = raw;
            break;
        }
        // We assume the literal is in-bounds for the type, otherwise the SPIR-V would be malformed.

        if (opcode == spv::OpConstant) {
            data[result_at].redefine(prim);
        } else {
            Variable* var = new Variable(prim, spv::StorageClass::StorageClassPushConstant, true);
            applyVarDeco(queue, *var, result_at);
            selectName(*var);
            data[result_at].redefine(var);
        }
        break;
    }
    case spv::OpConstantComposite:  // 44
    case spv::OpSpecConstantComposite:  // 51
    case spv::OpCompositeConstruct:  // 80
    {
        // Can create struct, array/vector, or matrix
        Type* ret = getType(dst_type_at, data);
        std::vector<const Value*> values;
        // operands 2+ are refs to components
        for (unsigned i = 2; i < operands.size(); ++i) {
            Value* val = getValue(i, data);
            values.push_back(val);
        }
        // If the result type is an array, we may need to unpack some values to reach the desired count
        // "If constructing a vector, the total number of components in all the operands must equal the number of
        //  components in Result Type."
        if (ret->getBase() == DataType::ARRAY) {
            if (values.size() < ret->getSize()) {
                std::vector<const Value*> replacement;
                for (const Value* val : values) {
                    if (val->getType().getBase() == DataType::ARRAY) {
                        const auto& arr = static_cast<const Array&>(*val);
                        for (const Value* el : arr)
                            replacement.push_back(el);
                    } else
                        replacement.push_back(val);
                }
                values = replacement;
            }
        }
        Array& arr = static_cast<Array&>(*ret->construct());
        if (ret->getBase() == DataType::COOP_MATRIX)
            arr.dummyFill(true, values.size());
        for (unsigned i = 0; i < values.size(); ++i)
            arr[i]->copyFrom(*values[i]);
        auto set_unsized = [&](Value& seen) {
            if (seen.getType().getBase() == DataType::COOP_MATRIX)
                static_cast<CoopMatrix&>(seen).setUnsized();
            return true;
        };
        arr.recursiveApply(set_unsized);

        if (opcode != spv::OpSpecConstantComposite) {
            data[result_at].redefine(&arr);
        } else {
            Variable* var = new Variable(&arr, spv::StorageClass::StorageClassPushConstant, true);
            applyVarDeco(queue, *var, result_at);
            selectName(*var);
            data[result_at].redefine(var);
        }
        break;
    }
    case spv::OpSpecConstantTrue:  // 48
    case spv::OpSpecConstantFalse:  // 49
    {
        // Specialization constants should be constant at compile time. They may have defaults, but their value does not
        // have to match that. They are constant inputs very similar to OpVariable, so much so that we will treat them
        // as such.
        assert(hasResultType);
        // Note: booleans cannot have non-standard precision
        Primitive* default_val = new Primitive(opcode == spv::OpSpecConstantTrue);
        Variable* var = new Variable(default_val, spv::StorageClass::StorageClassPushConstant, true);
        applyVarDeco(queue, *var, result_at);
        selectName(*var);
        data[result_at].redefine(var);
        break;
    }
    case spv::OpSpecConstantOp: {  // 52
        // This instruction is essentially an opcode delegator since the const operand[2] must be a valid opcode of a
        // statically-resolvable opcode
        // We will get around having to reimplement each instruction by creating a temporary instruction to resolve.
        assert(operands[2].type == Token::Type::CONST);
        spv::Op delegate_op = static_cast<spv::Op>(std::get<unsigned>(operands[2].raw));
        Instruction inst(delegate_op, true, true);
        // Pass in the necessary operands to the instruction
        for (unsigned i = 0; i < operands.size(); ++i) {
            if (i == 2)
                // Skip operand i == 2, which is the delegated opcode
                continue;
            inst.operands.emplace_back(operands[i]);
        }
        inst.makeResult(data, location, queue);
        break;
    }
    case spv::OpFunction: {  // 54
        assert(operands[2].type == Token::Type::CONST);
        Type* fx_type = getType(3, data);
        bool entry = false;
        // Look for any entry point decorations
        std::vector<const Instruction*> decos;
        if (const auto* decorations = find_request(queue, result_at); decorations != nullptr) {
            for (auto location : *decorations) {
                const Instruction& deco = queue->insts[location];
                switch (deco.getOpcode()) {
                case spv::OpEntryPoint:
                case spv::OpExecutionMode:
                case spv::OpExecutionModeId:
                    entry = true;
                    break;
                default:
                    break;
                }
                decos.push_back(&deco);
            }
        }
        Function* fx;
        EntryPoint* ep;
        if (entry) {
            ep = new EntryPoint(fx_type, location);
            fx = ep;
        } else
            fx = new Function(fx_type, location);

        for (const auto* deco : decos) {
            switch (deco->opcode) {
            case spv::OpDecorate:  // 71
                break;  // not currently needed
            case spv::OpName: {  // 5
                assert(deco->operands[1].type == Token::Type::STRING);
                std::string name = std::get<std::string>(deco->operands[1].raw);
                fx->setName(name);
                break;
            }
            case spv::OpExecutionMode: {
                // examples:
                // - OpExecutionMode %main OriginUpperLeft
                // - OpExecutionMode %main LocalSize 8 1 1
                // - OpExecutionMode %main OutputVertices 3
                assert(deco->operands[1].type == Token::Type::CONST);
                switch (static_cast<spv::ExecutionMode>(std::get<uint32_t>(deco->operands[1].raw))) {
                case spv::ExecutionMode::ExecutionModeLocalSize:
                    assert(deco->operands.size() == 5);
                    ep->sizeX = std::get<uint32_t>(deco->operands[2].raw);
                    ep->sizeY = std::get<uint32_t>(deco->operands[3].raw);
                    ep->sizeZ = std::get<uint32_t>(deco->operands[4].raw);
                    break;
                case spv::ExecutionMode::ExecutionModeOutputVertices:
                    assert(deco->operands.size() == 3);
                    ep->sizeX = std::get<uint32_t>(deco->operands[2].raw);
                    break;
                default:
                    break;
                }
            }
            case spv::OpExecutionModeId: {
                // examples:
                // - OpExecutionModeId %main LocalSizeId %uint_8 %uint_1 %uint_1
                assert(deco->operands[1].type == Token::Type::CONST);
                switch (static_cast<spv::ExecutionMode>(std::get<uint32_t>(deco->operands[1].raw))) {
                case spv::ExecutionMode::ExecutionModeLocalSizeId:
                    assert(deco->operands.size() == 5);
                    ep->sizeX = static_cast<const Primitive*>(deco->getValue(src_at, data))->data.u;
                    ep->sizeY = static_cast<const Primitive*>(deco->getValue(src_at + 1, data))->data.u;
                    ep->sizeZ = static_cast<const Primitive*>(deco->getValue(src_at + 2, data))->data.u;
                    break;
                default:
                    break;
                }
            }
            case spv::OpEntryPoint:
            default:
                break;  // other decorations should not occur
            }
        }
        if (entry)
            data[result_at].redefine(ep);
        else
            data[result_at].redefine(fx);
        break;
    }
    case spv::OpVariable: {  // 59
        assert(hasResultType);
        checkRef(dst_type_at, data.getBound());
        assert(operands[2].type == Token::Type::CONST);
        unsigned storage = std::get<unsigned>(operands[2].raw);
        // Note: We cannot necessarily initalize val now, because of some forward declarations of type.
        // Program is responsible for construction of vars within a static context and execute will handle dynamic.
        Variable* var = new Variable(nullptr, static_cast<spv::StorageClass>(storage), false);
        applyVarDeco(queue, *var, result_at);
        data[result_at].redefine(var);
        break;
    }
    case spv::OpAccessChain: {  // 65
        std::vector<unsigned> indices;
        assert(operands[2].type == Token::Type::REF);
        unsigned head = std::get<unsigned>(operands[2].raw);
        for (unsigned i = 3; i < operands.size(); ++i) {
            const Value* at = getValue(i, data);
            if (const auto at_base = at->getType().getBase(); at_base != DataType::UINT && at_base != DataType::INT)
                throw std::runtime_error("AccessChain index is not an integer!");
            const Primitive& pat = *static_cast<const Primitive*>(at);
            indices.push_back(pat.data.u);
        }
        Type* point_to = getType(dst_type_at, data);
        assert(point_to != nullptr);
        data[result_at].redefine(new Pointer(head, indices, *point_to));
        break;
    }
    case spv::OpVectorShuffle: {  // 79
        Value* first = getValue(src_at, data);
        Value* second = getValue(src_at + 1, data);
        // both first and second must be arrays
        if (const Type& ft = first->getType();
            first->getType().getBase() != second->getType().getBase() || ft.getBase() != DataType::ARRAY)
            throw std::runtime_error("First two src operands to VectorShuffle must be arrays!");
        Array& fa = *static_cast<Array*>(first);
        Array& sa = *static_cast<Array*>(second);
        unsigned fsize = fa.getSize();
        unsigned ssize = sa.getSize();
        std::vector<const Value*> vals;
        for (unsigned i = 4; i < operands.size(); ++i) {
            assert(operands[i].type == Token::Type::UINT);
            auto idx = std::get<unsigned>(operands[i].raw);
            if (idx < fsize) {
                vals.push_back(fa[idx]);
                continue;
            }
            idx -= fsize;
            if (idx < ssize) {
                vals.push_back(sa[idx]);
                continue;
            }
            std::stringstream error;
            error << "VectorShuffle index " << (i - 4) << " is beyond the bounds of source arrays!";
            throw std::runtime_error(error.str());
        }
        Type* retType = getType(dst_type_at, data);
        Array& arr = static_cast<Array&>(*retType->construct());
        for (unsigned i = 0; i < vals.size(); ++i)
            arr[i]->copyFrom(*vals[i]);
        data[result_at].redefine(&arr);
        break;
    }
    case spv::OpCompositeExtract: {  // 81
        Type* res_type = getType(dst_type_at, data);
        Value* to_ret = res_type->construct();
        Value* composite = getValue(src_at, data);
        const Value* extracted = composite_extract(composite, 3, operands);
        to_ret->copyFrom(*extracted);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpCompositeInsert: {  // 82
        Type* res_type = getType(dst_type_at, data);
        Value* to_ret = res_type->construct();
        const Value* composite = getValue(src_at + 1, data);
        to_ret->copyFrom(*composite);
        const Value* replacement = getValue(src_at, data);
        Value* extracted = composite_extract(to_ret, 4, operands);
        extracted->copyFrom(*replacement);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpCopyObject: {  // 83
        const Value* src = getValue(src_at, data);
        Type* res_type = getType(dst_type_at, data);
        Value* to_ret = res_type->construct();
        to_ret->copyFrom(*src);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpTranspose: {  // 84
        Type* res_type = getType(dst_type_at, data);
        Value* to_ret = res_type->construct();
        const Value* input = getValue(src_at, data);

        auto verify_matrix_type = [](const Value* val) {
            const Type& ty = val->getType();
            if (ty.getBase() != DataType::ARRAY || ty.getElement().getBase() != DataType::ARRAY) {
                std::stringstream error;
                error << "Cannot compute transpose of non-matrix type!";
                throw std::runtime_error(error.str());
            }
        };
        verify_matrix_type(to_ret);
        verify_matrix_type(input);
        const Array& inp_arr = *static_cast<const Array*>(input);
        Array& ret_arr = *static_cast<Array*>(to_ret);

        unsigned inp_size = inp_arr.getSize();
        unsigned ret_size = ret_arr.getSize();
        for (unsigned i = 0; i < ret_size; ++i) {
            Array& inside = *static_cast<Array*>(ret_arr[i]);
            unsigned j_size = inside.getSize();
            for (unsigned j = 0; j < j_size; ++j) {
                const Array& from_inside = *static_cast<const Array*>(inp_arr[j]);
                if (unsigned from_in_sz = from_inside.getSize(); j_size != inp_size || from_in_sz != ret_size) {
                    std::stringstream error;
                    error << "Cannot compute transpose of matrix " << from_in_sz << "x" << inp_size;
                    error << " to matrix " << j_size << "x" << ret_size << "!";
                    throw std::runtime_error(error.str());
                }
                inside[j]->copyFrom(*from_inside[i]);
            }
        }

        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpSampledImage: {  // 86
        const Value* image_v = getValue(src_at, data);
        if (image_v->getType().getBase() != DataType::IMAGE)
            throw std::runtime_error("The second operand to OpSampledImage must be an image!");
        const Value* sampler_v = getValue(src_at + 1, data);
        if (sampler_v->getType().getBase() != DataType::SAMPLER)
            throw std::runtime_error("The third operand to OpSampledImage must be a sampler!");

        SampledImage* to_ret =
            new SampledImage(static_cast<const Sampler&>(*sampler_v), static_cast<const Image&>(*image_v));
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpImageSampleImplicitLod:  // 87
    case spv::OpImageSampleExplicitLod:  // 88
    case spv::OpImageSampleProjImplicitLod:  // 91
    case spv::OpImageSampleProjExplicitLod:  // 92
    {
        const Value* sampled_v = getValue(src_at, data);
        if (sampled_v->getType().getBase() != DataType::SAMPLED_IMG)
            throw std::runtime_error("The third operand to OpImageSample* must be an sampler!");

        bool proj = (opcode == spv::OpImageSampleProjImplicitLod) || (opcode == spv::OpImageSampleProjExplicitLod);
        Value* to_ret = handleImage(data, *sampled_v, getValue(src_at + 1, data), 4, proj);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpImageFetch:  // 95
    case spv::OpImageRead:  // 98
    {
        const Value* image_v = getValue(src_at, data);
        if (image_v->getType().getBase() != DataType::IMAGE)
            throw std::runtime_error("The third operand to OpImage* must be an image!");

        Value* to_ret = handleImage(data, *image_v, getValue(src_at + 1, data), 4);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpImage: {  // 100
        Value* sampler_v = getValue(src_at, data);
        Image& image = static_cast<SampledImage&>(*sampler_v).getImage();
        data[result_at].redefine(&image, false);
        break;
    }
    case spv::OpImageQuerySizeLod: {  // 103
        const Value* image_v = getValue(src_at, data);
        const Value* lod_v = getValue(src_at + 1, data);
        // "Level of Detail is used to compute which mipmap level to query and must be a 32-bit integer type scalar."
        uint32_t lod = static_cast<const Primitive*>(lod_v)->data.u;
        const Image& image = static_cast<const Image&>(*image_v);
        std::array<uint32_t, 4> size = image.getSize(lod);
        Type* ret_type = getType(dst_type_at, data);
        Value* ret = ret_type->construct();
        Array& arr = *static_cast<Array*>(ret);
        assert(arr.getSize() <= 4);
        for (unsigned i = 0; i < arr.getSize(); ++i) {
            Primitive prim(static_cast<uint64_t>(size[i]));
            arr[i]->copyFrom(prim);
        }
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpConvertFToU:  // 109
        TYPICAL_E_UNARY_OP(FLOAT, static_cast<uint64_t>(a->data.f));
    case spv::OpConvertFToS:  // 110
        TYPICAL_E_UNARY_OP(FLOAT, static_cast<int64_t>(a->data.f));
    case spv::OpConvertSToF: {  // 111
        // Force interpret the integer as signed, regardless of its source type. This matches the allowance in the spec
        // that the input can be either signedness but has two variants: UToF and SToF.
        UnOp sfx = [](const Primitive* a) { return static_cast<double>(a->data.i); };
        element_int_unary_op(
            checkRef(src_at, data_len),
            OpDst {checkRef(dst_type_at, data_len), result_at},
            data,
            sfx,
            sfx
        );
        break;
    }
    case spv::OpConvertUToF: {  // 112
        UnOp ufx = [](const Primitive* a) { return static_cast<double>(a->data.u); };
        element_int_unary_op(
            checkRef(src_at, data_len),
            OpDst {checkRef(dst_type_at, data_len), result_at},
            data,
            ufx,
            ufx
        );
        break;
    }
    case spv::OpUConvert:  // 113
                           // Convert from Int or Uint -> Uint
    case spv::OpSConvert:  // 114
                           // Convert from Int or Uint -> Int
    case spv::OpFConvert: {  // 115
        // Convert from Float -> Float
        Type* res_type = getType(dst_type_at, data);
        Value* to_ret = res_type->construct();
        const Value* from = getValue(src_at, data);
        to_ret->copyFrom(*from);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpBitcast: {  // 124
        Type* res_type = getType(dst_type_at, data);
        Value* to_ret = res_type->construct();
        const Value* from = getValue(src_at, data);
        to_ret->copyReinterp(*from);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpSNegate:  // 126
        INT_E_UNARY_OP(-);
    case spv::OpFNegate:  // 127
        TYPICAL_E_UNARY_OP(FLOAT, -a->data.f);
    case spv::OpIAdd:  // 128
        INT_E_BIN_OP(+);
    case spv::OpFAdd:  // 129
        TYPICAL_E_BIN_OP(FLOAT, a->data.f + b->data.f);
    case spv::OpISub:  // 130
        INT_E_BIN_OP(-);
    case spv::OpFSub:  // 131
        TYPICAL_E_BIN_OP(FLOAT, a->data.f - b->data.f);
    case spv::OpIMul:  // 132
        INT_E_BIN_OP(*);
    case spv::OpFMul:  // 133
        TYPICAL_E_BIN_OP(FLOAT, a->data.f * b->data.f);
    case spv::OpUDiv:  // 134
        TYPICAL_E_BIN_OP(UINT, a->data.u / b->data.u);
    case spv::OpSDiv:  // 135
        TYPICAL_E_BIN_OP(INT, a->data.i / b->data.i);
    case spv::OpFDiv: {  // 136
        OpDst dst {checkRef(0, data_len), result_at};
        BinOp op = [](const Primitive* a, const Primitive* b) {
            if (b->data.f == 0.0) {  // divisor is neg or pos zero
                Console::warn("FDiv undefined since divisor is 0! Defaults to IEEE754.");
                if (std::isnan(a->data.f))
                    return a->data.f;
                if (a->data.f == 0.0)
                    return std::nan("1");
                double ret = std::numeric_limits<double>::infinity();
                return (std::signbit(b->data.f) != std::signbit(a->data.f)) ? -ret : ret;
            }
            return a->data.f / b->data.f;
        };
        element_bin_op(checkRef(2, data_len), checkRef(3, data_len), dst, data, op, DataType::FLOAT);
        break;
    }
    case spv::OpUMod: {  // 137
        BinOp fx = [](const Primitive* a, const Primitive* b) {
            if (b->data.u == 0) {
                Console::warn("UMod undefined since divisor is 0!");
                return Primitive(static_cast<uint64_t>(0));
            }
            return Primitive(a->data.u % b->data.u);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_bin_op(checkRef(src_at, data_len), checkRef(src_at + 1, data_len), dst, data, fx, DataType::UINT);
        break;
    }
    case spv::OpSRem: {  // 138
        BinOp fx = [](const Primitive* a, const Primitive* b) {
            if (b->data.i == 0) {
                Console::warn("SRem undefined since divisor is 0!");
                return Primitive(static_cast<uint64_t>(0));
            }
            if (a->data.i == std::numeric_limits<int64_t>::min() && b->data.i == -1) {
                Console::warn("SRem undefined since dividend is INT_MIN and divisor is -1 causing overflow!");
                return Primitive(static_cast<uint64_t>(0));
            }
            return Primitive(a->data.i % b->data.i);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_bin_op(checkRef(src_at, data_len), checkRef(src_at + 1, data_len), dst, data, fx, DataType::INT);
        break;
    }
    case spv::OpSMod: {  // 139
        BinOp fx = [](const Primitive* a, const Primitive* b) {
            if (b->data.i == 0) {
                Console::warn("SMod undefined since divisor is 0!");
                return Primitive(static_cast<uint64_t>(0));
            }
            if (a->data.i == std::numeric_limits<int64_t>::min() && b->data.i == -1) {
                Console::warn("SMod undefined since dividend is INT_MIN and divisor is -1 causing overflow!");
                return Primitive(static_cast<uint64_t>(0));
            }
            int64_t res = a->data.i % b->data.i;
            if (res != 0 && ((a->data.i ^ b->data.i) < 0)) {
                // If the dividend and divisor have different signs, we need to adjust the result
                res += b->data.i;
            }
            return Primitive(res);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_bin_op(checkRef(src_at, data_len), checkRef(src_at + 1, data_len), dst, data, fx, DataType::INT);
        break;
    }
    case spv::OpFRem: {  // 140
        BinOp fx = [](const Primitive* a, const Primitive* b) {
            if (b->data.f == 0.0) {
                Console::warn("FRem undefined since divisor is 0!");
                return std::nan("1");
            }
            return a->data.f - b->data.f * std::trunc(a->data.f / b->data.f);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_bin_op(checkRef(src_at, data_len), checkRef(src_at + 1, data_len), dst, data, fx, DataType::FLOAT);
        break;
    }
    case spv::OpFMod: {  // 141
        BinOp fx = [](const Primitive* a, const Primitive* b) {
            if (b->data.f == 0.0) {
                Console::warn("FMod undefined since divisor is 0!");
                return std::nan("1");
            }
            return a->data.f - b->data.f * std::floor(a->data.f / b->data.f);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_bin_op(checkRef(src_at, data_len), checkRef(src_at + 1, data_len), dst, data, fx, DataType::FLOAT);
        break;
    }
    case spv::OpVectorTimesScalar: {  // 142
        Value* vec_val = getValue(src_at, data);
        const Type& vec_type = vec_val->getType();
        if (vec_type.getBase() != DataType::ARRAY)
            throw std::runtime_error("Could not load vector in VectorTimesScalar!");
        const Array& vec = *static_cast<Array*>(vec_val);
        if (vec_type.getElement().getBase() != DataType::FLOAT)
            throw std::runtime_error("Cannot multiply vector with non-float element type!");

        Value* scal_val = getValue(src_at + 1, data);
        if (scal_val == nullptr || scal_val->getType().getBase() != DataType::FLOAT)
            throw std::runtime_error("Could not load scalar in VectorTimesScalar!");
        const Primitive& scal = *static_cast<Primitive*>(scal_val);

        Type* res_type = getType(dst_type_at, data);
        Array& res = *static_cast<Array*>(res_type->construct());
        for (unsigned i = 0; i < vec.getSize(); ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            Primitive intermed(vec_e.data.f * scal.data.f);
            res[i]->copyFrom(intermed);
        }

        data[result_at].redefine(&res);
        break;
    }
    case spv::OpMatrixTimesScalar: {  // 143
        Type* res_type = getType(dst_type_at, data);
        Value* res = res_type->construct();
        Array& mres = *static_cast<Array*>(res);
        const Array& mat = *static_cast<Array*>(getValue(src_at, data));
        const Primitive& cons = *static_cast<Primitive*>(getValue(src_at + 1, data));
        unsigned ncols = mat.getSize();
        unsigned nrows = mat.getType().getElement().getSize();
        for (unsigned i = 0; i < ncols; ++i) {
            const Array& column = *static_cast<const Array*>(mat[i]);
            Array& dst_col = *static_cast<Array*>(mres[i]);
            for (unsigned j = 0; j < nrows; ++j) {
                const Primitive& val = *static_cast<const Primitive*>(column[j]);
                Primitive el = multiply_same(val, cons);
                Primitive& dst = *static_cast<Primitive*>(dst_col[j]);
                dst.copyFrom(el);
            }
        }
        data[result_at].redefine(res);
        break;
    }
    case spv::OpVectorTimesMatrix: {  // 144
        Type* res_type = getType(dst_type_at, data);
        // V * M
        // (1xA) * (AxB) = (1xB)
        // Vector's "number of components must equal the number of components in each column in Matrix."
        // Rows x Columns -> mat[column][row]
        Value* res = res_type->construct();
        Array& vres = *static_cast<Array*>(res);
        const Array& vec = *static_cast<Array*>(getValue(src_at, data));
        const Array& mat = *static_cast<Array*>(getValue(src_at + 1, data));

        //           [3 4 5]   [(0*3 + 1*4 + 2*5)]
        // [0 1 2] * [6 7 8] = [(0*6 + 1*7 + 2*8)]
        unsigned b = vres.getSize();
        unsigned a = vec.getSize();
        for (unsigned i = 0; i < b; ++i) {
            Primitive el(static_cast<uint64_t>(0));
            const Array& mcolumn = *static_cast<const Array*>(mat[i]);
            for (unsigned j = 0; j < a; ++j) {
                const Primitive& vecv = *static_cast<const Primitive*>(vec[j]);
                const Primitive& matv = *static_cast<const Primitive*>(mcolumn[j]);
                Primitive eli = multiply_same(vecv, matv);
                if (j == 0)
                    el = eli;
                else
                    accum_same(el, eli);
            }
            Primitive& dst = *static_cast<Primitive*>(vres[i]);
            dst.copyFrom(el);
        }
        data[result_at].redefine(res);
        break;
    }
    case spv::OpMatrixTimesVector: {  // 145
        Type* res_type = getType(dst_type_at, data);
        // M * V
        // (AxB) * (Bx1) = (Ax1)
        // Vector's "number of components must equal the number of columns in Matrix."
        // Rows x Columns -> mat[column][row]
        Value* res = res_type->construct();
        Array& vres = *static_cast<Array*>(res);
        const Array& mat = *static_cast<Array*>(getValue(src_at, data));
        const Array& vec = *static_cast<Array*>(getValue(src_at + 1, data));

        // [0 1]   [6]
        // [2 3] * [7] = [(0*6 + 2*7 + 4*8) (1*6 + 3*7 + 5*8)]
        // [4 5]   [8]
        unsigned a = vres.getSize();
        unsigned b = vec.getSize();
        for (unsigned i = 0; i < a; ++i) {
            Primitive el(static_cast<uint64_t>(0));
            for (unsigned j = 0; j < b; ++j) {
                const Array& mcolumn = *static_cast<const Array*>(mat[j]);
                const Primitive& matv = *static_cast<const Primitive*>(mcolumn[i]);
                const Primitive& vecv = *static_cast<const Primitive*>(vec[j]);
                Primitive eli = multiply_same(vecv, matv);
                if (j == 0)
                    el = eli;
                else
                    accum_same(el, eli);
            }
            Primitive& dst = *static_cast<Primitive*>(vres[i]);
            dst.copyFrom(el);
        }
        data[result_at].redefine(res);
        break;
    }
    case spv::OpMatrixTimesMatrix: {  // 146
        Type* res_type = getType(dst_type_at, data);
        // (AxB) * (BxC) = (AxC)
        // RightMatrix's "number of columns must equal the number of columns in Result Type. Its columns must have the
        // same number of components as the number of columns in LeftMatrix."
        // Rows x Columns -> mat[column][row]
        Value* res = res_type->construct();
        Array& mres = *static_cast<Array*>(res);
        const Array& lmat = *static_cast<Array*>(getValue(src_at, data));
        const Array& rmat = *static_cast<Array*>(getValue(src_at + 1, data));
        unsigned a = lmat.getType().getElement().getSize();
        unsigned b = lmat.getSize();
        unsigned c = rmat.getSize();
        for (unsigned i = 0; i < c; ++i) {
            Array& res_column = *static_cast<Array*>(mres[i]);
            for (unsigned j = 0; j < a; ++j) {
                const auto& rcolumn = static_cast<const Array&>(*(rmat[i]));
                Primitive el(static_cast<uint64_t>(0));
                for (unsigned k = 0; k < b; ++k) {
                    // Get (k, j) in left, (i, k) in right
                    const auto& lcolumn = static_cast<const Array&>(*(lmat[k]));
                    const auto& lv = static_cast<const Primitive&>(*(lcolumn[j]));
                    const auto& rv = static_cast<const Primitive&>(*(rcolumn[k]));
                    Primitive eli = multiply_same(lv, rv);
                    if (k == 0)
                        el = eli;
                    else
                        accum_same(el, eli);
                }
                auto& dst = static_cast<Primitive&>(*res_column[j]);
                dst.copyFrom(el);
            }
        }
        data[result_at].redefine(res);
        break;
    }
    case spv::OpOuterProduct: {  // 147
        Type* res_type = getType(dst_type_at, data);
        Array& mres = *static_cast<Array*>(res_type->construct());
        const auto& v1 = static_cast<const Array&>(*getValue(src_at, data));
        const auto& v2 = static_cast<const Array&>(*getValue(src_at + 1, data));
        // The number of components in Vector 2 must equal the number of result columns according to spec
        assert(v2.getSize() == mres.getSize());
        for (unsigned i = 0; i < v2.getSize(); ++i) {
            const Primitive& v2e = *static_cast<const Primitive*>(v2[i]);
            auto& res_col = static_cast<Array&>(*mres[i]);
            for (unsigned j = 0; j < v1.getSize(); ++j) {
                const Primitive& v1e = *static_cast<const Primitive*>(v1[j]);
                Primitive eli = multiply_same(v1e, v2e);
                Primitive& res = static_cast<Primitive&>(*res_col[j]);
                res.copyFrom(eli);
            }
        }
        data[result_at].redefine(&mres);
        break;
    }
    case spv::OpDot: {  // 148
        const auto& op0 = static_cast<const Array&>(*getValue(src_at, data));
        const auto& op1 = static_cast<const Array&>(*getValue(src_at + 1, data));
        assert(op0.getType().getBase() == DataType::ARRAY && "The first operand to OpDot must be an array!");
        assert(op1.getType().getBase() == DataType::ARRAY && "The second operand to OpDot must be an array!");
        assert(
            op0.getType().getElement().getBase() == DataType::FLOAT &&
            "The first operand to OpDot must be a float array!"
        );
        assert(
            op1.getType().getElement().getBase() == DataType::FLOAT &&
            "The second operand to OpDot must be a float array!"
        );
        assert(op0.getSize() == op1.getSize() && "The operands to OpDot must have matching sizes!");

        double product = ArrayMath::dot(op0, op1);
        Primitive tot_prim(product);
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(tot_prim);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpIAddCarry: {  // 149
        // Despite being called I (for int), only uints are allowed as inputs
        ExtArithOp op = [](const Primitive* a, const Primitive* b, Primitive* f, Primitive* s) {
            s->data.u = (a->uAdd(b, f)) ? 1 : 0;
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_extended_arith_op(
            checkRef(src_at, data_len),
            checkRef(src_at + 1, data_len),
            dst,
            data,
            op,
            DataType::UINT
        );
        break;
    }
    case spv::OpISubBorrow: {  // 150
        ExtArithOp op = [](const Primitive* a, const Primitive* b, Primitive* f, Primitive* s) {
            s->data.u = (a->uSub(b, f)) ? 1 : 0;
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_extended_arith_op(
            checkRef(src_at, data_len),
            checkRef(src_at + 1, data_len),
            dst,
            data,
            op,
            DataType::UINT
        );
        break;
    }
    case spv::OpUMulExtended: {  // 151
        ExtArithOp op = [](const Primitive* a, const Primitive* b, Primitive* f, Primitive* s) { a->uMul(b, f, s); };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_extended_arith_op(
            checkRef(src_at, data_len),
            checkRef(src_at + 1, data_len),
            dst,
            data,
            op,
            DataType::UINT
        );
        break;
    }
    case spv::OpAny: {  // 154
        Value* vec_val = getValue(src_at, data);
        const Type& vec_type = vec_val->getType();
        if (vec_type.getBase() != DataType::ARRAY)
            throw std::runtime_error("Could not load vector argument to OpAny!");
        const Array& vec = *static_cast<Array*>(vec_val);
        if (vec_type.getElement().getBase() != DataType::BOOL)
            throw std::runtime_error("Vector operand of OpAny must have bool type!");

        unsigned size = vec.getSize();
        bool any = false;
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            any |= vec_e.data.b;
        }
        data[result_at].redefine(new Primitive(any));
        break;
    }
    case spv::OpAll: {  // 155
        Value* vec_val = getValue(src_at, data);
        const Type& vec_type = vec_val->getType();
        if (vec_type.getBase() != DataType::ARRAY)
            throw std::runtime_error("Could not load vector argument to OpAll!");
        const Array& vec = *static_cast<Array*>(vec_val);
        if (vec_type.getElement().getBase() != DataType::BOOL)
            throw std::runtime_error("Vector operand of OpAny must have bool type!");

        unsigned size = vec.getSize();
        bool all = true;
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            all &= vec_e.data.b;
        }
        data[result_at].redefine(new Primitive(all));
        break;
    }
    case spv::OpIsNan:  // 156
        TYPICAL_E_UNARY_OP(FLOAT, std::isnan(a->data.f));
    case spv::OpIsInf:  // 157
        TYPICAL_E_UNARY_OP(FLOAT, std::isinf(a->data.f));
    case spv::OpLogicalEqual:  // 164
        TYPICAL_E_BIN_OP(BOOL, a->data.b == b->data.b);
    case spv::OpLogicalNotEqual:  // 165
        TYPICAL_E_BIN_OP(BOOL, a->data.b != b->data.b);
    case spv::OpLogicalOr:  // 166
        TYPICAL_E_BIN_OP(BOOL, a->data.b || b->data.b);
    case spv::OpLogicalAnd:  // 167
        TYPICAL_E_BIN_OP(BOOL, a->data.b && b->data.b);
    case spv::OpLogicalNot:  // 168
        TYPICAL_E_UNARY_OP(BOOL, !(a->data.b));
    case spv::OpSelect: {  // 169
        Value* condition = getValue(src_at, data);
        Value* first = getValue(src_at + 1, data);
        Value* second = getValue(src_at + 2, data);

        const Type& type = condition->getType();
        DataType dt = type.getBase();
        // Condition must be a scalar or a vector of boolean type
        if (dt != DataType::ARRAY) {
            // Simple case, we can choose between the two options
            assert(dt == DataType::BOOL);
            auto cond = static_cast<Primitive*>(condition);
            Value* to_use = (cond->data.b) ? first : second;
            // Now we must clone to result
            Value* cloned = to_use->getType().construct();
            cloned->copyFrom(*to_use);
            data[result_at].redefine(cloned);
        } else {
            // Complex case, we must build a result where each component of condition chooses a value to use
            const auto& cond_arr = *static_cast<Array*>(condition);
            unsigned cond_size = cond_arr.getSize();
            const Type& first_type = first->getType();
            if (const auto base = first_type.getBase(); base != DataType::ARRAY && base != DataType::STRUCT)
                throw std::runtime_error(
                    "First option in Select with vector condition must be either vector, array, "
                    "or struct!"
                );
            const Type& second_type = second->getType();
            if (const auto base = second_type.getBase(); base != DataType::ARRAY && base != DataType::STRUCT)
                throw std::runtime_error(
                    "Second option in Select with vector condition must be either vector, array, "
                    "or struct!"
                );
            const auto& first_agg = *static_cast<Aggregate*>(first);
            assert(first_agg.getSize() == cond_size && "Size of second Select argument must match condition's size!");
            const auto& second_agg = *static_cast<Aggregate*>(second);
            assert(second_agg.getSize() == cond_size && "Size of second Select argument must match condition's size!");

            std::vector<const Value*> es;
            for (unsigned i = 0; i < cond_size; ++i) {
                const auto& cond_bool = static_cast<const Primitive&>(*cond_arr[i]);
                es.push_back(cond_bool.data.b ? first_agg[i] : second_agg[i]);
            }

            Type* res_type = getType(dst_type_at, data);
            Aggregate* result = (res_type->getBase() == DataType::ARRAY)
                                    ? static_cast<Aggregate*>(new Array(res_type->getElement(), cond_size))
                                    : static_cast<Aggregate*>(new Struct(*res_type));
            result->addElements(es);
            data[result_at].redefine(result);
        }
        break;
    }
    case spv::OpIEqual:  // 170
        INT_E_BIN_OP(==);
    case spv::OpINotEqual:  // 171
        INT_E_BIN_OP(!=);
    case spv::OpUGreaterThan:  // 172
        TYPICAL_E_BIN_OP(UINT, a->data.u > b->data.u);
    case spv::OpSGreaterThan:  // 173
        TYPICAL_E_BIN_OP(INT, a->data.i > b->data.i);
    case spv::OpUGreaterThanEqual:  // 174
        TYPICAL_E_BIN_OP(UINT, a->data.u >= b->data.u);
    case spv::OpSGreaterThanEqual:  // 175
        TYPICAL_E_BIN_OP(INT, a->data.i >= b->data.i);
    case spv::OpULessThan:  // 176
        TYPICAL_E_BIN_OP(UINT, a->data.u < b->data.u);
    case spv::OpSLessThan:  // 177
        TYPICAL_E_BIN_OP(INT, a->data.i < b->data.i);
    case spv::OpULessThanEqual:  // 178
        TYPICAL_E_BIN_OP(UINT, a->data.u <= b->data.u);
    case spv::OpSLessThanEqual:  // 179
        TYPICAL_E_BIN_OP(INT, a->data.i <= b->data.i);
    case spv::OpFOrdEqual:  // 180
        TYPICAL_E_BIN_OP(FLOAT, a->data.f == b->data.f);
    case spv::OpFUnordEqual:  // 181
        TYPICAL_E_BIN_OP(FLOAT, std::isnan(a->data.f) || std::isnan(b->data.f) || a->data.f == b->data.f);
    case spv::OpFOrdNotEqual:  // 182
        TYPICAL_E_BIN_OP(FLOAT, a->data.f != b->data.f);
    case spv::OpFUnordNotEqual:  // 183
        TYPICAL_E_BIN_OP(FLOAT, std::isnan(a->data.f) || std::isnan(b->data.f) || a->data.f != b->data.f);
    case spv::OpFOrdLessThan:  // 184
        TYPICAL_E_BIN_OP(FLOAT, a->data.f < b->data.f);
    case spv::OpFUnordLessThan:  // 185
        TYPICAL_E_BIN_OP(FLOAT, std::isnan(a->data.f) || std::isnan(b->data.f) || a->data.f < b->data.f);
    case spv::OpFOrdGreaterThan:  // 186
        TYPICAL_E_BIN_OP(FLOAT, a->data.f > b->data.f);
    case spv::OpFUnordGreaterThan:  // 187
        TYPICAL_E_BIN_OP(FLOAT, std::isnan(a->data.f) || std::isnan(b->data.f) || a->data.f > b->data.f);
    case spv::OpFOrdLessThanEqual:  // 188
        TYPICAL_E_BIN_OP(FLOAT, a->data.f <= b->data.f);
    case spv::OpFUnordLessThanEqual:  // 189
        TYPICAL_E_BIN_OP(FLOAT, std::isnan(a->data.f) || std::isnan(b->data.f) || a->data.f <= b->data.f);
    case spv::OpFOrdGreaterThanEqual:  // 190
        TYPICAL_E_BIN_OP(FLOAT, a->data.f >= b->data.f);
    case spv::OpFUnordGreaterThanEqual:  // 191
        TYPICAL_E_BIN_OP(FLOAT, std::isnan(a->data.f) || std::isnan(b->data.f) || a->data.f >= b->data.f);
    case spv::OpShiftRightLogical: {  // 194
        E_SHIFT_OP([](const Primitive* a, const Primitive* b) { return a->data.u >> b->data.u; });
        break;
    }
    case spv::OpShiftRightArithmetic: {  // 195
        const auto* val = getValue(src_at, data);
        const auto type = val->getType();
        auto prec = element_type(type).getPrecision();
        uint64_t bitmask = (prec == 64) ? ~0ULL : (1ULL << prec) - 1;
        E_SHIFT_OP([bitmask](const Primitive* a, const Primitive* b) {
            auto base = a->data.u;
            // Fill the shifted bits with the sign bit.
            // We emulate integer at 64-bit precision, so the sign bit is bit 63.
            bool sign = (base & (1ULL << 63)) > 0;
            auto shifted = base >> b->data.u;
            if (sign)
                shifted |= ~bitmask;
            else
                shifted &= bitmask;
            // fill in the bits from the sign
            return shifted;
        });
        break;
    }
    case spv::OpShiftLeftLogical: {  // 196
        E_SHIFT_OP([](const Primitive* a, const Primitive* b) { return a->data.u << b->data.u; });
        break;
    }
    case spv::OpBitwiseOr:  // 197
        INT_E_BIN_OP(|);
    case spv::OpBitwiseXor:  // 198
        INT_E_BIN_OP(^);
    case spv::OpBitwiseAnd:  // 199
        INT_E_BIN_OP(&);
    case spv::OpNot:  // 200
        INT_E_UNARY_OP(~);
    case spv::OpBitFieldInsert: {  // 201
        const auto& offset_p = static_cast<const Primitive&>(*getValue(src_at + 2, data));
        if (auto base = offset_p.getType().getBase(); base != DataType::INT && base != DataType::UINT)
            throw std::runtime_error("The type of bitfield insert offset operand must be an integer!");
        uint32_t offset = offset_p.data.u;
        const auto& count_p = static_cast<const Primitive&>(*getValue(src_at + 3, data));
        if (auto base = count_p.getType().getBase(); base != DataType::INT && base != DataType::UINT)
            throw std::runtime_error("The type of bitfield insert count operand must be an integer!");

        const Value* base_v = getValue(src_at, data);
        const Value* insert_v = getValue(src_at + 1, data);
        // Check the type of base_v and insert_v as ints (either signed or unsigned) manually
        auto base_base = element_base(*base_v);
        auto insert_base = element_base(*insert_v);
        assert(base_base == DataType::INT || base_base == DataType::UINT);
        assert(insert_base == DataType::INT || insert_base == DataType::UINT);

        uint32_t src_mask = ((1 << count_p.data.u) - 1);
        uint32_t insertion_mask = ~(src_mask << offset);

        BinOp fx_s = [&](const Primitive* a, const Primitive* b) {
            return int32_t((a->data.u & insertion_mask) | ((b->data.u & src_mask) << offset));
        };
        BinOp fx_u = [&](const Primitive* a, const Primitive* b) {
            uint32_t base = (a->data.u & insertion_mask);
            uint32_t insert = ((b->data.u & src_mask) << offset);
            return insert | base;
        };
        bool is_result_signed = type_base(*getType(dst_type_at, data)) == DataType::INT;
        assert(is_result_signed || type_base(*getType(dst_type_at, data)) == DataType::UINT);
        OpDst dst {std::get<unsigned>(operands[dst_type_at].raw), result_at};
        element_bin_op(
            // No need to checkRef for the first two arguments since they have been checked already
            std::get<unsigned>(operands[src_at].raw),
            std::get<unsigned>(operands[src_at + 1].raw),
            dst,
            data,
            is_result_signed ? fx_s : fx_u,
            DataType::VOID
        );
        break;
    }
    case spv::OpBitFieldSExtract:  // 202
    case spv::OpBitFieldUExtract:  // 203
    {
        // Base can be sint or uint regardless of which, however, the result must match the type of base, and only
        // SExtract will do sign extensions
        bool extend = (opcode == spv::OpBitFieldSExtract);
        // Both offset and count are consumed as unsigned values, but the spec doesn't explicitly say they must be uint
        const auto& offset_p = static_cast<const Primitive&>(*getValue(src_at + 1, data));
        if (auto base = offset_p.getType().getBase(); base != DataType::INT && base != DataType::UINT)
            throw std::runtime_error("The type of bitfield extract offset operand must be an integer!");
        const auto& count_p = static_cast<const Primitive&>(*getValue(src_at + 2, data));
        if (auto base = count_p.getType().getBase(); base != DataType::INT && base != DataType::UINT)
            throw std::runtime_error("The type of bitfield extract count operand must be an integer!");
        uint32_t mask = ((1 << count_p.data.u) - 1);

        uint32_t single, other;
        if (extend && mask != 0) {
            single = (0x1 << (count_p.data.u - 1));
            other = (0xFFFF'FFFF >> (32 - count_p.data.u)) << count_p.data.u;
        }

        UnOp ufx = [&](const Primitive* a) {
            if (mask == 0)
                return Primitive(static_cast<uint64_t>(0));
            uint32_t val = (a->data.u >> offset_p.data.u) & mask;
            if (extend && ((val & single) > 0))
                val |= other;
            return Primitive(val);
        };
        UnOp ifx = [&](const Primitive* a) {
            if (mask == 0)
                return Primitive(static_cast<uint64_t>(0));
            uint32_t val = (a->data.u >> offset_p.data.u) & mask;
            if (extend && ((val & single) > 0))
                val |= other;
            Primitive prim(-1);
            prim.data.u = val;
            return prim;
        };
        element_int_unary_op(
            checkRef(src_at, data_len),
            OpDst {checkRef(dst_type_at, data_len), result_at},
            data,
            ufx,
            ifx
        );
        break;
    }
    case spv::OpBitReverse: {  // 204
        auto src_at_idx = checkRef(src_at, data_len);
        const Value& operand = *data[src_at_idx].getValue();
        const Type& el_type = element_type(operand.getType());
        auto base = el_type.getBase();
        assert((base == DataType::UINT || base == DataType::INT) && "Cannot reverse bits of non-integral-typed value!");
        auto dst_type_idx = checkRef(dst_type_at, data_len);
        const Type& dst_type = element_type(*data[dst_type_idx].getType());
        unsigned src_width = el_type.getPrecision();
        // It may be a little unintuitive, but the spec says that the width used is of the "Result Type".
        unsigned max = dst_type.getPrecision() - 1;

        UnOp op = [src_width, max, dst_type](const Primitive* a) {
            uint64_t raw = a->getRaw();
            uint64_t res = 0;
            for (unsigned i = 0; i < src_width; ++i)
                res |= ((raw >> i) & 1) << (max - i);
            Primitive tmp(res);
            Primitive ret(dst_type);
            ret.copyReinterp(tmp);
            return ret;
        };
        OpDst dst {dst_type_idx, result_at};
        element_unary_op(base, src_at_idx, dst, data, op);
        break;
    }
    case spv::OpLabel:  // 248
        data[result_at].redefine(new Primitive(location));
        break;
    case spv::OpPtrEqual:  // 401
    case spv::OpPtrNotEqual:  // 402
    {
        const Value* first = getValue(src_at, data);
        const Value* second = getValue(src_at + 1, data);
        if (first->getType().getBase() != DataType::POINTER)
            throw std::runtime_error("The type of the first operand for pointer comparison must be a pointer!");
        if (second->getType().getBase() != DataType::POINTER)
            throw std::runtime_error("The type of the second operand for pointer comparison must be a pointer!");
        const Pointer& first_ptr = *static_cast<const Pointer*>(first);
        const Pointer& second_ptr = *static_cast<const Pointer*>(second);
        Value* head_first = getHeadValue(first_ptr, data);
        Value* head_second = getHeadValue(second_ptr, data);
        const Value* first_pointed = first_ptr.dereference(*head_first);
        const Value* second_pointed = second_ptr.dereference(*head_second);
        bool result =
            (opcode == spv::OpPtrEqual) ? (first_pointed == second_pointed) : (first_pointed != second_pointed);
        data[result_at].redefine(new Primitive(result));
        break;
    }
    case spv::OpConvertUToAccelerationStructureKHR: {  // 4447
        assert(hasResultType);
        // TODO: needs the get an acceleration structure from a buffer via a 64-bit address. How to do this?
        Value* address_ptr = getValue(src_at, data);
        assert(address_ptr != nullptr);
        // uint64_t address = 0;
        if (address_ptr->getType().getBase() == DataType::ARRAY) {
            // case uvec2
            Array& address_components = static_cast<Array&>(*address_ptr);
            assert(address_components.getSize() == 2);
            // address = static_cast<Primitive&>(*(address_components[0])).data.u;
            // address <<= 32;
            // uint32_t lower = static_cast<Primitive&>(*(address_components[1])).data.u;
            // address |= lower;
        } else {
            // case uint64_t
            throw std::runtime_error("uint64_t is unsupported for OpConvertUToAccelerationStructureKHR.");
        }

        throw std::runtime_error("OpConvertUToAccelerationStructureKHR not implemented.");

        // Type* res_type = getType(dst_type_at, data);
        //  AccelStruct res; // update me; set the acceleration structure
        //  std::vector<const Value*> values {&res};
        //  data[result_at].redefine(res_type->construct(values));
        break;
    }
    case spv::OpSDot: {  // 4450
        const auto& op0 = static_cast<const Array&>(*getValue(src_at, data));
        const auto& op1 = static_cast<const Array&>(*getValue(src_at + 1, data));
        assert(op0.getType().getBase() == DataType::ARRAY && "The first operand to OpSDot must be an array!");
        assert(op1.getType().getBase() == DataType::ARRAY && "The second operand to OpSDot must be an array!");
        assert(
            op0.getType().getElement().getBase() == DataType::INT && "The first operand to OpSDot must be a int array!"
        );
        assert(
            op1.getType().getElement().getBase() == DataType::INT && "The second operand to OpSDot must be a int array!"
        );
        assert(op0.getSize() == op1.getSize() && "The operands to OpSDot must have matching sizes!");

        int32_t dot_product = 0;
        for (unsigned i = 0; i < op0.getSize(); ++i) {
            const int32_t second_elem = static_cast<const Primitive*>(op1[i])->data.i;
            const int32_t this_elem = static_cast<const Primitive*>(op0[i])->data.i;
            dot_product += second_elem * this_elem;
        }
        Primitive tot_prim(dot_product);
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(tot_prim);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpTypeCooperativeMatrixKHR: {  // 4456
        Type* sub = getType(1, data);
        const auto& scope = static_cast<const Primitive&>(*getValue(src_at, data));
        const auto& rows = static_cast<const Primitive&>(*getValue(src_at + 1, data));
        const auto& cols = static_cast<const Primitive&>(*getValue(src_at + 2, data));
        // We don't care about "MatrixUse" in the interpreter. It doesn't affect how we store the data.

        data[result_at].redefine(new Type(Type::coopMatrix(scope.data.u, rows.data.u, cols.data.u, *sub)));
        break;
    }
    case spv::OpTypeRayQueryKHR: {  // 4472
        data[result_at].redefine(new Type(Type::rayQuery()));
        break;
    }
    case spv::OpRayQueryGetIntersectionTypeKHR: {  // 4479
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        unsigned type;
        switch (ray_query.getAccelStruct().getIntersectionType(intersection)) {
        default:  // Intersection::Type::None:
            type = 0;
            break;
        case Intersection::Type::AABB:
            type = 1;
            break;
        case Intersection::Type::Triangle:
            type = intersection ? 1 : 0;
            break;
        case Intersection::Type::Generated:
            type = 2;
            break;
        }
        data[result_at].redefine(new Primitive(type));
        break;
    }
    case spv::OpTypeAccelerationStructureKHR: {  // 5341
        data[result_at].redefine(new Type(Type::accelStruct()));
        break;
    }
    case spv::OpRayQueryGetRayTMinKHR: {  // 6016
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        Primitive res(ray_query.getAccelStruct().getTrace().rayTMin);
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetRayFlagsKHR: {  // 6017
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        Primitive res(ray_query.getAccelStruct().getTrace().rayFlags.get());
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetIntersectionTKHR: {  // 6018
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        Primitive res(ray_query.getAccelStruct().getIntersectionT(intersection));
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetIntersectionInstanceCustomIndexKHR: {  // 6019
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        Primitive res(ray_query.getAccelStruct().getIntersectionInstanceCustomIndex(intersection));
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetIntersectionInstanceIdKHR: {  // 6020
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        Primitive res(ray_query.getAccelStruct().getIntersectionInstanceId(intersection));
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetKHR: {  // 6021
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        Primitive res(ray_query.getAccelStruct().getIntersectionInstanceShaderBindingTableRecordOffset(intersection));
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetIntersectionGeometryIndexKHR: {  // 6022
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        Primitive res(ray_query.getAccelStruct().getIntersectionGeometryIndex(intersection));
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetIntersectionPrimitiveIndexKHR: {  // 6023
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        Primitive res(ray_query.getAccelStruct().getIntersectionPrimitiveIndex(intersection));
        Value* ret = getType(dst_type_at, data)->construct();
        ret->copyFrom(res);
        data[result_at].redefine(ret);
        break;
    }
    case spv::OpRayQueryGetIntersectionBarycentricsKHR: {  // 6024
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        std::vector<Primitive> barycentrics = ray_query.getIntersectionBarycentrics(intersection);
        const Type* res_type = getType(dst_type_at, data);
        data[result_at].redefine(construct_from_vec(barycentrics, res_type));
        break;
    }
    case spv::OpRayQueryGetIntersectionFrontFaceKHR: {  // 6025
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        Type* res_type = getType(dst_type_at, data);
        Primitive prim(ray_query.getAccelStruct().getIntersectionFrontFace(intersection));
        Value* res = res_type->construct();
        res->copyFrom(prim);
        data[result_at].redefine(res);
        break;
    }
    case spv::OpRayQueryGetIntersectionCandidateAABBOpaqueKHR: {  // 6026
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        data[result_at].redefine(new Primitive(ray_query.getAccelStruct().getIntersectionCandidateAABBOpaque()));
        break;
    }
    case spv::OpRayQueryGetIntersectionObjectRayDirectionKHR: {  // 6027
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        std::vector<Primitive> direction = ray_query.getIntersectionObjectRayDirection(intersection);
        const Type* res_type = getType(dst_type_at, data);
        data[result_at].redefine(construct_from_vec(direction, res_type));
        break;
    }
    case spv::OpRayQueryGetIntersectionObjectRayOriginKHR: {  // 6028
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;
        std::vector<Primitive> origin = ray_query.getIntersectionObjectRayOrigin(intersection);
        const Type* res_type = getType(dst_type_at, data);
        data[result_at].redefine(construct_from_vec(origin, res_type));
        break;
    }
    case spv::OpRayQueryGetWorldRayDirectionKHR: {  // 6029
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        std::vector<Primitive> direction = ray_query.getAccelStruct().getWorldRayDirection();
        const Type* res_type = getType(dst_type_at, data);
        data[result_at].redefine(construct_from_vec(direction, res_type));
        break;
    }
    case spv::OpRayQueryGetWorldRayOriginKHR: {  // 6030
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        std::vector<Primitive> origin = ray_query.getAccelStruct().getWorldRayOrigin();
        const Type* res_type = getType(dst_type_at, data);
        data[result_at].redefine(construct_from_vec(origin, res_type));
        break;
    }
    case spv::OpRayQueryGetIntersectionObjectToWorldKHR: {  // 6031
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;

        Type* res_type = getType(dst_type_at, data);
        Array& result = static_cast<Array&>(*res_type->construct());
        assert(result.getSize() == 4);  // Expecting 4 columns

        const auto object_to_world = ray_query.getIntersectionObjectToWorld(intersection);  // column-major order
        unsigned idx = 0;
        for (unsigned col = 0; col < result.getSize(); ++col) {
            Array& col_locations = static_cast<Array&>(*(result[col]));
            for (unsigned row = 0; row < col_locations.getSize(); ++row) {
                Primitive& destination = static_cast<Primitive&>(*(col_locations[row]));
                destination.copyFrom(object_to_world[idx]);
                idx++;
            }
        }
        data[result_at].redefine(&result);
        break;
    }
    case spv::OpRayQueryGetIntersectionWorldToObjectKHR: {  // 6032
        RayQuery& ray_query = static_cast<RayQuery&>(*getFromPointer(2, data));
        const bool intersection = static_cast<Primitive&>(*getValue(src_at + 1, data)).data.u == 1;

        Type* res_type = getType(dst_type_at, data);
        Array& result = static_cast<Array&>(*res_type->construct());
        assert(result.getSize() == 4);  // Expecting 4 columns

        const auto world_to_object = ray_query.getIntersectionWorldToObject(intersection);  // column-major order
        unsigned idx = 0;
        for (unsigned col = 0; col < result.getSize(); ++col) {
            Array& col_locations = static_cast<Array&>(*(result[col]));
            for (unsigned row = 0; row < col_locations.getSize(); ++row) {
                Primitive& destination = static_cast<Primitive&>(*(col_locations[row]));
                destination.copyFrom(world_to_object[idx]);
                ++idx;
            }
        }
        data[result_at].redefine(&result);
        break;
    }
    }

    return true;
}

bool Instruction::makeResultGlsl(DataView& data, unsigned location, unsigned result_at) const noexcept(false) {
    unsigned data_len = data.getBound();
    // https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.pdf
    // extension opcode at operand[3]
    unsigned ext_opcode = std::get<unsigned>(operands[3].raw);
    bool made = true;

    constexpr unsigned dst_type_at = 0;
    constexpr unsigned src_at = 4;

    switch (ext_opcode) {
    default: {
        std::stringstream err;
        err << "Unknown GLSL opcode: " << ext_opcode;
        throw std::runtime_error(err.str());
    }
    case GLSLstd450Round:  // 1
        TYPICAL_E_UNARY_OP(FLOAT, std::round(a->data.f));
    case GLSLstd450RoundEven: {  // 2
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        UnOp op = [](const Primitive* a) {
            auto whole = a->data.f;
            auto frac = std::abs(std::modf(whole, &whole));
            bool to_trunc;
            if (frac < 0.5)
                to_trunc = true;
            else if (frac > 0.5)
                to_trunc = false;
            else  // Round to nearest even number
                to_trunc = (static_cast<int>(whole) % 2) == 0;

            if (to_trunc)
                return whole;
            else
                return whole + (std::signbit(whole) ? -1.0 : 1.0);
        };
        element_unary_op(DataType::FLOAT, checkRef(src_at, data_len), dst, data, op);
        break;
    }
    case GLSLstd450Trunc:  // 3
        TYPICAL_E_UNARY_OP(FLOAT, std::trunc(a->data.f));
    case GLSLstd450FAbs:  // 4
        TYPICAL_E_UNARY_OP(FLOAT, std::abs(a->data.f));
    case GLSLstd450SAbs:  // 5
        TYPICAL_E_UNARY_OP(INT, (a->data.i > 0) ? a->data.i : -a->data.i);
    case GLSLstd450FSign: {  // 6
        UnOp op = [](const Primitive* a) {
            bool sgnbit = std::signbit(a->data.f);
            return (a->data.f == 0.0) ? (sgnbit ? -0.0 : 0.0) : (sgnbit ? -1.0 : 1.0);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_unary_op(DataType::FLOAT, checkRef(src_at, data_len), dst, data, op);
        break;
    }
    case GLSLstd450SSign:  // 7
        TYPICAL_E_UNARY_OP(INT, std::clamp(a->data.i, static_cast<int64_t>(-1L), static_cast<int64_t>(1L)));
    case GLSLstd450Floor:  // 8
        TYPICAL_E_UNARY_OP(FLOAT, std::floor(a->data.f));
    case GLSLstd450Ceil:  // 9
        TYPICAL_E_UNARY_OP(FLOAT, std::ceil(a->data.f));
    case GLSLstd450Fract:  // 10
        TYPICAL_E_UNARY_OP(FLOAT, a->data.f - std::floor(a->data.f));
    case GLSLstd450Radians:  // 11
        PRECISION_E_UNARY_OP(a * std::numbers::pi / 180.0);
    case GLSLstd450Degrees:  // 12
        PRECISION_E_UNARY_OP(a * 180.0 / std::numbers::pi);
    case GLSLstd450Sin:  // 13
        PRECISION_E_UNARY_OP(std::sin(a));
    case GLSLstd450Cos:  // 14
        PRECISION_E_UNARY_OP(std::cos(a));
    case GLSLstd450Tan:  // 15
        PRECISION_E_UNARY_OP(std::tan(a));
    case GLSLstd450Asin:  // 16
        PRECISION_E_UNARY_OP(std::asin(a));
    case GLSLstd450Acos:  // 17
        PRECISION_E_UNARY_OP(std::acos(a));
    case GLSLstd450Atan:  // 18
        PRECISION_E_UNARY_OP(std::atan(a));
    case GLSLstd450Sinh:  // 19
        PRECISION_E_UNARY_OP(std::sinh(a));
    case GLSLstd450Cosh:  // 20
        PRECISION_E_UNARY_OP(std::cosh(a));
    case GLSLstd450Tanh:  // 21
        PRECISION_E_UNARY_OP(std::tanh(a));
    case GLSLstd450Asinh:  // 22
        PRECISION_E_UNARY_OP(std::asinh(a));
    case GLSLstd450Acosh:  // 23
        PRECISION_E_UNARY_OP(std::acosh(a));
    case GLSLstd450Atanh:  // 24
        PRECISION_E_UNARY_OP(std::atanh(a));
    case GLSLstd450Atan2:  // 25
        TYPICAL_E_BIN_OP(FLOAT, std::atan2(static_cast<float>(a->data.f), static_cast<float>(b->data.f)));
    case GLSLstd450Pow:  // 26
        TYPICAL_E_BIN_OP(FLOAT, std::pow(static_cast<float>(a->data.f), static_cast<float>(b->data.f)));
    case GLSLstd450Exp:  // 27
        PRECISION_E_UNARY_OP(std::exp(a));
    case GLSLstd450Log:  // 28
        PRECISION_E_UNARY_OP(std::log(a));
    case GLSLstd450Exp2:  // 29
        PRECISION_E_UNARY_OP(std::exp2(a));
    case GLSLstd450Log2:  // 30
        PRECISION_E_UNARY_OP(std::log2(a));
    case GLSLstd450Sqrt:  // 31
        TYPICAL_E_UNARY_OP(FLOAT, std::sqrt(a->data.f));
    case GLSLstd450InverseSqrt:  // 32
        TYPICAL_E_UNARY_OP(FLOAT, 1.0 / std::sqrt(a->data.f));
    case GLSLstd450Determinant: {  // 33
        const Type* res_type = getType(dst_type_at, data);
        Value* ret = res_type->construct();
        const Value* matrix = getValue(src_at, data);
        auto result = ArrayMath::determinant(static_cast<const Array&>(*matrix));
        Primitive prim(result);
        ret->copyFrom(prim);
        data[result_at].redefine(ret);
        break;
    }
    case GLSLstd450MatrixInverse: {  // 34
        const Type* res_type = getType(dst_type_at, data);
        Array& ret = static_cast<Array&>(*res_type->construct());
        const auto& matrix = static_cast<const Array&>(*getValue(src_at, data));
        unsigned size = matrix.getSize();

        // Can provide limited support using glm (for 2x2, 3x3, and 4x4). Should cover most cases
        if (size == 2) {
            glm::mat2 mat;
            ArrayMath::value_to_glm<decltype(mat), 2, 2>(matrix, mat);
            decltype(mat) inv = glm::inverse(mat);
            ArrayMath::glm_to_value<decltype(mat), 2, 2>(inv, ret);
        } else if (size == 3) {
            glm::mat3 mat;
            ArrayMath::value_to_glm<decltype(mat), 3, 3>(matrix, mat);
            decltype(mat) inv = glm::inverse(mat);
            ArrayMath::glm_to_value<decltype(mat), 3, 3>(inv, ret);
        } else if (size == 4) {
            glm::mat4 mat;
            ArrayMath::value_to_glm<decltype(mat), 4, 4>(matrix, mat);
            decltype(mat) inv = glm::inverse(mat);
            ArrayMath::glm_to_value<decltype(mat), 4, 4>(inv, ret);
        } else
            throw std::runtime_error("Inverse for matrix sizes other than 2, 3, or 4 currently unsupported!");

        data[result_at].redefine(&ret);
        break;
    }
    case GLSLstd450Modf: {  // 35
        // fraction = modf(input, whole_pointer);
        // OpExtInst %float %23 = %1 Modf %20 %22
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        // whole_pointer is a pointer to a float value which can be modified. The only modifiable values in SPIR-V are
        // variables, so we know whole_pointer should resolve to a float variable
        Value* whole_val;
        constexpr unsigned whole_index = 5;
        if (Variable* found = getVariable(whole_index, data); found != nullptr)
            whole_val = &found->getVal();
        else {
            const Value* found_val = getValue(whole_index, data);
            if (found_val == nullptr)
                throw std::runtime_error("Couldn't resolve Modf whole pointer, which is neither a variable nor value!");
            if (found_val->getType().getBase() != DataType::POINTER)
                throw std::runtime_error("Modf whole pointer found of non-pointer type!");
            const Pointer& whole_ptr = *static_cast<const Pointer*>(found_val);
            Value* head_val = getHeadValue(whole_ptr, data);
            whole_val = whole_ptr.dereference(*head_val);
        }

        Type* dst_type = getType(dst_type_at, data);
        int comp = -1;
        if (dst_type->getBase() == DataType::ARRAY) {
            // verify that whole is also an array type
            if (whole_val->getType().getBase() != DataType::ARRAY)
                throw std::runtime_error("Whole number pointer operand to modf doesn't match the array dest type!");
            comp = 0;
        }

        UnOp op = [&](const Primitive* a) {
            double whole;
            double fract = std::modf(a->data.f, &whole);
            Primitive whole_pr(whole);
            if (comp == -1)
                whole_val->copyFrom(whole_pr);
            else {
                (*static_cast<Array*>(whole_val))[comp]->copyFrom(whole_pr);
                ++comp;
            }
            return fract;
        };

        element_unary_op(DataType::FLOAT, checkRef(src_at, data_len), dst, data, op);
        break;
    }
    case GLSLstd450ModfStruct: {  // 36
        const Value* src1 = data[checkRef(src_at, data_len)].getValue();

        // Operate on a single primitive scalar or array of primitives
        // Note, the logic of this case is very similar to element_unary_op, but it necessarily differs in the output
        // construction, and thus, must be independent.
        const Type& type = src1->getType();
        std::vector<Primitive> prims;
        assert(element_base(*src1) == DataType::FLOAT && "Cannot do ModfStruct operation on non-float input!");

        Value* res = data[checkRef(dst_type_at, data_len)].getType()->construct();
        // According to the spec, res must be an aggregate of fractions and wholes (in that order)
        auto& agg = static_cast<Aggregate&>(*res);

        auto op = [](const Value* input, Value* fract, Value* whole) {
            Primitive f(0.0);
            Primitive w(0.0);
            f.data.f = std::modf(static_cast<const Primitive*>(input)->data.f, &w.data.f);

            fract->copyFrom(f);
            whole->copyFrom(w);
        };

        if (type.getBase() == DataType::ARRAY) {
            const Array& operand = *static_cast<const Array*>(src1);
            unsigned asize = operand.getSize();
            prims.reserve(asize * 2);

            auto& fracts = static_cast<Aggregate&>(*agg[0]);
            auto& wholes = static_cast<Aggregate&>(*agg[1]);

            for (unsigned i = 0; i < asize; ++i)
                op(operand[i], fracts[i], wholes[i]);
        } else
            op(src1, agg[0], agg[1]);

        data[result_at].redefine(res);
        break;
    }
    case GLSLstd450FMin:  // 37
        TYPICAL_E_BIN_OP(FLOAT, std::min(a->data.f, b->data.f));
    case GLSLstd450UMin:  // 38
        TYPICAL_E_BIN_OP(UINT, std::min(a->data.u, b->data.u));
    case GLSLstd450SMin:  // 39
        TYPICAL_E_BIN_OP(INT, std::min(a->data.i, b->data.i));
    case GLSLstd450FMax:  // 40
        TYPICAL_E_BIN_OP(FLOAT, std::max(a->data.f, b->data.f));
    case GLSLstd450UMax:  // 41
        TYPICAL_E_BIN_OP(UINT, std::max(a->data.u, b->data.u));
    case GLSLstd450SMax:  // 42
        TYPICAL_E_BIN_OP(INT, std::max(a->data.i, b->data.i));
    case GLSLstd450FClamp: {  // 43
        TernOp fx = [](const Primitive* x, const Primitive* minVal, const Primitive* maxVal) {
            if (minVal->data.f > maxVal->data.f)
                Console::warn("FClamp undefined since minVal > maxVal!");
            return std::clamp(x->data.f, minVal->data.f, maxVal->data.f);
        };
        E_TERN_OP(FLOAT, fx);
        break;
    }
    case GLSLstd450UClamp: {  // 44
        TernOp fx = [](const Primitive* x, const Primitive* minVal, const Primitive* maxVal) {
            if (minVal->data.u > maxVal->data.u)
                Console::warn("UClamp undefined since minVal > maxVal!");
            return std::clamp(x->data.u, minVal->data.u, maxVal->data.u);
        };
        E_TERN_OP(UINT, fx);
        break;
    }
    case GLSLstd450SClamp: {  // 45
        TernOp fx = [](const Primitive* x, const Primitive* minVal, const Primitive* maxVal) {
            if (minVal->data.i > maxVal->data.i)
                Console::warn("SClamp undefined since minVal > maxVal!");
            return std::clamp(x->data.i, minVal->data.i, maxVal->data.i);
        };
        E_TERN_OP(INT, fx);
        break;
    }
    case GLSLstd450FMix: {  // 46
        //   GLSL's FMix = x * (1 - a) + y * a
        // This is the same as:
        //   std::lerp   = x + a(y - x)
        // However, we cannot use std::lerp because it has NaN edge behavior which isn't described in the SPIR-V spec.
        TernOp fx = [](const Primitive* x, const Primitive* y, const Primitive* a) {
            return x->data.f * (1.0 - a->data.f) + y->data.f * a->data.f;
        };
        E_TERN_OP(FLOAT, fx);
        break;
    }
    // GLSLstd450IMix missing documentation
    case GLSLstd450Step:  // 48
        TYPICAL_E_BIN_OP(FLOAT, ((b->data.f < a->data.f) ? 0.0 : 1.0));
    case GLSLstd450SmoothStep: {  // 49
        TernOp fx = [](const Primitive* lo, const Primitive* hi, const Primitive* x) {
            double t = std::clamp((x->data.f - lo->data.f) / (hi->data.f - lo->data.f), 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        };
        E_TERN_OP(FLOAT, fx);
        break;
    }
    case GLSLstd450Fma: {  // 50
        TernOp fx = [](const Primitive* a, const Primitive* b, const Primitive* c) {
            return (a->data.f * b->data.f) + c->data.f;
        };
        E_TERN_OP(FLOAT, fx);
        break;
    }
    case GLSLstd450PackSnorm2x16:  // 56
    case GLSLstd450PackUnorm2x16:  // 57
    case GLSLstd450PackHalf2x16:  // 58
    {
        // input of vec2 -> 32-bit integer
        std::function<uint32_t(double f)> pack;
        switch (ext_opcode) {
        case GLSLstd450PackSnorm2x16:
            pack = [](double f) {
                double res = std::round(std::clamp(f, -1.0, 1.0) * 32767.0);
                return uint32_t(int16_t(res));
            };
            break;
        case GLSLstd450PackUnorm2x16:
            pack = [](double f) {
                double res = std::round(std::clamp(f, 0.0, 1.0) * 65535.0);
                return uint32_t(uint16_t(res));
            };
            break;
        case GLSLstd450PackHalf2x16:
            pack = [](double f) { return uint32_t(FpConvert::encode_flt16(f)); };
            break;
        default:
            assert(false);  // unhandled case!
            break;
        }
        const auto& input = static_cast<const Array&>(*getValue(src_at, data));
        assert(input.getSize() == 2);
        assert(input.getType().getElement().getBase() == DataType::FLOAT);
        uint32_t res_lo = pack(static_cast<const Primitive*>(input[0])->data.f);
        uint32_t res_hi = pack(static_cast<const Primitive*>(input[1])->data.f);

        const Type* res_type = getType(dst_type_at, data);
        Primitive& ret = static_cast<Primitive&>(*res_type->construct());
        ret.data.u = (res_hi << 16) | res_lo;  // set it raw. Could plausibly be an int, but need to access u
        data[result_at].redefine(&ret);
        break;
    }
    case GLSLstd450UnpackSnorm2x16:  // 60
    case GLSLstd450UnpackUnorm2x16:  // 61
    {
        const Type* res_type = getType(dst_type_at, data);
        Array& ret = static_cast<Array&>(*res_type->construct());
        const auto& input = static_cast<const Primitive&>(*getValue(src_at, data));
        const bool SIGN = (ext_opcode == GLSLstd450UnpackSnorm2x16);
        constexpr unsigned SIZE = 2;

        uint16_t u[SIZE] = {0};
        uint32_t p = input.data.f;
        for (unsigned i = 0; (i < SIZE) && (p > 0); ++i, p >>= 16)
            u[i] = p & 0xFFFF;

        double f[SIZE];
        for (unsigned i = 0; i < SIZE; ++i) {
            if (SIGN) {
                int16_t num = int16_t(u[i]);
                auto tmp = static_cast<double>(num);
                f[i] = std::clamp(tmp / 32767.0, -1.0, 1.0);
            } else {
                auto tmp = static_cast<double>(u[i]);
                f[i] = tmp / 65535.0;
            }
        }

        for (unsigned i = 0; i < SIZE; ++i) {
            Primitive prim(f[i]);
            ret[i]->copyFrom(prim);
        }
        data[result_at].redefine(&ret);
        break;
    }
    case GLSLstd450UnpackHalf2x16: {  // 62
        const Type* res_type = getType(dst_type_at, data);
        Array& ret = static_cast<Array&>(*res_type->construct());
        assert(ret.getSize() == 2);

        const auto& input = static_cast<const Primitive&>(*getValue(src_at, data));
        uint32_t all_bits = input.data.u;
        for (unsigned i = 0; i < 2; ++i, all_bits >>= 16) {
            uint16_t bits = uint16_t(all_bits & 0xFFFF);
            // The spec requires the output to be f floats
            static_cast<Primitive*>(ret[i])->data.f = FpConvert::decode_flt16<double>(bits);
        }
        data[result_at].redefine(&ret);
        break;
    }
    case GLSLstd450UnpackSnorm4x8:  // 63
    case GLSLstd450UnpackUnorm4x8:  // 64
    {
        const Type* res_type = getType(dst_type_at, data);
        Array& ret = static_cast<Array&>(*res_type->construct());
        const auto& input = static_cast<const Primitive&>(*getValue(src_at, data));
        bool SIGN = (ext_opcode == GLSLstd450UnpackSnorm4x8);
        constexpr unsigned SIZE = 4;

        uint8_t u[SIZE] = {0};
        uint32_t p = input.data.f;
        for (unsigned i = 0; (i < SIZE) && (p > 0); ++i, p >>= 8)
            u[i] = p & 0xFF;

        double f[SIZE];
        for (unsigned i = 0; i < SIZE; ++i) {
            if (SIGN) {
                int8_t num = int8_t(u[i]);
                auto tmp = static_cast<double>(num);
                f[i] = std::clamp(tmp / 127.0, -1.0, 1.0);
            } else {
                auto tmp = static_cast<double>(u[i]);
                f[i] = tmp / 255.0;
            }
        }

        for (unsigned i = 0; i < SIZE; ++i) {
            Primitive prim(f[i]);
            ret[i]->copyFrom(prim);
        }
        data[result_at].redefine(&ret);
        break;
    }
    case GLSLstd450Length: {  // 66
        // Like a sqrt of dot product with itself
        const Value& x = *getValue(src_at, data);
        Value* ret = getType(dst_type_at, data)->construct();

        const Type& vec_type = x.getType();
        if (auto base = vec_type.getBase(); base != DataType::ARRAY) {
            assert(base == DataType::FLOAT && "If the operand to Length is scalar, it must be a float!");
            ret->copyFrom(x);
            data[result_at].redefine(ret);
            break;
        }
        assert(vec_type.getElement().getBase() == DataType::FLOAT && "Operand to Length must be a vector of floats!");
        const auto& arr = static_cast<const Array&>(x);

        double total = 0;
        for (unsigned i = 0; i < arr.getSize(); ++i) {
            auto n = static_cast<const Primitive*>(arr[i])->data.f;
            total += n * n;
        }
        if (total != 0)
            total = std::sqrt(total);

        Primitive tot_prim(total);
        ret->copyFrom(tot_prim);
        data[result_at].redefine(ret);
        break;
    }
    case GLSLstd450Distance: {  // 67
        Value* vec_1_val = getValue(src_at, data);
        Value* vec_2_val = getValue(src_at + 1, data);
        assert(vec_1_val->getType() == vec_2_val->getType());

        Type* res_type = getType(dst_type_at, data);

        const Type& vec_type = vec_1_val->getType();
        if (vec_type.getBase() != DataType::ARRAY) {
            assert(vec_type.getBase() == DataType::FLOAT);
            const auto one = static_cast<Primitive*>(vec_1_val)->data.f;
            const auto two = static_cast<Primitive*>(vec_2_val)->data.f;
            Primitive prim(std::sqrt((one - two) * (one - two)));
            Value* res = res_type->construct();
            res->copyFrom(prim);
            data[result_at].redefine(res);
            break;
        }

        if (vec_type.getElement().getBase() != DataType::FLOAT)
            throw std::runtime_error("Vector (in distance calculation) element must have float type!");

        const Array& vec_1 = *static_cast<Array*>(vec_1_val);
        const Array& vec_2 = *static_cast<Array*>(vec_2_val);
        assert(vec_1.getSize() == vec_2.getSize());

        auto sum = 0.0;
        for (unsigned i = 0; i < vec_1.getSize(); ++i) {
            const auto vec_1_i = static_cast<const Primitive*>(vec_1[i])->data.f;
            const auto vec_2_i = static_cast<const Primitive*>(vec_2[i])->data.f;
            const auto diff = vec_1_i - vec_2_i;
            sum += (diff * diff);
        }
        const auto result = std::sqrt(sum);

        Primitive prim(result);
        Value* res = res_type->construct();
        res->copyFrom(prim);
        data[result_at].redefine(res);
        break;
    }
    case GLSLstd450Cross: {  // 68
        std::vector<double> x = Statics::extractVec(getValue(src_at, data), "Cross Operand x", 3);
        std::vector<double> y = Statics::extractVec(getValue(src_at + 1, data), "Cross Operand y", 3);
        Type* res_type = getType(dst_type_at, data);
        Value* res = res_type->construct();

        Array& arr = static_cast<Array&>(*res);
        Primitive tmp(x[1] * y[2] - y[1] * x[2]);
        arr[0]->copyFrom(tmp);
        tmp = Primitive(x[2] * y[0] - y[2] * x[0]);
        arr[1]->copyFrom(tmp);
        tmp = Primitive(x[0] * y[1] - y[0] * x[1]);
        arr[2]->copyFrom(tmp);
        data[result_at].redefine(res);
        break;
    }
    case GLSLstd450Normalize: {  // 69
        Value* vec_val = getValue(src_at, data);
        const Type& vec_type = vec_val->getType();
        Type* res_type = getType(dst_type_at, data);
        if (vec_type.getBase() != DataType::ARRAY) {
            // "Normalize" a scalar value
            assert(vec_type.getBase() == DataType::FLOAT);
            auto single = static_cast<Primitive*>(vec_val)->data.f;
            if (single != 0.0)
                single = 1.0;
            Primitive prim(single);
            Value* res = res_type->construct();
            res->copyFrom(prim);
            data[result_at].redefine(res);
            break;
        }

        const Array& vec = *static_cast<Array*>(vec_val);
        if (vec_type.getElement().getBase() != DataType::FLOAT)
            throw std::runtime_error("Normalize vector element must have float type!");

        unsigned size = vec.getSize();
        double vsize = 0;
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            vsize += vec_e.data.f * vec_e.data.f;
        }
        vsize = std::sqrt(vsize);

        Array& res = static_cast<Array&>(*res_type->construct());
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            auto component = vec_e.data.f;
            if (vsize != 0)
                component /= vsize;
            Primitive created(component);
            res[i]->copyFrom(created);
        }

        data[result_at].redefine(&res);
        break;
    }
    case GLSLstd450FaceForward: {  // 70
        const Type* res_type = getType(dst_type_at, data);
        Value* ret = res_type->construct();

        Value* n_val = getValue(src_at, data);
        Value* i_val = getValue(src_at + 1, data);
        Value* nref_val = getValue(src_at + 2, data);

        // N * (dot(Nref, I) < 0)? +1 : -1
        auto dot_result = ArrayMath::dot(nref_val, i_val);
        double mult = std::signbit(dot_result) ? 1.0 : -1.0;

        if (res_type->getBase() == DataType::FLOAT) {
            // All must be the same type
            assert(n_val->getType().getBase() == DataType::FLOAT);
            assert(i_val->getType().getBase() == DataType::FLOAT);
            assert(nref_val->getType().getBase() == DataType::FLOAT);

            Primitive prim(static_cast<const Primitive*>(n_val)->data.f * mult);
            ret->copyFrom(prim);
        } else {
            assert(n_val->getType().getBase() == DataType::ARRAY);
            assert(i_val->getType().getBase() == DataType::ARRAY);
            assert(nref_val->getType().getBase() == DataType::ARRAY);
            assert(element_base(*n_val) == DataType::FLOAT);
            assert(element_base(*i_val) == DataType::FLOAT);
            assert(element_base(*nref_val) == DataType::FLOAT);

            Array& res = static_cast<Array&>(*ret);
            const auto& n = static_cast<const Array&>(*n_val);

            for (unsigned i = 0; i < res.getSize(); ++i) {
                Primitive prim(static_cast<double>(static_cast<const Primitive*>(n[i])->data.f * mult));
                res[i]->copyFrom(prim);
            }
        }

        data[result_at].redefine(ret);
        break;
    }
    case GLSLstd450Reflect: {  // 71
        Value* incident_val = getValue(src_at, data);
        Value* normal_val = getValue(src_at + 1, data);
        assert(incident_val->getType() == normal_val->getType());

        Type* res_type = getType(dst_type_at, data);

        const Type& vec_type = incident_val->getType();
        if (vec_type.getBase() != DataType::ARRAY) {
            assert(vec_type.getBase() == DataType::FLOAT);
            const double incident = static_cast<Primitive*>(incident_val)->data.f;
            const double normal = static_cast<Primitive*>(normal_val)->data.f;
            Primitive prim(incident - 2 * (normal * incident) * normal);
            Value* res = res_type->construct();
            res->copyFrom(prim);
            data[result_at].redefine(res);
            break;
        }

        if (vec_type.getElement().getBase() != DataType::FLOAT)
            throw std::runtime_error("Vector (in reflect calculation) element must have float type!");

        // Calculate: I - 2 * dot(N, I) * N
        const Array& normal = *static_cast<Array*>(normal_val);
        const Array& incident = *static_cast<Array*>(incident_val);
        double dot_product = ArrayMath::dot(normal, incident);

        //   2 * dot(N, I) * N
        std::vector<double> second_term;
        const double scaled_dot_product = 2.0 * dot_product;
        for (unsigned i = 0; i < normal.getSize(); ++i) {
            const double normal_elem = static_cast<const Primitive*>(normal[i])->data.f;
            second_term.push_back(scaled_dot_product * normal_elem);
        }

        //   I - (2 * dot(N, I) * N)
        std::vector<double> result;
        for (unsigned i = 0; i < incident.getSize(); ++i) {
            const double incident_elem = static_cast<const Primitive*>(incident[i])->data.f;
            result.push_back(incident_elem - second_term[i]);
        }

        // Finished calculations; now store them
        assert(result.size() == incident.getSize());
        Array& res = static_cast<Array&>(*res_type->construct());
        for (unsigned i = 0; i < result.size(); ++i) {
            Primitive prim(result[i]);
            res[i]->copyFrom(prim);
        }

        data[result_at].redefine(&res);
        break;
    }
    case GLSLstd450Refract: {  // 72
        const Type* res_type = getType(dst_type_at, data);
        Value* ret = res_type->construct();

        Value* i_val = getValue(src_at, data);
        Value* n_val = getValue(src_at + 1, data);
        Value* eta_val = getValue(src_at + 2, data);

        // k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I))
        // if k < 0.0: 0.0 in all components.
        // else: eta * I - (eta * dot(N, I) + sqrt(k)) * N

        double dotni = ArrayMath::dot(n_val, i_val);
        assert(eta_val->getType().getBase() == DataType::FLOAT && "Eta in Refract operation must be a scalar float!");
        double eta = static_cast<const Primitive*>(eta_val)->data.f;
        double k = 1.0 - eta * eta * (1.0 - dotni * dotni);
        double etadotsqrtk = (k < 0.0) ? 0.0 : (eta * dotni + std::sqrt(k));

        if (res_type->getBase() == DataType::ARRAY) {
            Array& res = static_cast<Array&>(*ret);
            const auto& i_arr = static_cast<const Array&>(*i_val);
            const auto& n_arr = static_cast<const Array&>(*n_val);

            for (unsigned i = 0; i < res.getSize(); ++i) {
                double element = 0.0;
                if (k >= 0.0) {
                    double first = static_cast<const Primitive*>(i_arr[i])->data.f;
                    double second = static_cast<const Primitive*>(n_arr[i])->data.f;
                    element = (first * eta) - (second * etadotsqrtk);
                }
                Primitive prim(element);
                res[i]->copyFrom(prim);
            }
        } else {
            double res = 0.0;
            if (k >= 0.0) {
                double first = static_cast<const Primitive*>(i_val)->data.f;
                double second = static_cast<const Primitive*>(n_val)->data.f;
                res = (first * eta) - (second * etadotsqrtk);
            }
            Primitive prim(res);
            ret->copyFrom(prim);
        }

        data[result_at].redefine(ret);
        break;
    }
    case GLSLstd450FindILsb: {  // 73
        UnOp op = [](const Primitive* a) {
            uint64_t count = std::countr_zero(a->data.u);
            if (count >= 32)
                return std::bit_cast<uint64_t>(-1LL);
            return count;
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_int_unary_op(checkRef(src_at, data_len), dst, data, op, op);
        break;
    }
    case GLSLstd450FindSMsb: {  // 74
        UnOp op = [](const Primitive* a) {
            int count;
            if (a->data.i < 0)
                count = std::countl_one(a->data.u);
            else
                count = std::countl_zero(a->data.u);
            // At this point, count is in the range [1, 64]. We must translate that into a bit location
            if (count >= 64)
                return std::bit_cast<uint64_t>(-1LL);
            // Now range is [1, 63]
            return static_cast<uint64_t>(63 - count);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_int_unary_op(checkRef(src_at, data_len), dst, data, op, op);
        break;
    }
    case GLSLstd450FindUMsb: {  // 75
        UnOp op = [](const Primitive* a) {
            int count = std::countl_zero(a->data.u);
            if (count >= 64)
                return std::bit_cast<uint64_t>(-1LL);
            // Now range is [0, 63]
            return static_cast<uint64_t>(63 - count);
        };
        OpDst dst {checkRef(dst_type_at, data_len), result_at};
        element_int_unary_op(checkRef(src_at, data_len), dst, data, op, op);
        break;
    }
    }
    return made;
}
#undef TYPICAL_E_BIN_OP
#undef INT_E_BIN_OP
#undef TYPICAL_E_UNARY_OP
#undef E_SHIFT_OP
#undef E_TERN_OP

bool Instruction::makeResultPrintf(DataView& data, unsigned location, unsigned result_at) const noexcept(false) {
    // extension opcode at operand[3]
    unsigned ext_opcode = std::get<unsigned>(operands[3].raw);
    if (ext_opcode != 1)
        throw std::runtime_error("Unsupported (!= 1) debug printf instruction!");

    // Operand 4 should be the format string and all operands after components to that
    // We will use the underlying printf function to match the necessary behavior
    Value* format = getValue(4, data);
    if (format->getType().getBase() != DataType::STRING)
        throw std::runtime_error("Error in printf call! First argument must be the string format specifier!");
    std::string format_string = static_cast<String*>(format)->get();

    unsigned operand = 5;
    unsigned last = 0;
    for (unsigned i = 0; i < format_string.size(); ++i) {
        // Look for %, which denotes the beginning of a inserted operand
        if (format_string[i] == '%') {
            // continue until one of the format specifiers
            bool done = false;
            for (unsigned j = i + 1; j < format_string.size(); ++j) {
                char c = format_string[j];
                done = true;
                switch (c) {
                case '%':
                    // escaped percent character
                    break;
                // Numbers to print
                case 'c':
                case 'd':
                case 'e':
                case 'f':
                case 'i':
                case 'o':
                case 'u':
                case 'x':
                // Print string
                case 's': {
                    if (operands.size() <= operand)
                        throw std::runtime_error("Error in printf call! Format specifier seen without a value!");
                    Value* val = getValue(operand, data);
                    operand++;
                    auto base = val->getType().getBase();
                    std::string now = format_string.substr(last, j - last + 1);
                    const char* cnow = now.c_str();
                    if (c != 's') {
                        if (!Primitive::isPrimitive(base))
                            throw std::runtime_error("Could not cast value in printf call to required type!");
                        Primitive& prim = static_cast<Primitive&>(*val);

                        if (base == DataType::FLOAT)
                            printf(cnow, prim.data.f);
                        else if (base == DataType::UINT || base == DataType::BOOL)
                            printf(cnow, prim.data.u);
                        else {
                            assert(base == DataType::INT);
                            printf(cnow, prim.data.i);
                        }
                    } else {
                        if (base != DataType::STRING)
                            throw std::runtime_error("Could not cast value in printf call to required string!");
                        printf(cnow, static_cast<String*>(val)->get().c_str());
                    }
                    last = j + 1;
                    break;
                }
                default:
                    done = false;
                }

                if (done) {
                    i = j;
                    break;
                }
            }
            if (!done)
                throw std::runtime_error("Malformed printf format string! Value type not found.");
        }
    }
    if (last < format_string.size())
        printf("%s", format_string.substr(last).c_str());

    return true;
}
