/*
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
#include <variant>
#include <vector>

#include "../external/spirv.hpp"

import data;
import frame;
import tokens;
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

        unsigned checkRef(unsigned idx, unsigned len) const noexcept(false) {
            assert(operands[idx].type == Token::Type::REF);
            auto result_at = std::get<unsigned>(operands[idx].raw);
            if (result_at >= len) {
                std::stringstream err;
                err << "Reference found (" << result_at << ") beyond data bound (" << len << ")!";
                throw new std::runtime_error(err.str());
            }
            return result_at;
        }

        Type* getType(unsigned idx, std::vector<Data>& data) const {
            return data[checkRef(idx, data.size())].getType();
        }
        Value* getValue(unsigned idx, std::vector<Data>& data) const {
            return data[checkRef(idx, data.size())].getValue();
        }
        Function* getFunction(unsigned idx, std::vector<Data>& data) const {
            return data[checkRef(idx, data.size())].getFunction();
        }
        Variable* getVariable(unsigned idx, std::vector<Data>& data) const {
            return data[checkRef(idx, data.size())].getVariable();
        }

    public:
        Instruction(spv::Op opcode, bool has_result, bool has_result_type):
            opcode(opcode),
            hasResult(has_result),
            hasResultType(has_result_type) {}

        /// @brief Attempts to create an instruction with the given opcode, reading from the specified words
        /// @param insts the vector of insts to place the instruction in
        /// @param opcode the opcode of the instruction to create
        /// @param words a vector of words which holds the necesary arguments for the instruction
        /// @return a pointer to the instruction created. This is a convenience, where the pointer returned is the
        ///         last instruction in the insts vector.
        static Instruction* makeOp(
            std::vector<Instruction>& insts,
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
            std::vector<Token::Type> to_load;
            std::vector<Token::Type> optional;
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
                inst.operands.emplace_back(Token::Type::REF, words[i++]);
            }
            // Then the result comes next
            if (has_result) {
                check_limit("result of ");
                inst.operands.emplace_back(Token::Type::REF, words[i++]);
            }

            for (const auto& type : to_load) {
                check_limit("");
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
                throw std::length_error(err.str());
            }

            return &inst;
        }

        bool isEntry() const {
            return opcode == spv::OpEntryPoint;
        }

        /// @brief Sorts input and output variables given by this OpEntry into the provided vectors
        /// @param data list of data to access for determining the storage class of each variable
        /// @param ins a list of ref indices in data pointing to in variables
        /// @param outs a list of ref indices in data pointing to out variables
        /// @return result of generation
        void ioGen(std::vector<Data>& data, std::vector<unsigned>& ins, std::vector<unsigned>& outs) const noexcept(false) {
            assert(isEntry());

            const unsigned len = data.size();
            // Operands 3+ are the interface variables
            for (unsigned i = 3; i < operands.size(); ++i) {
                unsigned id = checkRef(i, len);
                auto* var = data[id].getVariable();
                if (var == nullptr) { // interface refs must be variables
                    std::stringstream error;
                    error << "Found interface reference, %" << id << ", which is not a variable!";
                    throw std::runtime_error(error.str());
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
                    throw std::runtime_error("Invalid storage class for interface variable!");
                }
            }
        }

        unsigned getEntryStart(std::vector<Data>& data) const noexcept(false) {
            assert(isEntry());

            // The entry function ref is operand 1
            Function* fx = getFunction(1, data);
            if (fx == nullptr)
                throw std::runtime_error("Missing entry function in entry declaration!");
            return fx->getLocation();
        }

        /// @brief Create the result in pre-parsing.
        /// Note, this is before execution, so instructions which have runtime returns should do nothing here.
        /// @param data the vector of Data objects used by the program
        /// @param location the index of this instruction in the program
        /// @return the result
        void makeResult(std::vector<Data>& data, unsigned location) const noexcept(false) {
            if (!hasResult)
                return;

            const unsigned len = data.size();
            // Result type comes before result, if present
            unsigned result_at = checkRef(hasResultType, len);

            switch (opcode) {
            default:
                return; // instruction has no necessary&static result to construct
            case spv::OpTypeVoid: // 19
                return data[result_at].redefine(new Type(Type::primitive(DataType::VOID)));
            case spv::OpTypeFloat: // 22
                assert(operands[1].type == Token::Type::UINT);
                return data[result_at].redefine(new Type(Type::primitive(DataType::FLOAT,
                        std::get<unsigned>(operands[1].raw))));
            case spv::OpTypeVector: { // 23
                Type* sub = getType(1, data);
                assert(operands[2].type == Token::Type::UINT);
                return data[result_at].redefine(new Type(
                        Type::array(std::get<unsigned>(operands[2].raw), *sub)));
            }
            case spv::OpTypePointer: { // 32
                Type* pt_to = getType(2, data);
                assert(operands[1].type == Token::Type::CONST); // storage class we don't need
                return data[result_at].redefine(new Type(Type::pointer(*pt_to)));
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
                return data[result_at].redefine(Data(new Type(Type::function(ret, params))));
            }
            case spv::OpConstant: { // 43
                // integer or floating point constant
                Type* ret = getType(0, data);
                assert(operands[2].type == Token::Type::UINT);
                Primitive* prim = new Primitive(std::get<unsigned>(operands[2].raw));
                prim->cast(*ret);
                return data[result_at].redefine(Data(prim));
            }
            case spv::OpConstantComposite: { // 44
                // Can create struct, array/vector, or matrix
                Type* ret = getType(0, data);
                std::vector<const Value*> values;
                // operands 2+ are refs to components
                for (unsigned i = 2; i < operands.size(); ++i) {
                    Value* val = getValue(i, data);
                    values.push_back(val);
                }
                auto* val = ret->construct(values);
                return data[result_at].redefine(Data(val));
            }
            case spv::OpFunction: { // 54
                assert(operands[2].type == Token::Type::CONST);
                Type* fx_type = getType(3, data);
                return data[result_at].redefine(Data(new Function(fx_type, location)));
            }
            case spv::OpVariable: { // 59
                assert(hasResultType);
                Type* var_type = getType(0, data);
                assert(operands[2].type == Token::Type::CONST);
                unsigned storage = std::get<unsigned>(operands[2].raw);

                Variable* var = Variable::makeVariable(static_cast<spv::StorageClass>(storage), *var_type);
                if (operands.size() > 3) { // included default value
                    Value* defaultVal = getValue(3, data);
                    var->setVal(*defaultVal);
                }
                return data[result_at].redefine(Data(var));
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
        void applyDecoration(std::vector<Data>& data) const noexcept(false) {
            switch (opcode) {
            default:
            case spv::OpMemberName: // 6
            case spv::OpMemberDecorate: // 72
                throw std::runtime_error("Unimplemented function!");
            case spv::OpName: { // 5
                unsigned named = checkRef(0, data.size());
                assert(operands[1].type == Token::Type::STRING);
                std::string name = std::get<std::string>(operands[1].raw);
                // We can decorate variables and functions with names
                if (auto var = data[named].getVariable(); var != nullptr) {
                    var->setName(name);
                } else if (auto fx = data[named].getFunction(); fx != nullptr) {
                    fx->setName(name);
                } else
                    throw std::runtime_error("Name decoration only legal for variables and functions!");
                break;
            }
            case spv::OpDecorate: // 71
                break;
            }
        }

        void execute(std::vector<Data>& data, std::vector<Frame>& frame_stack, bool verbose) const {
            bool inc_pc = true;
            Frame& frame = frame_stack.back();

            Value* dst_val = nullptr;
            switch (opcode) {
            default:
                throw std::runtime_error("Unsupported instruction execution!");
                break;
            case spv::OpFunctionEnd: // 56
                throw std::runtime_error("Missing return before function end!");
            case spv::OpLoad: { // 61
                Type* ret_type = getType(0, data);
                dst_val = ret_type->construct();
                // Load from a pointer, which may be a variable
                const Value* from_val;
                if (Variable* from = getVariable(2, data); from != nullptr) {
                    from_val = from->getVal();
                } else {
                    Value* from_v = getValue(2, data);
                    from_val = from_v;
                }
                dst_val->copyFrom(*from_val);
                break;
            }
            case spv::OpStore: { // 62
                Value* val = getValue(1, data);
                Variable* dst = getVariable(0, data);
                dst->setVal(*val);
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
                dst_val = retType->construct(vals);
                break;
            }
            case spv::OpLabel: // 248
                break;  // should print for verbose
            case spv::OpBranch: { // 249
                Value* dst = getValue(0, data);
                Primitive* dst2 = static_cast<Primitive*>(dst);
                frame.setPC(dst2->data.u32);
                inc_pc = false;
                break;
            }
            case spv::OpReturn: // 253
                // verify that the stack didn't expect a return value
                if (frame.hasReturn())
                    throw std::runtime_error("Missing value for function return!");
                frame_stack.pop_back();
                inc_pc = !frame_stack.empty(); // don't increment PC if we are at the end of program
                break;
            }

            if (dst_val != nullptr) {
                assert(operands[1].type == Token::Type::REF);
                auto result_at = std::get<unsigned>(operands[1].raw);
                data[result_at].redefine(dst_val);
            }

            if (inc_pc)
                frame_stack.back().incPC();
        }

    };
};
