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

    virtual void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to aggregate!");
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

    /// @brief Constructs an array from a list of elements.
    ///
    /// Be careful, since no checking is done to verify that all elements match type. This should only be used
    /// internally, never through some user-generated parsing logic
    /// Also, this may never be used with an empty array.
    /// @param elements a pointer of value elements to pull types from in constructing this's type. When this
    /// constructor is used, the struct takes ownership of all elements given (and will delete them on destruction).
    Array(std::vector<Value*>& elements)
        : Aggregate(Type::array(elements.size(), elements[0]->getType())) {
        this->elements = elements;
    }

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

    void copyReinterp(const Value& other) noexcept(false) override {
        // We can only reinterpret from other arrays currently
        if (other.getType().getBase() != DataType::ARRAY)
            throw std::runtime_error("Could not copy reinterp non-array to array!");
        const auto& array_o = static_cast<const Array&>(other);
        unsigned size = getSize();
        if (size != array_o.getSize())
            throw std::runtime_error("Cannot copy reinterp from array of a different size!");
        for (unsigned i = 0; i < size; ++i)
            elements[i]->copyReinterp(*array_o[i]);
    }

    /// @brief Set the elements directly, giving all memory ownership to the array
    void setElementsDirectly(std::vector<Value*>& vals) {
        // Clean up any elements which existed before
        for (const auto& e : elements)
            delete e;
        this->elements = vals;
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

    Type createTypeFrom(const std::vector<Value*>& elements, const std::vector<std::string>& names) {
        std::vector<const Type*> types;
        types.reserve(elements.size());
        for (const Value* element : elements)
            types.push_back(&element->getType());
        return Type::structure(types, names);
    }

public:
    Struct(Type t): Aggregate(t) {}

    /// @brief Constructs a structure from elements and names.
    ///
    /// @param elements a pointer of value elements to pull types from in constructing this's type. When this
    /// constructor is used, the struct takes ownership of all elements given (and will delete them on destruction).
    /// @param names names of the elements, in order
    Struct(std::vector<Value*>& elements, std::vector<std::string> names)
        : Aggregate(createTypeFrom(elements, names)) {
        this->elements = elements;
    }

    unsigned getSize() const override {
        return type.getFields().size();
    }
};
