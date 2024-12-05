/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include "type.hpp"
#include "value.hpp"
export module value.sampler;
import value.aggregate;
import value.image;
import value.primitive;
import value.statics;
import value.string;

export class Sampler : public Value {
    unsigned defaultLod;
    Image image;

    inline static const std::vector<std::string> names {"lod", "image"};

public:
    Sampler(Type t): Value(t), defaultLod(0), image(t.getElement()) {};

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to sampler!");
    }

    /// @brief Copy the image's fields from the other struct, if possible
    /// @param other the struct to copy from
    void copyFrom(const Struct& str) noexcept(false) {
        const Struct& other = Statics::extractStruct(static_cast<const Value*>(&str), "sampler", names);

        // lod: <uint>
        defaultLod = Statics::extractUint(other[0], names[0]);

        // image: <image>
        image.copyFrom(*other[1]);
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Can copy from a struct, assuming that the correct fields are present
        if (const auto& new_type = new_val.getType(); new_type.getBase() == DataType::STRUCT) {
            copyFrom(static_cast<const Struct&>(new_val));
            return; // will either throw an exception or do a successful copy
        }

        Value::copyFrom(new_val);  // verifies matching types
        const auto& other = static_cast<const Sampler&>(new_val);
        this->defaultLod = other.defaultLod;
        this->image.copyFrom(other.image);
    }

    // Here is what a sampler looks like in YAML:
    // sampler :
    //   lod : <uint>
    //   image :
    //     <image>
    Struct* toStruct() const {
        std::vector<Value*> elements;
        elements.reserve(names.size());
        elements.push_back(new Primitive(defaultLod));
        elements.push_back(image.toStruct());
        return new Struct(elements, names);
    }

    const unsigned getImplicitLod() const {
        return defaultLod;
    }

    Image& getImage() {
        return image;
    }
    const Image& getImage() const {
        return image;
    }
};
