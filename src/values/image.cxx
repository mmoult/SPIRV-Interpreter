/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <string>
#include <cstdint>
#include <vector>

#include "../external/spirv.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "../external/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb/stb_image_write.h"
#include "type.hpp"
#include "value.hpp"
export module value.image;
import value.aggregate;
import value.primitive;
import value.string;

export class Image : public Value {
    /// @brief a path to an image file (if any). If none, the string must be empty
    std::string reference;

    /// @brief the format of how components of pixels are stored and the count
    /// Each of the first four members must be some value 0-4, with no repeats, excepting 0, which indicates disablement
    /// The last member, count, must be the number of elements previous which are nonzero
    struct Component {
        unsigned r;
        unsigned g;
        unsigned b;
        unsigned a;
        unsigned count;

        /// @param in the input value to parse from
        /// @param check whether extra checking should be done. Should be false if we expect `in` is valid
        Component(unsigned in, bool check) {
            if (check) {
                if (in == 0)
                    throw std::runtime_error("Image component must have at least one channel active! All 0 found.");
                if (in > 4321)
                    throw std::runtime_error("Image component exceeds maximum legal value (4321)!");
            }

            unsigned scale = 1000;
            unsigned i = 0;
            count = 0;
            while (scale > 0) { // must init all indices even if in becomes 0
                unsigned factor = in / scale;
                if (factor > 0) {
                    if (check && factor > 4)
                        throw std::runtime_error("Image component has digit which exceeds the maximum value (4)!");
                    in -= (factor * scale);
                    ++count;
                }
                (*this)[i++] = factor;
                scale /= 10;
            }

            if (check) {
                // At the very end, make sure there are no repeats and no gaps
                bool digits[] = {false, false, false, false};
                for (unsigned i = 0; i < 4; ++i) {
                    unsigned dig = (*this)[i];
                    if (dig == 0)
                        continue;
                    if (dig > count)
                        throw std::runtime_error("Image component digit exceeds count maximum!");
                    dig--;
                    if (digits[dig])
                        throw std::runtime_error(
                            "Image component digit is repeated! Cannot have multiple channels at the same index."
                        );
                    digits[dig] = true;
                }
            }
        }

        unsigned& operator[](unsigned index) {
            switch (index) {
            case 0: return r;
            case 1: return g;
            case 2: return b;
            case 3: return a;
            default: throw std::runtime_error("Component indexed with invalid value!");
            }
        }

        unsigned operator[](unsigned index) const {
            switch (index) {
            case 0: return r;
            case 1: return g;
            case 2: return b;
            case 3: return a;
            default: throw std::runtime_error("Component indexed with invalid value!");
            }
        }

        void assertCompatible(const Component& other) {
            // It is possible to copy from an image with different component order, but all active channels on one must
            // also be active in the other (bidirectional check).
            for (unsigned i = 0; i < 4; ++i) {
                unsigned n = other[i];
                unsigned t = (*this)[i];
                if (n == 0 != t == 0)
                    throw std::runtime_error(
                        "Cannot copy image from another with an incompatible components value! Order of active "
                        "channels may vary, but which channels are active must be the same."
                    );
            }
        }
    };
    Component comps;
    /// Dimensions of the image. xx is the length of x, yy the length of y, zz the len of z
    unsigned xx;
    unsigned yy;
    unsigned zz;
    /// @brief condensed image data, where typically a set of four elements is a single pixel.
    /// The format determines the type of the image data, so we don't need/want to store that info for each component
    /// of every pixel (which is needlessly wasteful). However, this means that we must reinterpret cast the data for
    /// every use.
    std::vector<uint32_t> data;
    // TODO I suspect that there are a couple extra fields which need to be encoded in the type. As far as I can tell,
    // these only really apply to floats, but we probably need to track them to get correct output
    // 1) Normalization: whether the range spans 255 or 1
    // 2) Signedness: whether half the range is negative or all in positive
    // Of course, the two options are orthogonal. We can have a non-normalized, positively signed range of [0.0, 255.0]
    // or a normalized negatively signed range of [-0.5, 0.5].

public:
    Image(Type t): Value(t), comps(t.getComps(), false), xx(0), yy(0), zz(0) {};

