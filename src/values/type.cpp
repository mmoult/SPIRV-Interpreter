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
import rayTrace.accelStruct;
import rayTrace.rayQuery;
import value.aggregate;
import value.primitive;
import value.string;

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
    case DataType::STRING:
        return new String("");
    case DataType::ACCEL_STRUCT:
        return new AccelStructManager(*this);
    case DataType::RAY_QUERY:
        return new RayQuery(*this);
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
    case DataType::STRING: // TODO
    case DataType::BOOL:
    case DataType::VOID:
        return true;
    case DataType::ARRAY:
        return subSize == rhs.subSize && (*subElement == *(rhs.subElement));
    case DataType::STRUCT: {
        // try to match all fields
        // For each field, if a name is provided for both, it must match
        unsigned len = subList.size();
        if (len != rhs.subList.size())
            return false;
        for (unsigned i = 0; i < subList.size(); ++i) {
            if (*subList[i] != *rhs.subList[i])
                return false;
            if (!(nameList[i].empty() || rhs.nameList[i].empty())) {
                if (nameList[i] != rhs.nameList[i])
                    return false;
            }
        }
        return true;
    }
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
        if (other.base != base && other.subElement->base != DataType::VOID && subElement->base != DataType::VOID)
            throw std::invalid_argument("Cannot find union of array and non-array types!");

        // Assume a void type array will become the other array type if it's a non-void type
        if (other.subElement->base == DataType::VOID || subElement->base == DataType::VOID) {
            if (other.subElement->base == DataType::VOID && subElement->base == DataType::VOID)
                return Type::array(0, *(new Type(Type::primitive(DataType::VOID))));
            else if (other.subElement->base == DataType::VOID)
                return Type::array(0, *(new Type((*subElement))));
            else if (subElement->base == DataType::VOID)
                return Type::array(0, *(new Type((*other.subElement))));
        }

        // Find the union of their subElements
        const unsigned new_size = (other.subSize == 0 || subSize == 0 || other.subSize != subSize) ? 0 : subSize;
        Type sub = subElement->unionOf(*other.subElement);

        // Because the subElement is a const pointer, we need for the sub to be equal to one or the other so we can
        // borrow it as the subElement for the new unioned type
        if (sub == *subElement && subSize == new_size)
            return *this;
        else if (sub == *other.subElement && other.subSize == new_size)
            return other;
        else  // Create a new unioned type
            return Type::array(new_size, *(new Type(sub)));
    }
    case DataType::STRUCT: {
        // TODO more complex logic to compare nonequivalent types
        // The issue is data management- we may need to construct a new type, but if that type is discarded or
        // superseded, data is leaked. The function may need an extra argument of new datas to be deleted if the value
        // is discarded

        // Check if the two structs are the same
        if (*this == other)
            return *this;

        // Try to create a unioned struct
        if (subList.size() == other.subList.size()) {
            std::vector<const Type*> new_sub_list;
            for (unsigned i = 0; i < subList.size(); ++i)
                new_sub_list.push_back(new Type(subList[i]->unionOf(*other.subList[i])));
            return *(new Type(Type::structure(new_sub_list, nameList)));
        }

        throw std::runtime_error("Cannot currently take union of different struct types!");
    }
    case DataType::STRING: {
        // TODO
        if (base == other.base)
            break;
        throw std::runtime_error("Cannot find union of string and non-string types!");
    }
    // TODO support other types
    }
    return t;
}

Value* Type::asValue(std::vector<Type*>& to_delete) const {
    // This could easily balloon to a recursive nightmare. I don't want to do that, however, so let's implement it very
    // simply and we can expand as needed
#define SIMPLE(TYPE, LOWER) \
    case DataType::TYPE: \
        return new String(#LOWER);

    switch (base) {
    SIMPLE(FLOAT, float);
    SIMPLE(UINT, uint);
    SIMPLE(INT, int);
    SIMPLE(BOOL, bool);
    SIMPLE(STRUCT, struct);
    SIMPLE(ARRAY, array);
    SIMPLE(STRING, string);
    SIMPLE(VOID, void);
    SIMPLE(FUNCTION, function);
    SIMPLE(POINTER, pointer);
    SIMPLE(ACCEL_STRUCT, accelStruct);
    SIMPLE(RAY_QUERY, rayQuery);
    }
#undef SIMPLE
}
