module;
#include <algorithm> // for min
#include <cassert>
#include <cstdint> // for uint32_t and int32_t
#include <exception>
#include <map>
#include <sstream>
#include <stdexcept>
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
    // Above is usable in TOML, below only internal to SPIR-V
    VOID = 6,
    FUNCTION = 7,
    POINTER = 8,
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

    /// @brief Creates a value corresponding to this type, with optional inputs
    /// @param values an optional vector of values to use
    /// @return a new value whose ownership belongs to the caller
    /// @throws if the type cannot be constructed
    [[nodiscard]] Value* construct(std::vector<const Value*>* values) const noexcept(false);

public:
    // Factory methods to create type variants:

    /// @brief Factory for floats, uints, ints, bools, voids
    /// May define a custom size (assuming the interpreter supports it), but the default is 32.
    /// @param primitive the primitive type to use (should not use composite, function, or pointer)
    /// @param size the size of the type. Not all primitives have a usable size (bool and void don't)
    /// @return the created type
    static Type primitive(DataType primitive, unsigned size = 32) {
        assert(primitive != DataType::COMPOSITE && primitive != DataType::FUNCTION &&
               primitive != DataType::POINTER);
        assert(size == 32 || (primitive != DataType::BOOL && primitive != DataType::VOID));
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

    void addMember(std::string name, Type* type) {
        assert(base == DataType::COMPOSITE);
        // TODO must add through list- spirv is not required to give name for each field in struct
    }

    void setElement(const Type& e) {
        assert(base == DataType::ARRAY);
        subElement = &e;
    }
    const Type& getElement() const {
        assert(base == DataType::ARRAY);
        return *subElement;
    }
    unsigned getSize() const {
        assert(base == DataType::ARRAY);
        return subSize;
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

class Value {
protected:
    Type type;

    void newline(std::stringstream& dst, unsigned indents) const {
        dst << '\n';
        for (unsigned i = 0; i < indents; ++i)
            dst << "  ";
    }

public:
    Value(Type type): type(type) {}
    virtual ~Value() = default;

    const Type& getType() const { return type; }

    /// @brief Copy the value into this
    /// The Value implementation of this method does NOT perform the copy, it just throws
    /// a failure if the copy cannot be done.
    /// The subclass is responsible for defining an implementation which will handle the copy
    /// logic
    /// @param new_val the value to copy from
    virtual void copyFrom(const Value& new_val) noexcept(false) {
        if (!new_val.getType().sameBase(getType()))
            throw std::runtime_error("Cannot copy from value of different type!");
    }

    virtual void print(std::stringstream& dst, unsigned indents = 0) const = 0;

    virtual bool isNested() const = 0;

    virtual bool equals(const Value& val) const {
        return type == val.type;
    }
};

export using ValueMap = std::map<std::string, const Value*>;

export class Array : public Value  {
    // Array owns and manages the memory of all elements.
    std::vector<Value*> elements;

public:
    Array(const Type& sub_element, unsigned size): Value(Type::array(size, sub_element)) {}

    virtual ~Array() {
        for (const auto& e : elements)
            delete e;
    }
    Array(const Array& other) = delete;
    Array& operator=(const Array& other) = delete;

    void setType(Type& element_type, unsigned size) {
        type = Type::array(size, element_type);
    }

    void addElements(std::vector<const Value*>& es) noexcept(false) {
        // Test that the size matches the current type's:
        if (unsigned tsize = type.getSize(); es.size() != tsize) {
            std::stringstream err;
            err << "Could not add " << es.size() << " elements to array of size " << tsize << "!";
            throw std::runtime_error(err.str());
        }
        for (const Value* e: es) {
            // Construct an element from the element type, then copy data from e to it.
            Value* val = type.getElement().construct();
            try {
                val->copyFrom(*e);
            } catch (std::exception& e) {
                delete &val;
                std::stringstream err;
                err << "Could not add array element because: " << e.what() <<  "!";
                throw std::runtime_error(err.str());
            }
            elements.push_back(val);
        }
    }
    void dummyFill() noexcept(false) {
        unsigned tsize = type.getSize();
        for (unsigned i = 0; i < tsize; ++i) {
            Value* val = type.getElement().construct();
            elements.push_back(val);
        }
    }

    unsigned getSize() const { return elements.size(); }

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

        // Do the actual copy now
        const Array& other = static_cast<const Array&>(new_val);
        if (unsigned osize = other.getSize(); osize != elements.size()) {
            std::stringstream err;
            err << "Cannot copy array of size " << osize << " into array of size " << elements.size() << "!";
            throw std::runtime_error(err.str());
        }
        for (unsigned i = 0; i < elements.size(); ++i)
            elements[i]->copyFrom(*other.elements[i]);
    }

    virtual void print(std::stringstream& dst, unsigned indents = 0) const override {
        bool noNested = true;
        for (const auto& element: elements)
            noNested &= !element->isNested();

        if (noNested) {
            dst << "[ ";
            bool first = true;
            for (const auto& element: elements) {
                if (first)
                    first = false;
                else
                    dst << ", ";
                element->print(dst, indents + 1);
            }
            dst << " ]";
        } else {
            // If at least one element is nested, put each on its own line
            dst << '[';
            for (const auto& element: elements) {
                newline(dst, indents + 1);
                element->print(dst, indents + 1);
                dst << ',';
            }
            newline(dst, indents);
            dst << ']';
        }
    }

    bool isNested() const override {
        return true;
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        const auto& other = static_cast<const Array&>(val);
        // Shouldn't have to test lengths since that is encoded in the type
        for (unsigned i = 0; i < elements.size(); ++i) {
            if (!elements[i]->equals(*other.elements[i]))
                return false;
        }
        return true;
    }
};

export class Struct : public Value {

public:
    Struct(): Value(Type::struct_()) {}

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

        // Do the actual copy now
        const Struct& other = static_cast<const Struct&>(new_val);
        throw std::runtime_error("Unimplemented function!");
    }

    void print(std::stringstream& dst, unsigned indents = 0) const override {
        assert(false); // unimplemented!
    }

    bool isNested() const override {
        return true;
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        
        assert(false); // unimplemented!
        return false;
    }
};

