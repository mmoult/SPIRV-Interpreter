/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "../../external/spirv.hpp"
#include "../../values/type.hpp"
#include "../../values/value.hpp"
export module spv.data.data;
import value.aggregate;
import value.coopMatrix;
import value.primitive;
import value.string;

export class Variable final : public Valuable {
    /// The variable owns this value. When it is set, another value is copied over and decorations (such as relaxed
    /// precision or type conversions) are applied.
    Value* val;
    /// Used to determine whether this variable is in, out, or other
    spv::StorageClass storage;
    /// Name of the variable, how this variable can be referenced by external-facing data files
    std::string name;
    /// Indicates which builtin this variable is, if any
    spv::BuiltIn builtIn = spv::BuiltIn::BuiltInMax;
    // Whether this variable is a spec constant, which is treated as a value and a variable
    bool specConst;

    // Optional settings holding decorated state:
    constexpr static unsigned UNSET = std::numeric_limits<unsigned>::max();
    /// if this is decorated with NonWritable
    bool nonwritable = false;
    /// The location of this variable. "location" can only be used on in/out variables and is therefore mutually
    /// exclusive with "binding", which can only be used on buffers. This field holds both
    unsigned location = UNSET;
    /// The descriptor set of this variable.
    unsigned descr_set = UNSET;

public:
    /// @brief Construct a new variable directly (instead of through makeVariable)
    /// @param value saved (not copied) as the variable's value. Must be on the heap! If null, you must initValue later!
    /// @param storage_class the category which defines this variable's storage/use
    Variable(Value* value, spv::StorageClass storage_class, bool spec_const)
        : val(value), storage(storage_class), specConst(spec_const) {}

    Variable(const Variable& other)
        : storage(other.storage)
        , name(other.name)
        , builtIn(other.builtIn)
        , specConst(other.specConst)
        , nonwritable(other.nonwritable) {
        if (other.val != nullptr) {
            val = other.val->getType().construct();
            val->copyFrom(*other.val);
        } else
            val = nullptr;
    }
    Variable& operator=(const Variable&) = delete;
    virtual ~Variable() {
        if (val != nullptr)
            delete val;
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

    void initValue(const Type& t) {
        // Only initialize the value once
        assert(this->val == nullptr);

        // Construct the value from the given type
        // For whatever reason, the SPIR-V spec says that the type of each OpVariable must be an OpTypePointer, although
        // it is actually storing the value.
        // Therefore, before we construct, we need to dereference the pointer
        if (t.getBase() != DataType::POINTER)
            throw std::invalid_argument("Cannot initialize variable with non-pointer type!");
        this->val = t.getPointedTo().construct();
        if (val->getType().getBase() == DataType::COOP_MATRIX)
            static_cast<CoopMatrix*>(this->val)->setUnsized();
    }

    bool isThreaded() const {
        return storage == spv::StorageClassPrivate || storage == spv::StorageClassFunction;
    }

    const Value& getVal() const {
        assert(val != nullptr);
        return *val;
    }
    Value& getVal() {
        assert(val != nullptr);
        return *val;
    }

    void setBuiltIn(spv::BuiltIn built_in) {
        builtIn = built_in;
    }
    spv::BuiltIn getBuiltIn() const {
        return builtIn;
    }

    void forbidWrite() {
        nonwritable = true;
    }
    bool isWritable() const {
        return !nonwritable;
    }

    void setBinding(unsigned location) {
        this->location = location;
    }
    unsigned getBinding() const {
        return location;
    }

    void setDescriptorSet(unsigned set) {
        this->descr_set = set;
    }
    unsigned getDescriptorSet() const {
        return descr_set;
    }

    static inline bool isUnset(unsigned location_data) {
        return location_data == UNSET;
    }
    static inline unsigned getUnset() {
        return UNSET;
    }

    [[nodiscard]] Value* asValue() const override {
        // Represent this variable with its value, storage class, and if set, name
        // Don't currently display decorations although they could be helpful.
        std::vector<Value*> elements;
        std::vector<std::string> names;

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
    Function(Type* type, unsigned location) : type(type), location(location) {}
    virtual ~Function() = default;

    void setName(const std::string& new_name) {
        name = new_name;
    }

    unsigned getLocation() const {
        return location;
    }

    [[nodiscard]] Value* asValue() const override {
        std::vector<Value*> elements;
        std::vector<std::string> names;
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

export struct EntryPoint final : public Function {
    // Workgroup size in three dimensions:
    unsigned sizeX = 1;
    unsigned sizeY = 1;
    unsigned sizeZ = 1;

    EntryPoint(Type* type, unsigned location) : Function(type, location) {}
};

export class Data {
    void* raw;
    bool own = true;

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
    Data() : raw(nullptr), type(DType::UNDEFINED) {};
    explicit(false) Data(Variable* var) : raw(var), type(DType::VARIABLE) {};
    explicit(false) Data(Function* func) : raw(func), type(DType::FUNCTION) {};
    explicit(false) Data(EntryPoint* entry) : raw(entry), type(DType::ENTRY) {};
    explicit(false) Data(Value* val) : raw(val), type(DType::VALUE) {};
    explicit(false) Data(Type* type) : raw(type), type(DType::TYPE) {};

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
                return &var->getVal();
        }
        return nullptr;
    }
    const Value* getValue() const {
        if (type == DType::VALUE)
            return static_cast<const Value*>(raw);
        if (type == DType::VARIABLE) {
            auto var = static_cast<Variable*>(raw);
            if (var->isSpecConst())
                return &var->getVal();
        }
        return nullptr;
    }

    // Convenience function to not need to define the Data for each use
    template<typename T>
    void redefine(T* var, bool own = true) {
        redefine(Data(var), own);
    }

    void redefine(const Data& other) noexcept(false) {
        clear();
        raw = other.raw;
        type = other.type;
        own = other.own;
    }
    void redefine(const Data& other, bool own) noexcept(false) {
        redefine(other);
        this->own = own;
    }

    void clear() {
        if (own) {
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
        }

        type = DType::UNDEFINED;
    }

    Data clone() const {
        // NOTE: If this data does not own its value, then cloning it will *not* grant ownership for the clone. This
        // should be ok since we only use weak data for RT where we are referencing the main stage which we know will
        // outlive any cloned substage.
        if (!own && type != DType::UNDEFINED) {
            Data other;
            other.raw = this->raw;
            other.own = false;
            other.type = this->type;
            return other;
        }

        switch (type) {
        default:
            assert(false);
            return Data();
        case DType::UNDEFINED:
            return Data();
        case DType::VARIABLE:
            return Data(new Variable(*static_cast<Variable*>(raw)));
        case DType::FUNCTION:
            return Data(new Function(*static_cast<Function*>(raw)));
        case DType::ENTRY:
            return Data(new EntryPoint(*static_cast<EntryPoint*>(raw)));
        case DType::VALUE: {
            Value* before = static_cast<Value*>(raw);
            Value* after = before->getType().construct();
            after->copyFrom(*before);
            return Data(after);
        }
        case DType::TYPE:
            return Data(new Type(*static_cast<Type*>(raw)));
        }
    }

    /// @brief Move the data held by other into this, transferring ownership
    /// @param other transferred from. Cleared before return.
    void move(Data& other) {
        redefine(other);
        other.own = false;
        other.clear();
    }
};