    bool equals(const Value& val) const override {
        if (!Value::equals(val))  // guarantees matching types
            return false;
        const Image& other = static_cast<const Image&>(val);

        // reference is not compared since it only is used in generating the data
        if (xx != other.xx || yy != other.yy || zz != other.zz)
            return false;

        // The ordering of components does not have to be identical, but the number of components per fragment must
        // be equivalent.
        if (comps.count != other.comps.count || data.size() != other.data.size())
            return false;

        // Do a data analysis
        for (unsigned i = 0; i < data.size(); i += comps.count) {
            for (unsigned j = 0; j < comps.count; ++j) {
                if (data[i + comps[j]] != other.data[i + other.comps[j]])
                    return false;
            }
        }
        return false;
    }

    /// @brief Copy the image's fields from the other struct, if possible
    /// @param other the struct to copy from
    void copyFrom(const Struct& other) noexcept(false) {
        if (other.getSize() > 4)
            throw std::runtime_error(
                "Unrecognized field in image struct! Only \"ref\", \"dim\", \"comps\", and \"data\" are allowed."
            );
        const std::vector<std::string>& type_names = other.getType().getNames();

        // ref: <string>
        if (type_names[0] != "ref")
            throw std::runtime_error("\"ref\" must be the first field in the image struct!");
        const Value* ref = other[0];
        if (ref->getType().getBase() != DataType::STRING)
            throw std::runtime_error(
                "The first image field, \"ref\", must be a string path to the image source or empty!"
            );
        reference = static_cast<const String*>(ref)->get();

        // dim: uvec1, uvec2, or uvec3
        if (type_names[1] != "dim")
            throw std::runtime_error("\"dim\" must be the second field in the image struct!");
        const Value& dim = *other[1];
        if (const Type& dim_type = dim.getType();
        dim_type.getBase() != DataType::ARRAY || dim_type.getElement().getBase() != DataType::UINT)
            throw std::runtime_error("The second image field, \"dim\", must be an array of uint dimensions!");
        const Array& dim_a = static_cast<const Array&>(dim);
        // The number of dimensions must match what this's type expects
        unsigned dim_size = dim_a.getSize();
        if (unsigned exp_size = this->type.getDim(); dim_size != exp_size) {
            std::stringstream err;
            err << "Could not copy image from struct because its field \"dim\" had " << dim_size << " dimensions, but ";
            err << exp_size << " dimensions were expected!";
            throw std::runtime_error(err.str());
        }
        if (dim_size < 1 || dim_size > 3)
            throw std::runtime_error(
                "Invalid number of dimensions in image struct! Must be between 1 and 3, inclusive."
            );
        xx = static_cast<const Primitive*>(dim_a[0])->data.u32;
        if (dim_size > 1) {
            yy = static_cast<const Primitive*>(dim_a[1])->data.u32;
            if (dim_size > 2)
                zz = static_cast<const Primitive*>(dim_a[2])->data.u32;
        }

        //const Type& element = this->type.getElement();
        // Now that we have the expected dimensions, fetch data (if any) from the reference path
        if (!reference.empty()) {
            int width, height, channels;
            unsigned char *img = stbi_load(reference.c_str(), &width, &height, &channels, 0);
            if (img == nullptr) {
                std::stringstream err;
                err << "Could not load image from path \"" << reference << "\"!";
                throw std::runtime_error(err.str());
            }
            // I don't think the width or height should ever be negative
            assert(height >= 1 && width >= 1);

            unsigned gx = static_cast<unsigned>(width);
            unsigned gy = static_cast<unsigned>(height);
            unsigned gc = static_cast<unsigned>(channels);
            if (gx < xx || gy < yy) {
                std::stringstream err;
                err << "The dimensions of the image loaded from file (" << gx << " x " << gy << ") are insufficient";
                err << " for the image dimensions required: " << xx << " x " << yy << " x " << zz;
                throw std::runtime_error(err.str());
            }
            // TODO: also handle the third dimension. Obviously, images in png files cannot be 3D, so we will have to
            // think of a reasonable way they can/should be represented

            // Now, transfer the data from img to our "data" field
            // Data has been loaded in as a sequence of RGBA bytes (values 0-255) from left -> right, top -> bottom.
            // TODO handle more than just unsigned normalized float element type
            unsigned size = gx * gy * gc;
            data.resize(size);
            // TODO handle if the number of channels gotten != 4
            for (unsigned i = 0; i < size; i += gc) {
                unsigned ii = 0;
                for (unsigned j = 0; j < comps.count; ++j) {
                    if (comps[j] == 0)
                        continue;
                    float norm = img[i + ii] / 255.0;
                    data[i + comps[j] - 1] = *reinterpret_cast<uint32_t*>(&norm);
                    ++ii;
                }
            }

            // Finally, delete the image loaded
            stbi_image_free(img);
            // example write: stbi_write_png("sky.png", width, height, channels, img, width * channels);
        }

        // comps: <uint>
        if (type_names[2] != "comps")
            throw std::runtime_error("\"comps\" must be the third field in the image struct!");
        const Value& comps_v = *other[2];
        if (comps_v.getType().getBase() != DataType::UINT)
            throw std::runtime_error(
                "The third image field, \"comps\", must be an unsigned int specifying the presence and order of the "
                "pixel components: Red, Green, Blue, Alpha (in that order). For example: \"1234\" indicates all four "
                "channels are present in their default order; \"0010\" indicates only blue is present; \"2341\" means "
                "that all four channels are present in the order ARGB."
            );
        unsigned comps_got = static_cast<const Primitive&>(comps_v).data.u32;
        Component comp_new(comps_got, true);
        if (reference.empty())  // the component field only matters if we aren't specifying data through a file
            comps.assertCompatible(comp_new);

        // data : array<float> or array<uint> or array<int>
        // TODO: differentiate between float [0, 255] and float normal [0.0, 1.0]
        if (type_names[3] != "data")
            throw std::runtime_error("\"data\" must be the fourth field in the image struct!");
        const Value& data_v = *other[3];
        if (data_v.getType().getBase() != DataType::ARRAY)
            throw std::runtime_error(
                "The fourth image field, \"data\", must be an array of uint, int, or float values."
            );
        const Array& data_a = static_cast<const Array&>(data_v);
        if (!reference.empty()) {
            // Verify that the data is empty
            if (data_a.getSize() != 0) {
                throw std::runtime_error(
                    "Image exists with both an image reference and literal data. Only one may be provided at a time!"
                );
            }
        } else {
            const Type& element = data_a.getType().getElement();
            if (DataType ebase = element.getBase();
            ebase != DataType::FLOAT && ebase != DataType::UINT && ebase != DataType::INT)
                throw std::runtime_error("The image field \"data\" must have elements of type: uint, int, or float!");
            unsigned size = data_a.getSize();
            // Verify that the data matches expected from the given dimensions
            unsigned total = xx * comps.count;
            if (yy > 0)
                total *= yy;
            if (zz > 0)
                total *= zz;
            if (total != size) {
                std::stringstream err;
                err << "The amount of data provided for the image does not match the dimensions given! Dimensions were ";
                err << xx << " x " << yy << " x " << zz << ", with " << comps.count << " active channels. This requires ";
                err << total << " values, however, " << size << " were provided.";
                throw std::runtime_error(err.str());
            }
            // TODO: there can actually be more data entries provided if they are mipmaps... implement
            // Now copy the data over
            data.resize(size);
            // TODO actually cannot do this in case the data elements have different type :/
            for (unsigned i = 0; i < size; i += comps.count) {
                for (unsigned j = 0; j < comps.count; ++j) {
                    if (comps[j] == 0)
                        continue;
                    const auto& prim = static_cast<const Primitive&>(*data_a[i + comp_new[j] - 1]);
                    data[i + comps[j] - 1] = prim.data.u32;
                }
            }
        }
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Can copy from a struct, assuming that the correct fields are present
        if (const auto& new_type = new_val.getType(); new_type.getBase() == DataType::STRUCT) {
            copyFrom(static_cast<const Struct&>(new_val));
            return; // will either throw an exception or do a successful copy
        }

        Value::copyFrom(new_val);  // verifies matching types
        const Image& other = static_cast<const Image&>(new_val);
        comps.assertCompatible(other.comps);

        this->xx = other.xx;
        this->yy = other.yy;
        this->zz = other.zz;

        // Now, copy over the data:
        // If a string reference is defined in the other, load data from file TODO
        // Otherwise, do a copy of the other's data array
        data.resize(other.data.size());
        // TODO actually cannot do this in case the data elements have different type :/
        for (unsigned i = 0; i < data.size(); i += comps.count) {
            for (unsigned j = 0; j < comps.count; ++j) {
                if (comps[j] == 0)
                    continue;
                data[i + comps[j] - 1] = other.data[i + other.comps[j] - 1];
            }
        }
    }

