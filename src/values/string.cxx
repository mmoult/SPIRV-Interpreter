/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

#include "type.hpp"
#include "value.hpp"
export module value.string;

export class String : public Value {
    std::string internal;

public:
    String(std::string str): Value(Type::string()), internal(str) {}

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

        // Do the actual copy now
        const String& other = static_cast<const String&>(new_val);
        internal = other.internal;
    }

    std::string get() const {
        return internal;
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        const auto& other = static_cast<const String&>(val);
        // I cannot think of why this would be used, but implement it in case...
        return other.internal == internal;
    }
};
