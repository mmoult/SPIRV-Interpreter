/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef SPV_DATA_DATA_HPP
#define SPV_DATA_DATA_HPP

#include <cassert>
#include <cstdint>
#include <limits>

#include "../../util/spirv.hpp"
#include "../../values/type.hpp"
#include "../../values/value.hpp"

class Variable final : public Valuable {
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

    void initValue(const Type& t);

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

    [[nodiscard]] Value* asValue() const override;
};

class Function : public Valuable {
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

    [[nodiscard]] Value* asValue() const override;
};

struct EntryPoint final : public Function {
    // Workgroup size in three dimensions:
    unsigned sizeX = 1;
    unsigned sizeY = 1;
    unsigned sizeZ = 1;

    EntryPoint(Type* type, unsigned location) : Function(type, location) {}
};

class Data {
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
    Value* getValue();
    const Value* getValue() const;

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

    void clear();

    Data clone() const;

    /// @brief Move the data held by other into this, transferring ownership
    /// @param other transferred from. Cleared before return.
    void move(Data& other) {
        redefine(other);
        other.own = false;
        other.clear();
    }
};
#endif
