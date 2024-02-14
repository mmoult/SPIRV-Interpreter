module;
#include <cstdint>
#include <map>
#include <sstream>
#include <tuple>

import utils;
import value;
export module data;

export class Variable {
    Type* type;
    Value* val;
    // We don't need to keep track of storage class
    std::string name;
    std::map<uint32_t, uint32_t> decorations;

public:
    Variable(Type* type, Value* val = nullptr): type(type), val(val) {}

    void decorate(uint32_t deco_type, uint32_t deco_value) {
        // TODO: is it legal to have two decorations on the same type?
        decorations[deco_type] = deco_value;
    }

    void setName(std::string& new_name) {
        name = new_name;
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

    std::tuple<Type*, bool> getType() {
        if (type != DType::TYPE)
            return std::tuple(nullptr, false);
        return std::tuple(static_cast<Type*>(raw), true);
    }
    std::tuple<Variable*, bool> getVariable() {
        if (type != DType::VARIABLE)
            return std::tuple(nullptr, false);
        return std::tuple(static_cast<Variable*>(raw), true);
    }
    std::tuple<Function*, bool> getFunction() {
        if (type != DType::FUNCTION)
            return std::tuple(nullptr, false);
        return std::tuple(static_cast<Function*>(raw), true);
    }

    // Convenience function to not need to define the Data for each use
    template<typename T>
    Utils::May<const bool> redefine(T* var) {
        return redefine(Data(var));
    }

    Utils::May<const bool> redefine(const Data& other) {
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
            return Utils::unexpected<const bool>(err.str());
        }
        raw = other.raw;
        type = other.type;
        return Utils::expected();
    }

    Utils::May<const bool> clear() {
        // TODO should be able to clear values for reuse (in loops or second function call, etc)
        return Utils::unexpected<const bool>("Not implemented!");
    }
};
