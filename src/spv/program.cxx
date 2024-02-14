module;
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "../external/spirv.hpp"

import data;
import instructions;
import utils;
export module program;

export namespace Spv {
    class Program {
        uint8_t* buffer;
        int length;
        /// @brief endianness of the program. true = big, false = little
        bool endian;
        int idx;

        std::vector<Instruction> insts;
        std::vector<unsigned> decorations;
        unsigned entry;

        // An list of disparate data entries, where length == bound. Each entry can be:
        std::vector<Data> data;

        Program(): buffer(nullptr), length(0), endian(false), idx(0) {}

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
        static Utils::May<Program> parse(uint8_t* buffer, int length) {
            Program* program = new Program();
            program->buffer = buffer;
            program->length = length;

            std::string msg;
#define REQUIRE(cond, e_msg) \
    if (!(cond)) { \
        msg = e_msg; \
        goto check_err; \
    }
            bool entry_found = false;

            REQUIRE(program->determineEndian(), "Corrupted binary! Magic number missing.");
            REQUIRE(program->skip(2), "Corrupted binary! Version and/or generator missing.");

            uint32_t bound;
            REQUIRE(program->getWord(bound), "Corrupted binary! Missing bound.");
            std::fill_n(std::back_inserter(program->data), bound, Data());

            REQUIRE(program->skip(1), "Corrupted binary! Missing reserved word.");

            while (program->idx < length) {
                // Each instruction is at least 1 word = 32 bits, where:
                // - high bits = word count
                // - low bits = opcode
                uint32_t control;
                REQUIRE(program->getWord(control), "Corrupted binary! Missing instruction control word.");
                uint16_t word_count = control >> 16;
                REQUIRE(word_count >= 1, "Corrupted binary! Word count for instruction less than 1.");
                uint16_t opcode = control & 0xffff;

                std::vector<uint32_t> words;
                for (; word_count > 1; --word_count) { // first word in count is the control (already parsed)
                    uint32_t word;
                    REQUIRE(program->getWord(word), "Corrupted binary! Missing data in instruction stream!");
                    words.push_back(word);
                }

                Utils::May<Instruction> make_inst = Instruction::makeOp(program->insts, opcode, words);
                if (!make_inst) {
                    msg = make_inst.error();
                    goto check_err;
                }
                Instruction& inst = make_inst.value();
                unsigned location = program->insts.size() - 1;

                if (inst.isEntry()) {
                    entry_found = true;
                    program->entry = location;
                }

                // Process the instruction as necessary
                // If it is a decoration (ie modifies a type not yet defined), save it for later
                if (inst.isDecoration())
                    program->decorations.push_back(location);

                // If it has a result, let it save itself in the data vector
                Utils::May<const bool> made_result = inst.makeResult(program->data, location);
                if (!made_result) {
                    msg = made_result.error();
                    goto check_err;
                }
            }

            if (!entry_found) {
                msg = "Missing entry function in SPIR-V source!";
                goto check_err;
            }

            // Finally, after all necessary data should exist, apply all decoration instructions
            for (unsigned dec_i : program->decorations) {
                Utils::May<const bool> apply_dec = program->insts[dec_i].applyDecoration(program->data);
                if (!apply_dec) {
                    msg = apply_dec.error();
                    goto check_err;
                }
            }

            check_err:
            if (!msg.empty()) {
                delete program;
                return Utils::unexpected<Program>(msg);
            }
#undef REQUIRE

            return Utils::expected(*program);
        }
    };
};