    // Here is what an image looks like in YAML:
    // img :
    //   ref : <string>
    //   dim : uvec3, uvec2, or uint
    //   comps : <uint>
    //   data :
    //   - float, int, or uint, as long as it is consistent
    //   - <...>
    Struct* toStruct() const {
        std::vector<std::string> names{"ref", "dim", "comps", "data"};
        std::vector<Value*> elements;
        elements.reserve(4);
        elements.push_back(new String(reference));
        std::vector<Value*> dims;
        unsigned num_dims = type.getDim();
        dims.reserve(num_dims);
        dims.push_back(new Primitive(xx));
        if (num_dims > 1) {
            dims.push_back(new Primitive(yy));
            if (num_dims > 2)
                dims.push_back(new Primitive(zz));
        }
        elements.push_back(new Array(dims));
        // Reconstruct the components uint from the actual components breakdown
        elements.push_back(new Primitive(comps.r * 1000 + comps.g * 100 + comps.b * 10 + comps.a));
        Array* dat = new Array(type.getElement(), 0);
        // populate the dat array with the image's actual data
        std::vector<Value*> values;
        const Type& dat_type = type.getElement();
        for (const unsigned dat : data) {
            Primitive* prim = new Primitive(dat);
            prim->cast(dat_type);
            values.push_back(prim);
        }
        dat->setElementsDirectly(values);
        elements.push_back(dat);
        return new Struct(elements, names);
    }

