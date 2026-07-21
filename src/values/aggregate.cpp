/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "aggregate.hpp"
#include "primitive.hpp"

#include <sstream>

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
            type.replaceFieldType(&elements[i]->getType(), i);
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
    if (val.getType().getBase() != type.getBase())
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
    // Runtime arrays have size 0 by default. If this size is 0, then we assume the correct length from what is
    // given now. Afterward, this should no longer have 0 length
    if (elements.empty()) {
        Value::copyFrom(new_val);
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

void Array::copyReinterp(const Value& other) noexcept(false) {
    unsigned size = getSize();
    unsigned o_size;
    const Type* o_el_type;
    std::vector<const Value*> others;
    if (other.getType().getBase() == DataType::ARRAY) {
        const auto& array_o = static_cast<const Array&>(other);
        o_size = array_o.getSize();
        o_el_type = &array_o.getTypeAt(0);

        for (const Value* element : array_o)
            others.push_back(element);
    } else {
        o_size = 1;
        o_el_type = &other.getType();
        others.push_back(&other);
    }

    if (size == o_size) {
        // The simple case: the array sizes match, so we reinterpret across
        for (unsigned i = 0; i < size; ++i)
            elements[i]->copyReinterp(*others[i]);
        return;
    }

    const Type& t_el_type = this->getTypeAt(0);
    if (!o_el_type->isPrimitive())
        throw std::runtime_error("Unsupported reinterpret from array of non-primitives to array of nonequal size!");
    if (!t_el_type.isPrimitive())
        throw std::runtime_error("Unsupported reinterpret to array of non-primitives from array of nonequal size!");
    // Total bits must match
    unsigned t_bitsize = t_el_type.getPrecision();
    unsigned o_bitsize = o_el_type->getPrecision();
    if (o_bitsize * o_size != t_bitsize * size)
        throw std::runtime_error("Unsupported reinterpret from array of primitives with a non-matching bit total!");

    // "any single component of S (mapping to multiple components of L) maps its lower-ordered bits to the
    // lower-numbered components of L."
    uint64_t raw = 0;
    constexpr auto CAPACITY = sizeof(raw) * 8;
    const unsigned THIS_MASK = ((t_bitsize == CAPACITY) ? 0 : (1 << t_bitsize)) - 1;
    unsigned other_index = 0;
    int bit_at = 0;
    for (auto* element : elements) {
        while (bit_at < t_bitsize) {
            assert(other_index <= o_size);
            const auto* from = static_cast<const Primitive*>(others[other_index]);
            uint64_t fetch = from->getRaw();
            if (bit_at >= 0) {
                raw |= (fetch << bit_at);
                if (static_cast<unsigned>(bit_at + o_bitsize) > CAPACITY)
                    // The raw at other_index didn't place all of its bits. Skip increment
                    break;
            } else
                raw |= (fetch >> -bit_at);

            ++other_index;
            bit_at += o_bitsize;
        }
        Primitive prim(raw & THIS_MASK, t_bitsize);
        bit_at -= t_bitsize;
        // Must use a conditional to avoid undefined behavior C spec 6.5.7:
        // "If the value of the right operand is negative or is greater than or equal to the width of the promoted
        // left operand, the behavior is undefined."
        if (t_bitsize != CAPACITY)
            raw >>= t_bitsize;
        else
            raw = 0;

        static_assert(CAPACITY >= (sizeof(prim.data.all) * 8));  // This algorithm only works if capacity is big enough
        element->copyReinterp(prim);
    }
}
