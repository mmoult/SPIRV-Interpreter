/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "data.hpp"

#include <cstdint>
#include <stdexcept>

#include "../../values/aggregate.hpp"
#include "../../values/coop-matrix.hpp"
#include "../../values/primitive.hpp"
#include "../../values/string.hpp"

void Variable::initValue(const Type& t) {
    // Only initialize the value once
    assert(this->val == nullptr);

    // Construct the value from the given type
    // For whatever reason, the SPIR-V spec says that the type of each OpVariable must be an OpTypePointer, although
    // it is actually storing the value.
    // Therefore, before we construct, we need to dereference the pointer
    if (t.getBase() != DataType::POINTER)
        throw std::invalid_argument("Cannot initialize variable with non-pointer type!");
    this->val = t.getPointedTo().construct();
    auto set_unsized = [&](Value& seen) {
        if (seen.getType().getBase() == DataType::COOP_MATRIX)
            static_cast<CoopMatrix&>(seen).setUnsized();
        return true;
    };
    val->recursiveApply(set_unsized);
}

[[nodiscard]] Value* Variable::asValue() const {
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

[[nodiscard]] Value* Function::asValue() const {
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

void Data::clear() {
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

Data Data::clone() const {
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

Value* Data::getValue() {
    if (type == DType::VALUE)
        return static_cast<Value*>(raw);
    if (type == DType::VARIABLE) {
        auto var = static_cast<Variable*>(raw);
        if (var->isSpecConst())
            return &var->getVal();
    }
    return nullptr;
}

const Value* Data::getValue() const {
    if (type == DType::VALUE)
        return static_cast<const Value*>(raw);
    if (type == DType::VARIABLE) {
        auto var = static_cast<Variable*>(raw);
        if (var->isSpecConst())
            return &var->getVal();
    }
    return nullptr;
}
