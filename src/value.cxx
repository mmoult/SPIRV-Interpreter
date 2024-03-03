module;
#include <cassert>
#include <cstdint> // for uint32_t and int32_t
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

import utils;
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

// necessary forward reference
export class Value;

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

    Utils::May<Value*> construct(std::vector<Value*>* values) const;

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

    // Construct a value from the type
    Utils::May<Value*> construct() const {
        return construct(nullptr);
    }
    Utils::May<Value*> construct(std::vector<Value*>& values) const {
        return construct(&values);
    }

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

    const Type& getPointedTo() const {
        assert(base == DataType::POINTER);
        return *subElement;
    }

    bool sameBase(const Type& rhs) const {
        return base == rhs.base;
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

    DataType getBase() const {
        return base;
    }
};

class Value {
protected:
    Type type;

public:
    Value(Type type): type(type) {}
    virtual ~Value() = default;

    const Type& getType() const { return type; }

    virtual Utils::May<bool> copyFrom(const Value& new_val) {
        if (!new_val.getType().sameBase(getType()))
            return Utils::unexpected<bool>("Cannot copy from value of different type!");
        return Utils::expected();
    }
};

export using ValueMap = std::map<std::string, const Value*>;

export class Array : public Value  {
    std::vector<Value*> elements;
public:
    Array(): Value(Type::array(0, Type::primitive(DataType::ANY))) {}

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

        elements.push_back(e);
        type.incrementSize();
        return true;
    }

    unsigned getSize() const { return elements.size(); }

    Utils::May<bool> copyFrom(const Value& new_val) override {
        auto res = Value::copyFrom(new_val);
        if (!res)
            return res;

        // Do the actual copy now
        const Array& other = static_cast<const Array&>(new_val);
        if (unsigned osize = other.getSize(); osize != elements.size()) {
            std::stringstream err;
            err << "Cannot copy array of size " << osize << " into array of size " << elements.size() << "!";
            return Utils::unexpected<bool>(err.str());
        }
        for (unsigned i = 0; i < elements.size(); ++i) {
            if (auto eres = elements[i]->copyFrom(*other.elements[i]); !eres)
                return eres;
        }

        return res; // must have been success to get here
    }
};

export class Struct : public Value {
public:
    Struct(): Value(Type::struct_()) {}

    Utils::May<bool> copyFrom(const Value& new_val) override {
        auto res = Value::copyFrom(new_val);
        if (!res)
            return res;

        // Do the actual copy now
        const Struct& other = static_cast<const Struct&>(new_val);
        return Utils::unexpected<bool>("Unimplemented function!");
    }
};

export class Pointer : public Value {
    /// @brief A list of indices. The first points to an index of Data, any/all others point to indices within previous
    std::vector<unsigned> to;

public:
    Pointer(std::vector<unsigned> to, Type t): Value(t), to(to) {}

    Utils::May<bool> copyFrom(const Value& new_val) override {
        auto res = Value::copyFrom(new_val);
        if (!res)
            return res;

        // Do the actual copy now
        const Pointer& other = static_cast<const Pointer&>(new_val);
        return Utils::unexpected<bool>("Unimplemented function!");
    }
};

export struct Primitive : public Value {

