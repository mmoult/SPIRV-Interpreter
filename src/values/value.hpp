/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_VALUE_HPP
#define VALUES_VALUE_HPP

#include <cassert>
#include <map>
#include <sstream>
#include <stdexcept>

#include "type.hpp"

class Value {
protected:
    Type type;

public:
    Value(Type type): type(type) {}
    virtual ~Value() = default;

    const Type& getType() const { return type; }

    /// @brief Copy the value into this
    /// The Value implementation of this method does NOT perform the copy, it just throws a failure if the copy cannot
    // be done. The subclass is responsible for defining an implementation which will handle the copy logic.
    /// @param new_val the value to copy from
    virtual void copyFrom(const Value& new_val) noexcept(false) {
        if (!new_val.getType().sameBase(getType()))
            throw std::runtime_error("Cannot copy from value of different type!");
    }

    virtual bool equals(const Value& val) const {
        return type == val.type;
    }
};

using ValueMap = std::map<std::string, const Value*>;
#endif
