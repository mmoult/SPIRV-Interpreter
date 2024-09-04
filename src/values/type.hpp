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

#include "valuable.hpp"

enum class DataType {
    FLOAT,
    UINT,
    INT,
    BOOL,
    STRUCT,
    ARRAY,
    STRING,
    // Above is usable in TOML, below only internal to SPIR-V
    VOID,
    FUNCTION,
    POINTER,
    ACCEL_STRUCT,
    RAY_QUERY,
    IMAGE,
};

inline std::ostream& operator<<(std::ostream& os, const DataType& type) {
#define SWITCH(NAME) \
    case DataType::NAME: { \
        std::string str = #NAME; \
        os << str; \
        break; \
    }

    switch (type) {
    SWITCH(FLOAT)
    SWITCH(UINT)
    SWITCH(INT)
    SWITCH(BOOL)
    SWITCH(STRUCT)
    SWITCH(ARRAY)
    SWITCH(STRING)
    SWITCH(VOID)
    SWITCH(FUNCTION)
    SWITCH(POINTER)
    SWITCH(ACCEL_STRUCT)
    SWITCH(RAY_QUERY)
    SWITCH(IMAGE)
    }
#undef SWITCH
    return os;
}

// necessary forward reference
class Value;

class Type : public Valuable {
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

    inline Type(DataType base, const std::vector<const Type*>& sub_list, const std::vector<std::string>& name_list):
        base(base),
        subSize(0),
        subElement(nullptr),
        subList(sub_list),
        nameList(name_list) {}

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
    static inline Type structure(const std::vector<const Type*>& sub_list) {
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
    static inline Type structure(const std::vector<const Type*>& sub_list, const std::vector<std::string>& name_list) {
        assert(sub_list.size() == name_list.size());
        return Type(DataType::STRUCT, sub_list, name_list);
    }

    static inline Type function(const Type* return_, const std::vector<const Type*>& subList) {
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

    static inline Type accelStruct(
        const std::vector<const Type*>& sub_list = std::vector<const Type*>{},
        const std::vector<std::string>& name_list = std::vector<std::string>{}
    ) {
        assert(sub_list.size() == name_list.size());
        return Type(DataType::ACCEL_STRUCT, sub_list, name_list);
    }

    static inline Type rayQuery() {
        return Type(DataType::RAY_QUERY, 0, nullptr);
    }

    /// @brief Creates an image type
    /// @param sampled the base type of the image. Should be a numeric scalar or void
    /// @param dim the number of dimensions. Ie a 1D image = 1, 2D = 2, 3D = 3. Max is 3
    /// @param comps integer defining the use and order of RGBA components. Each digit defines the order, starting
    ///              from 1 (0 indicates the component is unused). For example,
    ///              - comps = 1234 means that all channels of RGBA are included and given in order
    ///              - comps = 1000 means that only red is enabled
    ///              - comps = 2341 means that all components active in ARGB order
    /// @return the created image type
    static inline Type image(const Type* sampled, unsigned dim, unsigned comps) {
        assert(dim <= 3);      // max of 2 bits
        assert(comps <= 4321); // max of 13 bits
        return Type(DataType::IMAGE, (comps << 8) | dim, sampled);
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
        assert(base == DataType::ARRAY || base == DataType::IMAGE);
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

    inline unsigned getDim() const {
        assert(base == DataType::IMAGE);
        return subSize & 0x3;
    }

    inline unsigned getComps() const {
        assert(base == DataType::IMAGE);
        return subSize >> 8;
    }

    inline const std::vector<const Type*>& getFields() const {
        assert(base == DataType::STRUCT || base == DataType::ACCEL_STRUCT);
        return subList;
    }
    inline const std::vector<std::string>& getNames() const {
        assert(base == DataType::STRUCT || base == DataType::ACCEL_STRUCT);
        return nameList;
    }

    inline const Type& getPointedTo() const {
        assert(base == DataType::POINTER);
        return *subElement;
    }

    inline bool sameBase(const Type& rhs) const {
        // STRUCT and ACCEL_STRUCT are considered the same
        bool is_struct_and_accel_struct =
            (base == DataType::STRUCT && rhs.base == DataType::ACCEL_STRUCT) ||
            (base == DataType::ACCEL_STRUCT && rhs.base == DataType::STRUCT);

        return base == rhs.base || is_struct_and_accel_struct;
    }

    inline void nameMember(unsigned i, std::string name) noexcept(false) {
        assert(base == DataType::STRUCT || base == DataType::ACCEL_STRUCT);
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

    [[nodiscard]] Value* asValue() const override;
};
#endif
