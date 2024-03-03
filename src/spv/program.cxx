module;
#include <cassert>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "../external/spirv.hpp"

import data;
import frame;
import instructions;
import utils;
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

    public:
        Program(): buffer(nullptr), length(0), endian(false), idx(0) {}

        Utils::May<bool> parse(uint8_t* buffer, int length) {
            this->buffer = buffer;
            this->length = length;

#define REQUIRE(cond, e_msg) \
    if (!(cond)) \
        return Utils::unexpected<bool>(e_msg);

            REQUIRE(determineEndian(), "Corrupted binary! Magic number missing.");
            REQUIRE(skip(2), "Corrupted binary! Version and/or generator missing.");

            uint32_t bound;
            REQUIRE(getWord(bound), "Corrupted binary! Missing bound.");
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

                Utils::May<Instruction*> make_inst = Instruction::makeOp(insts, opcode, words);
                REQUIRE(make_inst, make_inst.error());

                Instruction& inst = *make_inst.value();
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

                // If it has a result, let it save itself in the data vector
                else if (auto made_result = inst.makeResult(data, location); !made_result)
                    return Utils::unexpected<bool>(made_result.error());
            }

            REQUIRE(entry_found, "Missing entry function in SPIR-V source!");
            if (auto res = insts[entry].ioGen(data, ins, outs); !res)
                return Utils::unexpected<bool>(res.error());

            // Finally, after all necessary data should exist, apply all decoration instructions
            for (unsigned dec_i : decorations) {
                if (auto apply_dec = insts[dec_i].applyDecoration(data); !apply_dec)
                    return Utils::unexpected<bool>(apply_dec.error());
            }
#undef REQUIRE

            return Utils::expected();
        }

        Utils::May<bool> setup(ValueMap& provided) {
            const unsigned len = data.size();
            // First, create a list of variables needed as inputs
            std::vector<Variable*> inputs;
            for (const auto in : ins) {
                auto [var, valid] = data[in].getVariable();
                assert(valid); // already checked in ioGen
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
                        if (auto copy = var->setVal(*val); !copy)
                            return Utils::unexpected<bool>(copy.error());
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
                    return Utils::unexpected<bool>(err.str());
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
                return Utils::unexpected<bool>(error.str());
            }

            return Utils::expected();
        }

        Utils::May<bool> execute(ValueMap& outputs, bool verbose) {
            Instruction& entry_inst = insts[entry];
            auto es = entry_inst.getEntryStart(data);
            if (!es)
                return Utils::unexpected<bool>(es.error());
            unsigned start = es.value();

            // SPIR-V forbids recursion (either direct or indirect), so we don't have to keep a stack frame for locals
            // However, we will simulate a stack frame for holding temporaries (args and returns) and pc
            std::vector<Frame> frame_stack;
            std::vector<Value*> entry_args;
            frame_stack.emplace_back(start + 1, entry_args, 0);
            while (!frame_stack.empty()) {
                unsigned i_at = frame_stack.back().getPC();
                if (i_at >= insts.size())
                    return Utils::unexpected<bool>("Program execution left program's boundaries!");
                if (const auto res = insts[i_at].execute(data, frame_stack, verbose); !res)
                    return res;
            }
            return Utils::expected();
        }
    };
};
