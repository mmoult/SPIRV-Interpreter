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

void handleTypes(
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

Spv::Instruction* Spv::Instruction::makeOp(
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
        err << "Cannot use unsupported SPIR-V instruction (" << opcode << ")!";
        throw std::invalid_argument(err.str());
    }
    case spv::OpNop: // 1
    case spv::OpTypeVoid: // 19
    case spv::OpTypeBool: // 20
    case spv::OpConstantTrue: // 41
    case spv::OpConstantFalse: // 42
    case spv::OpFunctionEnd: // 56
    case spv::OpLabel: // 248
    case spv::OpReturn: // 253
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
        to_load.push_back(Type::REF);
        to_load.push_back(Type::REF);
        break;
    case spv::OpTypeRuntimeArray: // 29
    case spv::OpReturnValue: // 254
        to_load.push_back(Type::REF);
        break;
    case spv::OpTypeStruct: // 30
    case spv::OpTypeFunction: // 33
    case spv::OpConstantComposite: // 44
    case spv::OpCompositeConstruct: // 80
    case spv::OpFAdd: // 129
    case spv::OpVectorTimesScalar: // 142
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
    }

    Spv::Instruction& inst = insts.emplace_back(op, has_result, has_type);
    // Create tokens as requested
    unsigned i = 0;

    auto check_limit = [&](const std::string& parse_what) {
        if (i >= words.size()) {
            std::stringstream err;
            err << "Missing words while parsing " << parse_what << "instruction " << opcode << "!";
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
        handleTypes(type, inst.operands, words, i);
    }

    for (unsigned ii = 0; ii < optional.size(); ++ii) {
        const auto& type = optional[ii];
        do {
            if (i >= words.size())
                goto after_optional; // no more needed!

            handleTypes(type, inst.operands, words, i);

        // If on last iteration and repeating, go again (and until no words left)
        } while (ii >= (optional.size() - 1) && repeating);
    }
    after_optional:

    // Verify that there are no extra words
    if (i < words.size()) {
        std::stringstream err;
        err << "Extra words while parsing instruction " << opcode << "!";
        throw std::length_error(err.str());
    }

    return &inst;
}
