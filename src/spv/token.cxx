/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <string>
#include <variant>
export module token;

export namespace Spv {
    struct Token {
        enum class Type {
            REF,    // variable ref like %1 or %12
            CONST,  // id literal like "Shader" or "Location"
            UINT,   // literal uint used for constants, offsets, etc
            INT,    // literal int used for constants
            FLOAT,  // literal float used for constants
            STRING, // string like "main" or "GLSL.std.450"
            // Note: we strip comments during parsing
        } type;

        std::variant<unsigned, int, float, std::string> raw;

        Token(Type type, unsigned val): type(type), raw(val) {
            // For CONST, UINT, REF
            assert(type == Type::CONST || type == Type::UINT || type == Type::REF);
        }
        Token(int inum): type(Type::INT), raw(inum) {}
        Token(float fnum): type(Type::FLOAT), raw(fnum) {}
        Token(std::string str): type(Type::STRING), raw(str) {}
    };
};
