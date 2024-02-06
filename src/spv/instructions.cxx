module;
#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "../external/spirv.hpp"

import tokens;
import utils;
export module instructions;


export namespace Spv {

    class Instruction {
        spv::Op opcode;
        std::vector<Token> operands;
        bool hasResult;
        bool hasResultType;
        
        static bool parseString(const std::vector<uint32_t>& words, unsigned& i, std::stringstream& str) {

            return false;
        }

        static void handleTypes(const Token::Type& type, Instruction& inst, const std::vector<uint32_t>& words, unsigned& i) {
            uint32_t word = words[i++];

            switch (type) {
            default:
                inst.operands.emplace_back(type, word);
                break;
            case Token::Type::INT:
                inst.operands.emplace_back(std::bit_cast<int32_t>(word));
                break;
            case Token::Type::FLOAT:
                inst.operands.emplace_back(std::bit_cast<float>(word));
                break;
            case Token::Type::STRING: {
                std::stringstream ss;
                parseString(words, --i, ss);
                inst.operands.emplace_back(ss.str());
                break;
            }
            }
        }

    public:
        Instruction(spv::Op opcode, bool has_result, bool has_result_type):
            opcode(opcode),
            hasResult(has_result),
            hasResultType(has_result_type) {}

        static Utils::May<Instruction> makeOp(std::vector<Instruction>& insts, uint16_t opcode, std::vector<uint32_t>& words) {
            // Very first, fetch SPIR-V info for the opcode (and also validate it is real)
            bool has_result;
            bool has_type;
            spv::Op op = static_cast<spv::Op>(opcode);
            if (!spv::HasResultAndType(static_cast<spv::Op>(opcode), &has_result, &has_type))
                return Utils::May<Instruction>::none("Cannot parse invalid SPIR-V opcode!");
            
            // Create token operands from the words available and for the given opcode
            std::vector<Token::Type> to_load;
            std::vector<Token::Type> optional;
            bool repeating = false; // whether the last optional type may be repeated
            switch (op) {
            default: {
                // Unsupported op
                std::stringstream err;
                err << "Cannot use unsupported SPIR-V instruction (" << opcode << ")!";
                return Utils::May<Instruction>::none(err.str());
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
                to_load.push_back(Token::Type::CONST);
                to_load.push_back(Token::Type::UINT);
                optional.push_back(Token::Type::STRING);
                optional.push_back(Token::Type::STRING);
                break;
            case spv::OpName: // 5
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::STRING);
                break;
            case spv::OpExtInstImport: // 11
                to_load.push_back(Token::Type::STRING);
                break;
            case spv::OpEntryPoint: // 15
                to_load.push_back(Token::Type::CONST);
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::STRING);
                optional.push_back(Token::Type::REF);
                repeating = true;
                break;
            case spv::OpExecutionMode: // 16
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::CONST);
                optional.push_back(Token::Type::UINT);
                repeating = true;
                break;
            case spv::OpCapability: // 17
                to_load.push_back(Token::Type::CONST);
                break;
            case spv::OpTypeInt: // 21
                to_load.push_back(Token::Type::UINT);
                to_load.push_back(Token::Type::UINT);
                break;
            case spv::OpTypeFloat: // 22
                to_load.push_back(Token::Type::UINT);
                break;
            case spv::OpTypeVector: // 23
            case spv::OpTypeMatrix: // 24
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::UINT);
                break;
            case spv::OpTypeArray: // 28
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::REF);
                break;
            case spv::OpTypeRuntimeArray: // 29
            case spv::OpReturnValue: // 254
                to_load.push_back(Token::Type::REF);
                break;
            case spv::OpTypeStruct: // 30
            case spv::OpTypeFunction: // 33
            case spv::OpConstantComposite: // 44
                to_load.push_back(Token::Type::REF);
                optional.push_back(Token::Type::REF);
                repeating = true;
                break;
            case spv::OpTypePointer: // 32
                to_load.push_back(Token::Type::CONST);
                to_load.push_back(Token::Type::REF);
                break;
            case spv::OpConstant: // 43
                optional.push_back(Token::Type::UINT);
                repeating = true;
                break;
            case spv::OpVariable: // 59
                to_load.push_back(Token::Type::CONST);
                optional.push_back(Token::Type::REF);
                break;
            case spv::OpLoad: // 61
                to_load.push_back(Token::Type::REF);
                optional.push_back(Token::Type::UINT);
                break;
            case spv::OpStore: // 62
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::REF);
                optional.push_back(Token::Type::UINT);
                break;
            case spv::OpAccessChain: // 65
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::REF);
                optional.push_back(Token::Type::REF);
                repeating = true;
                break;
            case spv::OpDecorate: // 71
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::CONST);
                to_load.push_back(Token::Type::UINT);
                optional.push_back(Token::Type::UINT);
                repeating = true;
                break;
            }

            Instruction& inst = insts.emplace_back(op, has_result, has_type);
            // Create tokens as requested
            unsigned i = 0;

            // If the op has a result type, that comes first
            if (has_type) {
                if (i >= words.size())
                    return Utils::May<Instruction>::none("Missing words while parsing instruction result type!");
                inst.operands.emplace_back(Token::Type::REF, words[i++]);
            }
            // Then the result comes next
            if (has_result) {
                if (i >= words.size())
                    return Utils::May<Instruction>::none("Missing words while parsing instruction result!");
                inst.operands.emplace_back(Token::Type::REF, words[i++]);
            }

            for (const auto& type : to_load) {
                if (i >= words.size())
                    return Utils::May<Instruction>::none("Missing words while parsing instruction!");

                handleTypes(type, inst, words, i);
            }

            for (unsigned ii = 0; ii < optional.size(); ++ii) {
                const auto& type = optional[ii];
                do {
                    if (i >= words.size())
                        goto after_optional; // no more needed!

                    handleTypes(type, inst, words, i);

                // If on last iteration and repeating, go again (and until no words left)
                } while (ii >= (optional.size() - 1) && repeating);
            }
            after_optional:

            // Verify that there are no extra words
            if (i < words.size())
                return Utils::May<Instruction>::none("Extra words while parsing instruction!");

            return Utils::May<Instruction>::some(inst);
        }

    };
};
