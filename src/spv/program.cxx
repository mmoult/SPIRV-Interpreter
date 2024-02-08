module;
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "../external/spirv.hpp"

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

            REQUIRE(program->determineEndian(), "Corrupted binary! Magic number missing.");
            REQUIRE(program->skip(2), "Corrupted binary! Version and/or generator missing.");

            uint32_t bound;
            REQUIRE(program->getWord(bound), "Corrupted binary! Missing bound.");
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

                Utils::May<Instruction> inst = Instruction::makeOp(program->insts, opcode, words);
                if (!inst.is()) {
                    msg = inst.str();
                    goto check_err;
                }
            }

            check_err:
            if (!msg.empty()) {
                delete program;
                return Utils::May<Program>::none(msg);
            }
#undef REQUIRE

            return Utils::May<Program>::some(*program);
        }
    };
};
