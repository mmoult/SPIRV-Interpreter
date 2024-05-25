/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits> // for nan
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "../external/spirv.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
module instruction;
import data;
import frame;
import token;
import value.aggregate;
import value.pointer;
import value.primitive;

const std::vector<unsigned>* find_request(Spv::Instruction::DecoQueue* queue, unsigned at) {
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

void Spv::Instruction::applyVarDeco(Spv::Instruction::DecoQueue* queue, Variable& var, unsigned result_at) const {
    if (const auto* decorations = find_request(queue, result_at); decorations != nullptr) {
        for (auto location : *decorations) {
            const Spv::Instruction& deco = queue->insts[location];
            switch (deco.opcode) {
            case spv::OpDecorate: { // 71
                // OpDecorate THIS Field
                assert(deco.operands[1].type == Spv::Token::Type::CONST);
                auto field = static_cast<spv::Decoration>(std::get<unsigned>(deco.operands[1].raw));
                // The SpecId decoration can serve as a name if the name is not present
                switch (field) {
                case spv::Decoration::DecorationSpecId:
                    if (var.getName().empty()) {
                        unsigned spec_id = std::get<unsigned>(deco.operands[2].raw);
                        var.setName(std::to_string(spec_id));
                    }
                    break;
                default:
                    break;
                }
                break;
            }
            case spv::OpName: { // 5
                assert(deco.operands[1].type == Spv::Token::Type::STRING);
                std::string name = std::get<std::string>(deco.operands[1].raw);
                var.setName(name);
            }
            default:
                break; // other decorations should not occur
            }
        }
    }
}

/**
 * Multiplies the two primitives, x and y, of unknown type, although their type must be the same. Returns the value of
 * the same type.
 */
Primitive multiply_same(const Primitive& x, const Primitive& y) {
    switch (x.getType().getBase()) {
    case DataType::FLOAT:
        return Primitive(x.data.fp32 * y.data.fp32);
    case DataType::UINT:
        return Primitive(x.data.u32 * y.data.u32);
    case DataType::INT:
        return Primitive(x.data.i32 * y.data.i32);
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
        x.data.fp32 += y.data.fp32;
        break;
    case DataType::UINT:
        x.data.u32 += y.data.u32;
        break;
    case DataType::INT:
        x.data.i32 += y.data.i32;
        break;
    default:
        throw std::invalid_argument("Can only multiply primitives of type float, uint, or int!");
    }
}

struct OpSrc {
    DataType type;
    unsigned val1;
    unsigned val2;
};
struct OpDst {
    unsigned type;
    unsigned at;
};
template<typename F> // (const Primitive*, const Primitive* -> data primitive)
void element_bin_op(const OpSrc& srcs, const OpDst& dst, std::vector<Data>& data, F&& op) {
    const Value* src1 = data[srcs.val1].getValue();
    const Value* src2 = data[srcs.val2].getValue();

    // Operate on two primitive arrays or two primitive scalars
    const Type& type1 = src1->getType();
    const Type& type2 = src2->getType();
    if (!type1.sameBase(type2))
        throw std::runtime_error("Cannot use operands of different bases!");
    std::vector<Primitive> prims;
    std::vector<const Value*> pprims;

    if (type1.getBase() == DataType::ARRAY) {
        if (type1.getElement().getBase() != srcs.type)
            throw std::runtime_error("Cannot do binary operation on other-typed arrays!");
        const Array& op1 = *static_cast<const Array*>(src1);
        const Array& op2 = *static_cast<const Array*>(src2);
        if (op1.getSize() != op2.getSize())
            throw std::runtime_error("Cannot do binary operation on arrays of different size!");
        unsigned asize = op1.getSize();
        prims.reserve(asize);
        pprims.reserve(asize);
        for (unsigned i = 0; i < asize; ++i) {
            auto result = op(
                static_cast<const Primitive*>(op1[i]),
                static_cast<const Primitive*>(op2[i])
            );
            prims.emplace_back(result);
            pprims.push_back(&prims[i]);
        }
    } else {
        if (type1.getBase() != srcs.type)
            throw std::runtime_error("Cannot do binary operation on other-typed elements!");
        const Primitive* op1 = static_cast<const Primitive*>(src1);
        const Primitive* op2 = static_cast<const Primitive*>(src2);
        auto result = op(op1, op2);
        prims.emplace_back(result);
        pprims.push_back(&prims[0]);
    }

    Value* res = data[dst.type].getType()->construct(pprims);
    data[dst.at].redefine(res);
}
template<typename UF, typename IF>
void element_int_bin_op(const OpSrc& srcs, const OpDst& dst, std::vector<Data>& data, UF&& u_op, IF&& i_op) {
    Value* first = data[srcs.val1].getValue();
    const Type& type = first->getType();
    DataType dt = type.getBase();
    if (dt == DataType::ARRAY)
        dt = type.getElement().getBase();
    if (dt != DataType::INT && dt != DataType::UINT)
        throw std::runtime_error("Cannot perform integer-typed binary operation on non-integer base operands!");
    OpSrc src{dt, srcs.val1, srcs.val2};

    if (dt == DataType::UINT)
        element_bin_op(src, dst, data, u_op);
    else
        element_bin_op(src, dst, data, i_op);
}
template<typename F> // (const Primitive*, const Primitive* -> data primitive)
void element_unary_op(const OpSrc& src, const OpDst& dst, std::vector<Data>& data, F&& op) {
    const Value* src1 = data[src.val1].getValue();

    // Operate on a single primitive scalar or array of primitives
    const Type& type = src1->getType();
    std::vector<Primitive> prims;
    std::vector<const Value*> pprims;

    if (type.getBase() == DataType::ARRAY) {
        if (type.getElement().getBase() != src.type)
            throw std::runtime_error("Cannot do unary operation on other-typed array!");
        const Array& operand = *static_cast<const Array*>(src1);
        unsigned asize = operand.getSize();
        prims.reserve(asize);
        pprims.reserve(asize);
        for (unsigned i = 0; i < asize; ++i) {
            auto result = op(static_cast<const Primitive*>(operand[i]));
            prims.emplace_back(result);
            pprims.push_back(&prims[i]);
        }
    } else {
        if (type.getBase() != src.type)
            throw std::runtime_error("Cannot do unary operation on other-typed element!");
        const Primitive* operand = static_cast<const Primitive*>(src1);
        auto result = op(operand);
        prims.emplace_back(result);
        pprims.push_back(&prims[0]);
    }

    Value* res = data[dst.type].getType()->construct(pprims);
    data[dst.at].redefine(res);
}

bool Spv::Instruction::makeResult(
    std::vector<Data>& data,
    unsigned location,
    Spv::Instruction::DecoQueue* queue
) const noexcept(false) {
    if (!hasResult)
        return false; // no result made!

    // Result type comes before result, if present
    unsigned data_len = data.size();
    unsigned result_at = checkRef(hasResultType, data_len);

// Typical element-wise binary operation
#define TYPICAL_E_BIN_OP(E_TYPE, BIN_OP) { \
    OpSrc src{DataType::E_TYPE, checkRef(2, data_len), checkRef(3, data_len)}; \
    OpDst dst{checkRef(0, data_len), result_at}; \
    element_bin_op(src, dst, data, [](const Primitive* a, const Primitive* b) { return BIN_OP; }); \
    break; \
}
// Integer (either signed or unsigned as long as they match) element-wise binary operation
// Spec requires a very specific type of edge behavior where: "
//   The resulting value equals the low-order N bits of the correct result R, where N is the component width and R is
//   computed with enough precision to avoid overflow and underflow.
// ".
// For the time being, we are ignoring this stipulation because checking is slow and well-formed programs are typically
// expected not to overflow or underflow.
#define INT_E_BIN_OP(BIN_OP) { \
    element_int_bin_op( \
        OpSrc{DataType::INT, checkRef(2, data_len), checkRef(3, data_len)}, \
        OpDst{checkRef(0, data_len), result_at}, \
        data, \
        [](const Primitive* a, const Primitive* b) { return a->data.u32 BIN_OP b->data.u32; }, \
        [](const Primitive* a, const Primitive* b) { return a->data.i32 BIN_OP b->data.i32; } \
    ); \
    break; \
}
#define TYPICAL_E_UNARY_OP(E_TYPE, UNARY_OP) { \
    OpSrc src{DataType::E_TYPE, checkRef(2, data_len), 0}; \
    OpDst dst{checkRef(0, data_len), result_at}; \
    element_unary_op(src, dst, data, [](const Primitive* a) { return UNARY_OP; }); \
    break; \
}

    switch (opcode) {
    default: {
        std::stringstream err;
        err << "Cannot make result for unsupported instruction " << printOpcode(opcode) << "!";
        throw std::runtime_error(err.str());
    }
    case spv::OpExtInstImport: { // 11
        // Determine which extension the string represents
        assert(operands[1].type == Token::Type::STRING);
        std::string ext_name = std::get<std::string>(operands[1].raw);
        Extension ext;
        if (ext_name.find("GLSL.std.") == 0)
            ext = Extension::GLSL_STD;
        else {
            std::stringstream err;
            err << "Unsupported extension: " << ext_name;
            throw std::runtime_error(err.str());
        }
        data[result_at].redefine(new Primitive(unsigned(ext)));
        break;
    }
    case spv::OpExtInst: { // 12
        // This is a tricky one because the semantics rely entirely on the extension used
        // First, pull the extension to find where to go next
        Value* val = getValue(2, data);
        if (val->getType().getBase() != DataType::UINT)
            throw std::runtime_error("Corrupted extension information!");
        const Primitive& prim = *static_cast<Primitive*>(val);
        Extension ext = static_cast<Extension>(prim.data.u32);
        switch (ext) {
        case Extension::GLSL_STD:
            makeResultGlsl(data, location, result_at);
            break;
        default:
            assert(false);
        }
        break;
    }
    case spv::OpTypeVoid: // 19
        data[result_at].redefine(new Type(Type::primitive(DataType::VOID)));
        break;
    case spv::OpTypeBool: // 20
        data[result_at].redefine(new Type(Type::primitive(DataType::BOOL)));
        break;
    case spv::OpTypeInt: // 21
        assert(operands[1].type == Token::Type::UINT);
        assert(operands[2].type == Token::Type::UINT);
        data[result_at].redefine(new Type(Type::primitive(
            std::get<unsigned>(operands[2].raw) == 0 ? DataType::UINT : DataType::INT,
            std::get<unsigned>(operands[1].raw)
        )));
        break;
    case spv::OpTypeFloat: // 22
        assert(operands[1].type == Token::Type::UINT);
        data[result_at].redefine(new Type(Type::primitive(DataType::FLOAT,
                std::get<unsigned>(operands[1].raw))));
        break;
    case spv::OpTypeVector:   // 23
    case spv::OpTypeMatrix: { // 24
        // Element type for vectors, Column type for matrices
        // A matrix is an array of columns. This is a little confusing because its "columns" are displayed horizontally
        Type* sub = getType(1, data);
        assert(operands[2].type == Token::Type::UINT);
        data[result_at].redefine(new Type(
                Type::array(std::get<unsigned>(operands[2].raw), *sub)));
        break;
    }
    case spv::OpTypeArray: { // 28
        Type* sub = getType(1, data);
        // Unlike OpTypeVector, the length is stored in an OpConstant
        Primitive& len_val = *static_cast<Primitive*>(getValue(2, data));
        data[result_at].redefine(new Type(
                // The size must be a positive integer, so we can safely pull from u32
                Type::array(len_val.data.u32, *sub)));
        break;
    }
    case spv::OpTypeRuntimeArray: { // 29
        Type* sub = getType(1, data);
        // We use a length of 0 to indicate unknown
        data[result_at].redefine(new Type(Type::array(0, *sub)));
        break;
    }
    case spv::OpTypeStruct: { // 30
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
                case spv::OpName: // 5
                case spv::OpMemberDecorate: // 72
                    break; // not currently needed
                case spv::OpMemberName: { // 6
                    assert(deco.operands[1].type == Token::Type::UINT);
                    unsigned idx = std::get<unsigned>(deco.operands[1].raw);
                    assert(deco.operands[2].type == Token::Type::STRING);
                    std::string name = std::get<std::string>(deco.operands[2].raw);
                    strct->nameMember(idx, name);
                    break;
                }
                default:
                    break; // other decorations should not occur
                }
            }
        }
        data[result_at].redefine(strct);
        break;
    }
    case spv::OpTypePointer: { // 32
        Type* pt_to = getType(2, data);
        assert(operands[1].type == Token::Type::CONST); // storage class we don't need
        data[result_at].redefine(new Type(Type::pointer(*pt_to)));
        break;
    }
    case spv::OpTypeFunction: { // 33
        // OpTypeFunction %return %params...
        Type* ret = getType(1, data);

        // Now cycle through all parameters
        std::vector<Type*> params;
        for (unsigned i = 2; i < operands.size(); ++i) {
            Type* param = getType(i, data);
            params.push_back(param);
        }
        data[result_at].redefine(new Type(Type::function(ret, params)));
        break;
    }
    case spv::OpConstantTrue: // 41
    case spv::OpConstantFalse: // 42
        data[result_at].redefine(new Primitive(opcode == spv::OpConstantTrue));
        break;
    case spv::OpConstant: { // 43
        // integer or floating point constant
        Type* ret = getType(0, data);
        assert(operands[2].type == Token::Type::UINT);
        Primitive* prim = new Primitive(std::get<unsigned>(operands[2].raw));
        prim->cast(*ret);
        data[result_at].redefine(prim);
        break;
    }
    case spv::OpConstantComposite: // 44
    case spv::OpCompositeConstruct: { // 80
        // Can create struct, array/vector, or matrix
        Type* ret = getType(0, data);
        std::vector<const Value*> values;
        // operands 2+ are refs to components
        for (unsigned i = 2; i < operands.size(); ++i) {
            Value* val = getValue(i, data);
            values.push_back(val);
        }
        auto* val = ret->construct(values);
        data[result_at].redefine(val);
        break;
    }
    case spv::OpSpecConstantTrue: // 48
    case spv::OpSpecConstantFalse: { // 49
        // Specialization constants should be constant at compile time. They may have defaults, but their value does not
        // have to match that. They are constant inputs very similar to OpVariable, so much so that we will treat them
        // as such.
        assert(hasResultType);
        // Note: booleans cannot have non-standard precision
        Primitive* default_val = new Primitive(opcode == spv::OpSpecConstantTrue);
        Variable* var = Variable::makeSpecConst(default_val);
        applyVarDeco(queue, *var, result_at);
        data[result_at].redefine(var);
        break;
    }
    case spv::OpSpecConstantOp: { // 52
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
    case spv::OpFunction: { // 54
        assert(operands[2].type == Token::Type::CONST);
        Type* fx_type = getType(3, data);
        auto fx = new Function(fx_type, location);
        if (const auto* decorations = find_request(queue, result_at); decorations != nullptr) {
            for (auto location : *decorations) {
                const Instruction& deco = queue->insts[location];
                switch (deco.opcode) {
                case spv::OpDecorate: // 71
                    break; // not currently needed
                case spv::OpName: { // 5
                    assert(deco.operands[1].type == Token::Type::STRING);
                    std::string name = std::get<std::string>(deco.operands[1].raw);
                    fx->setName(name);
                }
                default:
                    break; // other decorations should not occur
                }
            }
        }
        data[result_at].redefine(fx);
        break;
    }
    case spv::OpVariable: { // 59
        assert(hasResultType);
        Type* var_type = getType(0, data);
        assert(operands[2].type == Token::Type::CONST);
        unsigned storage = std::get<unsigned>(operands[2].raw);

        Variable* var = Variable::makeVariable(static_cast<spv::StorageClass>(storage), *var_type);
        if (operands.size() > 3) { // included default value
            Value* defaultVal = getValue(3, data);
            // defaultVal may be nullptr in a valid shader if it is dynamically generated
            // In that case, wait until execution to set default value
            if (defaultVal != nullptr)
                var->setVal(*defaultVal);
        }
        applyVarDeco(queue, *var, result_at);
        data[result_at].redefine(var);
        break;
    }
    case spv::OpAccessChain: { // 65
        std::vector<unsigned> indices;
        assert(operands[2].type == Token::Type::REF);
        unsigned head = std::get<unsigned>(operands[2].raw);
        for (unsigned i = 3; i < operands.size(); ++i) {
            const Value* at = getValue(i, data);
            if (const auto at_base = at->getType().getBase(); at_base != DataType::UINT
                                                            && at_base != DataType::INT)
                throw std::runtime_error("AccessChain index is not an integer!");
            const Primitive& pat = *static_cast<const Primitive*>(at);
            indices.push_back(pat.data.u32);
        }
        Type* point_to = getType(0, data);
        assert(point_to != nullptr);
        data[result_at].redefine(new Pointer(head, indices, *point_to));
        break;
    }
    case spv::OpVectorShuffle: { // 79
        Value* first = getValue(2, data);
        Value* second = getValue(3, data);
        // both first and second must be arrays
        if (const Type& ft = first->getType();
            !first->getType().sameBase(second->getType()) ||
            ft.getBase() != DataType::ARRAY)
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
        Type* retType = getType(0, data);
        data[result_at].redefine(retType->construct(vals));
        break;
    }
    case spv::OpCompositeExtract: { // 81
        Type* res_type = getType(0, data);
        Value* to_ret = res_type->construct();
        const Value* composite = getValue(2, data);
        for (unsigned i = 3; i < operands.size(); ++i) {
            if (DataType dt = composite->getType().getBase(); dt != DataType::ARRAY && dt != DataType::STRUCT) {
                std::stringstream error;
                error << "Cannot extract from non-composite type!";
                throw std::runtime_error(error.str());
            }
            const Aggregate& agg = *static_cast<const Aggregate*>(composite);
            assert(operands[i].type == Token::Type::UINT);
            auto idx = std::get<unsigned>(operands[i].raw);
            if (idx >= agg.getSize()) {
                std::stringstream error;
                error << "Index " << idx << " beyond the bound of composite (" << agg.getSize() << ")!";
                throw std::runtime_error(error.str());
            }
            composite = agg[idx];
            // Repeat the process for all indices
        }
        to_ret->copyFrom(*composite);
        data[result_at].redefine(to_ret);
        break;
    }
    case spv::OpConvertFToS: // 110
        TYPICAL_E_UNARY_OP(FLOAT, static_cast<uint32_t>(a->data.fp32));
    case spv::OpConvertSToF: // 111
        TYPICAL_E_UNARY_OP(INT, static_cast<float>(a->data.i32));
    case spv::OpFNegate: // 127
        TYPICAL_E_UNARY_OP(FLOAT, -a->data.fp32);
    case spv::OpIAdd: // 128
        INT_E_BIN_OP(+);
    case spv::OpFAdd: // 129
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 + b->data.fp32);
    case spv::OpISub: // 130
        INT_E_BIN_OP(-);
    case spv::OpFSub: // 131
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 - b->data.fp32);
    case spv::OpIMul: // 132
        INT_E_BIN_OP(*);
    case spv::OpFMul: // 133
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 * b->data.fp32);
    case spv::OpFDiv: { // 136
        OpSrc src{DataType::FLOAT, checkRef(2, data_len), checkRef(3, data_len)};
        OpDst dst{checkRef(0, data_len), result_at};
        auto op = [](const Primitive* a, const Primitive* b) {
            // Spec says that the behavior is undefined if divisor is 0
            // We will go with explicit IEE754 because it is a common (and often expected) standard
            if (b->data.fp32 == 0.0) { // divisor is neg or pos zero
                if (std::isnan(a->data.fp32))
                    return a->data.fp32;
                float ret = std::numeric_limits<float>::infinity();
                return (std::signbit(b->data.fp32) != std::signbit(a->data.fp32))? -ret: ret;
            }
            return a->data.fp32 / b->data.fp32;
        };
        element_bin_op(src, dst, data, op);
        break;
    }
    case spv::OpVectorTimesScalar: { // 142
        Value* vec_val = getValue(2, data);
        const Type& vec_type = vec_val->getType();
        if (vec_type.getBase() != DataType::ARRAY)
            throw std::runtime_error("Could not load vector in VectorTimesScalar!");
        const Array& vec = *static_cast<Array*>(vec_val);
        if (vec_type.getElement().getBase() != DataType::FLOAT)
            throw std::runtime_error("Cannot multiply vector with non-float element type!");

        Value* scal_val = getValue(3, data);
        if (scal_val == nullptr || scal_val->getType().getBase() != DataType::FLOAT)
            throw std::runtime_error("Could not load scalar in VectorTimesScalar!");
        const Primitive& scal = *static_cast<Primitive*>(scal_val);

        unsigned size = vec.getSize();
        std::vector<Primitive> floats;
        std::vector<const Value*> pfloats;
        floats.reserve(size);
        pfloats.reserve(size);
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            Primitive& created = floats.emplace_back(0.f);
            created.data.fp32 = vec_e.data.fp32 * scal.data.fp32;
            pfloats.push_back(&floats[i]);
        }

        Type* res_type = getType(0, data);
        Value* res = res_type->construct(pfloats);
        data[result_at].redefine(res);
        break;
    }
    case spv::OpMatrixTimesScalar: { // 143
        Type* res_type = getType(0, data);
        Value* res = res_type->construct();
        Array& mres = *static_cast<Array*>(res);
        const Array& mat = *static_cast<Array*>(getValue(2, data));
        const Primitive& cons = *static_cast<Primitive*>(getValue(3, data));
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
    case spv::OpVectorTimesMatrix: { // 144
        Type* res_type = getType(0, data);
        // V * M
        // (1xA) * (AxB) = (1xB)
        // Vector's "number of components must equal the number of components in each column in Matrix."
        // Rows x Columns -> mat[column][row]
        Value* res = res_type->construct();
        Array& vres = *static_cast<Array*>(res);
        const Array& vec = *static_cast<Array*>(getValue(2, data));
        const Array& mat = *static_cast<Array*>(getValue(3, data));

        //           [3 4 5]   [(0*3 + 1*4 + 2*5)]
        // [0 1 2] * [6 7 8] = [(0*6 + 1*7 + 2*8)]
        unsigned b = vres.getSize();
        unsigned a = vec.getSize();
        for (unsigned i = 0; i < b; ++i) {
            Primitive el(0);
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
    case spv::OpMatrixTimesVector: { // 145
        Type* res_type = getType(0, data);
        // M * V
        // (AxB) * (Bx1) = (Ax1)
        // Vector's "number of components must equal the number of columns in Matrix."
        // Rows x Columns -> mat[column][row]
        Value* res = res_type->construct();
        Array& vres = *static_cast<Array*>(res);
        const Array& mat = *static_cast<Array*>(getValue(2, data));
        const Array& vec = *static_cast<Array*>(getValue(3, data));

        // [0 1]   [6]
        // [2 3] * [7] = [(0*6 + 2*7 + 4*8) (1*6 + 3*7 + 5*8)]
        // [4 5]   [8]
        unsigned b = vec.getSize();
        unsigned a = vres.getSize();
        for (unsigned i = 0; i < b; ++i) {
            const Primitive& vecv = *static_cast<const Primitive*>(vec[i]);
            Primitive el(0);
            for (unsigned j = 0; j < a; ++j) {
                const Array& mcolumn = *static_cast<const Array*>(mat[i]);
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
    case spv::OpMatrixTimesMatrix: { // 146
        Type* res_type = getType(0, data);
        // (AxB) * (BxC) = (AxC)
        // RightMatrix's "number of columns must equal the number of columns in Result Type. Its columns must have the
        // same number of components as the number of columns in LeftMatrix."
        // Rows x Columns -> mat[column][row]
        Value* res = res_type->construct();
        Array& mres = *static_cast<Array*>(res);
        const Array& lmat = *static_cast<Array*>(getValue(2, data));
        const Array& rmat = *static_cast<Array*>(getValue(3, data));
        unsigned a = lmat.getType().getElement().getSize();
        unsigned b = lmat.getSize();
        unsigned c = rmat.getSize();
        for (unsigned i = 0; i < c; ++i) {
            Array& res_column = *static_cast<Array*>(mres[i]);
            for (unsigned j = 0; j < a; ++j) {
                const Array& right_column = *static_cast<const Array*>(rmat[i]);
                Primitive el(0);
                for (unsigned k = 0; k < b; ++k) {
                    // Get (k, j) in left, (i, k) in right
                    const Primitive& rv = *static_cast<const Primitive*>(right_column[k]);
                    const Array& lcolumn = *static_cast<const Array*>(lmat[k]);
                    const Primitive& lv = *static_cast<const Primitive*>(lcolumn[j]);
                    Primitive eli = multiply_same(lv, rv);
                    if (k == 0)
                        el = eli;
                    else
                        accum_same(el, eli);
                }
                Primitive& dst = *static_cast<Primitive*>(res_column[j]);
                dst.copyFrom(el);
            }
        }
        data[result_at].redefine(res);
        break;
    }
    case spv::OpDot: { // 148
        Value* ops[2];
        ops[0] = getValue(2, data);
        ops[1] = getValue(3, data);
        const Array* arr[2];
        // Operands 2 and 3 must be float arrays of the same size
        for (unsigned i = 0; i < 2; ++i) {
            const Type& vec_type = ops[i]->getType();
            if (vec_type.getBase() != DataType::ARRAY) {
                std::stringstream err;
                err << "Operand " << i << " to OpDot must be a vector!";
                throw std::runtime_error(err.str());
            }
            if (vec_type.getElement().getBase() != DataType::FLOAT) {
                std::stringstream err;
                err << "Operand " << i << " to OpDot must be a vector of floats!";
                throw std::runtime_error(err.str());
            }
            arr[i] = static_cast<Array*>(ops[i]);
        }
        // Verify that both arrays have the same size
        unsigned size = arr[0]->getSize();
        if (unsigned osize = arr[1]->getSize(); osize != size) {
            std::stringstream err;
            err << "Cannot perform OpDot on vectors of different sizes! Found sizes " << size;
            err << " and " << osize << ".";
            throw std::runtime_error(err.str());
        }
        float total = 0;
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& n0 = *static_cast<const Primitive*>((*arr[0])[i]);
            const Primitive& n1 = *static_cast<const Primitive*>((*arr[1])[i]);
            total += n0.data.fp32 * n1.data.fp32;
        }
        data[result_at].redefine(new Primitive(total));
        break;
    }
    case spv::OpIsNan: // 156
        TYPICAL_E_UNARY_OP(FLOAT, std::isnan(a->data.fp32));
    case spv::OpIsInf: // 157
        TYPICAL_E_UNARY_OP(FLOAT, std::isinf(a->data.fp32));
    case spv::OpLogicalEqual: // 164
        TYPICAL_E_BIN_OP(BOOL, a->data.b32 == b->data.b32);
    case spv::OpLogicalNotEqual: // 165
        TYPICAL_E_BIN_OP(BOOL, a->data.b32 != b->data.b32);
    case spv::OpLogicalOr: // 166
        TYPICAL_E_BIN_OP(BOOL, a->data.b32 || b->data.b32);
    case spv::OpLogicalAnd: // 167
        TYPICAL_E_BIN_OP(BOOL, a->data.b32 && b->data.b32);
    case spv::OpLogicalNot: // 168
        TYPICAL_E_UNARY_OP(BOOL, !(a->data.b32));
    case spv::OpSelect: { // 169
        Value* condition = getValue(2, data);
        Value* first = getValue(3, data);
        Value* second = getValue(4, data);

        const Type& type = condition->getType();
        DataType dt = type.getBase();
        // Condition must be a scalar or a vector of boolean type
        if (dt != DataType::ARRAY) {
            // Simple case, we can choose between the two options
            assert(dt == DataType::BOOL);
            auto cond = static_cast<Primitive*>(condition);
            Value* to_use = (cond->data.b32)? first : second;
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
                throw std::runtime_error("First option in Select with vector condition must be either vector, array, "
                                         "or struct!");
            const Type& second_type = second->getType();
            if (const auto base = second_type.getBase(); base != DataType::ARRAY && base != DataType::STRUCT)
                throw std::runtime_error("Second option in Select with vector condition must be either vector, array, "
                                         "or struct!");
            const auto& first_agg = *static_cast<Aggregate*>(first);
            if (unsigned size = first_agg.getSize(); size != cond_size) {
                std::stringstream err;
                err << "Size of first option in Select does not match condition! " << size << " vs " << cond_size;
                throw std::runtime_error(err.str());
            }
            const auto& second_agg = *static_cast<Aggregate*>(second);
            if (unsigned size = second_agg.getSize(); size != cond_size) {
                std::stringstream err;
                err << "Size of second option in Select does not match condition! " << size << " vs " << cond_size;
                throw std::runtime_error(err.str());
            }

            std::vector<const Value*> es;
            for (unsigned i = 0; i < cond_size; ++i) {
                const Value* cond_i = cond_arr[i];
                const Primitive& cond_bool = static_cast<const Primitive*>(cond_i);
                es.push_back(cond_bool.data.b32? first_agg[i]: second_agg[i]);
            }

            Type* res_type = getType(0, data);
            Aggregate* result = (res_type->getBase() == DataType::ARRAY)?
                static_cast<Aggregate*>(new Array(res_type->getElement(), cond_size)):
                static_cast<Aggregate*>(new Struct(*res_type));
            result->addElements(es);
            data[result_at].redefine(result);
        }
        break;
    }
    case spv::OpSGreaterThan: // 173
        TYPICAL_E_BIN_OP(INT, a->data.i32 > b->data.i32);
    case spv::OpSGreaterThanEqual: // 175
        TYPICAL_E_BIN_OP(INT, a->data.i32 >= b->data.i32);
    case spv::OpSLessThan: // 177
        TYPICAL_E_BIN_OP(INT, a->data.i32 < b->data.i32);
    case spv::OpSLessThanEqual: // 179
        TYPICAL_E_BIN_OP(INT, a->data.i32 <= b->data.i32);
    case spv::OpFOrdEqual: // 180
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 == b->data.fp32);
    case spv::OpFOrdNotEqual: // 182
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 != b->data.fp32);
    case spv::OpFOrdLessThan: // 184
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 < b->data.fp32);
    case spv::OpFOrdGreaterThan: // 186
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 > b->data.fp32);
    case spv::OpFOrdLessThanEqual: // 188
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 <= b->data.fp32);
    case spv::OpFOrdGreaterThanEqual: // 190
        TYPICAL_E_BIN_OP(FLOAT, a->data.fp32 >= b->data.fp32);
    case spv::OpLabel: // 248
        data[result_at].redefine(new Primitive(location));
        break;
    }
