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
    std::vector<Image> mipmaps;

    inline static const std::vector<std::string> names {"lod", "mipmaps"};

public:
    Sampler(Type t): Value(t), defaultLod(0) {};

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to sampler!");
    }

    /// @brief Copy the image's fields from the other struct, if possible
    /// @param other the struct to copy from
    void copyFrom(const Struct& str) noexcept(false) {
        const Struct& other = Statics::extractStruct(static_cast<const Value*>(&str), "sampler", names);
        const std::vector<std::string>& type_names = other.getType().getNames();

        // lod: uint
        defaultLod = Statics::extractUint(other[0], names[0]);

        // mipmaps: [images, ...]
        const Array& arr = Statics::extractArray(other[1], names[1]);
        unsigned num_mipmaps = arr.getSize();
        if (num_mipmaps == 0) {
            throw std::runtime_error(
                "Attempting to copy struct into sampler, but the mipmap array is empty! There must be at least one "
                "image provided!"
            );
        }
        if (num_mipmaps <= defaultLod) {
            throw std::runtime_error(
                "The number of mipmaps provided is incompatible with the default level of detail (lod)! The lod is an "
                "index within the mipmaps array (with first expected to be the biggest mipmaps)."
            );
        }
        mipmaps.clear();
        const Type& image_type = type.getElement();
        for (unsigned i = 0; i < num_mipmaps; ++i) {
            mipmaps.emplace_back(image_type);
            mipmaps.back().copyFrom(*arr[i]);
        }
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

        this->mipmaps.clear();
        const Type& image_type = type.getElement();
        for (unsigned i = 0; i < other.mipmaps.size(); ++i) {
            mipmaps.emplace_back(image_type);
            mipmaps.back().copyFrom(other.mipmaps[i]);
        }
    }

    // Here is what a sampler looks like in YAML:
    // sampler :
    //   lod : <uint>
    //   mipmaps :
    //   - <image>
    //   - <...>
    Struct* toStruct() const {
        std::vector<Value*> elements;
        elements.reserve(2);
        elements.push_back(new Primitive(defaultLod));
        unsigned num_mips = mipmaps.size();
        Array* arr = new Array(type.getElement(), num_mips);

        std::vector<Value*> mips;
        mips.reserve(num_mips);
        for (unsigned i = 0; i < num_mips; ++i)
            mips.push_back(mipmaps[i].toStruct());

        arr->setElementsDirectly(mips);
        elements.push_back(arr);
        return new Struct(elements, names);
    }

    Image& sampleImplicitLod() {
        return mipmaps[defaultLod];
    }
    const Image& sampleImplicitLod() const {
        return mipmaps[defaultLod];
    }
};
