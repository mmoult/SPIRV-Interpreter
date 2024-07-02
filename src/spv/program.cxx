/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "data/manager.h"
#include "../external/spirv.hpp"
#include "../values/value.hpp"
export module spv.program;
import format.parse;
import front.debug;
import spv.data.data;
import spv.frame;
import spv.instruction;

export class Program {
    std::vector<Instruction> insts;
    unsigned entry;

    DataManager data;
    // Note: At some future time, we may associate one program with multiple data vectors. Therefore, the program
    // may keep ids, but never data objects directly!
    std::vector<unsigned> ins;
    std::vector<unsigned> outs;

    /// @brief For parsing the program from the binary words. Should identify whether the whole program is valid before
    ///        any instructions are executed.
    class ProgramLoader {
        uint8_t* buffer;
        int length;
        /// @brief endianness of the program. true = big, false = little
        bool endian;
        int idx;

        bool determineEndian() {
            // The first four bytes are the SPIR-V magic number
            // Determines the endianness of the program
            uint32_t magic;
            if (!getWord(magic))
                return false;
            if (magic == spv::MagicNumber)
                return true;
            // If the number fetched didn't match, try reversing the endianness and fetching again
            endian = !endian;
            idx -= 4;
            getWord(magic);
            return magic == spv::MagicNumber;
        }

        bool getWord(uint32_t& res) {
            if (idx + 4 > length)
                return false;

            res = 0;
            if (endian) {
                for (int i = idx + 4; idx < i; ++idx) {
                    res = res << 8;
                    res += buffer[idx];
                }
            } else {
                idx += 3;
                for (int i = 0; i < 4; ++i) {
                    res = res << 8;
                    res += buffer[idx - i];
                }
                ++idx;
            }
            return true;
        }

        /// Skip ahead by delta (in words)
        bool skip(int delta) {
            delta *= 4;
            if (idx + delta >= length)
                return false;
            idx += delta;
            return true;
        }

    public:
        ProgramLoader(uint8_t* buffer, int length): buffer(buffer), length(length), endian(true), idx(0) {}

        uint32_t parse(std::vector<Instruction>& insts) noexcept(false) {
#define REQUIRE(COND, MSG) \
if (!(COND)) \
    throw std::runtime_error(MSG);

            REQUIRE(determineEndian(), "Corrupted binary! Magic number missing.");
            REQUIRE(skip(2), "Corrupted binary! Version and/or generator missing.");

            uint32_t bound;
            if (!getWord(bound))
                throw std::runtime_error("Corrupted binary! Missing bound.");

            REQUIRE(skip(1), "Corrupted binary! Missing reserved word.");

            while (idx < length) {
                // Each instruction is at least 1 word = 32 bits, where:
                // - high bits = word count
                // - low bits = opcode
                uint32_t control;
                REQUIRE(getWord(control), "Corrupted binary! Missing instruction control word.");
                uint16_t word_count = control >> 16;
                REQUIRE(word_count >= 1, "Corrupted binary! Word count for instruction less than 1.");
                uint16_t opcode = control & 0xffff;

                std::vector<uint32_t> words;
                for (; word_count > 1; --word_count) { // first word in count is the control (already parsed)
                    uint32_t word;
                    REQUIRE(getWord(word), "Corrupted binary! Missing data in instruction stream!");
                    words.push_back(word);
                }

                Instruction& inst = *(Instruction::readOp(insts, opcode, words));
            }

#undef REQUIRE
            return bound;
        }
    };

    ValueMap getVariables(const std::vector<unsigned>& vars) const {
        ValueMap ret;
        for (const auto v : vars) {
            const auto var = data.getGlobal()[v].getVariable();
            ret.emplace(var->getName(), var->getVal());
        }
        return ret;
    }

    bool isIOGenCode(uint16_t opcode) const {
        switch (opcode) {
        case spv::OpSpecConstantTrue:
        case spv::OpSpecConstantFalse:
        case spv::OpSpecConstant:
        case spv::OpSpecConstantComposite:
        case spv::OpVariable:
            return true;
        default:
            return false;
        }
    }

public:

    void parse(uint8_t* buffer, int length) noexcept(false) {
        // Delegate parsing to a nested loader class. The loader has some fields which are not needed after parsing.
        // This allows for a cleaner separation of data.
        ProgramLoader load(buffer, length);
        uint32_t bound = load.parse(insts);
        data.setBound(bound);
    }

    void init(ValueMap& provided) noexcept(false) {
        Instruction::DecoQueue decorations(insts);
        bool entry_found = false; // whether the entry instruction has been found
        bool static_ctn = true; // whether we can construct results statically (until first OpFunction)
        DataView& global = data.getGlobal();
        for (unsigned location = 0; location < insts.size(); ++location) {
            Instruction& inst = insts[location];
            auto opcode = inst.getOpcode();

            if (static_ctn || inst.isStaticDependent()) {
                if (opcode == spv::OpFunction) {
                    static_ctn = false;
                    // Static construction is no longer legal at the first non-static
                    // OpFunction is static dependent, so intended fallthrough
                }

                // silently ignore all but the first entry found
                // (I think it is legal to have multiple- maybe add a way to distinguish desired?)
                if (opcode == spv::OpEntryPoint && !entry_found) {
                    entry_found = true;
                    entry = location;
                }

                // Process the instruction as necessary
                // If it has a static result, let it execute now on the data vector
                if (!inst.queueDecoration(data.getBound(), location, decorations)) {
                    inst.makeResult(global, location, &decorations);
                    if (isIOGenCode(opcode) && static_ctn)
                        inst.ioGen(global, ins, outs, provided);
                }
            }
        }

        if (!entry_found)
            throw std::runtime_error("Program is missing entry function!");
    }