export class Pointer : public Value {
    /// @brief A list of indices. The first points to an index of Data, any/all others point to indices within previous
    std::vector<unsigned> to;

public:
    Pointer(std::vector<unsigned> to, Type t): Value(t), to(to) {}

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

        // Do the actual copy now
        const Pointer& other = static_cast<const Pointer&>(new_val);
        throw std::runtime_error("Unimplemented function!");
    }

    virtual void print(std::stringstream& dst, unsigned indents = 0) const override {
        dst << "[ ";
        bool first = true;
        for (unsigned u: to) {
            if (first)
                first = false;
            else
                dst << ", ";
            dst << u;
        }
        dst << " ]";
    }

    bool isNested() const override {
        return true;
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        const auto& other = static_cast<const Pointer&>(val);
        // I cannot think of why this would be used, but implement it in case...
        if (to.size() != other.to.size())
            return false;
        for (unsigned i = 0; i < to.size(); ++i) {
            if (to[i] != other.to[i])
                return false;
        }
        return true;
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

    void copyFrom(const Value& new_val) noexcept(false) override {
        Value::copyFrom(new_val);

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
                throw std::runtime_error("Cannot convert to float!");
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
                throw std::runtime_error("Cannot convert to uint!");
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
                throw std::runtime_error("Cannot convert to int!");
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
                throw std::runtime_error("Cannot convert to bool!");
            }
            break;
        default:
            assert(false);
        }
    }

    /// @brief changes the type of the primitive *without* changing the value
    void cast(Type t) {
        type = t;
    }

    virtual void print(std::stringstream& dst, unsigned indents = 0) const override {
        switch (type.getBase()) { // copy from
        case DataType::FLOAT:
            dst << data.fp32;
            break;
        case DataType::UINT:
            dst << data.u32;
            break;
        case DataType::INT:
            dst << data.i32;
            break;
        case DataType::BOOL:
            if (data.i32)
                dst << "true";
            else
                dst << "false";
            break;
        default:
            assert(false); // should not be possible to have another type!
        }
    }

    bool isNested() const override {
        return false;
    }

    bool equals(const Value& val) const override {
        if (!Value::equals(val)) // guarantees matching types
            return false;
        const auto& other = static_cast<const Primitive&>(val);
        switch (type.getBase()) {
        case FLOAT:
            // Naive float comparison for the time being
            return data.fp32 == other.data.fp32;
        case UINT:
            return data.u32 == other.data.u32;
        case INT:
            return data.i32 == other.data.i32;
        case BOOL:
            return data.b32 == other.data.b32;
        case VOID:
            return true; // I don't know why this would happen, but just in case...
        default:
            assert(false);
            return false;
        }
    }
};

Value* Type::construct(std::vector<const Value*>* values) const {
    switch (base) {
    default:
        throw std::runtime_error("Unsupported type!");
    case DataType::VOID:
        throw std::runtime_error("Cannot construct void type!");
    // Primitive types
    case DataType::FLOAT:
    case DataType::UINT:
    case DataType::INT:
    case DataType::BOOL: {
        Primitive* prim = new Primitive(*this);
        if (values == nullptr)
            return prim;
        if (values->empty() || values->size() != 1) {
            delete prim;
            throw std::runtime_error("Cannot construct primitive from nonzero number of inputs!");
        }

        const Value* val = (*values)[0];
        try {
            prim->copyFrom(*val);
        } catch (std::exception& e) {
            delete prim;
            throw e;
        }
        return prim;
    }
    case DataType::ARRAY: {
        Array* arr = new Array(*subElement, subSize);
        // try to populate the array with each of the entries
        if (values != nullptr)
            arr->addElements(*values);
        else
            arr->dummyFill();
        return arr;
    }
    // TODO support other types
    }
}

Type Type::unionOf(std::vector<const Value*> elements) {
    if (elements.empty())
        throw std::invalid_argument("Cannot find union of types in empty vector!");
    Type t = elements[0]->getType();
    for (unsigned i = 1; i < elements.size(); ++i)
        t = t.unionOf(elements[i]->getType());
    return t;
}
