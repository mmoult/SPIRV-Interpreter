/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "type.hpp"

#include <cassert>
#include <stdexcept>
#include <vector>

#include "value.hpp"

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

bool Type::operator==(const Type& rhs) const {
    if (!sameBase(rhs))
        return false;

    switch (base) {
    default:
        assert(false); // unknown type!
        return false;
    case DataType::FLOAT:
    case DataType::UINT:
    case DataType::INT:
        return subSize == rhs.subSize;
    case DataType::BOOL:
    case DataType::VOID:
        return true;
    case DataType::ARRAY:
        return subSize == rhs.subSize && (*subElement == *(rhs.subElement));
    // TODO struct
    case DataType::FUNCTION:
        return (*subElement == *(rhs.subElement)) && subList == rhs.subList;
    case DataType::POINTER:
        return *subElement == *(rhs.subElement);
    }
}

Type Type::unionOf(const Type& other) const noexcept(false) {
    Type t = *this;
    std::string base_str;
    switch (base) {
    default:
        throw std::invalid_argument("Unsupported type!");
    case DataType::VOID:
        if (other.base == base)
            break;
        throw std::invalid_argument("Cannot find union of void and non-void types!");
    // Primitive types
    case DataType::UINT:
        // UINT can convert to any of the other primitives
        switch (other.base) {
        case DataType::UINT:
        case DataType::BOOL:
        case DataType::FLOAT:
        case DataType::INT:
            t.base = other.base;
            t.subSize = std::min(subSize, other.subSize);
            break;
        default:
            throw std::invalid_argument("Cannot find union between UINT and non-primitive type!");
        }
        break;
    case DataType::BOOL:
        if (base_str.empty())
            base_str = "Bool";
        [[fallthrough]];
    case DataType::FLOAT:
        if (base_str.empty())
            base_str = "Float";
        [[fallthrough]];
    case DataType::INT: {
        if (base_str.empty())
            base_str = "Int";
        
        // Shared logic for other primitives
        if (other.base == base ||
            other.base == DataType::UINT) { // UINT -> X
            // Select the more specific of precisions
            t.subSize = std::min(subSize, other.subSize);
            break;
        }
        std::stringstream error;
        error << "Cannot find union between " << base_str << " and type which is neither that nor UINT!";
        throw std::invalid_argument(error.str());
    }
    case DataType::ARRAY: {
        if (other.base != base)
            throw std::invalid_argument("Cannot find union of array and non-array types!");
        if (other.subSize != subSize) {
            std::stringstream error;
            error << "Cannot find union between arrays of different sizes (" << subSize << " and ";
            error << other.subSize << ")!";
            throw std::invalid_argument(error.str());
        }
        // Find the union of their subElements
        Type sub = subElement->unionOf(*other.subElement);
        // Because the subElement is a const pointer, we need for the sub to be equal to one or the other so we can
        // borrow it as the subElement for the new unioned type
        if (sub == *subElement)
            return *this;
        else if (sub == *other.subElement)
            return *other.subElement;
        throw std::runtime_error("Cannot currently take union of arrays with different unioned subelements!");
    }
    // TODO support other types
    }
    return t;
}
