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
#include <variant>
#include <vector>

#include "../external/spirv.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
export module instruction;
import data;
import frame;
import token;
import value.aggregate;
import value.pointer;
import value.primitive;


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
                to_load.push_back(Token::Type::CONST);
                to_load.push_back(Token::Type::UINT);
                optional.push_back(Token::Type::STRING);
                optional.push_back(Token::Type::STRING);
                break;
            case spv::OpName: // 5
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::STRING);
                break;
            case spv::OpMemberName: // 6
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::UINT);
                to_load.push_back(Token::Type::STRING);
                break;
            case spv::OpExtInstImport: // 11
                to_load.push_back(Token::Type::STRING);
                break;
            case spv::OpExtInst: // 12
                to_load.push_back(Token::Type::REF);
                optional.push_back(Token::Type::UINT);
                repeating = true;
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
            case spv::OpCompositeConstruct: // 80
            case spv::OpFAdd: // 129
            case spv::OpVectorTimesScalar: // 142
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
                optional.push_back(Token::Type::UINT);
                repeating = true;
                break;
            case spv::OpMemberDecorate: // 72
                to_load.push_back(Token::Type::REF);
                to_load.push_back(Token::Type::UINT);
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
            case spv::OpCompositeExtract: // 81
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

        /// @brief Sorts input and output variables given by this OpEntry into the provided vectors
        /// @param data list of data to access for determining the storage class of each variable
        /// @param ins a list of ref indices in data pointing to in variables
        /// @param outs a list of ref indices in data pointing to out variables
        /// @return result of generation
        void ioGen(std::vector<Data>& data, std::vector<unsigned>& ins, std::vector<unsigned>& outs) const noexcept(false) {
            assert(opcode == spv::OpEntryPoint);

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
            assert(opcode == spv::OpEntryPoint);

            // The entry function ref is operand 1
            Function* fx = getFunction(1, data);
            if (fx == nullptr)
                throw std::runtime_error("Missing entry function in entry declaration!");
            return fx->getLocation();
        }

        /// @brief Create the instruction result from the operands.
        /// This is first called before execution (for static instructions) but is also a fallback during
        /// execution for instructions which have shared behavior / don't distinguish between the two.
        /// @param data the vector of Data objects used by the program
        /// @param location the index of this instruction in the program
        /// @return whether some result was made. If used as a fallback, this should be true!
        bool makeResult(std::vector<Data>& data, unsigned location) const noexcept(false) {
            if (!hasResult)
                return false;

            const unsigned len = data.size();
            // Result type comes before result, if present
            unsigned result_at = checkRef(hasResultType, len);
            Data dst_dat;

            switch (opcode) {
            default:
                throw std::runtime_error("Unsupported instruction cannot make result!");
            case spv::OpExtInstImport: // 11
                break; // instruction has no necessary result to construct
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
                data[result_at].redefine(new Type(Type::structure(fields)));
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
                data[result_at].redefine(new Function(fx_type, location));
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
                data[result_at].redefine(var);
                break;
            }
            case spv::OpAccessChain: { // 65
                std::vector<unsigned> indices;
                assert(operands[2].type == Token::Type::REF);
                unsigned head = std::get<unsigned>(operands[2].raw);
                for (unsigned i = 3; i < operands.size(); ++i) {
                    assert(operands[i].type == Token::Type::UINT);
                    unsigned at = std::get<unsigned>(operands[i].raw);
                    indices.push_back(at);
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
                    composite = agg[i];
                    // Repeat the process for all indices
                }
                to_ret->copyFrom(*composite);
                data[result_at].redefine(to_ret);
                break;
            }
            case spv::OpLabel: // 248
                data[result_at].redefine(new Primitive(location));
                break;
            }

            return true;
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

        /// @brief whether instruction in non-static sections should make its result statically
        /// Some instructions, such as OpFunction and OpLabel, appear in non-static code sections, but need to
        /// make their results before execution because they depend on location info. Others, such as OpVariable,
        /// must be processed statically so decorations can be applied to them.
        bool isStaticDependent() const {
            return opcode == spv::OpFunction || opcode == spv::OpLabel || opcode == spv::OpVariable;
        }

        /// @brief Apply this instruction's decoration to the data
        /// @param data the data to apply the decoration to
        /// @return the result
        void applyDecoration(std::vector<Data>& data) const noexcept(false) {
            switch (opcode) {
            default:
            case spv::OpMemberName: { // 6
                Type* strct = getType(0, data);
                assert(strct->getBase() == DataType::STRUCT);
                assert(operands[1].type == Token::Type::UINT);
                unsigned idx = std::get<unsigned>(operands[1].raw);
                assert(operands[2].type == Token::Type::STRING);
                std::string name = std::get<std::string>(operands[2].raw);
                strct->nameMember(idx, name);
                break;
            }
            case spv::OpName: { // 5
                unsigned named = checkRef(0, data.size());
                assert(operands[1].type == Token::Type::STRING);
                std::string name = std::get<std::string>(operands[1].raw);
                // We can decorate variables and functions with names
                if (auto var = data[named].getVariable(); var != nullptr) {
                    var->setName(name);
                } else if (auto fx = data[named].getFunction(); fx != nullptr) {
                    fx->setName(name);
                }
                // The shader can name decorate other things (ex: %gl_PerVertex = OpTypeStruct),
                // but we don't care to handle these cases
                break;
            }
            case spv::OpDecorate: // 71
            case spv::OpMemberDecorate: // 72
                break; // TODO there are some decorations we care about
            }
        }

        void execute(std::vector<Data>& data, std::vector<Frame>& frame_stack, bool verbose) const {
            bool inc_pc = true;
            Frame& frame = frame_stack.back();

            Value* dst_val = nullptr;
            switch (opcode) {
            default:
                // fall back on the makeResult function (no fallback should use location!)
                if (!makeResult(data, 0))
                    throw std::runtime_error("Unsupported instruction execution!");
                // If the instruction did make a result, success! These instructions cannot modify control flow,
                // so assume inc_pc = true
                break;
            case spv::OpFunction: // 54
            case spv::OpLabel: // 248
                break;  // should print for verbose
            case spv::OpFunctionEnd: // 56
                throw std::runtime_error("Missing return before function end!");
            case spv::OpVariable: // 59
                // Since this instruction should be run statically, we can assume the variable already exists
                // All we need to do here is set the default value (in case not set before)
                if (operands.size() > 3) { // included default value
                    Variable* var = getVariable(1, data);
                    Value* defaultVal = getValue(3, data);
                    var->setVal(*defaultVal);
                }
                break;
            case spv::OpLoad: { // 61
                Type* ret_type = getType(0, data);
                dst_val = ret_type->construct();
                // Load from a pointer, which may be a variable
                const Value* from_val;
                if (Variable* from = getVariable(2, data); from != nullptr) {
                    from_val = from->getVal();
                } else {
                    Value* dst_ptr = getValue(2, data);
                    if (dst_ptr == nullptr || dst_ptr->getType().getBase() != DataType::POINTER) {
                        std::stringstream error;
                        error << "Load must read from either a variable or pointer!";
                        throw std::runtime_error(error.str());
                    }
                    Pointer& pointer = *static_cast<Pointer*>(dst_ptr);
                    unsigned start = pointer.getHead();
                    Value* head = data[start].getValue();
                    from_val = pointer.dereference(*head);
                }
                dst_val->copyFrom(*from_val);
                break;
            }
            case spv::OpStore: { // 62
                Value* val = getValue(1, data);
                if (Variable* dst = getVariable(0, data); dst != nullptr)
                    dst->setVal(*val);
                else {
                    // Then the dst should be a pointer
                    Value* dst_ptr = getValue(2, data);
                    if (dst_ptr == nullptr || dst_ptr->getType().getBase() != DataType::POINTER) {
                        std::stringstream error;
                        error << "Store must write to either a variable or pointer!";
                        throw std::runtime_error(error.str());
                    }
                    Pointer& pointer = *static_cast<Pointer*>(dst_ptr);
                    unsigned start = pointer.getHead();
                    Value* head = data[start].getValue();
                    Value* extracted = pointer.dereference(*head);
                    extracted->copyFrom(*val);
                }
                break;
            }
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
