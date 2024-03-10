module;
#include <cassert>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../external/spirv.hpp"

import data;
import frame;
import instructions;
import value;
export module program;

export namespace Spv {
    class Program {
        uint8_t* buffer;
        int length;
        /// @brief endianness of the program. true = big, false = little
        bool endian;
        int idx;

        std::vector<Instruction> insts;
        unsigned entry;

        // An list of disparate data entries, where length == bound. Each entry can be:
        std::vector<Data> data;
        std::vector<unsigned> ins;
        std::vector<unsigned> outs;


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

        void printVariables(std::stringstream& dst, const std::vector<unsigned>& vars) const {
            bool first = true;
            for (const auto v : vars) {
                if (first)
                    first = false;
                else
                    dst << '\n';
                auto var = data[v].getVariable();
                var->print(dst);
            }
        }

    public:
        Program(): buffer(nullptr), length(0), endian(false), idx(0) {}

        ~Program() {
            // The program manages the data, so it must clear on destruction
            for (Data& dat : data)
                dat.clear();
        } 
        Program(const Program& other) = delete;
        Program& operator=(const Program& other) = delete;

        void parse(uint8_t* buffer, int length) noexcept(false) {
            this->buffer = buffer;
            this->length = length;

#define REQUIRE(COND, MSG) \
    if (!(COND)) \
        throw std::runtime_error(MSG);

            REQUIRE(determineEndian(), "Corrupted binary! Magic number missing.");
            REQUIRE(skip(2), "Corrupted binary! Version and/or generator missing.");

            uint32_t bound;
            if (!getWord(bound))
                throw std::runtime_error("Corrupted binary! Missing bound.");
            std::fill_n(std::back_inserter(data), bound, Data());

            REQUIRE(skip(1), "Corrupted binary! Missing reserved word.");

            bool entry_found = false;
            std::vector<unsigned> decorations;
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

                Instruction& inst = *(Instruction::makeOp(insts, opcode, words));
                unsigned location = insts.size() - 1;

                // silently ignore all but the first entry found
                if (inst.isEntry() && !entry_found) {
                    entry_found = true;
                    entry = location;
                }

                // Process the instruction as necessary
                // If it is a decoration (ie modifies a type not yet defined), save it for later
                if (inst.isDecoration())
                    decorations.push_back(location);
                else // If it has a result, let it save itself in the data vector
                    inst.makeResult(data, location);
            }

            REQUIRE(entry_found, "Missing entry function in SPIR-V source!");
            insts[entry].ioGen(data, ins, outs);

            // Finally, after all necessary data should exist, apply all decoration instructions
            for (unsigned dec_i : decorations)
                insts[dec_i].applyDecoration(data);
#undef REQUIRE
        }

        void setup(ValueMap& provided) noexcept(false) {
            // First, create a list of variables needed as inputs
            std::vector<Variable*> inputs;
            for (const auto in : ins) {
                auto var = data[in].getVariable();
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

        bool checkOutputs(ValueMap& checks) const noexcept(true) {
            // First, create a list of variables from outputs
            std::vector<const Variable*> outputs;
            for (const auto out : outs) {
                auto var = data[out].getVariable();
                // var already checked not null in ioGen
                outputs.push_back(var);
            }

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
                            if (!dummy->equals(*var_val)) {
                                delete dummy;
                                return false;
                            }
                        } catch(const std::exception& e) {
                            if (dummy != nullptr)
                                delete dummy;
                            return false;
                        }
                        // Remove the interface from the compare list
                        outputs.erase(outputs.begin() + i);
                        --i;
                        break;
                    } else
                        continue; // this isn't a match, try next
                }

                if (!found)
                    return false;
            }

            // At this point, all outputs should be removed. If not, there are more outputs than in the check file
            // (which means the output is not equal to the check)
            return outputs.empty();
        }

        void execute(bool verbose) noexcept(false) {
            Instruction& entry_inst = insts[entry];
            unsigned start = entry_inst.getEntryStart(data);

            // SPIR-V forbids recursion (either direct or indirect), so we don't have to keep a stack frame for locals
            // However, we will simulate a stack frame for holding temporaries (args and returns) and pc
            std::vector<Frame> frame_stack;
            std::vector<Value*> entry_args;
            frame_stack.emplace_back(start + 1, entry_args, 0);
            while (!frame_stack.empty()) {
                unsigned i_at = frame_stack.back().getPC();
                if (i_at >= insts.size())
                    throw std::runtime_error("Program execution left program's boundaries!");
                insts[i_at].execute(data, frame_stack, verbose);
            }
        }

        void printInputs(std::stringstream& dst) const {
            printVariables(dst, ins);
        }
        void printOutputs(std::stringstream& dst) const {
            printVariables(dst, outs);
        }
    };
};
