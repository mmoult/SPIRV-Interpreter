/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm> // for min
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

export module type;

export enum DataType : unsigned {
    FLOAT = 0,
    UINT = 1,
    INT = 2,
    BOOL = 3,
    STRUCT = 4,
    ARRAY = 5,
    // Above is usable in TOML, below only internal to SPIR-V
    VOID = 6,
    FUNCTION = 7,
    POINTER = 8,
};

// necessary forward reference
class Value;

export class Type {
    DataType base;
    unsigned subSize;
    // memory for subElement and subList elements is NOT managed by the Type
    // In other words, the original allocator is expected to deallocate or transfer ownership
    const Type* subElement;
    std::vector<const Type*> subList;
    std::vector<std::string> nameList;

    Type(DataType base, unsigned sub_size, const Type* sub_element):
        base(base),
        subSize(sub_size),
        subElement(sub_element) {}
    
    Type(std::vector<const Type*> sub_list, std::vector<std::string> name_list):
        base(DataType::STRUCT),
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
    static Type primitive(DataType primitive, unsigned size = 32) {
        assert(primitive != DataType::STRUCT && primitive != DataType::FUNCTION &&
               primitive != DataType::POINTER);
        assert(size == 32 || (primitive != DataType::BOOL && primitive != DataType::VOID));
        return Type(primitive, size, nullptr);
    }

    /// @brief Construct an array type
    /// @param array_size the size of the array. The number of elements
    /// @param element a Type which will outlive this Type. Must not be null. Ownership is not transferred
    ///                to the constructed array- in other words, the allocator is expected to deallocate element
    ///                some time after the deallocation of this array
    static Type array(unsigned array_size, const Type& element) {
        return Type(DataType::ARRAY, array_size, &element);
    }

    /// @brief Construct a structure type
    /// @param sub_list a list of non-null types. Each Type must outlive the struct created here. Ownership is not
    ///                 transferred- meaning that the original allocator is expected to deallocate some time after
    ///                 the deallocation of this struct
    static Type structure(std::vector<const Type*> sub_list) {
        std::vector<std::string> names(sub_list.size());
        std::fill(names.begin(), names.end(), "");
        return Type(sub_list, names);
    }
    /// @brief Construct a structure type
    /// @param sub_list a list of non-null types. Each Type must outlive the struct created here. Ownership is not
    ///                 transferred- meaning that the original allocator is expected to deallocate some time after
    ///                 the deallocation of this struct
    /// @param name_list a list of string names, corresponding to the Types at the same indices. Must have the same
    ///                  length as sub_list
    static Type structure(std::vector<const Type*> sub_list, std::vector<std::string> name_list) {
        assert(sub_list.size() == name_list.size());
        return Type(sub_list, name_list);
    }

    static Type function(const Type* return_, std::vector<Type*>& subList) {
        Type t(DataType::FUNCTION, 0, return_);
        t.subList.reserve(subList.size());
        for (const auto& ty : subList)
            t.subList.push_back(ty);
        return t;
    }

    static Type pointer(const Type& point_to) {
        return Type(DataType::POINTER, 0, &point_to);
    }

    // Other methods:

    /// @brief Creates a value corresponding to this type, filling in values with dummies as necessary
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] Value* construct() const noexcept(false) {
        return construct(nullptr);
    }
    /// @brief Creates a value corresponding to this type with given inputs (used for fields, elements, etc)
    /// @param values a vector of values to use
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] Value* construct(std::vector<const Value*>& values) const noexcept(false) {
        return construct(&values);
    }

    const Type& getElement() const {
        assert(base == DataType::ARRAY);
        return *subElement;
    }
    unsigned getSize() const {
        assert(base == DataType::ARRAY);
        return subSize;
    }

    const std::vector<const Type*>& getFields() const {
        assert(base == DataType::STRUCT);
        return subList;
    }
    const std::vector<std::string>& getNames() const {
        assert(base == DataType::STRUCT);
        return nameList;
    }

    const Type& getPointedTo() const {
        assert(base == DataType::POINTER);
        return *subElement;
    }

    bool sameBase(const Type& rhs) const {
        return base == rhs.base;
    }

    void nameMember(unsigned i, std::string& name) noexcept(false) {
        assert(base == DataType::STRUCT);
        if (i >= nameList.size())
            throw std::invalid_argument("Cannot name member at index beyond existing!");
        nameList[i] = name;
    }

    bool operator==(const Type& rhs) const {
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
    bool operator!=(const Type& rhs) const { return !(*this == rhs); };

    /// @brief Returns the type which is general to all elements
    /// Must follow the same conversion rules as void Value::copyFrom(const Value& new_val)
    /// @param elements the elements to find the most general type for
    /// @return the general type common to all elements
    /// @throws if no such union type can be found
    static Type unionOf(std::vector<const Value*> elements) noexcept(false);

    Type unionOf(const Type& other) const noexcept(false) {
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

    DataType getBase() const {
        return base;
    }
};
