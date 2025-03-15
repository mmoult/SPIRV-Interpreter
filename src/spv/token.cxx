/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <iostream>
#include <string>
#include <variant>
export module spv.token;

export struct Token {
    enum class Type {
        REF,  // variable ref like %1 or %12
        CONST,  // id literal like "Shader" or "Location"
        UINT,  // literal uint used for constants, offsets, etc
        INT,  // literal int used for constants
        FLOAT,  // literal float used for constants
        STRING,  // string like "main" or "GLSL.std.450"
        // Note: we strip comments during parsing
    } type;

    std::variant<unsigned, int, float, std::string> raw;

    Token(Type type, unsigned val) : type(type), raw(val) {
        // For CONST, UINT, REF
        assert(type == Type::CONST || type == Type::UINT || type == Type::REF);
    }
    explicit Token(int inum) : type(Type::INT), raw(inum) {}
    explicit Token(float fnum) : type(Type::FLOAT), raw(fnum) {}
    explicit Token(std::string str) : type(Type::STRING), raw(str) {}

    void print() const {
        switch (type) {
        case Type::REF:
            std::cout << "%" << std::get<unsigned>(raw);
            break;
        case Type::CONST: {
            // Below will will eventually when text formatting is supported by compiler:
            // std::cout << std::format("{:#x}", std::get<unsigned>(raw));

            // Until then, here is the workaround
            std::ios oldState(nullptr);
            oldState.copyfmt(std::cout);
            std::cout << "0x" << std::hex << std::get<unsigned>(raw);
            std::cout.copyfmt(oldState);
            break;
        }
        case Type::UINT:
            std::cout << std::get<unsigned>(raw);
            break;
        case Type::INT:
            std::cout << std::get<int>(raw);
            break;
        case Type::FLOAT:
            std::cout << std::get<float>(raw);
            break;
        case Type::STRING:
            std::cout << "\"" << std::get<std::string>(raw) << "\"";
            break;
        default:
            assert(false);  // unhandled token type!
        }
    }
};