#undef TYPICAL_E_BIN_OP
#undef INT_E_BIN_OP
#undef TYPICAL_E_UNARY_OP

    return true;
}

bool Spv::Instruction::makeResultGlsl(
    std::vector<Data>& data,
    unsigned location,
    unsigned result_at
) const noexcept(false) {
    // https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.pdf
    // extension opcode at operand[3]
    unsigned ext_opcode = std::get<unsigned>(operands[3].raw);
    bool made = true;

    switch (ext_opcode) {
    default: {
        std::stringstream err;
        err << "Unknown GLSL opcode: " << ext_opcode;
        throw std::runtime_error(err.str());
    }
    case 69: { // Normalize
        Value* vec_val = getValue(4, data);
        const Type& vec_type = vec_val->getType();
        if (vec_type.getBase() != DataType::ARRAY)
            throw std::runtime_error("Could not load vector in Normalize!");
        const Array& vec = *static_cast<Array*>(vec_val);
        if (vec_type.getElement().getBase() != DataType::FLOAT)
            throw std::runtime_error("Normalize vector element must have float type!");

        unsigned size = vec.getSize();
        double vsize = 0;
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            vsize += vec_e.data.fp32 * vec_e.data.fp32;
        }
        vsize = std::sqrt(vsize);

        std::vector<Primitive> floats;
        std::vector<const Value*> pfloats;
        floats.reserve(size);
        pfloats.reserve(size);
        for (unsigned i = 0; i < size; ++i) {
            const Primitive& vec_e = *static_cast<const Primitive*>(vec[i]);
            Primitive& created = floats.emplace_back(static_cast<float>(vec_e.data.fp32 / vsize));
            pfloats.push_back(&floats[i]);
        }

        Type* res_type = getType(0, data);
        Value* res = res_type->construct(pfloats);
        data[result_at].redefine(res);
        break;
    }
    }
    return made;
}
