/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
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
};