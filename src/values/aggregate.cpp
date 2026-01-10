/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "aggregate.hpp"

void Aggregate::addElements(std::vector<const Value*>& es) noexcept(false) {
    // Test that the size matches the current type's:
    unsigned vecsize = es.size();
    if (unsigned tsize = getSize(); vecsize != tsize && tsize != 0) {
        std::stringstream err;
        err << "Could not add " << vecsize << " values to " << getTypeName() << " of size " << tsize << "!";
        throw std::runtime_error(err.str());
    }
    for (unsigned i = 0; i < vecsize; ++i) {
        // Construct an element from the element type, then copy data from e to it.
        const Type& typeAt = getTypeAt(i);
        Value* val = typeAt.construct();
        try {
            val->copyFrom(*es[i]);
        } catch (const std::exception& e) {
            delete val;
            std::stringstream err;
            err << "Could not add " << getTypeName() << " value #" << i << " because: " << e.what() << "!";
            throw std::runtime_error(err.str());
        }
        elements.push_back(val);
    }
}

void Aggregate::inferType() {
    if (type.getBase() == DataType::ARRAY) {
        // Must be at least one array element to perform this action!
        assert(!elements.empty());
        type.replaceSubElement(&elements[0]->getType());
    } else {
        assert(type.getBase() == DataType::STRUCT);
        // Replace each of the sub elements with the field's type
        for (unsigned i = 0; i < elements.size(); ++i)
            type.replaceFieldType(&elements[0]->getType(), i);
    }
}

void Aggregate::copyFrom(const Value& new_val) noexcept(false) {
    Value::copyFrom(new_val);

    // Do the actual copy now
    const Aggregate& other = static_cast<const Aggregate&>(new_val);
    unsigned size = elements.size();

    if (unsigned osize = other.elements.size(); osize != size) {
        std::stringstream err;
        err << "Cannot copy from " << getTypeName() << " of a different size (" << osize << " -> " << size << ")!";
        throw std::runtime_error(err.str());
    }
    for (unsigned i = 0; i < size; ++i)
        elements[i]->copyFrom(*other.elements[i]);
}

bool Aggregate::equals(const Value& val) const {
    if (!Value::equals(val))  // guarantees matching types
        return false;
    const auto& other = static_cast<const Aggregate&>(val);
    // Shouldn't have to test lengths since that is encoded in the type
    for (unsigned i = 0; i < elements.size(); ++i) {
        if (!elements[i]->equals(*other.elements[i]))
            return false;
    }
    return true;
}

void Array::copyFrom(const Value& new_val) noexcept(false) {
    Value::copyFrom(new_val);
    // Runtime arrays have size 0 by default. If this size is 0, then we assume the correct length from what is
    // given now. Afterward, this should no longer have 0 length
    if (elements.empty()) {
        const Array& other = static_cast<const Array&>(new_val);
        unsigned osize = other.elements.size();
        // Initialize an element for each element in other to copy to
        const Type& e_type = type.getElement();
        for (unsigned i = 0; i < osize; ++i) {
            Value* val = e_type.construct();
            elements.push_back(val);
        }
    }
    Aggregate::copyFrom(new_val);
}
