/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cmath>
#include <cstdint>
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

const std::vector<unsigned>* findRequest(Spv::Instruction::DecoQueue* queue, unsigned at) {
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

bool Spv::Instruction::makeResult(
    std::vector<Data>& data,
    unsigned location,
    Spv::Instruction::DecoQueue* queue
) const noexcept(false) {
    if (!hasResult)
        return false; // no result made!

    // Result type comes before result, if present
    unsigned result_at = checkRef(hasResultType, data.size());

    switch (opcode) {
    default: {
        std::stringstream err;
        err << "Unsupported instruction: " << opcode << "! Cannot make result.";
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
    case spv::OpTypeVector: { // 23
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
    case spv::OpTypeStruct: { // 30
        std::vector<const Type*> fields;
        for (unsigned i = 1; i < operands.size(); ++i) {
            const Type* sub = getType(i, data);
            fields.push_back(sub);
        }
        Type* strct = new Type(Type::structure(fields));
        // Search for any decorations which apply
        if (const auto* decorations = findRequest(queue, result_at); decorations != nullptr) {
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
    case spv::OpFunction: { // 54
        assert(operands[2].type == Token::Type::CONST);
        Type* fx_type = getType(3, data);
        auto fx = new Function(fx_type, location);
        if (const auto* decorations = findRequest(queue, result_at); decorations != nullptr) {
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
        if (const auto* decorations = findRequest(queue, result_at); decorations != nullptr) {
            for (auto location : *decorations) {
                const Instruction& deco = queue->insts[location];
                switch (deco.opcode) {
                case spv::OpDecorate: // 71
                    break; // not currently needed
                case spv::OpName: { // 5
                    assert(deco.operands[1].type == Token::Type::STRING);
                    std::string name = std::get<std::string>(deco.operands[1].raw);
                    var->setName(name);
                }
                default:
                    break; // other decorations should not occur
                }
            }
        }
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
    case spv::OpFAdd: { // 129
        // Adds two float arrays or two floats
        Value* val1 = getValue(2, data);
        Value* val2 = getValue(3, data);
        const Type& type1 = val1->getType();
        const Type& type2 = val2->getType();
        if (!type1.sameBase(type2))
            throw std::runtime_error("Cannot add float operands of different bases!");
        std::vector<Primitive> floats;
        std::vector<const Value*> pfloats;

        if (type1.getBase() == DataType::ARRAY) {
            if (type1.getElement().getBase() != DataType::FLOAT)
                throw std::runtime_error("Cannot use FAdd to add non-float arrays!");
            const Array& op1 = *static_cast<const Array*>(val1);
            const Array& op2 = *static_cast<const Array*>(val2);
            if (op1.getSize() != op2.getSize())
                throw std::runtime_error("Cannot FAdd arrays of different size!");
            unsigned asize = op1.getSize();
            floats.reserve(asize);
            pfloats.reserve(asize);
            for (unsigned i = 0; i < asize; ++i) {
                floats.emplace_back(static_cast<const Primitive*>(op1[i])->data.fp32 + 
                                    static_cast<const Primitive*>(op2[i])->data.fp32);
                pfloats.push_back(&floats[i]);
            }
        } else {
            // Must be a float
            if (type1.getBase() != DataType::FLOAT)
                throw std::runtime_error("Cannot use FAdd to add non-floats!");
            const Primitive& op1 = *static_cast<const Primitive*>(val1);
            const Primitive& op2 = *static_cast<const Primitive*>(val2);
            floats.emplace_back(op1.data.fp32 + op2.data.fp32);
            pfloats.push_back(&floats[0]);
        }

        Type* res_type = getType(0, data);
        Value* res = res_type->construct(pfloats);
        data[result_at].redefine(res);
        break;
    }
    case spv::OpVectorTimesScalar: { // 142
        Value* vec_val = getValue(2, data);
        const Type& vec_type = vec_val->getType();
        if (vec_type.getBase() != DataType::ARRAY)
            throw std::runtime_error("Could not load vector in VectorTimesScalar!");
        const Array& vec = *static_cast<Array*>(vec_val);
        if (vec_type.getElement().getBase() != DataType::FLOAT)
            throw std::runtime_error("Cannot mutliply vector with non-float element type!");

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
    case spv::OpLabel: // 248
        data[result_at].redefine(new Primitive(location));
        break;
    }

    return true;
}

bool Spv::Instruction::makeResultGlsl(std::vector<Data>& data, unsigned location, unsigned result_at) const noexcept(false) {
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
