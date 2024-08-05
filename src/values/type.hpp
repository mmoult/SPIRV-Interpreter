/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_TYPE_HPP
#define VALUES_TYPE_HPP

#include <algorithm> // for min
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

enum DataType : unsigned {
    FLOAT = 0,
    UINT = 1,
    INT = 2,
    BOOL = 3,
    STRUCT = 4,
    ARRAY = 5,
    STRING = 6,
    // Above is usable in TOML, below only internal to SPIR-V
    VOID = 7,
    FUNCTION = 8,
    POINTER = 9,
    RAY_TRACING_ACCELERATION_STRUCTURE = 10,
    RAY_QUERY = 11
};

// necessary forward reference
class Value;

class Type {
    DataType base;
    unsigned subSize;
    // memory for subElement and subList elements is NOT managed by the Type
    // In other words, the original allocator is expected to deallocate or transfer ownership
    const Type* subElement;
    std::vector<const Type*> subList;
    std::vector<std::string> nameList;

    inline Type(DataType base, unsigned sub_size, const Type* sub_element):
        base(base),
        subSize(sub_size),
        subElement(sub_element) {}

    inline Type(DataType base, std::vector<const Type*> sub_list, std::vector<std::string> name_list):
        base(base),
        subSize(0),
        subElement(nullptr),
        subList(sub_list.begin(), sub_list.end()),
        nameList(name_list.begin(), name_list.end()) {}

    /// @brief Creates a value corresponding to this type, with optional inputs
    /// @param values an optional vector of values to use
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] Value* construct(std::vector<const Value*>* values) const noexcept(false);

public:
    // Factory methods to create type variants:

    /// @brief Factory for floats, uints, ints, bools, voids
    /// May define a custom size (assuming the interpreter supports it), but the default is 32.
    /// @param primitive the primitive type to use (should not use STRUCT, function, or pointer)
    /// @param size the size of the type. Not all primitives have a usable size (bool and void don't)
    /// @return the created type
    static inline Type primitive(DataType primitive, unsigned size = 32) {
        assert(primitive != DataType::STRUCT && primitive != DataType::FUNCTION &&
               primitive != DataType::POINTER);
        assert(size == 32 || (primitive != DataType::BOOL && primitive != DataType::VOID));
        return Type(primitive, size, nullptr);
    }

    /// @brief Construct an array type
    /// @param array_size the size of the array. The number of elements. Must be > 0 for regular arrays, == 0 for
    ///                   runtime arrays.
    /// @param element a Type which will outlive this Type. Must not be null. Ownership is not transferred
    ///                to the constructed array- in other words, the allocator is expected to deallocate element
    ///                some time after the deallocation of this array
    static inline Type array(unsigned array_size, const Type& element) {
        return Type(DataType::ARRAY, array_size, &element);
    }

    /// @brief Construct a structure type
    /// @param sub_list a list of non-null types. Each Type must outlive the struct created here. Ownership is not
    ///                 transferred- meaning that the original allocator is expected to deallocate some time after
    ///                 the deallocation of this struct
    static inline Type structure(std::vector<const Type*> sub_list) {
        std::vector<std::string> names(sub_list.size());
        std::fill(names.begin(), names.end(), "");
        return Type(DataType::STRUCT, sub_list, names);
    }
    /// @brief Construct a structure type
    /// @param sub_list a list of non-null types. Each Type must outlive the struct created here. Ownership is not
    ///                 transferred- meaning that the original allocator is expected to deallocate some time after
    ///                 the deallocation of this struct
    /// @param name_list a list of string names, corresponding to the Types at the same indices. Must have the same
    ///                  length as sub_list
    static inline Type structure(std::vector<const Type*> sub_list, std::vector<std::string> name_list) {
        assert(sub_list.size() == name_list.size());
        return Type(DataType::STRUCT, sub_list, name_list);
    }

    static inline Type function(const Type* return_, std::vector<Type*>& subList) {
        Type t(DataType::FUNCTION, 0, return_);
        t.subList.reserve(subList.size());
        for (const auto& ty : subList)
            t.subList.push_back(ty);
        return t;
    }

    static inline Type pointer(const Type& point_to) {
        return Type(DataType::POINTER, 0, &point_to);
    }

    static inline Type string() {
        return Type(DataType::STRING, 0, nullptr);
    }

    static inline Type accelerationStructure(std::vector<const Type*> sub_list = std::vector<const Type*> {},
            std::vector<std::string> name_list = std::vector<std::string> {}) {
        assert(sub_list.size() == name_list.size());
        return Type(DataType::RAY_TRACING_ACCELERATION_STRUCTURE, sub_list, name_list);
    }

    static inline Type rayQuery() {
        return Type(DataType::RAY_QUERY, 0, nullptr);
    }

    // Other methods:

    /// @brief Creates a value corresponding to this type, filling in values with dummies as necessary
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] inline Value* construct() const noexcept(false) {
        return construct(nullptr);
    }
    /// @brief Creates a value corresponding to this type with given inputs (used for fields, elements, etc)
    /// @param values a vector of values to use
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] inline Value* construct(std::vector<const Value*>& values) const noexcept(false) {
        return construct(&values);
    }

    inline const Type& getElement() const {
        assert(base == DataType::ARRAY);
        return *subElement;
    }
    inline unsigned getSize() const {
        assert(base == DataType::ARRAY);
        return subSize;
    }

    inline unsigned getPrecision() const {
        assert(base == DataType::FLOAT || base == DataType::UINT || base == DataType::INT);
        return subSize;
    }

    inline const std::vector<const Type*>& getFields() const {
        assert(base == DataType::STRUCT || base == DataType::RAY_TRACING_ACCELERATION_STRUCTURE);
        return subList;
    }
    inline const std::vector<std::string>& getNames() const {
        assert(base == DataType::STRUCT || base == DataType::RAY_TRACING_ACCELERATION_STRUCTURE);
        return nameList;
    }

    inline const Type& getPointedTo() const {
        assert(base == DataType::POINTER);
        return *subElement;
    }

    inline bool sameBase(const Type& rhs) const {
        // STRUCT and RAY_TRACING_ACCELERATION_STRUCTURE are considered the same
        bool is_struct_and_accel_struct =
            (base == DataType::STRUCT && rhs.base == DataType::RAY_TRACING_ACCELERATION_STRUCTURE) ||
            (base == DataType::RAY_TRACING_ACCELERATION_STRUCTURE && rhs.base == DataType::STRUCT);

        return base == rhs.base || is_struct_and_accel_struct;
    }

    inline void nameMember(unsigned i, std::string name) noexcept(false) {
        assert(base == DataType::STRUCT || base == DataType::RAY_TRACING_ACCELERATION_STRUCTURE);
        if (i >= nameList.size())
            throw std::invalid_argument("Cannot name member at index beyond existing!");
        nameList[i] = name;
    }

    bool operator==(const Type& rhs) const;
    inline bool operator!=(const Type& rhs) const { return !(*this == rhs); };

    /// @brief Returns the type which is general to all elements
    /// Must follow the same conversion rules as void Value::copyFrom(const Value& new_val)
    /// @param elements the elements to find the most general type for
    /// @return the general type common to all elements
    /// @throws if no such union type can be found
    static Type unionOf(std::vector<const Value*> elements) noexcept(false);

    Type unionOf(const Type& other) const noexcept(false);

    inline DataType getBase() const {
        return base;
    }

    Value* asValue(std::vector<Type*>& to_delete) const;
};
#endif
