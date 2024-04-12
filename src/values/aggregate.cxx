/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "type.hpp"
#include "value.hpp"
export module value.aggregate;

/// Array or Struct
export class Aggregate : public Value {
protected:
    // Aggregate owns and manages the memory of all elements
    // However, we put elements in protected for easy access by subclasses
    std::vector<Value*> elements;

    virtual std::string getTypeName() const = 0;
    virtual const Type& getTypeAt(unsigned idx) const = 0;

public:
    Aggregate(Type t): Value(t) {}
    virtual ~Aggregate() {
        for (const auto& e : elements)
            delete e;
    }
    Aggregate(const Aggregate& other) = delete;
    Aggregate& operator=(const Aggregate& other) = delete;

    virtual unsigned getSize() const = 0;

    auto begin() {
        return elements.begin();
    }
    auto end() {
        return elements.end();
    }
    auto begin() const {
        return elements.begin();
    }
    auto end() const {
        return elements.end();
    }

    void addElements(std::vector<const Value*>& es) noexcept(false) {
        // Test that the size matches the current type's:
        unsigned tsize = getSize();
        if (unsigned vecsize = es.size(); vecsize != tsize) {
            std::stringstream err;
            err << "Could not add " << vecsize << " values to " << getTypeName() << " of size " << tsize << "!";
            throw std::runtime_error(err.str());
        }
        for (unsigned i = 0; i < tsize; ++i) {
            // Construct an element from the element type, then copy data from e to it.
            const Type& typeAt = getTypeAt(i);
            Value* val = typeAt.construct();
            try {
                val->copyFrom(*es[i]);
            } catch(const std::exception& e) {
                delete val;
                std::stringstream err;
                err << "Could not add " << getTypeName() << " value #" << i << " because: " << e.what() << "!";
                throw std::runtime_error(err.str());
            }
            elements.push_back(val);
        }
    }

    void dummyFill() noexcept(false) {
        for (unsigned i = 0; i < getSize(); ++i) {
            Value* val = getTypeAt(i).construct();
            elements.push_back(val);
        }
    }

    const Value* operator[](unsigned i) const {
        return elements[i];
    }
    Value* operator[](unsigned i) {
        return elements[i];
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

        // Do the actual copy now
        const Aggregate& other = static_cast<const Aggregate&>(new_val);
        unsigned size = elements.size();

        if (unsigned osize = other.elements.size(); osize != size) {
            std::stringstream err;
            err << "Cannot copy from " << getTypeName() << " of a different size (" << osize << " -> " <<
                   size << ")!";
            throw std::runtime_error(err.str());
        }
        for (unsigned i = 0; i < size; ++i)
            elements[i]->copyFrom(*other.elements[i]);
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        const auto& other = static_cast<const Aggregate&>(val);
        // Shouldn't have to test lengths since that is encoded in the type
        for (unsigned i = 0; i < elements.size(); ++i) {
            if (!elements[i]->equals(*other.elements[i]))
                return false;
        }
        return true;
    }
};

export class Array : public Aggregate  {
protected:
    std::string getTypeName() const override {
        return "array";
    }
    const Type& getTypeAt(unsigned idx) const override {
        return type.getElement();
    }

public:
    Array(const Type& sub_element, unsigned size): Aggregate(Type::array(size, sub_element)) {}

    unsigned getSize() const override {
        unsigned tsize = type.getSize();
        if (tsize == 0)
            return elements.size();
        return tsize;
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
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
};

export class Struct : public Aggregate {
protected:
    std::string getTypeName() const override {
        return "struct";
    }
    const Type& getTypeAt(unsigned idx) const override {
        return *type.getFields()[idx];
    }

public:
    Struct(Type t): Aggregate(t) {}

    unsigned getSize() const override {
        return type.getFields().size();
    }
};
