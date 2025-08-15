/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cstdint>
#include <string>

#include "type.hpp"
import value.aggregate;
export module value.coopMatrix;

export class CoopMatrix : public Array {
protected:
    unsigned rows;

    std::string getTypeName() const override {
        return "coopmat";
    }

public:
    CoopMatrix(const Type& sub_element, unsigned rows, unsigned cols)
        : Array(Type::coopMatrix(0, rows, cols, sub_element)), rows(rows) {}

    unsigned getNumRows() const {
        return rows;
    }

    // Gets the size of the array for this invocation
    unsigned getSize() const override {
        return elements.size();
    }

    void enforceSize(unsigned invocation, unsigned num_invocations) {
        uint32_t total_elements = type.getSize();
        // Split those elements between all in the frame stack
        uint32_t e_beg = (invocation * total_elements) / num_invocations;
        uint32_t e_fin = ((invocation + 1) * total_elements) / num_invocations;
        unsigned needed = e_fin - e_beg;

        const Type& subelement = type.getElement();
        while (getSize() < needed) {
            Value* val = subelement.construct();
            val->copyFrom(*elements.back());
            elements.push_back(val);
        }
    }
};
