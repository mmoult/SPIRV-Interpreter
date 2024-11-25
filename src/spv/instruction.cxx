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
#include "data/manager.h"
export module spv.instruction;
import spv.data.data;
import spv.frame;
import spv.token;
import value.pointer;

export class Instruction {
    spv::Op opcode;
    bool hasResult;
    bool hasResultType;
    std::vector<Token> operands;

    enum class Extension : unsigned {
        GLSL_STD_450 = 0,
        SPV_KHR_RAY_TRACING,
        SPV_KHR_RAY_QUERY,
        NONSEMANTIC_SHADER_DEBUG_INFO,
        NONSEMANTIC_DEBUG_PRINTF,
        INVALID,
    };

    /// @brief Find if a given extension is supported by the interpreter
    /// @param ext_name name of the extension
    /// @return whether the extension is supported
    Extension extensionFromString(const std::string& ext_name) const {
        // Contains only implemented extensions
        static const std::vector<std::string> supported_ext {
            "GLSL.std.450",
            "SPV_KHR_ray_tracing",
            "SPV_KHR_ray_query",
            "NonSemantic.Shader.DebugInfo.100",
            "NonSemantic.DebugPrintf"
        };

        for (unsigned i = 0; i < supported_ext.size(); ++i) {
            if (ext_name == supported_ext[i])
                return static_cast<Extension>(i);
        }
        return Extension::INVALID;
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

    Data& getData(unsigned idx, DataView& data) const {
        return data[checkRef(idx, data.getBound())];
    }
    Type* getType(unsigned idx, DataView& data) const {
        return getData(idx, data).getType();
    }
    Value* getValue(unsigned idx, DataView& data) const {
        return getData(idx, data).getValue();
    }
    Function* getFunction(unsigned idx, DataView& data) const {
        return getData(idx, data).getFunction();
    }
    EntryPoint* getEntryPoint(unsigned idx, DataView& data) const {
        return getData(idx, data).getEntryPoint();
    }
    Variable* getVariable(unsigned idx, DataView& data) const {
        return getData(idx, data).getVariable();
    }

    Value* getHeadValue(const Pointer& pointer, DataView& data) const noexcept(false) {
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

    Value* getFromPointer(unsigned index, DataView& data) const noexcept(false) {
        if (Variable* from = getVariable(index, data); from != nullptr)
            return from->getVal();

        Value* dst_ptr = getValue(index, data);
        if (dst_ptr == nullptr || dst_ptr->getType().getBase() != DataType::POINTER)
            throw std::runtime_error("Need either a variable or pointer!");

        Pointer& pointer = *static_cast<Pointer*>(dst_ptr);
        Value* head = getHeadValue(pointer, data);
        return pointer.dereference(*head);
    }

    bool makeResultGlsl(DataView& data, unsigned location, unsigned result_at) const noexcept(false);
    bool makeResultPrintf(DataView& data, unsigned location, unsigned result_at) const noexcept(false);

    [[nodiscard]] Value* handleImage(
        DataView& data,
        const Value& img,
        const Value* coords,
        unsigned img_qualifier
    ) const;

public:
    Instruction(spv::Op opcode, bool has_result, bool has_result_type)
        : opcode(opcode),
          hasResult(has_result),
          hasResultType(has_result_type) {}

    /// @brief Attempts to create an instruction with the given opcode, reading from the specified words
    /// @param insts the vector of insts to place the instruction in
    /// @param opcode the opcode of the instruction to create
    /// @param words a vector of words which holds the necesary arguments for the instruction
    static void readOp(
        std::vector<Instruction>& insts,
        uint16_t opcode,
        std::vector<uint32_t>& words
    ) noexcept(false);

    /// @brief Lets the instruction add its variable to input and/or output lists
    /// @param data list of data to access for determining the storage class of each variable
    /// @param ins a list of ref indices in data pointing to in variables
    /// @param outs a list of ref indices in data pointing to out variables
    /// @param specs a list of ref indices in data pointing to specialization constants
    /// @param provided a map of input variables. Needed for spec constants
    /// @param entry_point an entry point instruction to get the execution model
    void ioGen(
        DataView& data,
        std::vector<unsigned>& ins,
        std::vector<unsigned>& outs,
        std::vector<unsigned>& specs,
        ValueMap& provided,
        const Instruction& entry_point
    ) const noexcept(false) {
        switch (opcode) {
        case spv::OpSpecConstantTrue:
        case spv::OpSpecConstantFalse:
        case spv::OpSpecConstant:
        case spv::OpSpecConstantComposite:
        case spv::OpVariable:
            break;
        default:
            return;
        }

        const unsigned len = data.getBound();
        Variable& var = *getVariable(1, data);
        unsigned id = std::get<unsigned>(operands[1].raw);

        using SC = spv::StorageClass;
        switch (var.getStorageClass()) {
        case SC::StorageClassPushConstant:
            if (var.isSpecConst()) {
                // Try to find this's value in the map. If not present, we keep the original value.
                std::string name = var.getName();
                if (provided.contains(name))
                    var.setVal(*provided[name]);
                specs.push_back(id);
                break;
            }
            ins.push_back(id);
            break;
        case SC::StorageClassUniformConstant:
            // If the type is an image, then it may have been written to
            // TODO: for a more complete solution, we may need to recursively search the type for any images.
            if (var.getVal()->getType().getBase() == DataType::IMAGE && var.isWritable())
                outs.push_back(id);
            ins.push_back(id);
            break;
        case SC::StorageClassInput:
        case SC::StorageClassShaderRecordBufferKHR:
            ins.push_back(id);
            break;
        case SC::StorageClassUniform:
            ins.push_back(id);
            // Uniforms decorated with Bufferblock was the pre-SPIR-V 1.3 solution for what is now `StorageBuffer`.
            if (var.getVal()->getType().isBufferBlock() && var.isWritable())
                outs.push_back(id);
            break;
        case SC::StorageClassCrossWorkgroup:
        case SC::StorageClassStorageBuffer:
        case SC::StorageClassCallableDataKHR:
        case SC::StorageClassIncomingCallableDataKHR:
        case SC::StorageClassIncomingRayPayloadKHR:
            ins.push_back(id);
            if (var.isWritable())
                outs.push_back(id);
            break;
        case SC::StorageClassOutput:
        case SC::StorageClassRayPayloadKHR:
            outs.push_back(id);
            break;
        case SC::StorageClassHitAttributeKHR: {
            // Make sure <entry_point> is an actual entry point before identifying the execution model
            if (entry_point.opcode != spv::OpEntryPoint)
                throw std::runtime_error("Unsupported execution model for variable with storage class HitAttributeKHR");
            switch (std::get<unsigned>(entry_point.operands[0].raw)) {
                default:
                    throw std::runtime_error("Bad execution model using storage class HitAttributeKHR.");
                case spv::ExecutionModelIntersectionKHR:
                    ins.push_back(id);
                    outs.push_back(id);
                    break;
                case spv::ExecutionModelAnyHitKHR:
                case spv::ExecutionModelClosestHitKHR:
                    ins.push_back(id);
                    break;
            }
            break;
        }
        case SC::StorageClassPrivate:
        case SC::StorageClassFunction:
        case SC::StorageClassWorkgroup:
        default:
            // these aren't used for public interfaces
            break;
        }
    }

    spv::BuiltIn getVarBuiltIn(DataView& data) const {
        // The source of the variable could be OpVariable or some spec const variant.
        Variable* var = data[getResult()].getVariable();
        if (var == nullptr)
            return spv::BuiltIn::BuiltInMax;
        return var->getBuiltIn();
    }

    spv::Op getOpcode() const {
        return opcode;
    }

    const EntryPoint& getEntryPoint(DataView& data) const noexcept(false) {
        assert(opcode == spv::OpEntryPoint);

        // The entry function ref is operand 1
        const EntryPoint* fx = getEntryPoint(1, data);
        if (fx == nullptr)
            throw std::runtime_error("Missing entry function in entry declaration!");
        return *fx;
    }
    EntryPoint& getEntryPoint(DataView& data) {
        assert(opcode == spv::OpEntryPoint);

        // The entry function ref is operand 1
        EntryPoint* fx = getEntryPoint(1, data);
        if (fx == nullptr)
            throw std::runtime_error("Missing entry function in entry declaration!");
        return *fx;
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
        case spv::OpEntryPoint: // 15
        case spv::OpExecutionMode: // 16
        case spv::OpDecorate: // 71
        case spv::OpMemberDecorate: // 72
        case spv::OpExecutionModeId: // 331
            unsigned to_decor = checkRef((opcode == spv::OpEntryPoint)? 1 : 0, data_size);
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
    /// @param queue the decorations to apply
    /// @return whether some result was made. If used as a fallback, this should be true!
    bool makeResult(DataView& data, unsigned location, DecoQueue* queue) const noexcept(false);

    /// @brief whether instruction in non-static sections should make its result statically
    /// Some instructions, such as OpFunction and OpLabel, appear in non-static code sections, but need to
    /// make their results before execution because they depend on location info. Others, such as OpVariable,
    /// must be processed statically so decorations can be applied to them.
    bool isStaticDependent() const {
        return opcode == spv::OpFunction || opcode == spv::OpLabel || opcode == spv::OpVariable;
    }

    /// @brief Executes the instruction with the provided data, frame stack, and verbosity setting.
    /// @param data the data view at the current frame or the global if the frame stack is empty
    /// @param frame_stack holds variables, arguments, return addresses, and program counters
    /// @param verbose whether to print a verbose trace of execution
    /// @return whether the instruction execution blocks the invocation (such as by a barrier)
    bool execute(DataView& data, std::vector<Frame*>& frame_stack, bool verbose) const;

    void print() const;

    /// @brief Returns the result index. If there is none, 0 is returned.
    /// @return the result index
    unsigned getResult() const {
        if (hasResult)
            return std::get<unsigned>(operands[hasResultType? 1: 0].raw);
        return 0;
    }
};
