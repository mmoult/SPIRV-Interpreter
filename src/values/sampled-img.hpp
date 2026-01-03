/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef VALUE_SAMPLED_IMG_HPP
#define VALUE_SAMPLED_IMG_HPP

#include "aggregate.hpp"
#include "image.hpp"
#include "sampler.hpp"
#include "statics.hpp"
#include "string.hpp"
#include "type.hpp"
#include "value.hpp"

class SampledImage : public Value {
    Sampler sampler;
    Image image;

    inline static const std::vector<std::string> names {"sampler", "image"};

public:
    SampledImage(Type t) : Value(t), image(t.getElement()) {};
    SampledImage(const Sampler& sampler, const Image& image)
        : Value(Type::sampledImage(&image.getType())), sampler(sampler), image(image) {}

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to SampledImage!");
    }

    /// @brief Copy the image's fields from the other struct, if possible
    /// @param other the struct to copy from
    void copyFrom(const Struct& str) noexcept(false) {
        const Struct& other = Statics::extractStruct(static_cast<const Value*>(&str), "SampledImage", names);

        // sampler: <sampler
        sampler.copyFrom(*other[0]);

        // image: <image>
        image.copyFrom(*other[1]);
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Can copy from a struct, assuming that the correct fields are present
        if (const auto& new_type = new_val.getType(); new_type.getBase() == DataType::STRUCT) {
            copyFrom(static_cast<const Struct&>(new_val));
            return;  // will either throw an exception or do a successful copy
        }

        Value::copyFrom(new_val);  // verifies matching types
        const auto& other = static_cast<const SampledImage&>(new_val);
        this->sampler.copyFrom(other.sampler);
        this->image.copyFrom(other.image);
    }

    // A sampled image has two fields: sampler and image
    Struct* toStruct() const {
        std::vector<Value*> elements;
        elements.reserve(names.size());
        elements.push_back(sampler.toStruct());
        elements.push_back(image.toStruct());
        return new Struct(elements, names);
    }

    const unsigned getImplicitLod() const {
        return sampler.getImplicitLod();
    }

    Image& getImage() {
        return image;
    }
    const Image& getImage() const {
        return image;
    }
};
#endif
