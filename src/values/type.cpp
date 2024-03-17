/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <stdexcept>
#include <vector>

module type;
import value;
import value.aggregate;
import value.primitive;

Value* Type::construct(std::vector<const Value*>* values) const {
    switch (base) {
    default:
        throw std::runtime_error("Unsupported type!");
    case DataType::VOID:
        throw std::runtime_error("Cannot construct void type!");
    // Primitive types
    case DataType::FLOAT:
    case DataType::UINT:
    case DataType::INT:
    case DataType::BOOL: {
        Primitive* prim = new Primitive(*this);
        if (values == nullptr)
            return prim;
        if (values->empty() || values->size() != 1) {
            delete prim;
            throw std::runtime_error("Cannot construct primitive from nonzero number of inputs!");
        }

        const Value* val = (*values)[0];
        try {
            prim->copyFrom(*val);
        } catch (std::exception& e) {
            delete prim;
            throw e;
        }
        return prim;
    }
    case DataType::ARRAY:
    case DataType::STRUCT: {
        Aggregate* agg = (base == DataType::ARRAY)
                            ? static_cast<Aggregate*>(new Array(*subElement, subSize))
                            : static_cast<Aggregate*>(new Struct(*this));
        // try to populate with each of the entries
        if (values != nullptr)
            agg->addElements(*values);
        else
            agg->dummyFill();
        return agg;
    }
    // TODO support other types
    }
}

Type Type::unionOf(std::vector<const Value*> elements) {
    if (elements.empty())
        throw std::invalid_argument("Cannot find union of types in empty vector!");
    Type t = elements[0]->getType();
    for (unsigned i = 1; i < elements.size(); ++i)
        t = t.unionOf(elements[i]->getType());
    return t;
}