    [[nodiscard]] Array* read(int x, int y, int z) const {
        std::vector<Value*> vals(comps.count, nullptr);
        // The size of the array returned is the number of components in each texel
        const Type& el = type.getElement();
        bool oob = false;
        if (x < 0 || y < 0 || z < 0)
            oob = true;
        unsigned xu = static_cast<unsigned>(x);
        unsigned yu = static_cast<unsigned>(y);
        unsigned zu = static_cast<unsigned>(z);
        // If the coordinate specified matches or exceeds the maximum (exclusive), then we are out of bounds.
        // However, there is some special behavior for 0, since coordinate matching is appropriate there.
        if ((xu > 0 && xu >= xx) || (yu > 0 && yu >= yy) || (zu > 0 && zu >= zz))
            oob = true;

        // "See the client API specification for handling of coordinates outside the image."
        // For now, return black on out of bounds
        if (oob) {
            for (unsigned i = 0; i < comps.count; ++i) {
                auto prim = new Primitive(0);
                prim->cast(el);
                vals[i] = prim;
            }
        } else {
            unsigned yyy = xx * comps.count;
            unsigned zzz = yy * yyy;
            unsigned base = (xu * comps.count) + (yu * yyy) + (zu * zzz);
            assert(base < data.size());  // should be checked in copying that dimensions match data count actually given

            unsigned next = 0;
            for (unsigned i = 0; i < 4; ++i) {
                unsigned ch = comps[i];
                if (ch == 0)
                    continue;
                auto prim = new Primitive(data[base + ch - 1]);
                prim->cast(el);
                vals[next++] = prim;
            }
        }

        return new Array(vals);
    }
};
