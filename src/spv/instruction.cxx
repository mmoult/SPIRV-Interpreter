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
export module instruction;
import data;
import frame;
import token;
import value.pointer;

export namespace Spv {
class Instruction {
    spv::Op opcode;
    std::vector<Token> operands;
    bool hasResult;
    bool hasResultType;

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
    static Instruction* makeOp(
        std::vector<Instruction>& insts,
        uint16_t opcode,
        std::vector<uint32_t>& words
    ) noexcept(false);

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

    bool queueDecoration(std::vector<Data>& data, unsigned location, DecoQueue& queue) const {
        // If instruction is a decoration, queue it
        switch (opcode) {
        default:
            return false;
        case spv::OpName: // 5
        case spv::OpMemberName: // 6
        case spv::OpDecorate: // 71
        case spv::OpMemberDecorate: // 72
            unsigned to_decor = checkRef(0, data.size());
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
};
}; // namespace Spv
