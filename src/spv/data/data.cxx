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

export class Variable {
    // The variable owns this value. When it is set, another value is copied over and decorations (such as
    // relaxed precision or type conversions) are applied.
    Value* val;
    // Used to determine whether this variable is in, out, or other
    spv::StorageClass storage;
    // name of the variable, how this variable can be referenced by in and out toml files
    std::string name;
    spv::BuiltIn builtIn = spv::BuiltIn::BuiltInMax;

    // Whether this variable is a spec constant, which is treated as a value and a variable
    bool specConst;

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

    Value* asValue(std::vector<Type*>& to_delete) const {
        bool use_name = !name.empty();
        // Represent this variable with its value, storage class, and if set, name
        // Don't currently display decorations although they could be helpful.
        std::vector<const Type*> sub_list;
        if (use_name) {
            Type* str = new Type(Type::string());
            sub_list.push_back(str);
            to_delete.push_back(str);
        }
        // value is obviously already a value, so we can print it without any processing
        sub_list.push_back(&val->getType());
        Type* num = new Type(Type::primitive(DataType::INT));  // for storage class
        sub_list.push_back(num);
        to_delete.push_back(num);

        Type str_type = Type::structure(sub_list);
        // Name all used members then create the structure value
        unsigned count = 0;
        if (use_name)
            str_type.nameMember(count++, "name");
        str_type.nameMember(count++, "value");
        str_type.nameMember(count++, "storage-class");
        Struct* str = new Struct(str_type);

        // We can safely put stack pointers in the vector because all values are copied into the struct (and not saved!)
        std::vector<const Value*> es;
        String vname(name);
        if (use_name)
            es.push_back(&vname);
        es.push_back(val);
        Primitive storage_class(static_cast<int32_t>(storage));
        es.push_back(&storage_class);
        str->addElements(es);

        return str;
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

    Value* asValue(std::vector<Type*>& to_delete) const {
        bool use_name = !name.empty();
        // Populate the subelement list with the types of the three fields:
        // - name (only use if has been set (ie, is not ""))
        // - type
        // - location
        std::vector<const Type*> sub_list;
        if (use_name) {
            Type* str = new Type(Type::string());
            sub_list.push_back(str);
            to_delete.push_back(str);
        }
        Value* type_as_value = type->asValue(to_delete);
        // must copy the type's type because the type will be deleted before return
        Type* type_type = new Type(type_as_value->getType());
        sub_list.push_back(type_type);
        to_delete.push_back(type_type);
        Type* num = new Type(Type::primitive(DataType::UINT));
        to_delete.push_back(num);
        sub_list.push_back(num);

        Type str_type = Type::structure(sub_list);
        // Name all used members then create the structure value
        unsigned count = 0;
        if (use_name)
            str_type.nameMember(count++, "name");
        str_type.nameMember(count++, "type");
        str_type.nameMember(count++, "location");
        Struct* str = new Struct(str_type);

        // We can safely put stack pointers in the vector because all values are copied into the struct (and not saved!)
        std::vector<const Value*> es;
        String vname(name);
        if (use_name)
            es.push_back(&vname);
        es.push_back(type_as_value);
        Primitive vlocation(location);
        es.push_back(&vlocation);
        str->addElements(es);

        // Clean up
        delete type_as_value;  // can delete since aggregate created copy
        return str;
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
    };
    DType type;

public:
    Data(): raw(nullptr), type(DType::UNDEFINED) {};
    Data(Variable* var): raw(var), type(DType::VARIABLE) {};
    Data(Function* func): raw(func), type(DType::FUNCTION) {};
    Data(EntryPoint* entry): raw(entry), type(DType::ENTRY) {};
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
    GET_X(VARIABLE, Variable)
    GET_X(FUNCTION, Function)
    GET_X(ENTRY, EntryPoint);
#undef GET_X

    // Fetching of Values must be able to fetch spec constants, which are saved as program inputs but also need to be
    // usable like regular values.
    Value* getValue() {
        if (type == DType::VALUE)
            return static_cast<Value*>(raw);
        if (type == DType::VARIABLE) {
            auto var = static_cast<Variable*>(raw);
            if (var->isSpecConst())
                return var->getVal();
        }
        return nullptr;
    }
    const Value* getValue() const {
        if (type == DType::VALUE)
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
        }
        type = DType::UNDEFINED;
    }
};
