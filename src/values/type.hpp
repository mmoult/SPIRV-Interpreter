/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUES_TYPE_HPP
#define VALUES_TYPE_HPP

#include <bit>  // for std::bit_cast
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>  // for std::has_unique_object_representations_v
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
    SAMPLED_IMG,
    SAMPLER,
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
        SWITCH(SAMPLED_IMG)
        SWITCH(SAMPLER)
        SWITCH(COOP_MATRIX)
    default:
        assert(false);  // unhandled case!
    }
#undef SWITCH
    return os;
}

/// @brief The dimensionality of an image, mirroring SPIR-V's Dim operand.
/// Values are deliberately independent of SPIR-V headers. Users are responsible for mapping between enums.
enum class ImageDim : unsigned {
    D1 = 0,
    D2,
    D3,
    CUBE,
    RECT,
    BUFFER,
    SUBPASS_DATA,
};

/// @brief Image type fields packed into subsize
/// The bit order is implementation-defined, but not relied upon. Unused guarantees the bit sum matches 32 bits for
/// easy conversion.
struct ImageFields {
    /// The image's dimensionality, held as an ImageDim. Three bits hold all seven values.
    unsigned dim : 3;
    /// SPIR-V's Arrayed operand: whether the image is a stack of independent layers. Orthogonal to dim, because layers
    /// are addressed by index, never interpolated between, and never shrink with the mipmap level.
    unsigned arrayed : 1;
    /// Describes the presence and order of RGBA components. Each digit defines the order, starting from 1 (0 indicates
    /// the component is unused). For example:
    /// - 1234: all channels of RGBA are included and given in that order
    /// - 1000: only red is enabled
    /// - 2341: all components active in ARGB order
    /// The largest valid value is therefore 4321, which takes 13 bits.
    unsigned comps : 13;
    unsigned unused : 15;
};
// Guarantees there are no undefined padding bits in the byte match
static_assert(std::has_unique_object_representations_v<ImageFields>, "ImageFields width must sum to exactly 32");

class Value;  // necessary forward reference

class Type final : public Valuable {
    DataType base;
    /// Overloaded by base, since no type is more than one of these:
    ///  - FLOAT, UINT, INT: the precision, in bits
    ///  - ARRAY, COOP_MATRIX: the number of elements
    ///  - POINTER: the storage class
    ///  - IMAGE: an ImageFields, to be read back through bit_cast
    uint32_t subSize;
    // memory for subElement and subList elements is NOT managed by the Type
    // In other words, the original allocator is expected to deallocate or transfer ownership
    const Type* subElement;
    std::vector<const Type*> subList;
    std::vector<std::string> nameList;

    std::string name;
    /// Overloaded by base, since no type is both a struct and a cooperative matrix:
    ///  - STRUCT: nonzero if and only if decorated with BufferBlock
    ///  - COOP_MATRIX: the number of rows in the matrix
    unsigned rowsOrBufferBlock = 0;

    inline Type(DataType base, uint32_t sub_size, const Type* sub_element)
        : base(base), subSize(sub_size), subElement(sub_element) {}

    inline Type(DataType base, const std::vector<const Type*>& sub_list, const std::vector<std::string>& name_list)
        : base(base), subSize(0), subElement(nullptr), subList(sub_list), nameList(name_list) {}

public:
    inline Type() noexcept(true) : base(DataType::VOID), subSize(0), subElement(nullptr) {}
    // Copy, move, and assignment are all implicit.

    // Factory methods to create type variants:

    /// @brief Factory for floats, uints, ints, bools, voids
    /// May define a custom size (assuming the interpreter supports it), but the default is 32.
    /// @param primitive the primitive type to use (should not use STRUCT, function, or pointer)
    /// @param size the size of the type. Not all primitives have a usable size (bool and void don't)
    /// @return the created type
    static inline Type primitive(DataType primitive, unsigned size = 32) {
        assert(isPrimitive(primitive));
        assert(size == 32 || (primitive != DataType::BOOL));
        return Type(primitive, size, nullptr);
    }

    /// @brief Construct an array type
    /// @param array_size the size of the array, in elements. Must be > 0 for regular arrays, == 0 for runtime arrays.
    /// @param element a Type which will outlive this Type. Ownership is not transferred to the constructed array. In
    ///                other words, the allocator is expected to deallocate element some time after the deallocation of
    ///                this array
    static inline Type array(unsigned array_size, const Type& element) {
        return Type(DataType::ARRAY, array_size, &element);
    }

    /// @brief Construct a cooperative matrix type TODO
    /// @param scope the scope that components are spread across. Currently unused
    /// @param major the number of rows in the matrix
    /// @param minor the number of columns in the matrix
    /// @param element the type of each matrix element
    static inline Type coopMatrix(unsigned /* scope */, unsigned rows, unsigned cols, const Type& element) {
        // rows and cols come from module operands. construct() divides by rows, so a zero would be a division by
        // zero rather than a diagnosable error.
        if (rows == 0 || cols == 0)
            throw std::invalid_argument("Cooperative matrix must have at least one row and one column!");
        // The scope is a useful hint for compilation by indicating where the data should be stored. Not needed here.
        Type ret(DataType::COOP_MATRIX, rows * cols, &element);
        ret.rowsOrBufferBlock = rows;
        return ret;
    }

    /// @brief Construct a structure type
    /// @param sub_list a list of non-null types. Each Type must outlive the struct created here. Ownership is not
    ///                 transferred- meaning that the original allocator is expected to deallocate some time after the
    ///                 deallocation of this struct
    static inline Type structure(const std::vector<const Type*>& sub_list) {
        std::vector<std::string> names(sub_list.size());
        std::fill(names.begin(), names.end(), "");
        return Type(DataType::STRUCT, sub_list, names);
    }
    /// @brief Construct a structure type
    /// @param sub_list a list of non-null types. Each Type must outlive the struct created here. Ownership is not
    ///                 transferred- meaning that the original allocator is expected to deallocate some time after the
    ///                 deallocation of this struct
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

