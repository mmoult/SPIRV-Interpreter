/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_INSTRUCTION_HPP
#define SPV_INSTRUCTION_HPP

#include <cassert>
#include <cstdint>
#include <string>
#include <variant>  // std::get
#include <vector>

#include "../util/spirv.hpp"
#include "../values/pointer.hpp"
#include "../values/type.hpp"
#include "../values/value.hpp"
#include "data/data.hpp"
#include "data/manager.hpp"
#include "frame.hpp"
#include "token.hpp"

class Instruction {
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
    Extension extensionFromString(const std::string& ext_name) const;

    unsigned checkRef(unsigned idx, unsigned len) const noexcept(false);

    inline Data& getData(unsigned idx, DataView& data) const {
        return data[checkRef(idx, data.getBound())];
    }
    inline Type* getType(unsigned idx, DataView& data) const {
        return getData(idx, data).getType();
    }
    inline Value* getValue(unsigned idx, DataView& data) const {
        return getData(idx, data).getValue();
    }
    inline Function* getFunction(unsigned idx, DataView& data) const {
        return getData(idx, data).getFunction();
    }
    inline EntryPoint* getEntryPoint(unsigned idx, DataView& data) const {
        return getData(idx, data).getEntryPoint();
    }
    inline Variable* getVariable(unsigned idx, DataView& data) const {
        return getData(idx, data).getVariable();
    }

    Value* getHeadValue(const Pointer& pointer, DataView& data) const noexcept(false);

    Value* getFromPointer(unsigned index, DataView& data) const noexcept(false);

    bool makeResultGlsl(DataView& data, unsigned location, unsigned result_at) const noexcept(false);
    bool makeResultPrintf(DataView& data, unsigned location, unsigned result_at) const noexcept(false);

    [[nodiscard]] Value*
    handleImage(DataView& data, const Value& img, const Value* coords, unsigned img_qualifier, bool proj = false) const;

public:
    Instruction(spv::Op opcode, bool has_result, bool has_result_type)
        : opcode(opcode), hasResult(has_result), hasResultType(has_result_type) {}

    /// @brief Attempts to create an instruction with the given opcode, reading from the specified words
    /// @param insts the vector of insts to place the instruction in
    /// @param opcode the opcode of the instruction to create
    /// @param words a vector of words which holds the necesary arguments for the instruction
    static void readOp(std::vector<Instruction>& insts, uint16_t opcode, std::vector<uint32_t>& words) noexcept(false);

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
    ) const noexcept(false);

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

    const EntryPoint& getEntryPoint(DataView& data) const noexcept(false);
    EntryPoint& getEntryPoint(DataView& data);

    /// Fetch the shader stage if this instruction is an entry point
    spv::ExecutionModel getShaderStage() const {
        // Make sure <entry_point> is an actual entry point before identifying the execution model
        assert(opcode == spv::OpEntryPoint);
        return static_cast<spv::ExecutionModel>(std::get<unsigned>(operands[0].raw));
    }

    // There may be many decorations, but there are very few instructions which are decorated.
    // Therefore, it is best for space to iterate through a vector of requests, where each request is
    // bound to a single reference, but may have several attached decoration instructions pending.
    struct DecoRequest {
        unsigned toDecorate;
        std::vector<unsigned> pending;

        DecoRequest(unsigned to_deco) : toDecorate(to_deco) {}
    };
    struct DecoQueue : std::vector<DecoRequest> {
        std::vector<Instruction>& insts;

        DecoQueue(std::vector<Instruction>& insts) : insts(insts) {}
    };

    /// @brief The decoration equivalent of makeResult. Saves decoration requests into the queue
    /// @param data_size for checking the reference bounds
    /// @param location the index of this instruction within the program. Used as a back reference since a true pointer
    ///                 (such as using `this`) wouldn't work within a vector container.
    /// @param queue the queue to save into
    /// @return whether this is a decoration instruction
    bool queueDecoration(unsigned data_size, unsigned location, DecoQueue& queue) const;
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
    /// @param use_sbt whether the shader binding table should be used for raytracing interactions
    /// @return whether the instruction execution blocks the invocation (such as by a barrier)
    bool execute(
        DataView& data,
        std::vector<std::vector<Frame*>>& frame_stacks,
        unsigned invocation,
        unsigned num_invocations,
        bool verbose,
        bool use_sbt
    ) const;

    void print() const;

    /// @brief Returns the result index. If there is none, 0 is returned.
    /// @return the result index
    inline unsigned getResult() const {
        if (hasResult)
            return std::get<unsigned>(operands[hasResultType ? 1 : 0].raw);
        return 0;
    }

    /// @brief Returns the result type index. If there is none, 0 is returned.
    /// @return the result type index
    inline unsigned getResultType() const {
        if (hasResultType)
            return std::get<unsigned>(operands[0].raw);
        return 0;
    }

    /// @brief Select a name for the variable. Must be performed after value initialization
    void selectName(Variable& var) const;
};
#endif
