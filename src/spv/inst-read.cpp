/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <bit>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../external/spirv.hpp"
module instruction;
import frame;
import token;

bool parseString(const std::vector<uint32_t>& words, unsigned& i, std::stringstream& str) {
    // UTF-8 encoding with four codepoints per word, 0 terminated
    for (; i < words.size(); ++i) {
        uint32_t word = words[i];
        for (unsigned ii = 0; ii < 4; ++ii) {
            char code = static_cast<char>((word >> (ii * 8)) & 0xff);
            if (code == 0) {
                ++i; // this word was used, queue up the next
                return true;
            }
            str << code;
        }
    }
    return false; // we reached the end of string before expected (no 0 termination)!
}

void handle_type(
    const Spv::Token::Type& type,
    std::vector<Spv::Token>& operands,
    const std::vector<uint32_t>& words,
    unsigned& i
) {
    uint32_t word = words[i++];
    using Type = Spv::Token::Type;

    switch (type) {
    default:
        operands.emplace_back(type, word);
        break;
    case Type::INT:
        operands.emplace_back(std::bit_cast<int32_t>(word));
        break;
    case Type::FLOAT:
        operands.emplace_back(std::bit_cast<float>(word));
        break;
    case Type::STRING: {
        std::stringstream ss;
        parseString(words, --i, ss);
        operands.emplace_back(ss.str());
        break;
    }
    }
}