    static inline Type pointer(const Type& point_to, uint32_t storage) {
        return Type(DataType::POINTER, storage, &point_to);
    }
    static inline Type forwardPointer() {
        return Type(DataType::POINTER, -1, nullptr);
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
    /// @param dim the dimensionality
    /// @param arrayed whether the image is a stack of independent layers
    /// @param comps integer defining the use and order of RGBA components.
    /// @return the created image type
    static inline Type image(const Type* texel_type, ImageDim dim, bool arrayed, unsigned comps) {
        assert(comps <= 4321);  // the largest meaningful component order
        // Vulkan forbids arraying a 3D image: that would be a 4D texture, which does not exist.
        assert(!(arrayed && dim == ImageDim::D3));
        const ImageFields fields {
            .dim = static_cast<unsigned>(dim),
            .arrayed = arrayed ? 1u : 0u,
            .comps = comps,
            .unused = 0,
        };
        // bit_cast statically verifies conversion matches the number of bytes
        return Type(DataType::IMAGE, std::bit_cast<decltype(Type::subSize)>(fields), texel_type);
    }

    static inline Type sampledImage(const Type* image) {
        return Type(DataType::SAMPLED_IMG, 0, image);
    }

    static inline Type sampler() {
        return Type(DataType::SAMPLER, 0, nullptr);
    }

    // Other methods:

    /// @brief Creates a value corresponding to this type
    /// @param undef whether the values constructed should be undefined
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] Value* construct(bool undef = true) const noexcept(false);

    inline const Type& getElement() const {
        assert(
            base == DataType::ARRAY || base == DataType::IMAGE || base == DataType::SAMPLED_IMG ||
            base == DataType::COOP_MATRIX
        );
        return *subElement;
    }
    inline unsigned getSize() const {
        assert(base == DataType::ARRAY || base == DataType::COOP_MATRIX);
        return subSize;
    }

    inline unsigned getPrecision() const {
        assert(isPrimitive());
        return subSize;
    }

    inline ImageDim getImageDim() const {
        assert(base == DataType::IMAGE);
        return static_cast<ImageDim>(std::bit_cast<ImageFields>(subSize).dim);
    }

    inline bool isArrayed() const {
        assert(base == DataType::IMAGE);
        return std::bit_cast<ImageFields>(subSize).arrayed != 0;
    }

    /// @brief How many axes the texel data extends along, NOT counting layers.
    /// A cube map counts as two: each of its six faces is a square, and the faces are layers rather than a third axis.
    inline unsigned getSpatialDims() const {
        switch (getImageDim()) {
        case ImageDim::D1:
        case ImageDim::BUFFER:
            return 1;
        case ImageDim::D2:
        case ImageDim::RECT:
        case ImageDim::SUBPASS_DATA:
        case ImageDim::CUBE:
            return 2;
        case ImageDim::D3:
            return 3;
        }
        assert(false && "unhandled ImageDim!");
        return 1;
    }

    /// @brief Whether the texel data is divided into layers, which are addressed by index rather than interpolated.
    /// True for any arrayed image, and for every cube map, since a cube map's six faces are layers.
    inline bool hasLayers() const {
        return isArrayed() || getImageDim() == ImageDim::CUBE;
    }

    /// @brief How many components a coordinate operand carries for a sampling operation.
    /// A cube map takes three, but they are a direction vector rather than a position: the largest of the three
    /// selects a face and the other two are projected onto it.
    inline unsigned getCoordCount() const {
        const unsigned spatial = (getImageDim() == ImageDim::CUBE) ? 3 : getSpatialDims();
        return spatial + (isArrayed() ? 1 : 0);
    }

    inline unsigned getComps() const {
        assert(base == DataType::IMAGE);
        return std::bit_cast<ImageFields>(subSize).comps;
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
    inline void unforward(const Type& point_to, unsigned storage) {
        assert(base == DataType::POINTER);
        this->subElement = &point_to;
        this->subSize = storage;
    }
    inline uint32_t getStorage() const {
        assert(base == DataType::POINTER);
        return this->subSize;
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
        this->rowsOrBufferBlock = 1;
    }
    inline bool isBufferBlock() const {
        return this->base == DataType::STRUCT && this->rowsOrBufferBlock != 0;
    }

    inline void setNumRows(unsigned rows) {
        assert(this->base == DataType::COOP_MATRIX);
        this->rowsOrBufferBlock = rows;
    }
    inline unsigned getNumRows() const {
        assert(this->base == DataType::COOP_MATRIX);
        return rowsOrBufferBlock;
    }

    inline bool isPrimitive() const {
        return Type::isPrimitive(base);
    }
    static bool isPrimitive(DataType base) {
        switch (base) {
        case DataType::FLOAT:
        case DataType::UINT:
        case DataType::INT:
        case DataType::BOOL:
            return true;
        default:
            return false;
        }
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
    static Type unionOf(const std::vector<const Value*>& elements, std::vector<const Type*>& created) noexcept(false);

    /// @brief Returns the type which is general to this and other
    /// Must follow the same conversion rules as void Value::copyFrom(const Value& new_val)
    /// @param other the other type
    /// @param created a list of created types to be deleted after the call
    /// @return the general type common between this and other
    /// @throws if no such union type can be found
    Type unionOf(const Type& other, std::vector<const Type*>& created) const noexcept(false);

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
    bool isArray() const {
        return base == DataType::ARRAY || base == DataType::COOP_MATRIX;
    }

    [[nodiscard]] Value* asValue() const override;
};
#endif
