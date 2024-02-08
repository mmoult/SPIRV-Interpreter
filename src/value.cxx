module;
#include <cassert>
#include <cstdint> // for uint32_t and int32_t
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>
export module value;

enum DataType : unsigned {
    FLOAT = 0,
    UINT = 1,
    INT = 2,
    BOOL = 3,
    COMPOSITE = 4,
    UNKNOWN = 5, // for empty arrays
};

export class Type {
    DataType primitive;
    std::map<std::string, Type*> structElements;
    const Type* arrayElement;
    unsigned arraySize;

public:
    Type(): arrayElement(nullptr), primitive(DataType::COMPOSITE) {}; // for struct
    Type(DataType primitive): primitive(primitive), arrayElement(nullptr) {};
    Type(unsigned array_size, const Type* element):
        primitive(DataType::COMPOSITE),
        arrayElement(element),
        arraySize(array_size) {};

    void addMember(std::string name, Type* type) {
        assert(primitive == DataType::COMPOSITE);
        structElements[name] = type;
    }

    void incrementSize() {
        assert(arrayElement != nullptr);
        arraySize++;
    }
    void setElement(Type* e) {
        assert(arrayElement != nullptr);
        assert(e != nullptr);
        arrayElement = e;
    }

    bool operator==(const Type& rhs) const {
        if (arrayElement != nullptr) { // array
            if (rhs.arrayElement == nullptr)
                return false;
            if (arraySize != rhs.arraySize)
                return false;
            return *arrayElement == *(rhs.arrayElement);
        }
        if (primitive != DataType::COMPOSITE) // primitive
            return primitive == rhs.primitive;

        return structElements == rhs.structElements; // struct
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
            cachedType = std::make_optional(describeType());
        return cachedType.value();
    }
};

export using ValueMap = std::map<std::string, const Value*>; 

export class Array : public Value  {
    Type elementType;
    std::vector<const Value*> elements;
public:
    Array(): elementType(Type(DataType::UNKNOWN)) {}

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
        return Type(elements.size(), &elementType);
    }
};

export class Struct : public Value  {

protected:
    virtual Type describeType() const override {
        return Type();
    }
};

export class Primitive : public Value {
    DataType type;

    union {
        float fp32;
        uint32_t u32;
        int32_t i32;
        bool b32;
    } data;

public:
    Primitive(float fp32) : type(DataType::FLOAT) {
        data.fp32 = fp32;
    }
    Primitive(uint32_t u32) : type(DataType::UINT) {
        data.u32 = u32;
    }
    Primitive(int32_t i32) : type(DataType::INT) {
        data.i32 = i32;
    }
    Primitive(bool b32) : type(DataType::BOOL) {
        data.b32 = b32;
    }

protected:
    virtual Type describeType() const override {
        return Type(type);
    }
};