Spv::Instruction* Spv::Instruction::readOp(
    std::vector<Spv::Instruction>& insts,
    uint16_t opcode,
    std::vector<uint32_t>& words
) noexcept(false) {
    // Very first, fetch SPIR-V info for the opcode (and also validate it is real)
    bool has_result;
    bool has_type;
    spv::Op op = static_cast<spv::Op>(opcode);
    if (!spv::HasResultAndType(static_cast<spv::Op>(opcode), &has_result, &has_type))
        throw std::invalid_argument("Cannot parse invalid SPIR-V opcode!");

    // Create token operands from the words available and for the given opcode
    using Type = Spv::Token::Type;
    std::vector<Type> to_load;
    std::vector<Type> optional;

    // The result and result type will be handled by default (as needed), so do NOT include
    // them in to_load!
    bool repeating = false; // whether the last optional type may be repeated
    switch (op) {
    default: {
        // Unsupported op
        std::stringstream err;
        err << "Cannot use unsupported SPIR-V instruction (" << printOpcode(op) << ")!";
        throw std::invalid_argument(err.str());
    }
    case spv::OpNop: // 1
    case spv::OpTypeVoid: // 19
    case spv::OpTypeBool: // 20
    case spv::OpConstantTrue: // 41
    case spv::OpConstantFalse: // 42
    case spv::OpFunctionParameter: // 55
    case spv::OpFunctionEnd: // 56
    case spv::OpLabel: // 248
    case spv::OpKill: // 252
    case spv::OpReturn: // 253
    case spv::OpTerminateInvocation: // 4416
        // no operands to handle (besides result and type, if present)
        break;
    case spv::OpSource: // 3
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::STRING);
        optional.push_back(Type::STRING);
        break;
    case spv::OpName: // 5
        to_load.push_back(Type::REF);
        to_load.push_back(Type::STRING);
        break;
    case spv::OpMemberName: // 6
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        to_load.push_back(Type::STRING);
        break;
    case spv::OpExtInstImport: // 11
        to_load.push_back(Type::STRING);
        break;
    case spv::OpExtInst: // 12
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::REF);
        repeating = true;
        break;
    case spv::OpMemoryModel: // 14
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::CONST);
        break;
    case spv::OpEntryPoint: // 15
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::STRING);
        optional.push_back(Type::REF);
        repeating = true;
        break;
    case spv::OpExecutionMode: // 16
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::UINT);
        repeating = true;
        break;
    case spv::OpCapability: // 17
        to_load.push_back(Type::CONST);
        break;
    case spv::OpTypeInt: // 21
        to_load.push_back(Type::UINT);
        to_load.push_back(Type::UINT);
        break;
    case spv::OpTypeFloat: // 22
    case spv::OpConstant: // 43
        to_load.push_back(Type::UINT);
        break;
    case spv::OpTypeVector: // 23
    case spv::OpTypeMatrix: // 24
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        break;
    case spv::OpTypeArray: // 28
    case spv::OpIAdd: // 128
    case spv::OpFAdd: // 129
    case spv::OpISub: // 130
    case spv::OpFSub: // 131
    case spv::OpIMul: // 132
    case spv::OpFMul: // 133
    case spv::OpFDiv: // 136
    case spv::OpVectorTimesScalar: // 142
    case spv::OpDot: // 148
    case spv::OpLogicalOr: // 166
    case spv::OpSGreaterThan: // 173
    case spv::OpSGreaterThanEqual: // 175
    case spv::OpSLessThan: // 177
    case spv::OpSLessThanEqual: // 179
    case spv::OpFOrdLessThan: // 184
    case spv::OpFOrdGreaterThan: // 186
    case spv::OpFOrdLessThanEqual: // 188
    case spv::OpFOrdGreaterThanEqual: // 190
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        break;
    case spv::OpTypeRuntimeArray: // 29
    case spv::OpConvertSToF: // 111
    case spv::OpFNegate: // 127
    case spv::OpIsNan: // 156
    case spv::OpIsInf: // 157
    case spv::OpLogicalNot: // 168
    case spv::OpBranch: // 249
    case spv::OpReturnValue: // 254
        to_load.push_back(Type::REF);
        break;
    case spv::OpTypeStruct: // 30
    case spv::OpTypeFunction: // 33
    case spv::OpConstantComposite: // 44
    case spv::OpFunctionCall: // 57
    case spv::OpCompositeConstruct: // 80
        to_load.push_back(Type::REF);
        optional.push_back(Type::REF);
        repeating = true;
        break;
    case spv::OpTypePointer: // 32
    case spv::OpFunction: // 54
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::REF);
        break;
    case spv::OpVariable: // 59
        to_load.push_back(Type::CONST);
        optional.push_back(Type::REF);
        break;
    case spv::OpLoad: // 61
        to_load.push_back(Type::REF);
        optional.push_back(Type::UINT);
        break;
    case spv::OpStore: // 62
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::UINT);
        break;
    case spv::OpAccessChain: // 65
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::REF);
        repeating = true;
        break;
    case spv::OpDecorate: // 71
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::UINT);
        repeating = true;
        break;
    case spv::OpMemberDecorate: // 72
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        to_load.push_back(Type::CONST);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::UINT);
        repeating = true;
        break;
    case spv::OpVectorShuffle: // 79
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::UINT);
        repeating = true;
        break;
    case spv::OpCompositeExtract: // 81
        to_load.push_back(Type::REF);
        to_load.push_back(Type::UINT);
        optional.push_back(Type::UINT);
        repeating = true;
        break;
    case spv::OpSelect: // 169
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        break;
    case spv::OpPhi: // 245
        to_load.push_back(Type::REF); // value
        to_load.push_back(Type::REF); // block
        optional.push_back(Type::REF);
        optional.push_back(Type::REF);
        repeating = true;
        break;
    case spv::OpLoopMerge: // 246
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        optional.push_back(Type::UINT);
        repeating = true;
        break;
    case spv::OpSelectionMerge: // 247
        to_load.push_back(Type::REF);
        to_load.push_back(Type::CONST);
        break;
    case spv::OpBranchConditional: // 250
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        optional.push_back(Type::UINT);
        optional.push_back(Type::UINT);
        break;
    }

    Spv::Instruction& inst = insts.emplace_back(op, has_result, has_type);
    // Create tokens as requested
    unsigned i = 0;

    auto check_limit = [&](const std::string& parse_what) {
        if (i >= words.size()) {
            std::stringstream err;
            err << "Missing words while parsing " << parse_what << "instruction " << printOpcode(op) << "!";
            throw std::length_error(err.str());
        }
    };

    // If the op has a result type, that comes first
    if (has_type) {
        check_limit("result type of ");
        inst.operands.emplace_back(Type::REF, words[i++]);
    }
    // Then the result comes next
    if (has_result) {
        check_limit("result of ");
        inst.operands.emplace_back(Type::REF, words[i++]);
    }

    for (const auto& type : to_load) {
        check_limit("");
        handle_type(type, inst.operands, words, i);
    }

    if (!optional.empty()) {
        // Try optional.
        // If any in optional are present, all in list must exist
        // The list may be repeated if "repeating" is true
        do {
            if (i >= words.size())
                break;

            for (const auto opt_type : optional) {
                check_limit("");
                handle_type(opt_type, inst.operands, words, i);
            }
        } while (repeating);
    }

    // Verify that there are no extra words
    if (i < words.size()) {
        std::stringstream err;
        err << "Extra words while parsing instruction " << printOpcode(op) << "!";
        throw std::length_error(err.str());
    }

    return &inst;
}
