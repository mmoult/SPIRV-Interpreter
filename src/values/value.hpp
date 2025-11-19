/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_VALUE_HPP
#define VALUES_VALUE_HPP

#include <cassert>
#include <functional>
#include <map>
#include <stdexcept>

#include "type.hpp"

class Value {
protected:
    Type type;

public:
    explicit Value(const Type& type) : type(type) {}
    virtual ~Value() = default;

    const Type& getType() const {
        return type;
    }

    /// @brief Copy the value into this
    ///
    /// The Value implementation of this method does NOT perform the copy, it just throws a failure if the copy cannot
    /// be done. The subclass is responsible for defining an implementation which will handle the copy logic.
    ///
    /// Copying from one value into another is necessary for checking outputs (values transferred to dummy constructed
    /// of type matching the compared to).
    /// @param new_val the value to copy from
    virtual void copyFrom(const Value& new_val) noexcept(false) {
        if (new_val.getType().getBase() != type.getBase())
            throw std::runtime_error("Cannot copy from value of a different base type!");
    }

    /// @brief try to copy from the new_val, and return whether the copy was successful
    bool tryCopyFrom(const Value& new_val) noexcept(true) {
        try {
            this->copyFrom(new_val);
            return true;
        } catch (...) {
            return false;
        }
    }

    /// @brief copies the value of other (reinterpreted into the same type) into this
    virtual void copyReinterp(const Value& other) noexcept(false) = 0;

    /// @brief compares whether the two values (this and val) are equal.
    ///
    /// Similarly to `copyFrom`, this implementation will only verify that the types match. Implement a more complete
    /// comparison in all subclasses.
    ///
    /// The method is necessary for all types which may be in the shader output.
    ///
    /// @param val the other value to compare against.
    virtual bool equals(const Value& val) const {
        return type == val.type;
    }

    /// @brief Recursively applies a function to this value and all sub-values, in post-order traversal.
    ///
    /// @param seen the function to apply. Returns true to continue traversal, false to stop.
    virtual void recursiveApply(const std::function<bool(Value& seen)>& usage) {}
};

using ValueMap = std::map<std::string, const Value*>;

#endif
