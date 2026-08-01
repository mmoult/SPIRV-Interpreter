/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_POINTER_HPP
#define VALUES_POINTER_HPP

#include <sstream>
#include <stdexcept>
#include <vector>

#include "aggregate.hpp"
#include "coop-matrix.hpp"
#include "type.hpp"
#include "value.hpp"

class Pointer final : public Value {
    /// @brief an index in Data which all indices point into
    unsigned head;
    /// @brief A list of indices for recursively extracting from the Value at head
    std::vector<unsigned> indices;

public:
    /// @param point_to the pointee type. Type::pointer stores its ADDRESS without taking ownership (see the note on
    ///                 Type::subElement), so this must be a Type which outlives the Pointer - typically one owned by
    ///                 the module's Data. Taking it by value here would store the address of the parameter itself,
    ///                 which dies as soon as the constructor returns.
    Pointer(unsigned head, const std::vector<unsigned>& indices, const Type& point_to)
        : Value(Type::pointer(point_to, -1)), head(head), indices(indices) {}

    /// @brief Construct a scratch pointer with no meaningful pointee type.
    Pointer(unsigned head, const std::vector<unsigned>& indices)
        : Value(Type::forwardPointer()), head(head), indices(indices) {}

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

        // Do the actual copy now
        // const Pointer& other = static_cast<const Pointer&>(new_val);
        throw std::runtime_error("Unimplemented function!");
    }

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to pointer!");
    }

    // The defaulted copy shares the pointee Type that this Pointer's type already borrows
    Pointer(const Pointer& other) = default;
    Pointer& operator=(const Pointer&) = delete;

    [[nodiscard]] Pointer* clone() const {
        return new Pointer(*this);
    }

    unsigned getHead() const {
        return head;
    }
    const std::vector<unsigned>& getIndices() const {
        return indices;
    }

    Value* dereference(Value& start) const noexcept(false) {
        Value* res = &start;
        for (unsigned idx : indices) {
            if (DataType dt = res->getType().getBase();
                dt != DataType::ARRAY && dt != DataType::STRUCT && dt != DataType::COOP_MATRIX) {
                std::stringstream error;
                error << "Cannot extract from non-composite type!";
                throw std::runtime_error(error.str());
            }
            Aggregate& agg = *static_cast<Aggregate*>(res);
            if (idx >= agg.getSize()) {
                if (agg.getType().getBase() == DataType::COOP_MATRIX && static_cast<CoopMatrix&>(agg).isUnsized()) {
                    // Cooperative matrices have complications since their size may not be known
                    // If it is still indeterminate, push the index within bounds
                    if (agg.getSize() < 1)
                        throw std::runtime_error("Cannot access within an empty coopmat!");
                    idx = 0;
                } else {
                    std::stringstream error;
                    error << "Index " << idx << " beyond the bound of composite (" << agg.getSize() << ")!";
                    throw std::runtime_error(error.str());
                }
            }
            res = agg[idx];
            // Repeat the process for all indices
        }
        return res;
    }

    bool equals(const Value& val) const override {
        if (val.getType().getBase() != DataType::POINTER)  // guarantees matching types
            return false;
        const auto& other = static_cast<const Pointer&>(val);
        // I cannot think of why this would be used, but implement it in case...
        if ((head != other.head) || (indices.size() != other.indices.size()))
            return false;
        for (unsigned i = 0; i < indices.size(); ++i) {
            if (indices[i] != other.indices[i])
                return false;
        }
        return true;
    }

    unsigned decompose() {
        assert(!indices.empty());
        unsigned back = indices.back();
        indices.pop_back();
        return back;
    }
};
#endif
