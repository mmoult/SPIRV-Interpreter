module;
#include <cassert>
#include <cstdint> // for uint32_t and int32_t
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>
export module value;

export enum DataType : unsigned {
    FLOAT = 0,
    UINT = 1,
    INT = 2,
    BOOL = 3,
    COMPOSITE = 4,
    ARRAY = 5,
    ANY = 6, // for empty arrays
    // Above is usable in TOML, below only internal to SPIR-V
    VOID = 7,
    FUNCTION = 8,
    POINTER = 9,
};

export class Type {
    DataType base;
    unsigned subSize;
    const Type* subElement;
    std::vector<const Type*> subList;
    std::vector<std::string> nameList;

    Type(DataType base, unsigned sub_size, const Type* sub_element):
        base(base),
        subSize(sub_size),
        subElement(sub_element) {}

public:
    // Factory methods to create type variants:

    /*** @brief Factory for floats, uints, ints, bools, voids
     *
     * May define a custom size (assuming the interpreter supports it), but the default is 32.
     * @param primitive the primitive type to use. Should not use any but float, uint, int, bool, or void
     * @param size the size of the type. Not all primitives have a usable size (bool and void don't)
     */
    static Type primitive(DataType primitive, unsigned size = 32) {
        assert(size == 32 || (primitive != DataType::BOOL && primitive != DataType::VOID && primitive != DataType::ANY));
        assert(primitive != DataType::COMPOSITE && primitive != DataType::FUNCTION &&
               primitive != DataType::POINTER);
        return Type(primitive, size, nullptr);
    }

    static Type array(unsigned array_size, const Type& element) {
        return Type(DataType::ARRAY, array_size, &element);
    }

    static Type struct_() {
        return Type(DataType::COMPOSITE, 0, nullptr);
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

    void addMember(std::string name, Type* type) {
        assert(base == DataType::COMPOSITE);
        // TODO must add through list- spirv is not required to give name for each field in struct
    }

    void incrementSize() {
        // Must be an array to increment size
        assert(base == DataType::ARRAY);
        subSize++;
    }
    void setElement(Type* e) {
        assert(base == DataType::ARRAY);
        assert(e != nullptr);
        subElement = e;
    }

    bool operator==(const Type& rhs) const {
        if (base != rhs.base)
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
        case DataType::ANY:
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
};

export class Value {

protected:
    virtual Type describeType() const = 0;
    std::optional<Type> cachedType;

public:
    virtual ~Value() = default;

    const Type& getType() {
        if (!cachedType.has_value())
            cachedType = std::optional(describeType());
        return cachedType.value();
    }
};

export using ValueMap = std::map<std::string, const Value*>;

export class Array : public Value  {
    Type elementType;
    std::vector<const Value*> elements;
public:
    Array(): elementType(Type::primitive(DataType::ANY)) {}

    virtual ~Array() {
        for (const auto& e : elements)
            delete e;
    }
    Array(const Array& other) = delete;
    Array& operator=(const Array& other) = delete;

    bool addElement(Value* e) {
        // Set the element type to the intersection of all
        // If cannot cast any elements to some intersection type, we have a problem
        // TODO casting of elements and what not
        elementType = e->getType();

        elements.push_back(e);
        if (cachedType.has_value()) { // update the cached type, if any
            cachedType->incrementSize();
            // The element type, even if modified, is at the same pointer location: nothing to update!
        }
        return true;
    }

protected:
    virtual Type describeType() const override {
        return Type::array(elements.size(), elementType);
    }
};

export class Struct : public Value  {

protected:
    virtual Type describeType() const override {
        return Type::struct_();
    }
};

export class Primitive : public Value {

    union {
        float fp32;
        uint32_t u32;
        int32_t i32;
        bool b32;
    } data;

public:
    Primitive(float fp32, unsigned size = 32) {
        data.fp32 = fp32;
        cachedType = std::optional(Type::primitive(DataType::FLOAT, size));
    }
    Primitive(uint32_t u32, unsigned size = 32) {
        data.u32 = u32;
        cachedType = std::optional(Type::primitive(DataType::UINT, size));
    }
    Primitive(int32_t i32, unsigned size = 32) {
        data.i32 = i32;
        cachedType = std::optional(Type::primitive(DataType::INT, size));
    }
    Primitive(bool b32) {
        data.b32 = b32;
        cachedType = std::optional(Type::primitive(DataType::BOOL));
    }

protected:
    virtual Type describeType() const override {
        assert(false);
        return Type::primitive(DataType::ANY);
    }
};
