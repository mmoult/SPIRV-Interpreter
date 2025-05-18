/* © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;
#include <algorithm>  // for std::max
#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "../../external/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../external/stb/stb_image_write.h"
#include "type.hpp"
#include "value.hpp"
export module value.image;
import value.aggregate;
import value.primitive;
import value.statics;
import value.string;

export class Image final : public Value {
    /// Dimensions of the image. xx is the length of x, yy the length of y, zz the len of z
    unsigned xx, yy, zz;

    /// @brief The number of mipmap levels, which decrease in level of detail (LOD).
    /// Each mipmap has half the dimensions of the prior (truncating as needed except when dividing 1). Fields xx, yy,
    /// and zz determine the dimensions of the mimap with the most detail (index 0). The number of mipmaps must not be
    /// less than 1, nor should it exceed the value of `log2(max(dim.xyz)) + 1`.
    unsigned mipmaps;

    /// @brief condensed image data, where typically a set of four elements is a single pixel.
    /// The format determines the type of the image data, so we don't need/want to store that info for each component
    /// of every pixel (which is needlessly wasteful). However, this means that we must reinterpret cast the data for
    /// every use.
    /// Image provides two options to provide texel data:
    /// 1) an image file
    /// 2) a data array
    /// Only one may be provided at a time. Where one is provided, the other must be empty. Internally, both resolve to
    /// this flat data vector.
    std::vector<uint32_t> data;

    /// @brief A definition for how flat data and pixels correspond to each other
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

            count = 0;
            unsigned scale = 1000;
            for (unsigned i = 0; i < 4; ++i) {
                // must init all indices even if in becomes 0 (channels only guaranteed to start at 0 in debug builds)
                unsigned factor = in / scale;
                if (factor > 0) {
                    if (check && factor > 4)
                        throw std::runtime_error("Image component has digit which exceeds the maximum value (4)!");
                    in -= (factor * scale);
                    ++count;
                }
                (*this)[i] = factor;
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
            case 0:
                return r;
            case 1:
                return g;
            case 2:
                return b;
            case 3:
                return a;
            default:
                throw std::runtime_error("Component indexed with invalid value!");
            }
        }

        unsigned operator[](unsigned index) const {
            switch (index) {
            case 0:
                return r;
            case 1:
                return g;
            case 2:
                return b;
            case 3:
                return a;
            default:
                throw std::runtime_error("Component indexed with invalid value!");
            }
        }

        void assertCompatible(const Component& other) {
            if (count == 0) {  // unknown format is coerced to the other
                *this = other;
                return;
            }

            // It is possible to copy from an image with different component order, but all active channels on one must
            // also be active in the other (bidirectional check).
            for (unsigned i = 0; i < 4; ++i) {
                unsigned n = other[i];
                unsigned t = (*this)[i];
                if (n == 0 != t == 0) {
                    std::stringstream err;
                    err << "Cannot copy image from another with an incompatible components value! Order of active ";
                    err << "channels may vary, but which channels are active must be the same. Attempt to copy ";
                    err << other << " to " << *this;
                    throw std::runtime_error(err.str());
                }
            }
        }

        friend std::ostream& operator<<(std::ostream& os, const Component& comp) {
            if (comp.count == 0)
                os << "Unknown";
            else {
                for (unsigned i = 1; i < 4; ++i) {
                    if (comp.r == i)
                        os << 'R';
                    else if (comp.g == i)
                        os << 'G';
                    else if (comp.b == i)
                        os << 'B';
                    else if (comp.a == i)
                        os << 'A';
                }
            }
            return os;
        }
    };
    /// @brief the format of how pixel components are represented in `data`
    Component comps;

    /// @brief a path to an image file or the empty string.
    /// An image can have up to three dimensions. Below, a data encoding pattern is described for each dimensionality:
    /// 1D) Pixels in a single mipmap are expected from left to right. The left side of each mipmap is placed at the
    /// next available corner closest to the top-left image corner. For example, with 4 mipmaps of a size 8 image:
    /// (0)              -> +x
    ///   0 0 0 0 0 0 0 0
    ///   1 1 1 1 2 2 3 -
    ///
    /// 2D) The top-left corner of each mipmap level is placed at the next available corner closest to the top-left
    /// image corner. For example, with 4 mipmaps of an 8x8 image:
    /// (0, 0)                    -> +x
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 1 1 1 1
    ///   0 0 0 0 0 0 0 0 2 2 3 -
    ///   0 0 0 0 0 0 0 0 2 2 - -
    ///   0 0 0 0 0 0 0 0 - - - -
    ///   0 0 0 0 0 0 0 0 - - - -
    /// |
    /// v +y
    ///
    /// 3D) xy layers are placed horizontally in ascending z order. The top-left corner of each mipmap level is placed
    /// at the next available corner closest to the top-left image corner. For example, consider a 4x4x4 image with 3
    /// mipmaps, where each pixel is denoted [mipmap level][z index]:
    /// (0, 0, 0)                                                    -> +x, +z
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 10 10 11 11
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 10 10 11 11
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 20 -- -- --
    ///   00 00 00 00 01 01 01 01 02 02 02 02 03 03 03 03 -- -- -- --
    /// |
    /// v +y
    std::string reference;

    // TODO I suspect that there are a couple extra fields which need to be encoded in the type. As far as I can tell,
    // these only really apply to floats, but we probably need to track them to get correct output
    // 1) Normalization: whether the range spans 255 or 1
    // 2) Signedness: whether half the range is negative or all in positive
    // Of course, the two options are orthogonal. We can have a non-normalized, positively signed range of [0.0, 255.0]
    // or a normalized negatively signed range of [-0.5, 0.5].

    inline static const std::vector<std::string> names {"ref", "dim", "mipmaps", "comps", "data"};

    /// @brief Returns a value corresponding to an out-of-bounds image access.
    /// This value is vendor-specific, ie, not defined by the spec. All zeros is common in practice.
    [[nodiscard]] Array* outOfBoundsAccess() const {
        // "See the client API specification for handling of coordinates outside the image."
        // For now, return black on out of bounds
        const Type& el = type.getElement();
        std::vector<Value*> vals(comps.count, nullptr);
        for (unsigned i = 0; i < comps.count; ++i) {
            auto prim = new Primitive(0);
            prim->cast(el);
            vals[i] = prim;
        }
        return new Array(vals);
    }

    /// @brief Decomposes the float value into an unsigned int base and a float ratio
    ///
    /// The unsigned int base is an unsigned int with the int part of val.
    /// The ratio is how close the original value is to the next int of larger magnitude
    ///
    /// For example:
    /// - decompose(1.0)  = {1, 0.0}
    /// - decompose(3.4)  = {3, 0.4}
    static std::tuple<unsigned, float> decompose(float val) {
        float base;
        float dec = std::modf(val, &base);
        // We will be subtracting the decimal component from 1.0 later, and if the subtraction doesn't even register,
        // it is close enough to 0.0 to flatten it.
        if (1.0 - dec == 1.0)
            dec = 0.0;
        return {static_cast<unsigned>(base), dec};
    }

public:
    Image(Type t) : Value(t), comps(t.getComps(), false), xx(0), yy(0), zz(0) {};

    bool equals(const Value& val) const override {
        if (!Value::equals(val))  // guarantees matching types
            return false;
        const Image& other = static_cast<const Image&>(val);

        // reference is not compared since it only is used in generating the data
        if (xx != other.xx || yy != other.yy || zz != other.zz)
            return false;

        // The ordering of components does not have to be identical, but all active components per fragment in one image
        // need to be active in the other image too.
        if (comps.count != other.comps.count || data.size() != other.data.size())
            return false;
        for (unsigned i = 0; i < 4; ++i) {
            if ((comps[i] == 0) != (other.comps[i] == 0))
                return false;
        }

        // Do a data analysis
        // In theory, the data of all mipmaps should be synchronized. Therefore, we can compare only the mimaps with
        // most data (mipmap 0)
        const Type& subelement = type.getElement();
        for (unsigned i = 0; i < (xx * yy * zz); i += comps.count) {
            for (unsigned j = 0; j < 4; ++j) {
                if (comps[j] == 0)
                    continue;
                // Compare data in the primitive type (needed since float allow for a more lenient comparison)
                Primitive mine(data[i + comps[j] - 1]);
                Primitive your(other.data[i + other.comps[j] - 1]);
                mine.cast(subelement);
                your.cast(subelement);
                if (!mine.equals(your))
                    return false;
            }
        }
        return true;
    }

    /// @brief Copy the image's fields from the other struct, if possible
    /// @param other the struct to copy from
    void copyFrom(const Struct& str) noexcept(false) {
        const Struct& other = Statics::extractStruct(static_cast<const Value*>(&str), "image", names);

        // ref: <string>
        const Value* ref = other[0];
        if (ref->getType().getBase() != DataType::STRING)
            throw std::runtime_error(
                "The first image field, \"ref\", must be a string path to the image source or empty!"
            );
        reference = static_cast<const String*>(ref)->get();

        // dim: uvec1, uvec2, or uvec3
        unsigned dim_size = this->type.getDim();
        if (dim_size < 1 || dim_size > 3)
            throw std::runtime_error(
                "Invalid number of dimensions in image struct! Must be between 1 and 3, inclusive."
            );
        std::vector<unsigned> dims = Statics::extractUvec(other[1], names[1], dim_size);
        xx = dims[0];
        if (dim_size > 1) {
            yy = dims[1];
            if (dim_size > 2)
                zz = dims[2];
        }

        // Now that we have the expected dimensions, fetch data (if any) from the reference path
        // TODO: handle dimensions besides 2D and add support for mipmaps
        if (!reference.empty()) {
            int width, height, channels;
            unsigned char* img = stbi_load(reference.c_str(), &width, &height, &channels, 0);
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

            // Now, transfer the data from img to our "data" field
            // Data has been loaded in as a sequence of RGBA bytes (values 0-255) from left -> right, top -> bottom.
            // TODO handle more than just unsigned normalized float element type
            unsigned size = gx * gy * gc;
            data.resize(size);
            // TODO handle if the number of channels gotten != 4
            for (unsigned i = 0; i < size; i += gc) {
                unsigned ii = 0;
                for (unsigned j = 0; j < 4; ++j) {
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

        // mipmaps: <uint>
        mipmaps = Statics::extractUint(other[2], names[2]);

        // comps: <uint>
        const Value& comps_v = *other[3];
        if (comps_v.getType().getBase() != DataType::UINT)
            throw std::runtime_error(
                "The fourth image field, \"comps\", must be an unsigned int specifying the presence and order of the "
                "pixel components: Red, Green, Blue, Alpha (in that order). For example: \"1234\" indicates all four "
                "channels are present in their default order; \"0010\" indicates only blue is present; \"2341\" means "
                "that all four channels are present in the order ARGB."
            );
        unsigned comps_got = static_cast<const Primitive&>(comps_v).data.all;
        Component comp_new(comps_got, true);
        if (reference.empty())  // the component field only matters if we aren't specifying data through a file
            comps.assertCompatible(comp_new);

        // data : array<float> or array<uint> or array<int>
        // TODO: differentiate between float [0, 255] and float normal [0.0, 1.0]
        const Value& data_v = *other[4];
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
            unsigned total = 0;
            for (unsigned i = 0; i < mipmaps; ++i) {
                unsigned div = std::max(2 * i, 1u);
                unsigned xxx = std::max(xx / div, 1u);
                unsigned yyy = std::max(yy / div, 1u);
                unsigned zzz = std::max(zz / div, 1u);
                total += comps.count * xxx * yyy * zzz;
            }
            if (total != size) {
                std::stringstream err;
                err << "The amount of data provided for the image does not match the dimensions given! Dimensions "
                       "were ";
                err << xx << " x " << yy << " x " << zz << ", with " << comps.count
                    << " active channels. This requires ";
                err << total << " values, however, " << size << " were provided.";
                throw std::runtime_error(err.str());
            }
            // Now copy the data over
            data.resize(size);
            // TODO actually cannot do this in case the data elements have different type :/
            for (unsigned i = 0; i < size; i += comps.count) {
                for (unsigned j = 0; j < 4; ++j) {
                    if (comps[j] == 0)
                        continue;
                    const auto& prim = static_cast<const Primitive&>(*data_a[i + comp_new[j] - 1]);
                    data[i + comps[j] - 1] = prim.data.all;
                }
            }
        }
    }

    void copyFrom(const Value& new_val) noexcept(false) override {
        // Can copy from a struct, assuming that the correct fields are present
        if (const auto& new_type = new_val.getType(); new_type.getBase() == DataType::STRUCT) {
            copyFrom(static_cast<const Struct&>(new_val));
            return;  // will either throw an exception or do a successful copy
        }

        Value::copyFrom(new_val);  // verifies matching types
        const Image& other = static_cast<const Image&>(new_val);
        comps.assertCompatible(other.comps);

        this->xx = other.xx;
        this->yy = other.yy;
        this->zz = other.zz;
        this->mipmaps = other.mipmaps;

        // Now, copy over the data:
        // If a string reference is defined in the other, load data from file TODO
        // Otherwise, do a copy of the other's data array
        data.resize(other.data.size());
        // TODO actually cannot do this in case the data elements may have different type :/
        for (unsigned i = 0; i < data.size(); i += comps.count) {
            for (unsigned j = 0; j < 4; ++j) {
                if (comps[j] == 0)
                    continue;
                data[i + comps[j] - 1] = other.data[i + other.comps[j] - 1];
            }
        }
    }

    void copyReinterp(const Value& other) noexcept(false) override {
        if (!tryCopyFrom(other))
            throw std::runtime_error("Could not copy reinterp to image!");
    }

    // Here is what an image looks like in YAML:
    // img :
    //   ref : <string>
    //   dim : <uvec3, uvec2, or uint>
    //   mipmaps : <uint>
    //   comps : <uint>
    //   data :
    //   - float, int, or uint, as long as it is consistent
    //   - <...>
    Struct* toStruct() const {
        std::vector<Value*> elements;
        elements.reserve(names.size());
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
        elements.push_back(new Primitive(mipmaps));
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

    unsigned getDimensionality() const {
        return type.getDim();
    }

    static std::tuple<float, float, float, float> extractCoords(const Value* coords_v, unsigned dim, bool proj) {
        const Type* coord_type = &coords_v->getType();
        bool arrayed = false;
        if (coord_type->getBase() == DataType::ARRAY) {
            coord_type = &coord_type->getElement();
            arrayed = true;
        }
        DataType base = coord_type->getBase();
        float x = 0.0, y = 0.0, z = 0.0, q = 0.0;

        auto get = [](const Value* val, DataType base) {
            const auto& prim = static_cast<const Primitive&>(*val);
            if (base == DataType::INT)
                return static_cast<float>(prim.data.i32);
            if (base == DataType::UINT)
                return static_cast<float>(prim.data.u32);
            assert(base == DataType::FLOAT);
            return prim.data.fp32;
        };

        if (!arrayed) {
            assert(dim == 1 && !proj);
            x = get(coords_v, base);
        } else {
            const auto& coords = static_cast<const Array&>(*coords_v);
            assert(coords.getSize() >= dim + proj ? 1 : 0);
            x = get(coords[0], base);
            if (dim >= 2) {
                y = get(coords[1], base);
                if (dim >= 3)
                    z = get(coords[2], base);
            }
            if (proj)
                q = get(coords[dim], base);
        }
        return {x, y, z, q};
    }

    [[nodiscard]] Array* read(float x, float y, float z, float lod) const {
        if (x < 0 || y < 0 || z < 0 || lod < 0)
            return outOfBoundsAccess();

        // coordinates are given in the scale of lod=0, regardless of the actual lod to use
        auto [lBase, lRatio] = decompose(lod);
        {  // put this test in its own scope because we don't want the decomposed (besides lod) leaking out accidentally
            auto [xBase, xRatio] = decompose(x);
            auto [yBase, yRatio] = decompose(y);
            auto [zBase, zRatio] = decompose(z);

            if ((xBase > xx || (xBase == xx && xRatio > 0.0)) || (yBase > yy || (yBase == yy && yRatio > 0.0)) ||
                (zBase > zz || (zBase == zz && zRatio > 0.0)) ||
                (lBase > mipmaps || (lBase == mipmaps && lRatio > 0.0)))
                return outOfBoundsAccess();
        }

        auto for_lod = [](float coord, unsigned size, unsigned lod) {
            if (coord == 0.0)
                return std::tuple<unsigned, float>(0, 0.0);
            if (lod == 0)
                return decompose(coord);

            // we divide each dimension by 2 times the lod. For example, 0 is full size, 1 is half-size, etc
            unsigned divide = lod * 2;
            unsigned trunc = std::max(size / divide, 1u);
            // The integral division truncates, which means the actual divisor may exceed divide
            float actual_div = float(size) / float(trunc);
            float actual_rat = float(trunc) / float(size);

            // If the coord was between pixels which got consolidated, any decimal part it had should be erased.
            // Consider this example:
            // - coord 0.2 is 1/5 of the way between 0 and 1. In the mipmap, 0-1 is represented fully by the new 0. We
            // should not get *any* blending with the new 1, which represents the top-level 2-3.
            // However, if the coord was between pixels of different groups, the decimal part should be undisturbed.
            // Consider another example:
            // - coord 1.75 is 3/4 the way between 1 and 2. In the mimap, 0-1 is represented by the new 0, and 2-3 is
            // represented by the new 1. This scaling did *not* affect the ratio of the original coord's representation
            // by the now pixel 0 and 1.

            // This is complicated by the fact that pixel boundaries are not even if actual_div != divide.
            // Compute the coordinate with the correct scale
            float offset = std::fmod(coord, actual_div);
            float pix_size = actual_div / float(divide);
            float dec = 0.0;
            if (offset > actual_div - pix_size) {
                dec = 1.0 - (actual_div - offset) / pix_size;
                if (1.0 - dec == 1.0)
                    dec = 0.0;
            }

            float lowered = coord * actual_rat;
            unsigned integral = static_cast<unsigned>(std::floor(lowered));
            return std::tuple<unsigned, float>(integral, dec);
        };

        const Type& el = type.getElement();
        const DataType el_base = el.getBase();

        // Perform interpolation for all affected values. A single texel cannot have more than 4 components.
        float sums[] = {0.0, 0.0, 0.0, 0.0};
        unsigned lod_offs = 0;  // the first index where data of this lod is stored
        for (unsigned which_lod = 0; which_lod < 2; ++which_lod) {
            unsigned use_lod = lBase + which_lod;
            float lod_weight = (which_lod == 0) ? (1.0 - lRatio) : lRatio;
            if (lod_weight == 0.0)
                break;

            std::vector<std::tuple<unsigned, float>> interps;
            // Recompute the base and ratio for the given level of detail
            auto [bx, rx] = for_lod(x, xx, use_lod);
            auto [by, ry] = for_lod(y, yy, use_lod);
            auto [bz, rz] = for_lod(z, zz, use_lod);

            // Determine the "anchor", which is the data index which points to (bx, by, bz) for this lod.
            // We add some factor to the anchor to calculate the location of the alternate texel (ie, `b + 1`), for each
            // coordinate with nonzero ratio.

            // To get the anchor, we must first determine where the data for this lod starts. For the second iteration
            // of the for loop, we can use the data calculated from the previous iteration
            unsigned xxx = std::max(xx, 1u);
            unsigned yyy = std::max(yy, 1u);
            unsigned zzz = std::max(zz, 1u);
            for (unsigned lod_start = (which_lod == 0) ? 1 : use_lod; lod_start <= use_lod; ++lod_start) {
                lod_offs += comps.count * xxx * yyy * zzz;
                unsigned div = std::max(2 * lod_start, 1u);
                xxx = std::max(xx / div, 1u);
                yyy = std::max(yy / div, 1u);
                zzz = std::max(zz / div, 1u);
            }
            unsigned anchor = lod_offs;

            unsigned factor = comps.count;
            if (rx > 0.0)
                interps.push_back({factor, rx});
            anchor += bx * factor;
            factor *= xxx;
            if (ry > 0.0)
                interps.push_back({factor, ry});
            anchor += by * factor;
            factor *= yyy;
            if (rz > 0.0)
                interps.push_back({factor, rz});
            anchor += bz * factor;

            // We need every combo of different interps applied (either off or on), which maps perfectly onto bits
            // counting to 2^n, where n is the maximum number of interps.
            // Each bit in the increment variable corresponds to whether that interpolation index should be on
            for (unsigned i = 0; i < (1 << interps.size()); ++i) {
                unsigned total = anchor;
                float weight = lod_weight;
                for (unsigned bit = 0; bit < interps.size(); ++bit) {
                    auto [delta, this_ratio] = interps[bit];
                    if ((i >> bit) & 0x1) {
                        total += delta;
                        weight *= this_ratio;
                    } else {
                        weight *= (1.0 - this_ratio);
                    }
                }
                // Now that we have determined the location and the total weight, add to sum
                for (unsigned chan = 0; chan < comps.count; ++chan) {
                    assert(total + chan < data.size());  // safety assert for what should already have been checked
                    Primitive prim(data[total + chan]);
                    float converted;
                    if (el_base == DataType::FLOAT) {
                        converted = prim.data.fp32;
                    } else if (el_base == DataType::INT) {
                        converted = prim.data.i32;
                    } else {
                        assert(el_base == DataType::UINT);
                        converted = prim.data.u32;
                    }
                    sums[chan] += (converted * weight);
                }
            }
        }

        // The size of the array returned is the number of components in each texel
        std::vector<Value*> vals(comps.count, nullptr);

        // Output the channels in the same order defined by the comps
        unsigned chan = 0;
        for (unsigned chan = 0; chan < comps.count; ++chan) {
            float sum = sums[chan];
            Primitive from(0);
            if (el_base == DataType::FLOAT) {
                from = Primitive(sum);
            } else if (el_base == DataType::INT) {
                from = Primitive(static_cast<int>(sum));
            } else {
                assert(el_base == DataType::UINT);
                from = Primitive(static_cast<unsigned>(sum));
            }
            auto* prim = new Primitive(0);
            prim->cast(el);
            prim->copyFrom(from);
            vals[chan] = prim;
        }

        return new Array(vals);
    }

    bool write(int x, int y, int z, const Array& texel) {
        // Verify that the texel to write to is in bounds
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
        if (oob)
            return false;

        unsigned yyy = xx * comps.count;
        unsigned zzz = yy * yyy;
        unsigned base = (xu * comps.count) + (yu * yyy) + (zu * zzz);
        assert(base < data.size());  // should be checked in copying that dimensions match data count actually given

        // TODO: write at the same location to all mipmaps

        // fetch the values out of the texel presented
        const Type& el = type.getElement();
        std::vector<const Value*> values(1, nullptr);
        values.resize(1);
        unsigned tex_size = texel.getSize();
        assert(base + tex_size <= data.size());
        if (tex_size > 4)
            throw std::runtime_error("Texel array to write to image has too many channels (> 4)!");
        for (unsigned i = 0; i < tex_size; ++i) {
            values[0] = texel[i];
            const Value* gen = el.construct(values);
            // Note: gen (and thus el) MUST be a primitive for this to work!
            uint32_t got = static_cast<const Primitive*>(gen)->data.all;
            data[base + i] = got;
        }

        return true;
    }
};
