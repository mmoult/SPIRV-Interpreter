/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
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
export module spv.instruction;
import spv.data;
import spv.frame;
import spv.token;
import value.pointer;

export class Instruction {
    spv::Op opcode;
    bool hasResult;
    bool hasResultType;
    std::vector<Token> operands;

    enum class Extension {
        GLSL_STD,
    };

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

    Value* getHeadValue(const Pointer& pointer, std::vector<Data>& data) const noexcept(false) {
        unsigned start = pointer.getHead();
        // Head can be either a variable or a value
        Value* head = data[start].getValue();
        if (head == nullptr) {
            Variable* head_var = data[start].getVariable();
            if (head_var == nullptr)
                throw std::runtime_error("Pointer head is neither a variable or a value!");
            head = head_var->getVal();
        }
        // In some cases (especially common with HLSL), the head can itself be a pointer. If so, dereference that
        // pointer to get a simple head value:
        if (head != nullptr && head->getType().getBase() == DataType::POINTER) {
            Pointer& pointer = *static_cast<Pointer*>(head);
            head = getHeadValue(pointer, data);
            return pointer.dereference(*head);
        }
        return head;
    }

    Value* getFromPointer(unsigned index, std::vector<Data>& data) const noexcept(false) {
        if (Variable* from = getVariable(index, data); from != nullptr)
            return from->getVal();

        Value* dst_ptr = getValue(index, data);
        if (dst_ptr == nullptr || dst_ptr->getType().getBase() != DataType::POINTER)
            throw std::runtime_error("Need either a variable or pointer!");

        Pointer& pointer = *static_cast<Pointer*>(dst_ptr);
        Value* head = getHeadValue(pointer, data);
        return pointer.dereference(*head);
    }

    bool makeResultGlsl(std::vector<Data>& data, unsigned location, unsigned result_at) const noexcept(false);

    static std::string printOpcode(spv::Op opcode);

public:
    Instruction(spv::Op opcode, bool has_result, bool has_result_type)
        : opcode(opcode),
          hasResult(has_result),
          hasResultType(has_result_type) {}

    /// @brief Attempts to create an instruction with the given opcode, reading from the specified words
    /// @param insts the vector of insts to place the instruction in
    /// @param opcode the opcode of the instruction to create
    /// @param words a vector of words which holds the necesary arguments for the instruction
    /// @return a pointer to the instruction created. This is a convenience, where the pointer returned is the
    ///         last instruction in the insts vector.
    static Instruction* readOp(
        std::vector<Instruction>& insts,
        uint16_t opcode,
        std::vector<uint32_t>& words
    ) noexcept(false);

    /// @brief Lets the instruction add its variable to input and/or output lists
    /// @param data list of data to access for determining the storage class of each variable
    /// @param ins a list of ref indices in data pointing to in variables
    /// @param outs a list of ref indices in data pointing to out variables
    /// @param provided a map of input variables. Needed for spec constants
    void ioGen(
        std::vector<Data>& data,
        std::vector<unsigned>& ins,
        std::vector<unsigned>& outs,
        ValueMap& provided
    ) const noexcept(false) {
        const unsigned len = data.size();
        Variable* var = getVariable(1, data);
        unsigned id = std::get<unsigned>(operands[1].raw);
        assert(var != nullptr);  // should have already been created

        using SC = spv::StorageClass;
        switch (var->getStorageClass()) {
        case SC::StorageClassPushConstant:
            if (var->isSpecConst()) {
                // Try to find this's value in the map. If not present, we keep the original value.
                std::string name = var->getName();
                if (provided.contains(name))
                    var->setVal(*provided[name]);
            }
            [[fallthrough]];
        case SC::StorageClassUniformConstant:
        case SC::StorageClassInput:
            ins.push_back(id);
            break;
        case SC::StorageClassUniform:
        case SC::StorageClassWorkgroup:
        case SC::StorageClassCrossWorkgroup:
        case SC::StorageClassStorageBuffer:
        case SC::StorageClassHitAttributeKHR:  // TODO: does this belong here?
        case SC::StorageClassIncomingRayPayloadKHR:  // TODO: does this belong here?
            ins.push_back(id);
            outs.push_back(id);
            break;
        case SC::StorageClassOutput:
        case SC::StorageClassRayPayloadKHR:  // TODO: does this belong here? Read/write, not shared externally, no initializer
            outs.push_back(id);
            break;
        case SC::StorageClassPrivate:
        case SC::StorageClassFunction:
        default:
            // these aren't used for public interfaces
            break;
        }
    }

