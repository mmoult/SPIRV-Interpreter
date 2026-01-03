/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_COOPMATRIX_HPP
#define VALUES_COOPMATRIX_HPP

#include <algorithm>
#include <cstdint>
#include <string>

#include "aggregate.hpp"
#include "type.hpp"

class CoopMatrix : public Array {
protected:
    unsigned rows;
    bool unsized;

    std::string getTypeName() const override {
        return "coopmat";
    }

public:
    CoopMatrix(const Type& sub_element, unsigned rows, unsigned cols)
        : Array(Type::coopMatrix(0, rows, cols, sub_element)), rows(rows), unsized(false) {}

    unsigned getNumRows() const {
        return rows;
    }

    // Gets the size of the array for this invocation
    unsigned getSize() const override {
        return elements.size();
    }

    bool isUnsized() const {
        return unsized;
    }
    void setUnsized(bool unsized = true) {
        this->unsized = unsized;
    }

    void enforceSize(unsigned invocation, unsigned num_invocations) {
        if (unsized)
            unsized = false;
        else
            return;

        uint32_t total_elements = type.getSize();
        // Split those elements between all in the frame stack
        uint32_t e_beg = (invocation * total_elements) / num_invocations;
        uint32_t e_fin = ((invocation + 1) * total_elements) / num_invocations;
        unsigned needed = e_fin - e_beg;

        const Type& subelement = type.getElement();
        bool partial_filled = !elements.empty();
        while (getSize() < needed) {
            Value* val = subelement.construct();
            if (partial_filled)
                val->copyFrom(*elements.back());
            elements.push_back(val);
        }
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

        // Other matrices may have indeterminate size, in which case, we will copy their single element until all
        // elements in this are set
        const CoopMatrix& other = static_cast<const CoopMatrix&>(new_val);
        unsized &= other.unsized;
        if (other.isUnsized()) {
            if (elements.empty())
                return;
            if (other.getSize() < 1)
                throw std::runtime_error("Cannot copy non-empty coopmat from empty!");

            Value& element = *other.elements[0];
            for (unsigned i = 0; i < getSize(); ++i)
                elements[i]->copyFrom(element);
        } else
            Array::copyFrom(new_val);
    }
};
#endif
