module;
#include <cassert>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>

#include "../external/spirv.hpp"

import value;
export module data;

export class Variable {
    // The variable owns this value. When it is set, another value is copied over and decorations (such as
    // relaxed precision or type conversions) are applied.
    Value* val;
    // Used to determine whether this variable is in, out, or other
    spv::StorageClass storage;
    // name of the variable, how this variable can be referenced by in and out toml files
    std::string name;
    std::map<uint32_t, uint32_t> decorations;

    Variable(Value* value, spv::StorageClass storage_class): val(value), storage(storage_class) {}

public:
    Variable(const Variable&) = delete;
    Variable& operator= (const Variable&) = delete;
    ~Variable() {
        if (val != nullptr)
            delete val;
    }

    [[nodiscard]] static Variable* makeVariable(spv::StorageClass storage, const Type& t) noexcept(false) {
        // Construct the value from the given type
        // For whatever reason, the SPIR-V spec says that the type of each OpVariable must be an OpTypePointer,
        // although it is actually storing the value.
        // Therefore, before we construct, we need to dereference the pointer
        if (t.getBase() != DataType::POINTER)
            throw std::invalid_argument("Cannot initialize variable with non-pointer type!");
        auto* val = t.getPointedTo().construct();
        return new Variable(val, storage);
    }

    void decorate(uint32_t deco_type, uint32_t deco_value) {
        // TODO: is it legal to have two decorations on the same type?
        decorations[deco_type] = deco_value;
    }

    spv::StorageClass getStorageClass() {
        return storage;
    }

    void setName(std::string& new_name) {
        name = new_name;
    }
    const std::string& getName() const {
        return name;
    }

    void setVal(const Value& new_val) {
        val->copyFrom(new_val);
    }

    void print(std::stringstream& dst) const {
        dst << name << " = ";
        val->print(dst);
        dst << '\n';
    }
};

export class Function {
    Type* type;
    unsigned location;
    std::string name;

public:
    Function(Type* type, unsigned location): type(type), location(location) {}

    void setName(std::string& new_name) {
        name = new_name;
    }

    unsigned getLocation() const {
        return location;
    }
};

export class Data {
    // The Data owns but does not manage the raw.
    void* raw;

    enum class DType {
        UNDEFINED,
        VARIABLE,
        FUNCTION,
        VALUE,
        TYPE,
    };
    DType type;

public:
    Data(): raw(nullptr), type(DType::UNDEFINED) {};
    Data(Variable* var): raw(var), type(DType::VARIABLE) {};
    Data(Function* func): raw(func), type(DType::FUNCTION) {};
    Data(Value* val): raw(val), type(DType::VALUE) {};
    Data(Type* type): raw(type), type(DType::TYPE) {};

    Data& operator=(Data& other) = delete;

    // Return nullptr if not a valid cast- assume the caller has more info for a better error
#define GET_X(UPPER, CAPITAL) \
    CAPITAL* get##CAPITAL() { \
        if (type != DType::UPPER) \
            return nullptr; \
        return static_cast<CAPITAL*>(raw); \
    }; \
    const CAPITAL* get##CAPITAL() const { \
        if (type != DType::UPPER) \
            return nullptr; \
        return static_cast<const CAPITAL*>(raw); \
    };

    GET_X(TYPE, Type)
    GET_X(VALUE, Value)
    GET_X(VARIABLE, Variable)
    GET_X(FUNCTION, Function)
#undef GET_X

    // Convenience function to not need to define the Data for each use
    template<typename T>
    void redefine(T* var) {
        redefine(Data(var));
    }

    void redefine(const Data& other) noexcept(false) {
        clear();
        raw = other.raw;
        type = other.type;
    }

    void clear() {
        switch (type) {
        default:
            assert(false);
            break;
        case DType::UNDEFINED:
            // do nothing since there is no data to delete
            break;
        case DType::VARIABLE:
            delete static_cast<Variable*>(raw);
            break;
        case DType::FUNCTION:
            delete static_cast<Function*>(raw);
            break;
        case DType::VALUE:
            delete static_cast<Value*>(raw);
            break;
        case DType::TYPE:
            delete static_cast<Type*>(raw);
            break;
        }
        type = DType::UNDEFINED;
    }
};
