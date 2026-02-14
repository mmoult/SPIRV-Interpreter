/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_AGGREGATE_HPP
#define VALUES_AGGREGATE_HPP

#include <cassert>
#include <stdexcept>
#include <vector>

#include "type.hpp"
#include "value.hpp"

/// Array or Struct
class Aggregate : public Value {
protected:
    // Aggregate owns and manages the memory of all elements
    // However, we put elements in protected for easy access by subclasses
    std::vector<Value*> elements;

    virtual std::string getTypeName() const = 0;
    virtual const Type& getTypeAt(unsigned idx) const = 0;

public:
    Aggregate(const Type& t) : Value(t) {}

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

    void addElements(std::vector<const Value*>& es) noexcept(false);

    void dummyFill(bool undef = true) noexcept(false) {
        for (unsigned i = 0; i < getSize(); ++i) {
            Value* val = getTypeAt(i).construct(undef);
            elements.push_back(val);
        }
    }

    // Infer the type from the children elements. Useful if the subelement type currently stored was a temporary.
    void inferType();

    const Value* operator[](unsigned i) const {
        return elements[i];
    }
    Value* operator[](unsigned i) {
        return elements[i];
    }

    void copyFrom(const Value& new_val) noexcept(false) override;

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to aggregate!");
    }

    bool equals(const Value& val) const override;

    void recursiveApply(const std::function<bool(Value& seen)>& usage) override {
        for (auto& e : elements)
            e->recursiveApply(usage);

        usage(*this);
    }
};

class Array : public Aggregate {
protected:
    std::string getTypeName() const override {
        return "array";
    }
    const Type& getTypeAt(unsigned idx) const override {
        return type.getElement();
    }

public:
    Array(const Type& sub_element, unsigned size) : Aggregate(Type::array(size, sub_element)) {}

    /// @brief Constructs an array from a list of elements.
    ///
    /// Be careful, since no checking is done to verify that all elements match type. This should only be used
    /// internally, never through some user-generated parsing logic
    /// Also, this may never be used with an empty array.
    /// @param elements a pointer of value elements to pull types from in constructing this's type. When this
    /// constructor is used, the struct takes ownership of all elements given (and will delete them on destruction).
    explicit Array(std::vector<Value*>& elements) : Aggregate(Type::array(elements.size(), elements[0]->getType())) {
        this->elements = elements;
    }

    // Useful for subtype construction
    explicit Array(const Type& type) : Aggregate(type) {}

    unsigned getSize() const override {
        unsigned tsize = type.getSize();
        if (tsize == 0)
            return elements.size();
        return tsize;
    }

    void copyFrom(const Value& new_val) noexcept(false) override;

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

class Struct final : public Aggregate {
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
    Struct(const Type& t) : Aggregate(t) {}

    /// @brief Constructs a structure from elements and names.
    ///
    /// @param elements a pointer of value elements to pull types from in constructing this's type. When this
    /// constructor is used, the struct takes ownership of all elements given (and will delete them on destruction).
    /// @param names names of the elements, in order
    Struct(std::vector<Value*>& elements, const std::vector<std::string>& names)
        : Aggregate(createTypeFrom(elements, names)) {
        this->elements = elements;
    }

    unsigned getSize() const override {
        return type.getFields().size();
    }
};
#endif
