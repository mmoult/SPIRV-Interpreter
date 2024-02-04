module;
#include <cassert>
#include <string>
#include <variant>

#include "../external/spirv.hpp"
export module tokens;

export namespace Spv {
    struct Token {
        enum class Type {
            OP,     // operation like "OpEntryPoint"
            CONST,  // id literal like "Shader" or "Location"
            INT,    // literal int used for constants
            UINT,   // literal uint used for constants, offsets, etc
            FLOAT,  // literal float used for constants
            STRING, // string like "main" or "GLSL.std.450"
            REF,    // variable ref like %1 or %12
            // Note: we strip comments during parsing
        } type;

        std::variant<unsigned, int, float, std::string> raw;

        Token(Type type, unsigned val): type(type), raw(val) {
            // For OP, CONST, UINT, REF
            assert(type == Type::OP || type == Type::CONST || type == Type::UINT || type == Type::REF);
        }
        Token(int inum): type(Type::INT), raw(inum) {}
        Token(float fnum): type(Type::FLOAT), raw(fnum) {}
        Token(std::string str): type(Type::STRING), raw(str) {}
    };
};
