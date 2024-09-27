/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>

#include "../../external/spirv.hpp"
#include "../../values/type.hpp"
#include "../../values/value.hpp"
export module spv.data.data;
import value.aggregate;
import value.primitive;
import value.string;

export class Variable : public Valuable {
    // The variable owns this value. When it is set, another value is copied over and decorations (such as
    // relaxed precision or type conversions) are applied.
    Value* val;
    // Used to determine whether this variable is in, out, or other
    spv::StorageClass storage;
    // name of the variable, how this variable can be referenced by in and out toml files
    std::string name;
    /// Indicates which builtin this variable is, if any
    spv::BuiltIn builtIn = spv::BuiltIn::BuiltInMax;
    // Whether this variable is a spec constant, which is treated as a value and a variable
    bool specConst;
    /// Internal modifier applied if this is decorated with NonWritable
    bool noWrite = false;

    /// @brief Construct a new variable directly (instead of through makeVariable)
    /// @param value saved (not copied) as the variable's value. Must be on the heap!
    /// @param storage_class the category which defines this variable's storage/use
    Variable(Value* value, spv::StorageClass storage_class, bool spec_const):
        val(value),
        storage(storage_class),
        specConst(spec_const) {}

public:
    Variable(const Variable& other):
            storage(other.storage),
            name(other.name),
            builtIn(other.builtIn),
            specConst(other.specConst) {
        val = other.val->getType().construct();
    }
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
        return new Variable(val, storage, false);
    }

    /// @param value saved (not copied) as the variable's value. Must be on the heap!
    [[nodiscard]] static Variable* makeSpecConst(Value* value) {
        return new Variable(value, spv::StorageClass::StorageClassPushConstant, true);
    }

    spv::StorageClass getStorageClass() const {
        return storage;
    }

    bool isSpecConst() const {
        return specConst;
    }

    void setName(std::string new_name) {
        name = new_name;
    }
    const std::string& getName() const {
        return name;
    }

    void setVal(const Value& new_val) {
        val->copyFrom(new_val);
    }
    const Value* getVal() const {
        return val;
    }
    Value* getVal() {
        return val;
    }

    void setBuiltIn(spv::BuiltIn built_in) {
        builtIn = built_in;
    }
    spv::BuiltIn getBuiltIn() const {
        return builtIn;
    }

    void forbidWrite() {
        noWrite = true;
    }
    bool isWritable() const {
        return !noWrite;
    }

    [[nodiscard]] Value* asValue() const override {
        // Represent this variable with its value, storage class, and if set, name
        // Don't currently display decorations although they could be helpful.
        std::vector<Value*> elements;
        std::vector<std::string>  names;

        if (!name.empty()) {
            names.push_back("name");
            elements.push_back(new String(name));
        }

        names.push_back("value");
        Value* cloned = val->getType().construct();
        cloned->copyFrom(*val);
        elements.push_back(cloned);

        names.push_back("storage-class");
        elements.push_back(new Primitive(static_cast<int32_t>(storage)));

        return new Struct(elements, names);
    }
};

export class Function : public Valuable {
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

    [[nodiscard]] Value* asValue() const override {
        std::vector<Value*> elements;
        std::vector<std::string>  names;
        // Populate the representative struct with three fields:
        // - name (only use if has been set (ie, is not ""))
        // - type
        // - location

        if (!name.empty()) {
            names.push_back("name");
            elements.push_back(new String(name));
        }

        names.push_back("types");
        elements.push_back(type->asValue());

        names.push_back("location");
        elements.push_back(new Primitive(location));

        return new Struct(elements, names);
    }
};

export struct EntryPoint : public Function {
    // Workgroup size in three dimensions:
    unsigned sizeX = 1;
    unsigned sizeY = 1;
    unsigned sizeZ = 1;

    EntryPoint(Type* type, unsigned location): Function(type, location) {}
};

export class Data {
    // The Data owns but does not manage the raw.
    void* raw;

    enum class DType {
        UNDEFINED,
        VARIABLE,
        FUNCTION,
        ENTRY,
        VALUE,
        TYPE,
        WEAK
    };
    DType type;

public:
    Data(): raw(nullptr), type(DType::UNDEFINED) {};
    Data(Variable* var): raw(var), type(DType::VARIABLE) {};
    Data(Function* func): raw(func), type(DType::FUNCTION) {};
    Data(EntryPoint* entry): raw(entry), type(DType::ENTRY) {};
    Data(Value* val): raw(val), type(DType::VALUE) {};
    Data(Type* type): raw(type), type(DType::TYPE) {};

    static Data weak(Value* val) {
        Data ret(val);
        ret.type = DType::WEAK;
        return ret;
    }

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
    GET_X(VARIABLE, Variable)
    GET_X(FUNCTION, Function)
    GET_X(ENTRY, EntryPoint);
#undef GET_X

    // Fetching of Values must be able to fetch spec constants, which are saved as program inputs but also need to be
    // usable like regular values.
    Value* getValue() {
        if (type == DType::VALUE || type == DType::WEAK)
            return static_cast<Value*>(raw);
        if (type == DType::VARIABLE) {
            auto var = static_cast<Variable*>(raw);
            if (var->isSpecConst())
                return var->getVal();
        }
        return nullptr;
    }
    const Value* getValue() const {
        if (type == DType::VALUE || type == DType::WEAK)
            return static_cast<const Value*>(raw);
        if (type == DType::VARIABLE) {
            auto var = static_cast<Variable*>(raw);
            if (var->isSpecConst())
                return var->getVal();
        }
        return nullptr;
    }

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
        case DType::ENTRY:
            delete static_cast<EntryPoint*>(raw);
            break;
        case DType::VALUE:
            delete static_cast<Value*>(raw);
            break;
        case DType::TYPE:
            delete static_cast<Type*>(raw);
            break;
        case DType::WEAK:
            break;  // the whole point of weak is to avoid deletion- that is someone else's responsibility
        }
        type = DType::UNDEFINED;
    }
};
