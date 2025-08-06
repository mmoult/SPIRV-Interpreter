/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_TYPE_HPP
#define VALUES_TYPE_HPP

#include <algorithm>  // for min
#include <cassert>
#include <iostream>
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
    // Above is usable in YAML/JSON input, below only internal to SPIR-V
    VOID,
    FUNCTION,
    POINTER,
    ACCEL_STRUCT,
    RAY_QUERY,
    IMAGE,
    SAMPLER,  // image sampler, to be specific
    COOP_MATRIX,
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
        SWITCH(SAMPLER)
        SWITCH(COOP_MATRIX)
    default:
        assert(false);  // unhandled case!
    }
#undef SWITCH
    return os;
}

// necessary forward reference
class Value;

class Type final : public Valuable {
    DataType base;
    unsigned subSize;
    // memory for subElement and subList elements is NOT managed by the Type
    // In other words, the original allocator is expected to deallocate or transfer ownership
    const Type* subElement;
    std::vector<const Type*> subList;
    std::vector<std::string> nameList;

    std::string name;
    union {
        bool bufferBlock;  // for struct
        unsigned rows;  // for cooperative matrix
    };

    inline Type(DataType base, unsigned sub_size, const Type* sub_element)
        : base(base), subSize(sub_size), subElement(sub_element), rows(0) {}

    inline Type(DataType base, const std::vector<const Type*>& sub_list, const std::vector<std::string>& name_list)
        : base(base), subSize(0), subElement(nullptr), subList(sub_list), nameList(name_list), rows(0) {}

    /// @brief Creates a value corresponding to this type, with optional inputs
    /// @param values an optional vector of values to use
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] Value* construct(std::vector<const Value*>* values) const noexcept(false);

public:
    inline Type() noexcept(true) : base(DataType::VOID), subSize(0), subElement(nullptr) {}
    Type(const Type& t) = default;

    // Factory methods to create type variants:

    /// @brief Factory for floats, uints, ints, bools, voids
    /// May define a custom size (assuming the interpreter supports it), but the default is 32.
    /// @param primitive the primitive type to use (should not use STRUCT, function, or pointer)
    /// @param size the size of the type. Not all primitives have a usable size (bool and void don't)
    /// @return the created type
    static inline Type primitive(DataType primitive, unsigned size = 32) {
        assert(
            primitive == DataType::UINT || primitive == DataType::INT || primitive == DataType::FLOAT ||
            primitive == DataType::BOOL
        );
        assert(size == 32 || (primitive != DataType::BOOL));
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

    /// @brief Construct a cooperative matrix type TODO
    /// @param scope the scope that components are spread across. Currently unused
    /// @param major the number of rows in the matrix
    /// @param minor the number of columns in the matrix
    /// @param element the type of each matrix element
    static inline Type coopMatrix(unsigned scope, unsigned rows, unsigned cols, const Type& element) {
        // The scope is a useful hint for compilation by indicating where the data should be stored. Not needed here.
        Type ret(DataType::COOP_MATRIX, rows * cols, &element);
        ret.rows = rows;
        return ret;
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

    static inline Type accelStruct() {
        return Type(DataType::ACCEL_STRUCT, 0, nullptr);
    }

    static inline Type rayQuery() {
        return Type(DataType::RAY_QUERY, 0, nullptr);
    }

    /// @brief Creates an image type
    /// @param texel_type the base type of the image. Should be a numeric scalar or void
    /// @param dim the number of dimensions. Ie a 1D image = 1, 2D = 2, 3D = 3. Max is 3
    /// @param comps integer defining the use and order of RGBA components. Each digit defines the order, starting
    ///              from 1 (0 indicates the component is unused). For example,
    ///              - comps = 1234 means that all channels of RGBA are included and given in order
    ///              - comps = 1000 means that only red is enabled
    ///              - comps = 2341 means that all components active in ARGB order
    /// @return the created image type
    static inline Type image(const Type* texel_type, unsigned dim, unsigned comps) {
        assert(dim <= 3);  // max of 2 bits
        assert(comps <= 4321);  // max of 13 bits
        return Type(DataType::IMAGE, (comps << 8) | dim, texel_type);
    }

    static inline Type sampler(const Type* image) {
        return Type(DataType::SAMPLER, 0, image);
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
        assert(
            base == DataType::ARRAY || base == DataType::IMAGE || base == DataType::SAMPLER ||
            base == DataType::COOP_MATRIX
        );
        return *subElement;
    }
    inline unsigned getSize() const {
        assert(base == DataType::ARRAY || base == DataType::COOP_MATRIX);
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
        assert(base == DataType::STRUCT);
        return subList;
    }
    inline const std::vector<std::string>& getNames() const {
        assert(base == DataType::STRUCT);
        return nameList;
    }

    inline const Type& getPointedTo() const {
        assert(base == DataType::POINTER);
        return *subElement;
    }

    // TODO: deprecate or make private. The nearest valid use is operator==
    [[deprecated]]
    inline bool sameBase(const Type& rhs) const {
        return base == rhs.base;
    }

    inline void nameMember(unsigned i, const std::string& name) noexcept(false) {
        assert(base == DataType::STRUCT);
        if (i >= nameList.size())
            throw std::invalid_argument("Cannot name member at index beyond existing!");
        nameList[i] = name;
    }

    inline void setName(const std::string& name) {
        this->name = name;
    }
    inline const std::string& getName() const {
        return name;
    }

    inline void setBufferBlock() {
        assert(this->base == DataType::STRUCT);
        this->bufferBlock = true;
    }
    inline bool isBufferBlock() const {
        return this->base == DataType::STRUCT && this->bufferBlock;
    }

    inline void setNumRows(unsigned rows) {
        assert(this->base == DataType::COOP_MATRIX);
        this->rows = rows;
    }
    inline unsigned getNumRows() const {
        assert(this->base == DataType::COOP_MATRIX);
        return rows;
    }

    bool operator==(const Type& rhs) const;
    inline bool operator!=(const Type& rhs) const {
        return !(*this == rhs);
    };

    /// @brief Returns the type which is general to all elements
    /// Must follow the same conversion rules as void Value::copyFrom(const Value& new_val)
    /// @param elements the elements to find the most general type for
    /// @param created a list of created types to be deleted after the call
    /// @return the general type common to all elements
    /// @throws if no such union type can be found
    static Type unionOf(std::vector<const Value*> elements, std::vector<const Type*> created) noexcept(false);

    /// @brief Returns the type which is general to this and other
    /// Must follow the same conversion rules as void Value::copyFrom(const Value& new_val)
    /// @param other the other type
    /// @param created a list of created types to be deleted after the call
    /// @return the general type common between this and other
    /// @throws if no such union type can be found
    Type unionOf(const Type& other, std::vector<const Type*> created) const noexcept(false);

    void replaceSubElement(const Type* sub_element) {
        assert(sub_element != nullptr);
        assert(this->subElement != nullptr);
        this->subElement = sub_element;
    }
    void replaceFieldType(const Type* sub_element, unsigned index) {
        assert(sub_element != nullptr);
        assert(subList.size() > index);
        subList[index] = sub_element;
    }

    inline DataType getBase() const {
        return base;
    }

    [[nodiscard]] Value* asValue() const override;
};
#endif
