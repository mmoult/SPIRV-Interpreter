module;
#include <cassert>
#include <cstdint>
#include <map>
#include <sstream>
#include <tuple>

#include "../external/spirv.hpp"

import utils;
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

public:
    // Need to initialize before it is ready for use
    Variable(spv::StorageClass storage_class): val(nullptr), storage(storage_class) {}
    Variable(const Variable&) = delete;
    Variable& operator= (const Variable&) = delete;
    ~Variable() {
        if (val != nullptr)
            delete val;
    }

    Utils::May<bool> initialize(const Type& t) {
        // Construct the value from the given type
        // For whatever reason, the SPIR-V spec says that the type of each OpVariable must be an OpTypePointer,
        // although it is actually storing the value.
        // Therefore, before we construct, we need to dereference the pointer
        if (t.getBase() != DataType::POINTER)
            return Utils::unexpected<bool>("Cannot initialize variable with non-pointer type!");
        auto res = t.getPointedTo().construct();
        if (!res)
            return Utils::unexpected<bool>("Cannot construct given type!");
        val = res.value();
        return Utils::expected();
    }
    Utils::May<bool> initialize(const Type& t, const Value& def) {
        if (auto res = initialize(t); !res)
            return Utils::unexpected<bool>(res.error());
        return setVal(def);
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

    Utils::May<bool> setVal(const Value& new_val) {
        assert(val != nullptr); // must be initialized first!
        return val->copyFrom(new_val);
    }

    void print(std::stringstream& dst) const {
        assert(val != nullptr);
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

    ~Data() = default;
    //Data(const Data& other) = delete;
    Data& operator=(const Data& other) = delete;

#define GET_X(UPPER, CAPITAL) \
    std::tuple<CAPITAL*, bool> get##CAPITAL() { \
        if (type != DType::UPPER) \
            return std::tuple(nullptr, false); \
        return std::tuple(static_cast<CAPITAL*>(raw), true); \
    }

    GET_X(TYPE, Type)
    GET_X(VALUE, Value)
    GET_X(VARIABLE, Variable)
    GET_X(FUNCTION, Function)
#undef GET_X

    // Convenience function to not need to define the Data for each use
    template<typename T>
    Utils::May<bool> redefine(T* var) {
        return redefine(Data(var));
    }

    Utils::May<bool> redefine(const Data& other) {
        if (type != DType::UNDEFINED) {
            std::stringstream err;
            err << "Cannot redefine data holding ";
            switch (type) {
            default:
                err << "unknown";
                break;
            case DType::VARIABLE:
                err << "variable";
                break;
            case DType::FUNCTION:
                err << "function";
                break;
            case DType::VALUE:
                err << "value";
                break;
            case DType::TYPE:
                err << "type";
                break;
            }
            err << "!";
            return Utils::unexpected<bool>(err.str());
        }
        raw = other.raw;
        type = other.type;
        return Utils::expected();
    }

    Utils::May<bool> clear() {
        // TODO should be able to clear values for reuse (in loops or second function call, etc)
        return Utils::unexpected<bool>("Not implemented!");
    }
};
