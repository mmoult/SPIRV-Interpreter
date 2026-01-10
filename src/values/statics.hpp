/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_STATICS_HPP
#define VALUES_STATICS_HPP

#include <cassert>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "aggregate.hpp"
#include "primitive.hpp"
#include "string.hpp"
#include "type.hpp"
#include "value.hpp"

struct Statics {
    inline static const Type voidType = Type();
    inline static const Type uintType = Type::primitive(DataType::UINT);
    inline static const Type boolType = Type::primitive(DataType::BOOL);
    inline static const Type fp32Type = Type::primitive(DataType::FLOAT);
    inline static Type uvec2Type;
    inline static Type vec3Type;
    inline static Type vec4Type;

    void init() {
        if (uvec2Type.getBase() == DataType::VOID) {
            uvec2Type = Type::array(2, uintType);
            vec3Type = Type::array(3, fp32Type);
            vec4Type = Type::array(4, fp32Type);
        }
    }

    static const Array& extractArray(const Value* field, const std::string& name);

    static std::vector<float> extractVec(const Value* field, const std::string& name, unsigned size);

    static std::vector<uint32_t> extractUvec(const Value* field, const std::string& name, unsigned size);

    static std::string extractString(const Value* field, const std::string& name);

    static uint32_t extractUint(const Value* field, const std::string& name);

#if defined(COMPILER_GNU)
    [[gnu::no_dangling]]  // GCC erroneously believes this returns a dangling reference. Ignore.
#endif
    static const Struct&
    extractStruct(const Value* field, const std::string& name, const std::vector<std::string>& fields);
};
#endif
