module;
#include <bit>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "../external/spirv.hpp"

import data;
import tokens;
import utils;
import value;
export module instructions;


export namespace Spv {

    class Instruction {
        spv::Op opcode;
        std::vector<Token> operands;
        bool hasResult;
        bool hasResultType;

        static bool parseString(const std::vector<uint32_t>& words, unsigned& i, std::stringstream& str) {
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

        Utils::May<bool> checkRef(unsigned idx, unsigned len, unsigned& result_at) const {
            assert(operands[idx].type == Token::Type::REF);
            result_at = std::get<unsigned>(operands[idx].raw);
            if (result_at >= len) {
                std::stringstream err;
                err << "Reference found (" << result_at << ") beyond data bound (" << len << ")!";
                return Utils::unexpected<bool>(err.str());
            }
            return Utils::expected();
        }

        Utils::May<bool> getType(unsigned idx, std::vector<Data>& data, Type*& ty) const {
            unsigned at;
            if (auto res = checkRef(idx, data.size(), at); !res)
                return res;
            auto [type, valid] = data[at].getType();
            if (!valid) {
                std::stringstream err;
                err << "%" << at << " is not a type!";
                return Utils::unexpected<bool>(err.str());
            }
            ty = type;
            return Utils::expected();
        }

        Utils::May<bool> getValue(unsigned idx, std::vector<Data>& data, Value*& val) const {
            unsigned at;
            if (auto res = checkRef(idx, data.size(), at); !res)
                return res;
            auto [value, valid] = data[at].getValue();
            if (!valid) {
                std::stringstream err;
                err << "%" << at << " is not a value!";
                return Utils::unexpected<bool>(err.str());
            }
            val = value;
            return Utils::expected();
        }

    public:
        Instruction(spv::Op opcode, bool has_result, bool has_result_type):
            opcode(opcode),
            hasResult(has_result),
            hasResultType(has_result_type) {}

        static Utils::May<Instruction*> makeOp(std::vector<Instruction>& insts, uint16_t opcode, std::vector<uint32_t>& words) {
            // Very first, fetch SPIR-V info for the opcode (and also validate it is real)
            bool has_result;
            bool has_type;
            spv::Op op = static_cast<spv::Op>(opcode);
            if (!spv::HasResultAndType(static_cast<spv::Op>(opcode), &has_result, &has_type))
                return Utils::unexpected<Instruction*>("Cannot parse invalid SPIR-V opcode!");

            // Create token operands from the words available and for the given opcode
            std::vector<Token::Type> to_load;
            std::vector<Token::Type> optional;
            bool repeating = false; // whether the last optional type may be repeated
            switch (op) {
            default: {
                // Unsupported op
                std::stringstream err;
                err << "Cannot use unsupported SPIR-V instruction (" << opcode << ")!";
                return Utils::unexpected<Instruction*>(err.str());
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
            case spv::OpMemoryModel: // 14
                to_load.push_back(Token::Type::CONST);
                to_load.push_back(Token::Type::CONST);
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
            case spv::OpConstant: // 43
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
            case spv::OpFunction: // 54
                to_load.push_back(Token::Type::CONST);
                to_load.push_back(Token::Type::REF);
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
            case spv::OpVectorShuffle: // 79
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::REF);
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
                if (i >= words.size()) {
                    std::stringstream err;
                    err << "Missing words while parsing result type of instruction " << opcode << "!";
                    return Utils::unexpected<Instruction*>(err.str());
                }
                inst.operands.emplace_back(Token::Type::REF, words[i++]);
            }
            // Then the result comes next
            if (has_result) {
                if (i >= words.size()) {
                    std::stringstream err;
                    err << "Missing words while parsing result of instruction " << opcode << "!";
                    return Utils::unexpected<Instruction*>(err.str());
                }
                inst.operands.emplace_back(Token::Type::REF, words[i++]);
            }

            for (const auto& type : to_load) {
                if (i >= words.size()) {
                    std::stringstream err;
                    err << "Missing words while parsing instruction " << opcode << "!";
                    return Utils::unexpected<Instruction*>(err.str());
                }

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
            if (i < words.size()) {
                std::stringstream err;
                err << "Extra words while parsing instruction " << opcode << "!";
                return Utils::unexpected<Instruction*>(err.str());
            }

            return Utils::expected<Instruction*>(&inst);
        }

        bool isEntry() const {
            return opcode == spv::OpEntryPoint;
        }

        /// @brief Sorts input and output variables given by this OpEntry into the provided vectors
        /// @param data list of data to access for determining the storage class of each variable
        /// @param ins a list of ref indices in data pointing to in variables
        /// @param outs a list of ref indices in data pointing to out variables
        /// @return result of generation
        Utils::May<bool> ioGen(std::vector<Data>& data, std::vector<unsigned>& ins, std::vector<unsigned>& outs) {
            assert(isEntry());

            const unsigned len = data.size();
            // Operands 3+ are the interface variables
            for (unsigned i = 3; i < operands.size(); ++i) {
                unsigned id;
                if (auto ref = checkRef(i, len, id); !ref)
                    return Utils::unexpected<bool>(ref.error());
                auto [var, valid] = data[i].getVariable();
                if (!valid) { // interface refs must be variables
                    std::stringstream error;
                    error << "Found interface reference, %" << id << ", which is not a variable!";
                    return Utils::unexpected<bool>(error.str());
                }

                using SC = spv::StorageClass;
                switch (var->getStorageClass()) {
                case SC::StorageClassUniformConstant:
                case SC::StorageClassInput:
                case SC::StorageClassUniform:
                    ins.push_back(id);
                    break;
                case SC::StorageClassOutput:
                    outs.push_back(id);
                    break;
                default:
                    // TODO: it is likely there are other valid storage classes I have omitted above
                    return Utils::unexpected<bool>("Invalid storage class for interface variable!");
                }
            }
            return Utils::expected();
        }

        /// @brief Create the result in pre-parsing.
        /// Note, this is before execution, so instructions which have runtime returns should do nothing here.
        /// @param data the vector of Data objects used by the program
        /// @param location the index of this instruction in the program
        /// @return the result
        Utils::May<bool> makeResult(std::vector<Data>& data, unsigned location) const {
            if (!hasResult)
                return Utils::expected();

            const unsigned len = data.size();
            unsigned result_at;
            // Result type comes before result, if present
            if (auto res = checkRef(hasResultType, len, result_at); !res)
                return res;

            switch (opcode) {
            default:
                return Utils::expected();
            case spv::OpTypeVoid: // 19
                return data[result_at].redefine(new Type(Type::primitive(DataType::VOID)));
            case spv::OpTypeFloat: // 22
                assert(operands[1].type == Token::Type::UINT);
                return data[result_at].redefine(new Type(Type::primitive(DataType::FLOAT,
                        std::get<unsigned>(operands[1].raw))));
            case spv::OpTypeVector: { // 23
                Type* sub;
                if (const auto res = getType(1, data, sub); !res)
                    return res;
                assert(operands[2].type == Token::Type::UINT);
                return data[result_at].redefine(new Type(
                        Type::array(std::get<unsigned>(operands[2].raw), *sub)));
            }
            case spv::OpTypePointer: { // 32
                Type* pt_to;
                if (const auto res = getType(2, data, pt_to); !res)
                    return res;
                assert(operands[1].type == Token::Type::CONST); // storage class we don't need
                return data[result_at].redefine(new Type(Type::pointer(*pt_to)));
            }
            case spv::OpTypeFunction: { // 33
                // OpTypeFunction %return %params...
                Type* ret;
                if (const auto res = getType(1, data, ret); !res)
                    return res;

                // Now cycle through all parameters
                std::vector<Type*> params;
                for (unsigned i = 2; i < operands.size(); ++i) {
                    Type* param;
                    if (const auto res = getType(i, data, param); !res)
                        return res;
                    params.push_back(param);
                }
                return data[result_at].redefine(Data(new Type(Type::function(ret, params))));
            }
            case spv::OpConstant: { // 43
                // integer or floating point constant
                Type* ret;
                if (const auto res = getType(0, data, ret); !res)
                    return res;
                assert(operands[2].type == Token::Type::UINT);
                Primitive* prim = new Primitive(std::get<unsigned>(operands[2].raw));
                prim->cast(*ret);
                return data[result_at].redefine(Data(prim));
            }
            case spv::OpConstantComposite: { // 44
                // Can create struct, array/vector, or matrix
                Type* ret;
                if (const auto res = getType(0, data, ret); !res)
                    return res;
                std::vector<Value*> values;
                // operands 2+ are refs to components
                for (unsigned i = 2; i < operands.size(); ++i) {
                    Value* val;
                    if (const auto res = getValue(i, data, val); !res)
                        return res;
                    values.push_back(val);
                }
                auto res = ret->construct(&values, true);
                if (!res)
                    return Utils::unexpected<bool>(res.error());
                return data[result_at].redefine(Data(res.value()));
            }
            case spv::OpFunction: { // 54
                assert(operands[2].type == Token::Type::CONST);
                Type* fx_type;
                if (const auto res = getType(3, data, fx_type); !res)
                    return res;
                return data[result_at].redefine(Data(new Function(fx_type, location)));
            }
            case spv::OpVariable: { // 59
                assert(hasResultType);
                Type* var_type;
                if (const auto res = getType(0, data, var_type); !res)
                    return res;
                assert(operands[2].type == Token::Type::CONST);
                unsigned storage = std::get<unsigned>(operands[2].raw);
                // TODO default value
                return data[result_at].redefine(Data(new Variable(var_type, static_cast<spv::StorageClass>(storage))));
            }
            case spv::OpLabel: // 248
                return data[result_at].redefine(Data(new Primitive(location)));
            }
        }

        bool isDecoration() const {
            switch (opcode) {
                default:
                    return false;
                case spv::OpName: // 5
                case spv::OpMemberName: // 6
                case spv::OpDecorate: // 71
                case spv::OpMemberDecorate: // 72
                    return true;
            }
        }

        /// @brief Apply this instruction's decoration to the data
        /// @param data the data to apply the decoration to
        /// @return the result
        Utils::May<bool> applyDecoration(std::vector<Data>& data) const {
            switch (opcode) {
                default:
                case spv::OpMemberName: // 6
                case spv::OpMemberDecorate: // 72
                    return Utils::unexpected<bool>("Unimplemented function!");
                case spv::OpName: { // 5
                    unsigned named;
                    if (auto res = checkRef(0, data.size(), named); !res)
                        return res;
                    assert(operands[1].type == Token::Type::STRING);
                    std::string name = std::get<std::string>(operands[1].raw);
                    // We can decorate variables and functions with names
                    if (auto [var, valid] = data[named].getVariable(); valid) {
                        var->setName(name);
                    } else if (auto [fx, valid] = data[named].getFunction(); valid) {
                        fx->setName(name);
                    } else
                        return Utils::unexpected<bool>("Name decoration only legal for variables and functions!");
                    break;
                }
                case spv::OpDecorate: // 71
                    break;
            }
            return Utils::expected();
        }

    };
};