    void checkInputs(ValueMap& provided) noexcept(false) {
        // First, create a list of variables needed as inputs
        std::vector<Variable*> inputs;
        DataView& global = data.getGlobal();
        for (const auto in : ins) {
            auto var = global[in].getVariable();
            // var already checked not null in ioGen
            inputs.push_back(var);
        }

        // Next go through variables defined and verify they match needed
        for (const auto& [name, val] : provided) {
            bool found = false;
            // first, find the variable which matches the name
            for (unsigned i = 0; i < inputs.size(); ++i) {
                auto var = inputs[i];
                if (var->getName() == name) {
                    found = true;
                    // If this variable is a specialization constant, we do NOT want to set it now. It must be
                    // clear that the spec const was set initially and is not set again (important in case we have
                    // multiple data maps per program).
                    if (!var->isSpecConst())
                        var->setVal(*val);
                    // Remove the interface from the check list
                    inputs.erase(inputs.begin() + i);
                    --i;
                    break;
                } else
                    continue; // this isn't a match, try next
            }

            if (!found) {
                std::stringstream err;
                err << "Input specifies variable \"" << name << "\" which doesn't exist in the program!";
                throw std::runtime_error(err.str());
            }
        }

        // At this point, all in interfaces should be removed. If not, there are more vars needed not provided
        if (!inputs.empty()) {
            std::stringstream error;
            error << "Missing ";
            const auto missing = inputs.size();
            error << missing;
            if (missing == 1)
                error << " variable";
            else
                error << " variables";
            error << " in setup: ";
            for (unsigned i = 0; i < inputs.size(); ++i) {
                if (i > 0)
                    error << ", ";
                error << inputs[i]->getName();
            }
            error << "!";
            throw std::runtime_error(error.str());
        }
    }

    std::tuple<bool, unsigned> checkOutputs(ValueMap& checks) const noexcept(true) {
        // First, create a list of variables from outputs
        std::vector<const Variable*> outputs;
        const auto& global = data.getGlobal();
        for (const auto out : outs) {
            auto var = global[out].getVariable();
            // var already checked not null in ioGen
            outputs.push_back(var);
        }
        unsigned total_tests = outputs.size();

        // Next go through checks and find the corresponding in outputs
        for (const auto& [name, val] : checks) {
            bool found = false;
            // first, find the variable which matches the name
            for (unsigned i = 0; i < outputs.size(); ++i) {
                auto var = outputs[i];
                if (var->getName() == name) {
                    found = true;
                    // Now is the hard part- we need to compare whether this output matches the check file.
                    // The check file lost some type precision (ie 0.0 -> 0), so we assume outputs are the
                    // standard of type truth, although by definition the check values must be correct.
                    // Therefore, we construct a dummy with the output's type and copy values from the check
                    // into it, then compare for equality.
                    const Value* var_val = var->getVal();
                    const auto& v_type = var_val->getType();
                    Value* dummy;
                    try {
                        dummy = v_type.construct();
                        dummy->copyFrom(*val);
                        bool compare = dummy->equals(*var_val);
                        delete dummy;
                        if (!compare)
                            return std::tuple(false, total_tests);
                    } catch(const std::exception& e) {
                        if (dummy != nullptr)
                            delete dummy;
                        return std::tuple(false, total_tests);
                    }
                    // Remove the interface from the compare list
                    outputs.erase(outputs.begin() + i);
                    --i;
                    break;
                } else
                    continue; // this isn't a match, try next
            }

            if (!found)
                return std::tuple(false, total_tests);
        }

        // At this point, all outputs should be removed. If not, there are more outputs than in the check file
        // (which means the output is not equal to the check)
        return std::tuple(outputs.empty(), total_tests);
    }

    void execute(bool verbose, bool debug, ValueFormat& format) noexcept(false) {
        Debugger debugger(insts, format);
        Instruction& entry_inst = insts[entry];
        unsigned start = entry_inst.getEntryStart(data.getGlobal());

        // SPIR-V forbids recursion (either direct or indirect), so we don't have to keep a stack frame for locals
        // However, we will simulate a stack frame for holding temporaries (args and returns) and pc
        std::vector<Frame*> frame_stack;
        std::vector<const Value*> entry_args;
        frame_stack.push_back(new Frame(start, entry_args, 0, data));
        while (!frame_stack.empty()) {
            auto& cur_frame = *frame_stack.back();
            DataView& cur_data = cur_frame.getData();
            unsigned i_at = cur_frame.getPC();
            if (i_at >= insts.size())
                throw std::runtime_error("Program execution left program's boundaries!");

            // Print the line and invoke the debugger, if enabled
            if (verbose)
                debugger.printLine(i_at);
            if (debug) {
                if (debugger.invoke(i_at, cur_data, frame_stack))
                    break;
            }

            unsigned frame_depth = frame_stack.size();
            insts[i_at].execute(cur_data, frame_stack, verbose);  // execute the line of code

            // print the result if verbose
            if (unsigned result = insts[i_at].getResult();
                    // Print the result's value iff:
                    // - verbose mode is enabled
                    // - the instruction has a result to print
                    // - the instruction didn't add or remove a frame (in which case, the value may be undefined)
                    verbose && result > 0 && frame_stack.size() == frame_depth) {
                debugger.print(result, cur_data);
            }
        }
    }

    ValueMap getInputs() const {
        return getVariables(ins);
    }
    ValueMap getOutputs() const {
        return getVariables(outs);
    }
};