    union {
        float fp32;
        uint32_t u32;
        int32_t i32;
        bool b32;
    } data;

public:
    Primitive(float fp32, unsigned size = 32): Value(Type::primitive(DataType::FLOAT, size)) {
        data.fp32 = fp32;
    }
    Primitive(uint32_t u32, unsigned size = 32): Value(Type::primitive(DataType::UINT, size)) {
        data.u32 = u32;
    }
    Primitive(int32_t i32, unsigned size = 32): Value(Type::primitive(DataType::INT, size)) {
        data.i32 = i32;
    }
    Primitive(bool b32): Value(Type::primitive(DataType::BOOL)) {
        data.b32 = b32;
    }
    // Create a blank primitive from the given value
    Primitive(Type t): Value(t) {
        data.u32 = 0;
    }

public:
    Utils::May<bool> copyFrom(const Value& new_val) override {
        auto res = Value::copyFrom(new_val);
        if (!res)
            return res;

        // Do the actual copy now
        const Primitive& other = static_cast<const Primitive&>(new_val);
        const Type& from = other.getType();

        // TODO precision handling
        switch (type.getBase()) { // cast to
        case DataType::FLOAT:
            switch (from.getBase()) { // copy from
            case DataType::FLOAT:
                data.fp32 = other.data.fp32;
                break;
            case DataType::UINT:
                data.fp32 = static_cast<float>(other.data.u32);
                break;
            case DataType::INT:
                data.fp32 = static_cast<float>(other.data.i32);
                break;
            default:
                return Utils::unexpected<bool>("Cannot cast to float!");
            }
            break;
        case DataType::UINT:
            switch (from.getBase()) {
            case DataType::UINT:
                data.u32 = other.data.u32;
                break;
            default:
                // No int -> uint since if it was int, it is probably negative
                // No float -> uint since if it was float, probably had decimal component
                return Utils::unexpected<bool>("Cannot cast to uint!");
            }
            break;
        case DataType::INT:
            switch (from.getBase()) {
            case DataType::UINT:
                // TODO verify that it is not too large
                data.i32 = static_cast<int32_t>(other.data.u32);
                break;
            case DataType::INT:
                data.i32 = other.data.i32;
                break;
            default:
                return Utils::unexpected<bool>("Cannot cast to int!");
            }
            break;
        case DataType::BOOL:
            switch (from.getBase()) {
            case DataType::BOOL:
                data.b32 = other.data.b32;
            case DataType::UINT:
                data.b32 = other.data.u32 != 0;
                break;
            default:
                return Utils::unexpected<bool>("Cannot cast to bool!");
            }
            break;
        default:
            assert(false);
        }

        return res; // must have been success to get here
    }

    /// @brief changes the type of the primitive *without* changing the value
    void cast(Type t) {
        type = t;
    }
};

Utils::May<Value*> Type::construct(std::vector<Value*>* values) const {
    switch (base) {
    default:
        return Utils::unexpected<Value*>("Unsupported type!");
    case DataType::ANY:
        return Utils::unexpected<Value*>("Cannot construct any type!");
    case DataType::VOID:
        return Utils::unexpected<Value*>("Cannot construct void type!");
    // Primitive types
    case DataType::FLOAT:
    case DataType::UINT:
    case DataType::INT:
    case DataType::BOOL: {
        Primitive* prim = new Primitive(*this);
        if (values == nullptr)
            return Utils::expected<Value*>(prim);
        if (values->empty() || values->size() != 1) {
            delete prim;
            return Utils::unexpected<Value*>("Cannot construct primitive from nonzero number of inputs!");
        }

        Value* val = (*values)[0];
        if (auto res = prim->copyFrom(*val); !res) {
            delete prim;
            return Utils::unexpected<Value*>(res.error());
        }
        return Utils::expected<Value*>(prim);
    }
    case DataType::ARRAY: {
        Array* arr = new Array();
        // try to populate the array with each of the entries
        if (values != nullptr) {
            if (subSize != values->size()) {
                delete arr;
                std::stringstream err;
                err << "Could not construct array of size " << subSize << " from " << values->size() << " elements!";
                return Utils::unexpected<Value*>(err.str());
            }
            for (unsigned i = 0; i < values->size(); ++i) {
                if (!arr->addElement((*values)[i])) {
                    delete arr;
                    std::stringstream err;
                    err << "Could not add array element " << i << "!";
                    return Utils::unexpected<Value*>(err.str());
                }
            }
        } else {
            // create a dummy for each necessary entry
            while (arr->getSize() < subSize) {
                auto res = subElement->construct();
                if (!res)
                    return Utils::unexpected<Value*>(res.error());
                arr->addElement(res.value());
            }
        }
        return Utils::expected<Value*>(arr);
    }
    // TODO support other types
    }
}