    spv::Op getOpcode() const {
        return opcode;
    }

    unsigned getEntryStart(std::vector<Data>& data) const noexcept(false) {
        assert(opcode == spv::OpEntryPoint);

        // The entry function ref is operand 1
        Function* fx = getFunction(1, data);
        if (fx == nullptr)
            throw std::runtime_error("Missing entry function in entry declaration!");
        return fx->getLocation();
    }

    void setLocalSize(unsigned* local_size) {
        assert(opcode == spv::OpExecutionMode);

        if (std::get<unsigned>(operands[1].raw) == spv::ExecutionMode::ExecutionModeLocalSize) {
            for (unsigned i = 2; i < 5; ++i) {
                if (operands.size() > i) {
                    unsigned sz = std::get<unsigned>(operands[i].raw);
                    if (sz > 1) {
                        std::stringstream err;
                        err << "Execution Mode local sizes > 1 currently unsupported! Found: " << sz;
                        throw std::runtime_error(err.str());
                    }
                    local_size[i - 2] = sz;
                } else
                    break;  // default size local component size is 1
            }
        }
    }

    // There may be many decorations, but there are very few instructions which are decorated.
    // Therefore, it is best for space to iterate through a vector of requests, where each request is
    // bound to a single reference, but may have several attached decoration instructions pending.
    struct DecoRequest {
        unsigned toDecorate;
        std::vector<unsigned> pending;

        DecoRequest(unsigned to_deco): toDecorate(to_deco) {}
    };
    struct DecoQueue : std::vector<DecoRequest> {
        std::vector<Instruction>& insts;

        DecoQueue(std::vector<Instruction>& insts): insts(insts) {}
    };

    /// @brief The decoration equivalent of makeResult. Saves decoration requests into the queue
    /// @param data_size for checking the reference bounds
    /// @param location the index of this instruction within the program. Used as a back reference since a true pointer
    ///                 (such as using `this`) wouldn't work within a vector container.
    /// @param queue the queue to save into
    /// @return whether this is a decoration instruction
    bool queueDecoration(unsigned data_size, unsigned location, DecoQueue& queue) const {
        // If instruction is a decoration, queue it
        switch (opcode) {
        default:
            return false;
        case spv::OpName: // 5
        case spv::OpMemberName: // 6
        case spv::OpDecorate: // 71
        case spv::OpMemberDecorate: // 72
            unsigned to_decor = checkRef(0, data_size);
            // Search through the queue to see if the ref already has a request
            unsigned i = 0;
            for (; i < queue.size(); ++i) {
                if (queue[i].toDecorate == to_decor)
                    break;
            }
            if (i == queue.size()) {
                // The request was not found
                queue.emplace_back(to_decor);
            }
            queue[i].pending.push_back(location);
            break;
        }
        return true;
    }
private:
    void applyVarDeco(DecoQueue* queue, Variable& var, unsigned result_at) const;
public:

    /// @brief Create the instruction result from the operands.
    /// This is first called before execution (for static instructions) but is also a fallback during
    /// execution for instructions which have shared behavior / don't distinguish between the two.
    /// @param data the vector of Data objects used by the program
    /// @param location the index of this instruction in the program
    /// @return whether some result was made. If used as a fallback, this should be true!
    bool makeResult(std::vector<Data>& data, unsigned location, DecoQueue* queue) const noexcept(false);

    /// @brief whether instruction in non-static sections should make its result statically
    /// Some instructions, such as OpFunction and OpLabel, appear in non-static code sections, but need to
    /// make their results before execution because they depend on location info. Others, such as OpVariable,
    /// must be processed statically so decorations can be applied to them.
    bool isStaticDependent() const {
        return opcode == spv::OpFunction || opcode == spv::OpLabel || opcode == spv::OpVariable;
    }

    void execute(std::vector<Data>& data, std::vector<Frame>& frame_stack, bool verbose) const;

    void print() const;

    /// @brief Returns the result index. If there is none, 0 is returned.
    /// @return the result index
    unsigned getResult() const {
        if (hasResult)
            return std::get<unsigned>(operands[hasResultType? 1: 0].raw);
        return 0;
    }
};
