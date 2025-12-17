/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "type.hpp"

#include <cassert>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "value.hpp"
import value.aggregate;
import value.coopMatrix;
import value.image;
import value.primitive;
import value.raytrace.accelStruct;
import value.raytrace.rayQuery;
import value.sampledImg;
import value.sampler;
import value.string;

Value* Type::construct(std::vector<const Value*>* values, bool undef) const {
    switch (base) {
    default:
        throw std::runtime_error("Cannot construct unsupported type!");
    case DataType::VOID:
        throw std::runtime_error("Cannot construct void type!");
    // Primitive types
    case DataType::FLOAT:
    case DataType::UINT:
    case DataType::INT:
    case DataType::BOOL: {
        Primitive* prim = new Primitive(*this, undef);
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
    case DataType::ARRAY:
    case DataType::COOP_MATRIX:
    case DataType::STRUCT: {
        Aggregate* agg;
        if (base == DataType::ARRAY)
            agg = static_cast<Aggregate*>(new Array(*subElement, subSize));
        else if (base == DataType::COOP_MATRIX)
            agg = static_cast<Aggregate*>(new CoopMatrix(*subElement, rows, subSize / rows));
        else {
            assert(base == DataType::STRUCT);
            agg = static_cast<Aggregate*>(new Struct(*this));
        }
        // try to populate with each of the entries
        if (values != nullptr)
            agg->addElements(*values);
        else if (base != DataType::COOP_MATRIX)
            agg->dummyFill(undef);
        return agg;
    }
    case DataType::STRING:
        return new String("");
    case DataType::ACCEL_STRUCT:
        return new AccelStruct();
    case DataType::RAY_QUERY:
        return new RayQuery();
    case DataType::IMAGE:
        return new Image(*this);
    case DataType::SAMPLED_IMG:
        return new SampledImage(*this);
    case DataType::SAMPLER:
        return new Sampler();
    case DataType::POINTER:
        // We cannot actually construct a pointer, nor does that conceptually make sense. When this is requested, the
        // pointer is a shallow wrapper to indicate storage settings. In that case, construct the underlying value
        return subElement->construct(undef);
    }
}

Type Type::unionOf(std::vector<const Value*> elements, std::vector<const Type*> created) {
    if (elements.empty())
        throw std::invalid_argument("Cannot find union of types in empty vector!");
    Type t = elements[0]->getType();
    for (unsigned i = 1; i < elements.size(); ++i)
        t = t.unionOf(elements[i]->getType(), created);
    return t;
}

bool Type::operator==(const Type& rhs) const {
    if (base != rhs.getBase())
        return false;

    switch (base) {
    default:
        assert(false);  // unknown type!
        return false;
    case DataType::FLOAT:
    case DataType::UINT:
    case DataType::INT:
        return subSize == rhs.subSize;
    case DataType::STRING:  // TODO
    case DataType::BOOL:
    case DataType::SAMPLER:
    case DataType::VOID:
        return true;
    case DataType::ARRAY:
        return subSize == rhs.subSize && (*subElement == *(rhs.subElement));
    case DataType::STRUCT: {
        // try to match all fields
        // For each field, if a name is provided for both, it must match
        unsigned len = subList.size();
        if (len != rhs.subList.size())
            return false;
        for (unsigned i = 0; i < subList.size(); ++i) {
            if (*subList[i] != *rhs.subList[i])
                return false;
            if (!(nameList[i].empty() || rhs.nameList[i].empty())) {
                if (nameList[i] != rhs.nameList[i])
                    return false;
            }
        }
        return true;
    }
    case DataType::FUNCTION:
        return (*subElement == *(rhs.subElement)) && (subList == rhs.subList);
    case DataType::POINTER:
    case DataType::IMAGE:  // Can compare dimensions and other fields in the image itself
    case DataType::SAMPLED_IMG:
        return *subElement == *(rhs.subElement);
    }
}

Type Type::unionOf(const Type& other, std::vector<const Type*> created) const noexcept(false) {
    // Union should not care about the ordering of types. ie, a.unionOf(b) == b.unionOf(a)
    // To accomplish this, we "order" the two types by base before running any comparisons

    bool this_small = this->base <= other.base;
    const Type& small = this_small ? *this : other;
    const Type& large = this_small ? other : *this;

    static_assert(
        (DataType::FLOAT < DataType::UINT) && (DataType::UINT < DataType::INT) && (DataType::INT < DataType::BOOL),
        "Below logic depends on type ordering!"
    );

    switch (small.base) {
    default:
        throw std::invalid_argument("Cannot find union with unsupported types!");
    case DataType::FLOAT:
        // Float takes precedence over several types
        switch (large.base) {
        default:
            break;
        case DataType::FLOAT:
        case DataType::UINT:
        case DataType::INT: {
            Type t = small;
            t.subSize = std::max(small.subSize, large.subSize);
            return t;
        }
        }
        break;
    case DataType::UINT:
        // uint yields to a couple of types
        switch (large.base) {
        default:
            break;
        case DataType::UINT:
        case DataType::INT:
        case DataType::BOOL: {
            Type t = large;
            t.subSize = std::max(small.subSize, large.subSize);
            return t;
        }
        }
        break;
    case DataType::INT:
    case DataType::BOOL:
    case DataType::STRING: {
        if (large.base == small.base) {
            Type t = small;
            t.subSize = std::max(small.subSize, large.subSize);
            return t;
        }
        break;
    }
    case DataType::ARRAY: {
        if (large.base != DataType::ARRAY)
            break;

        if (small == large)
            return small;

        // Assume a void type array will become the other array type if it's a non-void type
        if (small.subElement->base == DataType::VOID)
            return large;
        else if (large.subElement->base == DataType::VOID)
            return small;

        // If the element count does not match, assume this is a runtime array (use 0 size)
        bool runtime_size = (small.subSize != large.subSize);

        // Try to use pre-existent objects to minimize the creation and deletion of extra type objects
        const Type* sub_el = nullptr;
        Type sub = small.subElement->unionOf(*large.subElement, created);
        if (sub == *small.subElement) {
            if (!runtime_size)
                return small;
            sub_el = small.subElement;
        } else if (sub == *large.subElement) {
            if (!runtime_size)
                return large;
            sub_el = large.subElement;
        } else {
            // Transfer the unioned type to the heap and save to our list
            sub_el = new Type(sub);
            created.push_back(sub_el);
        }

        assert(runtime_size);
        return Type::array(0, *sub_el);
    }
    case DataType::STRUCT: {
        if (large.base != DataType::STRUCT)
            break;
        if (small == large)
            return small;

        // The field count and names for these fields must match to get a reasonable union
        unsigned num_fields = small.subList.size();
        if (num_fields != large.subList.size()) {
            std::stringstream error;
            error << "Cannot find union between two structure types of different sizes!";
            throw std::invalid_argument(error.str());
        }

        for (unsigned i = 0; i < num_fields; ++i) {
            if (small.nameList[i] != large.nameList[i]) {
                std::stringstream error;
                error << "Cannot find union between two structure types with differently named fields! ";
                error << "Names \"" << small.nameList[i] << "\" and \"" << large.nameList[i] << "\" differ.";
                throw std::invalid_argument(error.str());
            }
        }

        std::vector<const Type*> sub_elements;
        sub_elements.reserve(num_fields);
        // Now that we suspect a union is possible, try to union all individual types to produce the result
        for (unsigned i = 0; i < num_fields; ++i) {
            const Type* sm = small.subList[i];
            const Type* lg = large.subList[i];
            Type unioned = sm->unionOf(*lg, created);
            if (unioned == *sm)
                sub_elements.push_back(sm);
            else if (unioned == *lg)
                sub_elements.push_back(lg);
            else {
                Type* create = new Type(unioned);
                created.push_back(create);
                sub_elements.push_back(create);
            }
        }
        return Type::structure(sub_elements, small.getNames());
    }
    }

    std::stringstream error;
    error << "Cannot find union between " << small.base << " and " << large.base << " types!";
    throw std::invalid_argument(error.str());
}

[[nodiscard]] Value* Type::asValue() const {
    // This could easily balloon to a recursive nightmare. I don't want to do that, however, so let's implement it very
    // simply and we can expand as needed
#define SIMPLE(TYPE, LOWER) \
    case DataType::TYPE: \
        return new String(#LOWER);

    switch (base) {
        SIMPLE(FLOAT, float);
        SIMPLE(UINT, uint);
        SIMPLE(INT, int);
        SIMPLE(BOOL, bool);
        SIMPLE(STRUCT, struct);
        SIMPLE(ARRAY, array);
        SIMPLE(STRING, string);
        SIMPLE(VOID, void);
        SIMPLE(FUNCTION, function);
        SIMPLE(POINTER, pointer);
        SIMPLE(ACCEL_STRUCT, accelStruct);
        SIMPLE(RAY_QUERY, rayQuery);
        SIMPLE(IMAGE, image);
        SIMPLE(SAMPLED_IMG, sampledImg);
        SIMPLE(SAMPLER, sampler);
        SIMPLE(COOP_MATRIX, cooperativeMatrix);
    default:
        assert(false);
        return nullptr;
    }
#undef SIMPLE
}
